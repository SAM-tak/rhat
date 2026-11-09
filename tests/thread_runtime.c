#if !defined(_WIN32)
#define _DEFAULT_SOURCE
#endif
// Allocation failures and exact lifetime accounting for the native thread bridge.
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

static atomic_int live_blocks, fail_after = -1;

static bool refuse_allocation(void)
{
    int n = atomic_load(&fail_after);
    return n >= 0 && atomic_fetch_sub(&fail_after, 1) == 0;
}

static void *counted_malloc(size_t size)
{
    if (refuse_allocation()) {
        return NULL;
    }
    void *p = malloc(size);
    if (p) {
        atomic_fetch_add(&live_blocks, 1);
    }
    return p;
}

static void *counted_calloc(size_t count, size_t size)
{
    if (refuse_allocation()) {
        return NULL;
    }
    void *p = calloc(count, size);
    if (p) {
        atomic_fetch_add(&live_blocks, 1);
    }
    return p;
}

static void counted_free(void *p)
{
    if (p) {
        atomic_fetch_sub(&live_blocks, 1);
    }
    free(p);
}

#define malloc counted_malloc
#define calloc counted_calloc
#define free counted_free
#include "../runtime/rhat_runtime.c"
#if defined(_WIN32)
static bool refuse_start;

static HANDLE WINAPI checked_start(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack,
                                   LPTHREAD_START_ROUTINE entry, LPVOID arg, DWORD flags,
                                   LPDWORD id)
{
    if (refuse_start) {
        return NULL;
    }
    return CreateThread(attributes, stack, entry, arg, flags, id);
}

#define CreateThread checked_start
#else
#include <pthread.h>
static bool refuse_start;

static int checked_start(pthread_t *id, const pthread_attr_t *attributes, void *(*entry)(void *),
                         void *arg)
{
    if (refuse_start) {
        return 1;
    }
    return pthread_create(id, attributes, entry, arg);
}

static int fail_sync = -1;

static int checked_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (fail_sync >= 0 && fail_sync-- == 0) {
        return 1;
    }
    return pthread_mutex_init(mutex, attr);
}

static int checked_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attr)
{
    if (fail_sync >= 0 && fail_sync-- == 0) {
        return 1;
    }
    return pthread_cond_init(condition, attr);
}

#define pthread_mutex_init checked_mutex_init
#define pthread_cond_init checked_cond_init
#define pthread_create checked_start
#endif
#include "../runtime/rhat_thread.c"
#if defined(_WIN32)
#undef CreateThread
#else
#undef pthread_create
#undef pthread_mutex_init
#undef pthread_cond_init
#endif
#undef malloc
#undef calloc
#undef free

static atomic_int executions;

static void entry(void *args)
{
    assert(*(int32_t *)args == 42);
    assert(rh_current_stacks() != NULL);
    atomic_fetch_add(&executions, 1);
}

static atomic_int delivered, total;

static void producer(void *args)
{
    int64_t channel;
    memcpy(&channel, args, sizeof(channel));
    for (int32_t j = 1; j <= 1000; j++) {
        assert(rh_channel_send(channel, &j, 1) == 0);
    }
}

static void consumer(void *args)
{
    int64_t channel;
    memcpy(&channel, args, sizeof(channel));

    struct {
        int32_t tag, value;
    } result;

    for (;;) {
        rh_channel_receive(channel, &result, NULL, 4, 0, 1, 2, 1);
        if (result.tag == 1) {
            return;
        }
        assert(result.tag == 0);
        atomic_fetch_add(&delivered, 1);
        atomic_fetch_add(&total, result.value);
    }
}

int main(void)
{
    RhTransferType scalar = {0, 4, 4, 0, 0, NULL, NULL};
    rh_runtime_init(1);
    int baseline = atomic_load(&live_blocks);
    atomic_store(&fail_after, 0);
    assert(rh_channel_new(&scalar, 1, 4) == 0);
    assert(atomic_load(&live_blocks) == baseline);
#if !defined(_WIN32)
    for (int stage = 0; stage < 3; stage++) {
        fail_sync = stage;
        assert(rh_channel_new(&scalar, 1, 4) == 0);
        assert(atomic_load(&live_blocks) == baseline);
    }
    fail_sync = -1;
#endif
    int64_t c = rh_channel_new(&scalar, 1, 4);
    int32_t value = 42;
    atomic_store(&fail_after, 0);
    assert(rh_channel_send(c, &value, 0) == 4);
    assert(rh_channel_send(c, &value, 0) == 0);
    assert(rh_channel_send(c, &value, 0) == 2);
    rh_channel_drop(c);
    assert(atomic_load(&live_blocks) == baseline);
    // Fail every native allocation stage, then retry with no injected failure.
    for (int stage = 0; stage < 5; stage++) {
        atomic_store(&fail_after, stage);
        int64_t t = rh_thread_spawn(entry, &scalar, &value, 1);
        atomic_store(&fail_after, -1);
        rh_thread_join(t);
        assert(atomic_load(&live_blocks) == baseline);
    }
    for (int j = 0; j < 100; j++) {
        int64_t a = rh_thread_spawn(entry, &scalar, &value, 1);
        int64_t b = rh_thread_spawn(entry, &scalar, &value, 1);
        assert(a && b);
        rh_thread_join(a);
        rh_thread_join(b);
        assert(atomic_load(&live_blocks) == baseline);
    }
    assert(atomic_load(&executions) >= 200);
    // A failed spawn packet containing a channel must undo its extra hold.
    RhTransferType handle = {6, 8, 8, 0, 0, NULL, NULL};
    for (int stage = 0; stage < 5; stage++) {
        c = rh_channel_new(&scalar, 1, 4);
        atomic_store(&fail_after, stage);
        // This entry is never reached: all five allocations precede start.
        int64_t t = rh_thread_spawn(entry, &handle, &c, 1);
        atomic_store(&fail_after, -1);
        assert(t == 0);
        rh_channel_drop(c);
        assert(atomic_load(&live_blocks) == baseline);
    }
    c = rh_channel_new(&scalar, 1, 4);
    refuse_start = true;
    assert(rh_thread_spawn(entry, &handle, &c, 1) == 0);
    refuse_start = false;
    rh_channel_drop(c);
    assert(atomic_load(&live_blocks) == baseline);
    c = rh_channel_new(&scalar, 3, 12);
    int64_t p1 = rh_thread_spawn(producer, &handle, &c, 1);
    int64_t p2 = rh_thread_spawn(producer, &handle, &c, 1);
    int64_t r1 = rh_thread_spawn(consumer, &handle, &c, 1);
    int64_t r2 = rh_thread_spawn(consumer, &handle, &c, 1);
    assert(p1 && p2 && r1 && r2);
    rh_thread_join(p1);
    rh_thread_join(p2);
    rh_channel_close(c);
    rh_thread_join(r1);
    rh_thread_join(r2);
    rh_channel_drop(c);
    assert(atomic_load(&delivered) == 2000);
    assert(atomic_load(&total) == 1001000);
    assert(atomic_load(&live_blocks) == baseline);
    rh_runtime_shutdown();
    assert(atomic_load(&live_blocks) == 0);
    puts("native lifetime checks passed");
    return 0;
}
