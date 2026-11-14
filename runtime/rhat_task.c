// std.task: a pool of worker threads that run jobs, and the tasks that hand their results back.
// A job is stepped until it finishes: each step answers -1 when it has finished, 0 when it can
// run again, or the id of a std.async wait it is waiting for. A job stays on the worker that
// first ran it, since its waits are in that worker's table. The pool does not know what a job
// is: an R^ frame is one kind, and others (an L^ machine run a slice at a time, a function
// call) fit the same steps.
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "rhat_thread.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rhat_sync.h"

// From the std.async runtime (rhat_net.c).
int64_t rh_async_external(int64_t *token);
void rh_async_complete(int64_t token);
int64_t rh_async_wait(double timeout);

typedef struct Task Task;
typedef struct Job Job;

// What the pool does with one kind of job.
typedef struct {
    int64_t (*step)(Job *job);
    void (*finish)(Job *job, Task *task); // Hands the finished job's result to its task.
    void (*release)(Job *job);
} JobOps;

struct Job {
    Job *next;
    const JobOps *ops;
    Task *task;
    int64_t wait; // The wait it is suspended on; 0 when it can run.
    const void *type;
    void *data;
};

struct Task {
    atomic_int holds;
    Mutex mutex;
    Condition finished;
    bool done, taken;
    void *result;
    void (*free_result)(void *);
    RhWatch **watches;
    size_t watch_count, watch_capacity;
};

typedef struct Pool Pool;

typedef struct {
    Pool *pool;
    Mutex mutex;
    Job *first, *last; // Jobs handed to it, not yet taken in.
    int64_t token;     // The external wait it sleeps on, completed when a job arrives.
    atomic_int load;
    bool stopping;
#if defined(_WIN32)
    HANDLE native;
#else
    pthread_t native;
#endif
} Worker;

struct Pool {
    Worker *workers;
    int64_t count;
    uint64_t stacks;
};

// The descriptor the compiler emits for each frame type a pool runs.
typedef struct {
    uint64_t size, align;
    int64_t (*step)(void *frame);
    void (*finish)(void *frame, void *out);
    uint64_t result_size, result_align;
} RhFrameJob;

static void task_release(Task *t)
{
    if (atomic_fetch_sub(&t->holds, 1) != 1) {
        return;
    }
    if (t->result && t->free_result) {
        t->free_result(t->result);
    }
    free(t->watches);
    cond_free(&t->finished);
    lock_free(&t->mutex);
    free(t);
}

// Records the result and wakes whoever waits: threads asleep in wait, and external waits.
static void task_complete(Task *t, void *result, void (*free_result)(void *))
{
    lock(&t->mutex);
    t->result = result;
    t->free_result = free_result;
    t->done = true;
    RhWatch **watches = t->watches;
    size_t count = t->watch_count;
    t->watches = NULL;
    t->watch_count = t->watch_capacity = 0;
    wake(&t->finished);
    unlock(&t->mutex);
    for (size_t j = 0; j < count; j++) {
        watches[j]->complete(watches[j]);
    }
    free(watches);
}

static int64_t frame_step(Job *job)
{
    return ((const RhFrameJob *)job->type)->step(job->data);
}

static void frame_finish(Job *job, Task *task)
{
    const RhFrameJob *type = job->type;
    void *result = malloc(type->result_size ? (size_t)type->result_size : 1);
    if (!result) {
        abort();
    }
    type->finish(job->data, result);
    task_complete(task, result, free);
}

static void frame_release(Job *job)
{
    free(job->data);
}

static const JobOps frame_ops = {frame_step, frame_finish, frame_release};

// A kind of job given by function pointers (std.task's JobKind): an L^ machine run a slice at a
// time, say. The pool keeps a copy of it with each job.
typedef struct {
    int64_t (*step)(void *job);
    void *(*finish)(void *job);
    void (*release)(void *job);
    void (*free_result)(void *result);
} RhJobKind;

typedef struct {
    RhJobKind kind;
    void *job;
} Foreign;

static int64_t foreign_step(Job *job)
{
    Foreign *f = job->data;
    return f->kind.step(f->job);
}

static void foreign_finish(Job *job, Task *task)
{
    Foreign *f = job->data;
    void *result = f->kind.finish(f->job);
    task_complete(task, result, f->kind.free_result);
}

static void foreign_release(Job *job)
{
    Foreign *f = job->data;
    f->kind.release(f->job);
    free(f);
}

static const JobOps foreign_ops = {foreign_step, foreign_finish, foreign_release};

