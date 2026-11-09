// OS threads, bounded channels and copies between independent stack shards.
#include "rhat_thread.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK Mutex;
typedef CONDITION_VARIABLE Condition;

static bool lock_init(Mutex *m)
{
    InitializeSRWLock(m);
    return true;
}

static void lock(Mutex *m)
{
    AcquireSRWLockExclusive(m);
}

static void unlock(Mutex *m)
{
    ReleaseSRWLockExclusive(m);
}

static void lock_free(Mutex *m)
{
    (void)m;
}

static bool cond_init(Condition *c)
{
    InitializeConditionVariable(c);
    return true;
}

static void wait(Condition *c, Mutex *m)
{
    if (!SleepConditionVariableSRW(c, m, INFINITE, 0)) {
        abort();
    }
}

static void wake(Condition *c)
{
    WakeAllConditionVariable(c);
}

static void cond_free(Condition *c)
{
    (void)c;
}
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Condition;

static bool lock_init(Mutex *m)
{
    return pthread_mutex_init(m, NULL) == 0;
}

static void lock(Mutex *m)
{
    if (pthread_mutex_lock(m)) {
        abort();
    }
}

static void unlock(Mutex *m)
{
    if (pthread_mutex_unlock(m)) {
        abort();
    }
}

static void lock_free(Mutex *m)
{
    pthread_mutex_destroy(m);
}

static bool cond_init(Condition *c)
{
    return pthread_cond_init(c, NULL) == 0;
}

static void wait(Condition *c, Mutex *m)
{
    if (pthread_cond_wait(c, m)) {
        abort();
    }
}

static void wake(Condition *c)
{
    pthread_cond_broadcast(c);
}

static void cond_free(Condition *c)
{
    pthread_cond_destroy(c);
}
#endif

typedef struct Message {
    struct Message *next;
    uint64_t size;
    unsigned char bytes[];
} Message;

typedef struct {
    atomic_uint_fast64_t holds;
    Mutex mutex;
    Condition readable, writable;
    const RhTransferType *type;
    Message *first, *last;
    uint64_t count, bytes, count_limit, byte_limit;
    bool closed;
} Channel;

// Public bridge handles are private integer fields of noncopyable R^ wrappers.
static Channel *channel_of(int64_t handle)
{
    return (Channel *)(uintptr_t)handle;
}

int64_t rh_channel_clone(int64_t handle)
{
    if (!handle) {
        return 0;
    }
    atomic_fetch_add_explicit(&channel_of(handle)->holds, 1, memory_order_relaxed);
    return handle;
}

void rh_channel_drop(int64_t handle)
{
    if (!handle) {
        return;
    }
    Channel *c = channel_of(handle);
    if (atomic_fetch_sub_explicit(&c->holds, 1, memory_order_acq_rel) != 1) {
        return;
    }
    Message *m = c->first;
    while (m) {
        Message *next = m->next;
        free(m);
        m = next;
    }
    cond_free(&c->readable);
    cond_free(&c->writable);
    lock_free(&c->mutex);
    free(c);
}

int64_t rh_channel_new(const RhTransferType *type, int64_t messages, int64_t bytes)
{
    if (messages <= 0 || bytes <= 0) {
        return -1;
    }
    Channel *c = calloc(1, sizeof(*c));
    if (!c) {
        return 0;
    }
    atomic_init(&c->holds, 1);
    if (!lock_init(&c->mutex)) {
        free(c);
        return 0;
    }
    if (!cond_init(&c->readable)) {
        lock_free(&c->mutex);
        free(c);
        return 0;
    }
    if (!cond_init(&c->writable)) {
        cond_free(&c->readable);
        lock_free(&c->mutex);
        free(c);
        return 0;
    }
    c->type = type;
    c->count_limit = (uint64_t)messages;
    c->byte_limit = (uint64_t)bytes;
    return (int64_t)(uintptr_t)c;
}

