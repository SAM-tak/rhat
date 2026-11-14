// The C half of lib/lhat: the C structs lhat.task reads and writes, and what R^ does not do --
// counts shared between threads, memory C frees, text built in C, an L^ type heap. How a job
// runs, what L^ sees, the pool, the waits and the sharing contract are the R^ of lhat.task.
#ifndef RHAT_LHAT_HOST_H
#define RHAT_LHAT_HOST_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lhat.h"
#include "stdlib/carry.h"
#include "stdlib/task_check.h"

// How big a task's machine is, and how many loop turns it runs before its worker takes
// another job: the measures L^'s own std.task uses.
#define LHAT_HOST_FRAMES 64
#define LHAT_HOST_SLOTS 2048
#define LHAT_HOST_SLICE 20000

/// What came of an L^ job: std.task's ForeignResult, which its Task holds until taken.
typedef struct LhatJobResult {
    LhatRunStatus status;
    LhatDetachedValue *detached;
    bool refused;  // The answer could not leave its machine.
    char *failure; // What it failed with and where, NULL when it did not.
} LhatJobResult;

/// An L^ job a pool worker runs a slice at a time: std.task's ForeignJob.
typedef struct LhatJob {
    const LhatProgram *program;
    LhatCarried *carried;
    LhatMachine *machine;
    LhatValue driving; // The coroutine being resumed, once there is one.
    bool driving_set;
    LhatRunResult ran;
    LhatJobResult *result;
} LhatJob;

/// std.task.Task as L^ sees it: a pool task's handle, the type its result has, and the holds
/// of the machines that have it.
typedef struct LhatTask {
    atomic_int holds;
    int64_t raw;
    atomic_bool taken;
    LhatHeap heap;
    LhatRuntimeType *type;
} LhatTask;

/// What lhat.task's exportL^ registration ends with: async's check, as L^'s own std.task makes
/// it, that a job's result fits the one slot a task carries.
static inline bool lhost_task_register(LhatProgram *program, const char *module)
{
#if LHAT_WITH_FRONTEND
    return lhat_register_instantiation_check_handler(program, module, NULL, "async", 0,
                                                     task_check_async, NULL) &&
           lhat_register_instantiation_check_handler(program, module, NULL, "async", 1,
                                                     task_check_async, NULL);
#else
    (void)program;
    (void)module;
    return true;
#endif
}

// R^'s runtime, which lhat.task drives directly: the pool's handle is kept in C (below), where
// a pool worker's callbacks can read it.
int64_t rh_pool_new(int64_t workers, uint64_t stacks);
void rh_pool_stop(int64_t pool);
int64_t rh_hardware_threads(void);
int64_t rh_pool_submit_job(int64_t pool, const void *kind, void *job);
bool rh_task_done(int64_t task);
void rh_task_watch(int64_t task, int64_t token);
void rh_task_wait(int64_t task);
void *rh_task_result_raw(int64_t task);
void rh_task_drop(int64_t task);

/// std.task's runtime under names R^ may call: R^ keeps rh_ for its own modules.
static inline int64_t lhost_pool_new(int64_t workers, int64_t stacks)
{
    return rh_pool_new(workers, (uint64_t)stacks);
}

static inline void lhost_pool_stop(int64_t pool)
{
    rh_pool_stop(pool);
}

static inline int64_t lhost_hardware_threads(void)
{
    return rh_hardware_threads();
}

static inline int64_t lhost_pool_submit(int64_t pool, const void *kind, void *job)
{
    return rh_pool_submit_job(pool, kind, job);
}

static inline bool lhost_raw_done(int64_t task)
{
    return rh_task_done(task);
}

static inline void lhost_raw_watch(int64_t task, int64_t token)
{
    rh_task_watch(task, token);
}

static inline void lhost_raw_wait(int64_t task)
{
    rh_task_wait(task);
}

static inline void lhost_raw_drop(int64_t task)
{
    rh_task_drop(task);
}