#if defined(_WIN32)
static DWORD WINAPI worker_main(void *data)
#else
static void *worker_main(void *data)
#endif
{
    Worker *w = data;
    RhThreadContext *context = rh_context_new(w->pool->stacks);
    if (!context) {
        abort();
    }
    rh_context_attach(context);
    Job **jobs = NULL;
    size_t count = 0, capacity = 0;
    int64_t sleeping = 0; // The id of the external wait for new jobs, while registered.
    for (;;) {
        lock(&w->mutex);
        Job *arrived = w->first;
        w->first = w->last = NULL;
        bool stopping = w->stopping;
        unlock(&w->mutex);
        for (; arrived; arrived = arrived->next) {
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 8;
                jobs = realloc(jobs, capacity * sizeof(*jobs));
                if (!jobs) {
                    abort();
                }
            }
            jobs[count++] = arrived;
        }
        if (!count && stopping) {
            break;
        }
        bool ran = false;
        for (size_t j = 0; j < count;) {
            Job *job = jobs[j];
            if (job->wait) {
                j++;
                continue;
            }
            ran = true;
            int64_t answer = job->ops->step(job);
            if (answer >= 0) {
                job->wait = answer;
                j++;
                continue;
            }
            job->ops->finish(job, job->task);
            job->ops->release(job);
            task_release(job->task);
            free(job);
            jobs[j] = jobs[--count];
            atomic_fetch_sub(&w->load, 1);
        }
        if (ran) {
            continue;
        }
        if (!sleeping) {
            int64_t token = 0;
            sleeping = rh_async_external(&token);
            lock(&w->mutex);
            bool pending = w->first || w->stopping;
            w->token = pending ? 0 : token;
            unlock(&w->mutex);
            if (pending) {
                rh_async_complete(token);
            }
        }
        int64_t ready = rh_async_wait(3600.0);
        if (ready == sleeping) {
            sleeping = 0;
            continue;
        }
        for (size_t j = 0; ready && j < count; j++) {
            if (jobs[j]->wait == ready) {
                jobs[j]->wait = 0;
            }
        }
    }
    free(jobs);
    rh_context_attach(NULL);
    rh_context_free(context);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

void rh_pool_stop(int64_t pool);

// A pool of workers, each with its own data stack shard; 0 when they cannot all start.
int64_t rh_pool_new(int64_t workers, uint64_t stacks)
{
    if (workers <= 0) {
        return 0;
    }
    Pool *p = calloc(1, sizeof(*p));
    if (!p || !(p->workers = calloc((size_t)workers, sizeof(Worker)))) {
        free(p);
        return 0;
    }
    p->stacks = stacks;
    for (int64_t j = 0; j < workers; j++) {
        Worker *w = &p->workers[j];
        w->pool = p;
        lock_init(&w->mutex);
        atomic_init(&w->load, 0);
#if defined(_WIN32)
        w->native = CreateThread(NULL, 0, worker_main, w, 0, NULL);
        bool started = w->native != NULL;
#else
        bool started = pthread_create(&w->native, NULL, worker_main, w) == 0;
#endif
        if (!started) {
            p->count = j;
            rh_pool_stop((int64_t)(uintptr_t)p);
            return 0;
        }
        p->count = j + 1;
    }
    return (int64_t)(uintptr_t)p;
}

// Hands job to the least busy worker and answers its task, which the caller holds.
static int64_t submit(Pool *p, Job *job)
{
    Task *t = calloc(1, sizeof(*t));
    if (!t) {
        job->ops->release(job);
        free(job);
        return 0;
    }
    atomic_init(&t->holds, 2); // The caller's and the job's.
    lock_init(&t->mutex);
    cond_init(&t->finished);
    job->task = t;
    Worker *w = &p->workers[0];
    for (int64_t j = 1; j < p->count; j++) {
        if (atomic_load(&p->workers[j].load) < atomic_load(&w->load)) {
            w = &p->workers[j];
        }
    }
    atomic_fetch_add(&w->load, 1);
    lock(&w->mutex);
    if (w->last) {
        w->last->next = job;
    }
    else {
        w->first = job;
    }
    w->last = job;
    int64_t token = w->token;
    w->token = 0;
    unlock(&w->mutex);
    if (token) {
        rh_async_complete(token);
    }
    return (int64_t)(uintptr_t)t;
}

// Copies an unstarted or suspended frame to a worker; its task, or 0.
int64_t rh_pool_submit_frame(int64_t pool, const RhFrameJob *type, const void *frame)
{
    Job *job = calloc(1, sizeof(*job));
    void *data = malloc((size_t)type->size);
    if (!pool || !job || !data) {
        free(job);
        free(data);
        return 0;
    }
    memcpy(data, frame, (size_t)type->size);
    job->ops = &frame_ops;
    job->type = type;
    job->data = data;
    return submit((Pool *)(uintptr_t)pool, job);
}

// Hands job, of the kind given, to a worker; its task, or 0.
int64_t rh_pool_submit_job(int64_t pool, const RhJobKind *kind, void *data)
{
    Job *job = calloc(1, sizeof(*job));
    Foreign *f = malloc(sizeof(*f));
    if (!pool || !job || !f) {
        free(job);
        free(f);
        return 0;
    }
    f->kind = *kind;
    f->job = data;
    job->ops = &foreign_ops;
    job->data = f;
    return submit((Pool *)(uintptr_t)pool, job);
}