void rh_channel_close(int64_t handle)
{
    if (!handle) {
        return;
    }
    Channel *c = channel_of(handle);
    lock(&c->mutex);
    c->closed = true;
    wake(&c->readable);
    wake(&c->writable);
    unlock(&c->mutex);
}

// The wire walker has four operations: measure, encode, decode, release an
// abandoned spawn packet. Channel holds transfer from a packet to decoded args.
typedef struct {
    unsigned char *bytes;
    uint64_t at, limit;
    RhStack *stack;
    int mode;
} Wire;

static bool wire_bytes(Wire *w, void *value, uint64_t size)
{
    if (size > w->limit - w->at) {
        return false;
    }
    if (w->mode == 1) {
        memcpy(w->bytes + w->at, value, (size_t)size);
    }
    if (w->mode == 2) {
        memcpy(value, w->bytes + w->at, (size_t)size);
    }
    w->at += size;
    return true;
}

static bool transfer(Wire *w, const RhTransferType *t, void *value)
{
    unsigned char *v = value;
    if (w->mode == 2) {
        memset(v, 0, (size_t)t->size);
    }
    if (t->kind == 0) {
        return wire_bytes(w, value, t->size);
    }
    if (t->kind == 6) {
        int64_t h = 0;
        if (w->mode < 2) {
            memcpy(&h, v + t->offset, sizeof(h));
        }
        if (w->mode == 3) {
            memcpy(&h, w->bytes + w->at, sizeof(h));
        }
        if (!wire_bytes(w, &h, sizeof(h))) {
            return false;
        }
        if (w->mode == 1) {
            rh_channel_clone(h);
        }
        if (w->mode == 2) {
            memcpy(v + t->offset, &h, sizeof(h));
        }
        if (w->mode == 3) {
            rh_channel_drop(h);
        }
        return true;
    }
    if (t->kind == 1) {
        for (uint64_t j = 0; j < t->count; j++) {
            if (!transfer(w, t->fields[j].type, v ? v + t->fields[j].offset : NULL)) {
                return false;
            }
        }
        return true;
    }
    if (t->kind == 5) {
        uint32_t tag = 0;
        if (w->mode < 2) {
            memcpy(&tag, v, sizeof(tag));
        }
        if (w->mode == 3) {
            memcpy(&tag, w->bytes + w->at, sizeof(tag));
        }
        if (!wire_bytes(w, &tag, sizeof(tag)) || tag >= t->count) {
            return false;
        }
        if (w->mode == 2) {
            memcpy(v, &tag, sizeof(tag));
        }
        return transfer(w, t->fields[tag].type, v ? v + t->offset : NULL);
    }
    uint64_t count = t->count;
    unsigned char *items = v;
    if (t->kind == 3 || t->kind == 4) {
        if (w->mode < 2) {
            memcpy(&count, v + (t->kind == 3 ? 8 : 0), sizeof(count));
        }
        if (w->mode == 3) {
            memcpy(&count, w->bytes + w->at, sizeof(count));
        }
        if (!wire_bytes(w, &count, sizeof(count))) {
            return false;
        }
        if (t->kind == 4 && count > t->count) {
            return false;
        }
        if (count && t->element->size > SIZE_MAX / count) {
            return false;
        }
        if (t->kind == 3) {
            if (w->mode < 2) {
                memcpy(&items, v, sizeof(items));
            }
            if (w->mode == 2) {
                // Capped arrays need their own descriptor, not the shard's entire limit.
                RhStack *owner = w->stack;
                if (t->offset == 3) {
                    owner = rh_transfer_alloc(w->stack, sizeof(*owner), _Alignof(RhStack));
                    memset(owner, 0, sizeof(*owner));
                }
                items = rh_transfer_alloc(w->stack, count * t->element->size, t->element->align);
                if (t->offset == 3) {
                    owner->base = items;
                    owner->top = owner->limit = owner->committed = items + count * t->element->size;
                }
                memcpy(v, &items, sizeof(items));
                memcpy(v + 8, &count, sizeof(count));
                memcpy(v + 16, &owner, sizeof(owner));
            }
        }
        else {
            items = v ? v + t->offset : NULL;
            if (w->mode == 2) {
                memcpy(v, &count, sizeof(count));
            }
        }
    }
    for (uint64_t j = 0; j < count; j++) {
        if (!transfer(w, t->element, items ? items + j * t->element->size : NULL)) {
            return false;
        }
    }
    return true;
}