static atomic_llong lhost_pool_handle, lhost_pool_workers;

/// The pool L^'s std.task runs on, or 0 before start.
static inline int64_t lhost_pool(void)
{
    return atomic_load(&lhost_pool_handle);
}

/// How many workers the pool has, or 0 before start.
static inline int64_t lhost_workers(void)
{
    return atomic_load(&lhost_pool_workers);
}

/// Installs the pool and its worker count, answering the pool it replaces.
static inline int64_t lhost_set_pool(int64_t pool, int64_t workers)
{
    atomic_store(&lhost_pool_workers, workers);
    return atomic_exchange(&lhost_pool_handle, pool);
}

/// The job value carried off machine, ready for a worker: NULL with *refusal 1 when it cannot
/// be carried, 2 when memory ran out.
static inline LhatJob *lhost_job_new(LhatMachine *machine, LhatValue value, int32_t *refusal)
{
    LhatJob *job = calloc(1, sizeof *job);
    const char *refused = NULL;
    *refusal = 2;
    if (!job) {
        return NULL;
    }
    job->program = lhat_machine_program(machine);
    if (!lhat_carry(value, &job->carried, &refused)) {
        *refusal = refused && strcmp(refused, "out of memory") ? 1 : 2;
        free(job);
        return NULL;
    }
    *refusal = 0;
    return job;
}

/// Whether value is a job async takes: a coroutine not yet started, or a closure that takes
/// nothing and does not yield.
static inline bool lhost_job_shape(LhatValue value)
{
    const LhatProto *proto = lhat_closure_proto(value);
    return lhat_is_object_kind(value, LHAT_OBJECT_COROUTINE) ||
           (proto && !lhat_proto_yields(proto) && !lhat_proto_parameters(proto));
}

// What a failed run reads as: what went wrong, the value it failed with, where, and the frames
// it stood in. Read while the machine still stands.
static inline char *lhost_job_failure(LhatMachine *machine, LhatRunResult ran)
{
    const char *said = lhat_run_status_message(ran.status);
    size_t value = lhat_is_nil(ran.value) ? 0 : lhat_value_write(ran.value, NULL, 0);
    size_t trace =
        lhat_machine_fault_depth(machine) >= 2 ? lhat_machine_traceback(machine, NULL, 0) : 0;
    size_t room = strlen(said) + value + trace + 40;
    char *text = malloc(room);
    if (!text) {
        return NULL;
    }
    size_t n = (size_t)snprintf(text, room, "%s", said);
    if (value) {
        n += (size_t)snprintf(text + n, room - n, ": ");
        lhat_value_write(ran.value, text + n, value + 1);
        n += value;
    }
    if (ran.line) {
        n += (size_t)snprintf(text + n, room - n, " (line %u)", (unsigned)ran.line);
    }
    if (trace) {
        n += (size_t)snprintf(text + n, room - n, "\n");
        lhat_machine_traceback(machine, text + n, trace + 1);
    }
    return text;
}

/// The job's end, after its last run: its answer detached from the machine, or what it failed
/// with, and the machine gone.
static inline void lhost_job_conclude(LhatJob *job, LhatRunStatus status)
{
    LhatJobResult *r = calloc(1, sizeof *r);
    if (!r) {
        abort();
    }
    r->status = status;
    if (status != LHAT_RUN_OK && status != LHAT_RUN_SUSPENDED && job->machine) {
        r->failure = lhost_job_failure(job->machine, job->ran);
    }
    if (status == LHAT_RUN_OK) {
        LhatDetachStatus d = lhat_machine_detach_result(job->machine, job->ran.value, &r->detached);
        r->status = d == LHAT_DETACH_OUT_OF_MEMORY ? LHAT_RUN_OUT_OF_MEMORY : LHAT_RUN_OK;
        r->refused = d == LHAT_DETACH_REFUSED;
    }
    if (job->machine) {
        lhat_machine_dispose(job->machine);
        job->machine = NULL;
    }
    job->result = r;
}