// Lets each worker finish its jobs, then joins it.
void rh_pool_stop(int64_t pool)
{
    Pool *p = (Pool *)(uintptr_t)pool;
    if (!p) {
        return;
    }
    for (int64_t j = 0; j < p->count; j++) {
        Worker *w = &p->workers[j];
        lock(&w->mutex);
        w->stopping = true;
        int64_t token = w->token;
        w->token = 0;
        unlock(&w->mutex);
        if (token) {
            rh_async_complete(token);
        }
    }
    for (int64_t j = 0; j < p->count; j++) {
        Worker *w = &p->workers[j];
#if defined(_WIN32)
        WaitForSingleObject(w->native, INFINITE);
        CloseHandle(w->native);
#else
        pthread_join(w->native, NULL);
#endif
        lock_free(&w->mutex);
    }
    free(p->workers);
    free(p);
}

bool rh_task_done(int64_t task)
{
    Task *t = (Task *)(uintptr_t)task;
    if (!t) {
        return true;
    }
    lock(&t->mutex);
    bool done = t->done;
    unlock(&t->mutex);
    return done;
}

// Completes token once the task is done: now, if it already is.
void rh_task_watch(int64_t task, int64_t token)
{
    Task *t = (Task *)(uintptr_t)task;
    RhWatch *w = (RhWatch *)(uintptr_t)token;
    if (!w) {
        return;
    }
    if (!t) {
        w->complete(w);
        return;
    }
    lock(&t->mutex);
    if (t->done) {
        unlock(&t->mutex);
        w->complete(w);
        return;
    }
    if (t->watch_count == t->watch_capacity) {
        size_t capacity = t->watch_capacity ? t->watch_capacity * 2 : 4;
        RhWatch **grown = realloc(t->watches, capacity * sizeof(*grown));
        if (!grown) {
            unlock(&t->mutex);
            w->complete(w);
            return;
        }
        t->watches = grown;
        t->watch_capacity = capacity;
    }
    t->watches[t->watch_count++] = w;
    unlock(&t->mutex);
}

// Blocks the calling thread until the task is done.
void rh_task_wait(int64_t task)
{
    Task *t = (Task *)(uintptr_t)task;
    if (!t) {
        return;
    }
    lock(&t->mutex);
    while (!t->done) {
        cond_wait(&t->finished, &t->mutex);
    }
    unlock(&t->mutex);
}

// Waits at most ms milliseconds for the job to finish, answering whether it has.
bool rh_task_wait_for(int64_t task, int64_t ms)
{
    Task *t = (Task *)(uintptr_t)task;
    if (!t) {
        return true;
    }
    lock(&t->mutex);
    if (!t->done) {
        cond_wait_for(&t->finished, &t->mutex, ms);
    }
    bool done = t->done;
    unlock(&t->mutex);
    return done;
}

// Writes Done<T> | Taken: the result (size bytes) at offset with value_tag the first time, then
// taken_tag. The task must be done.
void rh_task_take(int64_t task, void *out, int32_t value_tag, int32_t taken_tag, uint64_t offset,
                  uint64_t size)
{
    Task *t = (Task *)(uintptr_t)task;
    if (!t) {
        memcpy(out, &taken_tag, sizeof(taken_tag));
        return;
    }
    rh_task_wait(task);
    lock(&t->mutex);
    if (t->taken || !t->result) {
        unlock(&t->mutex);
        memcpy(out, &taken_tag, sizeof(taken_tag));
        return;
    }
    t->taken = true;
    memcpy((unsigned char *)out + offset, t->result, (size_t)size);
    if (t->free_result) {
        t->free_result(t->result);
    }
    t->result = NULL;
    unlock(&t->mutex);
    memcpy(out, &value_tag, sizeof(value_tag));
}

// Takes the result a job of a given kind finished with, once: it is the caller's to free then.
// NULL when it is not done, has no result or was taken.
void *rh_task_take_raw(int64_t task)
{
    Task *t = (Task *)(uintptr_t)task;
    if (!t) {
        return NULL;
    }
    lock(&t->mutex);
    void *result = t->done && !t->taken ? t->result : NULL;
    if (t->done) {
        t->taken = true;
        t->result = NULL;
    }
    unlock(&t->mutex);
    return result;
}

// The finished job's result, left with the task, which frees it when it is let go; NULL before
// the job finishes and once it is taken.
void *rh_task_result_raw(int64_t task)
{
    Task *t = (Task *)(uintptr_t)task;
    if (!t) {
        return NULL;
    }
    lock(&t->mutex);
    void *result = t->done ? t->result : NULL;
    unlock(&t->mutex);
    return result;
}

void rh_task_drop(int64_t task)
{
    Task *t = (Task *)(uintptr_t)task;
    if (t) {
        task_release(t);
    }
}