static Message *encode(const RhTransferType *type, void *value, uint64_t size)
{
    if (size > SIZE_MAX - sizeof(Message)) {
        return NULL;
    }
    Message *m = malloc(sizeof(*m) + (size_t)size);
    if (!m) {
        return NULL;
    }
    m->next = NULL;
    m->size = size;
    Wire w = {m->bytes, 0, size, NULL, 1};
    if (!transfer(&w, type, value) || w.at != size) {
        abort();
    }
    return m;
}

// Codes: 0 accepted, 1 closed, 2 full, 3 oversized, 4 allocation failed.
int64_t rh_channel_send(int64_t handle, void *value, int64_t blocking)
{
    if (!handle) {
        return 1;
    }
    Channel *c = channel_of(handle);
    Wire measure = {NULL, 0, UINT64_MAX, NULL, 0};
    if (!transfer(&measure, c->type, value)) {
        return 3;
    }
    uint64_t size = measure.at;
    if (size > c->byte_limit || size > SIZE_MAX - sizeof(Message)) {
        return 3;
    }
    lock(&c->mutex);
    while (!c->closed && (c->count == c->count_limit || size > c->byte_limit - c->bytes)) {
        if (!blocking) {
            unlock(&c->mutex);
            return 2;
        }
        wait(&c->writable, &c->mutex);
    }
    if (c->closed) {
        unlock(&c->mutex);
        return 1;
    }
    c->count++;
    c->bytes += size;
    unlock(&c->mutex);
    Message *m = encode(c->type, value, size);
    lock(&c->mutex);
    if (!m || c->closed) {
        c->count--;
        c->bytes -= size;
        int64_t status = c->closed ? 1 : 4;
        wake(&c->writable);
        unlock(&c->mutex);
        free(m);
        return status;
    }
    if (c->last) {
        c->last->next = m;
    }
    else {
        c->first = m;
    }
    c->last = m;
    wake(&c->readable);
    unlock(&c->mutex);
    return 0;
}

// Receive writes a tagged R^ result; successful values belong to the caller's stack.
void rh_channel_receive(int64_t handle, void *out, RhStack *stack, uint64_t offset,
                        int32_t value_tag, int32_t closed_tag, int32_t empty_tag, int64_t blocking)
{
    if (!handle) {
        memcpy(out, &closed_tag, sizeof(closed_tag));
        return;
    }
    Channel *c = channel_of(handle);
    lock(&c->mutex);
    while (!c->first && !c->closed) {
        if (!blocking) {
            memcpy(out, &empty_tag, sizeof(empty_tag));
            unlock(&c->mutex);
            return;
        }
        wait(&c->readable, &c->mutex);
    }
    if (!c->first) {
        memcpy(out, &closed_tag, sizeof(closed_tag));
        unlock(&c->mutex);
        return;
    }
    Message *m = c->first;
    c->first = m->next;
    if (!c->first) {
        c->last = NULL;
    }
    c->count--;
    c->bytes -= m->size;
    wake(&c->writable);
    unlock(&c->mutex);
    Wire w = {m->bytes, 0, m->size, stack, 2};
    if (!transfer(&w, c->type, (unsigned char *)out + offset) || w.at != m->size) {
        abort();
    }
    memcpy(out, &value_tag, sizeof(value_tag));
    free(m);
}