/// The job std.task hands back as its ForeignJob.
static inline LhatJob *lhost_job(void *job)
{
    return job;
}

/// The job's result, handed to its task once it has finished.
static inline LhatJobResult *lhost_job_finish(LhatJob *job)
{
    LhatJobResult *r = job->result;
    job->result = NULL;
    return r;
}

/// Frees a result nobody took.
static inline void lhost_result_free(LhatJobResult *r)
{
    lhat_detached_value_free(r->detached);
    free(r->failure);
    free(r);
}

/// Frees the job, its machine included when the pool stopped with it standing.
static inline void lhost_job_free(LhatJob *job)
{
    if (job->machine) {
        lhat_machine_dispose(job->machine);
    }
    if (job->result) {
        lhost_result_free(job->result);
    }
    lhat_carried_free(job->carried);
    free(job);
}

/// A task for the pool's task raw, typed by what value's body answers, with one hold: the
/// machine's that asked for it. NULL when memory ran out.
static inline LhatTask *lhost_task_new(int64_t raw, LhatValue value)
{
    LhatTask *t = calloc(1, sizeof *t);
    if (!t) {
        return NULL;
    }
    atomic_init(&t->holds, 1);
    t->raw = raw;
    const LhatRuntimeType *type = lhat_body_result_type(value);
    t->type = type ? lhat_runtime_type_clone(&t->heap, type)
                   : lhat_type_rt_new(&t->heap, LHAT_TYPE_RT_NIL);
    if (!t->type) {
        free(t);
        return NULL;
    }
    return t;
}

/// The pool's handle of the task.
static inline int64_t lhost_task_raw(LhatTask *t)
{
    return t->raw;
}

/// Adds delta to the holds on the task, answering how many remain.
static inline int32_t lhost_task_hold(LhatTask *t, int32_t delta)
{
    return atomic_fetch_add(&t->holds, delta) + delta;
}

/// Frees the task once nothing holds it.
static inline void lhost_task_free(LhatTask *t)
{
    lhat_object_free_all(&t->heap);
    free(t);
}

/// The type argument at index: the type of the result, at 0.
static inline const LhatRuntimeType *lhost_task_type(LhatTask *t, uint64_t index)
{
    return index == 0 ? t->type : NULL;
}

// The finished task's result, which stays with the pool's task until the last hold lets it go.
static inline LhatJobResult *lhost_task_result(LhatTask *t)
{
    return rh_task_result_raw(t->raw);
}

/// What the finished task failed with, as an L^ string in *out, or nil^.
static inline bool lhost_task_failure(LhatMachine *machine, LhatTask *t, LhatValue *out)
{
    LhatJobResult *r = lhost_task_result(t);
    *out = lhat_nil();
    return !r || !r->failure ||
           lhat_machine_make_string(machine, r->failure, strlen(r->failure), out);
}

/// Takes the finished task's result into machine as *out, or the std.task.TaskError it fails
/// with: Failed with what the job failed with, Refused when its answer could not leave its
/// machine, Taken when it was taken already.
static inline void lhost_task_take(LhatMachine *machine, LhatTask *t, LhatValue *out)
{
    static const char *const variants[] = {"Failed", "Refused", "Taken"};
    static const char *const messages[] = {
        "the job failed", "task result cannot transfer ownership", "task result was already taken"};
    LhatJobResult *r = lhost_task_result(t);
    int how = 2;
    if (!atomic_exchange(&t->taken, true)) {
        if (r && r->status == LHAT_RUN_OK && !r->refused) {
            *out = lhat_machine_adopt_result(machine, r->detached);
            r->detached = NULL;
            return;
        }
        how = r && r->status == LHAT_RUN_OK ? 1 : 0;
    }
    const char *message = how == 0 && r && r->failure ? r->failure : messages[how];
    const LhatErrorKind *kind = lhat_lookup_error_kind(lhat_machine_program(machine), "std.task",
                                                       "TaskError", variants[how]);
    if (!kind || !lhat_machine_make_error(machine, kind, message, lhat_nil(), out)) {
        *out = lhat_nil();
    }
}