static void release_value(const RhTransferType *t, unsigned char *value)
{
    if (t->kind == 6) {
        int64_t h;
        memcpy(&h, value + t->offset, sizeof(h));
        rh_channel_drop(h);
    }
    else if (t->kind == 1) {
        for (uint64_t j = t->count; j; j--) {
            release_value(t->fields[j - 1].type, value + t->fields[j - 1].offset);
        }
    }
    else if (t->kind == 5) {
        uint32_t tag;
        memcpy(&tag, value, sizeof(tag));
        release_value(t->fields[tag].type, value + t->offset);
    }
    else if (t->kind >= 2 && t->kind <= 4) {
        uint64_t count = t->count;
        unsigned char *items = value;
        if (t->kind == 3) {
            memcpy(&items, value, sizeof(items));
            memcpy(&count, value + 8, sizeof(count));
        }
        if (t->kind == 4) {
            memcpy(&count, value, sizeof(count));
            items += t->offset;
        }
        for (uint64_t j = count; j; j--) {
            release_value(t->element, items + (j - 1) * t->element->size);
        }
    }
}

typedef struct {
#if defined(_WIN32)
    HANDLE native;
#else
    pthread_t native;
#endif
    void (*entry)(void *);
    RhThreadContext *context;
    const RhTransferType *type;
    Message *packet;
    void *args;
} Thread;

#if defined(_WIN32)
static DWORD WINAPI thread_entry(void *data)
#else
static void *thread_entry(void *data)
#endif
{
    Thread *t = data;
    rh_context_attach(t->context);
    Wire w = {t->packet->bytes, 0, t->packet->size, &t->context->stacks[0], 2};
    if (!transfer(&w, t->type, t->args) || w.at != t->packet->size) {
        abort();
    }
    free(t->packet);
    t->packet = NULL;
    t->entry(t->args);
    release_value(t->type, t->args);
    free(t->args);
    t->args = NULL;
    rh_context_attach(NULL);
    rh_context_free(t->context);
    t->context = NULL;
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

int64_t rh_thread_spawn(void (*entry)(void *), const RhTransferType *type, void *args,
                        uint64_t stacks)
{
    Wire measure = {NULL, 0, UINT64_MAX, NULL, 0};
    if (!transfer(&measure, type, args)) {
        return 0;
    }
    Thread *t = calloc(1, sizeof(*t));
    if (!t) {
        return 0;
    }
    t->entry = entry;
    t->type = type;
    t->context = rh_context_new(stacks);
    t->args = calloc(1, (size_t)type->size);
    if (t->context && t->args) {
        t->packet = encode(type, args, measure.at);
    }
    if (t->packet) {
#if defined(_WIN32)
        t->native = CreateThread(NULL, 0, thread_entry, t, 0, NULL);
        if (t->native) {
            return (int64_t)(uintptr_t)t;
        }
#else
        if (!pthread_create(&t->native, NULL, thread_entry, t)) {
            return (int64_t)(uintptr_t)t;
        }
#endif
        Wire drop = {t->packet->bytes, 0, t->packet->size, NULL, 3};
        if (!transfer(&drop, type, NULL)) {
            abort();
        }
    }
    free(t->packet);
    free(t->args);
    rh_context_free(t->context);
    free(t);
    return 0;
}

void rh_thread_join(int64_t handle)
{
    if (!handle) {
        return;
    }
    Thread *t = (Thread *)(uintptr_t)handle;
#if defined(_WIN32)
    if (WaitForSingleObject(t->native, INFINITE) != WAIT_OBJECT_0) {
        abort();
    }
    CloseHandle(t->native);
#else
    if (pthread_join(t->native, NULL)) {
        abort();
    }
#endif
    free(t);
}

int64_t rh_hardware_threads(void)
{
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors ? info.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 1;
#endif
}

_Noreturn void rh_thread_unhandled(void)
{
    fputs("R^ runtime error: worker returned an unhandled error\n", stderr);
    exit(1);
}