/// Lets go of one hold on the task, freeing it and its pool task with the last.
static inline void lhost_task_let_go(LhatTask *t)
{
    if (lhost_task_hold(t, -1) == 0) {
        rh_task_drop(t->raw);
        lhost_task_free(t);
    }
}

int64_t rh_async_external(int64_t *token);

// One step of a join: the wait for the task to finish while it runs, then its result -- or the
// std.task.TaskError await would answer -- as what the coroutine ends with.
static bool lhost_join_step(LhatMachine *machine, void *context, const LhatValue *sent,
                            size_t sent_count, LhatValue *answers, int *answer_count)
{
    LhatTask *t = context;
    (void)sent;
    (void)sent_count;
    *answer_count = 1;
    if (!rh_task_done(t->raw)) {
        int64_t token = 0, id = rh_async_external(&token);
        if (id) {
            rh_task_watch(t->raw, token);
        }
        answers[0] = lhat_integer(id); // 0 is no wait: the job just comes back and looks again.
        return true;
    }
    lhost_task_take(machine, t, &answers[0]);
    return false;
}

static void lhost_join_release(LhatMachine *machine, void *context, const LhatValue *arguments,
                               size_t count, LhatValue *answers, int *answer_count)
{
    (void)machine;
    (void)arguments;
    (void)count;
    (void)answers;
    (void)answer_count;
    lhost_task_let_go(context);
}

/// A coroutine for await^ that waits for the task without holding its thread, and ends with
/// the task's result; false when memory ran out.
static inline bool lhost_task_join(LhatMachine *machine, LhatTask *t, LhatValue *out)
{
    lhost_task_hold(t, 1); // The coroutine's own hold.
    if (!lhat_machine_make_coroutine(machine, lhost_join_step, t, lhost_join_release, lhat_nil(),
                                     out)) {
        lhost_task_let_go(t);
        *out = lhat_nil();
        return false;
    }
    return true;
}

// The keys lhat.value roots values under in a machine's host root: negative, apart from the
// ones a host makes of its own.
static atomic_llong lhost_keys;

/// Roots value on machine under a key of its own, answering the key; 0 when it could not.
static inline int64_t lhost_hold(LhatMachine *machine, LhatValue value)
{
    int64_t key = -1 - atomic_fetch_add(&lhost_keys, 1);
    bool refused = false;
    return lhat_machine_table_set(machine, lhat_machine_host_root(machine), lhat_integer(key),
                                  value, &refused)
               ? key
               : 0;
}

/// The value rooted under key, nil^ once it is let go.
static inline LhatValue lhost_held(LhatMachine *machine, int64_t key)
{
    return lhat_table_get(lhat_machine_host_root(machine), lhat_integer(key));
}

/// Unroots the value under key.
static inline void lhost_unhold(LhatMachine *machine, int64_t key)
{
    bool refused = false;
    lhat_machine_table_set(machine, lhat_machine_host_root(machine), lhat_integer(key), lhat_nil(),
                           &refused);
}

/// value taken off its machine, or NULL when it cannot leave it.
static inline LhatCarried *lhost_carry(LhatValue value)
{
    LhatCarried *carried = NULL;
    const char *refused = NULL;
    return lhat_carry(value, &carried, &refused) ? carried : NULL;
}

/// Puts carried back together on machine into *out.
static inline bool lhost_uncarry(LhatMachine *machine, LhatCarried *carried, LhatValue *out)
{
    return lhat_uncarry(machine, carried, out);
}

/// Frees carried.
static inline void lhost_carried_free(LhatCarried *carried)
{
    lhat_carried_free(carried);
}

#endif
