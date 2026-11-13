// Locks, conditions and external-wait completion shared by the thread and task runtimes.
#ifndef RHAT_SYNC_H
#define RHAT_SYNC_H
#include <stdbool.h>
#include <stdlib.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK Mutex;
typedef CONDITION_VARIABLE Condition;

static inline bool lock_init(Mutex *m)
{
    InitializeSRWLock(m);
    return true;
}

static inline void lock(Mutex *m)
{
    AcquireSRWLockExclusive(m);
}

static inline void unlock(Mutex *m)
{
    ReleaseSRWLockExclusive(m);
}

static inline void lock_free(Mutex *m)
{
    (void)m;
}

static inline bool cond_init(Condition *c)
{
    InitializeConditionVariable(c);
    return true;
}

static inline void cond_wait(Condition *c, Mutex *m)
{
    if (!SleepConditionVariableSRW(c, m, INFINITE, 0)) {
        abort();
    }
}

static inline void wake(Condition *c)
{
    WakeAllConditionVariable(c);
}

static inline void cond_free(Condition *c)
{
    (void)c;
}
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Condition;

static inline bool lock_init(Mutex *m)
{
    return pthread_mutex_init(m, NULL) == 0;
}

static inline void lock(Mutex *m)
{
    if (pthread_mutex_lock(m)) {
        abort();
    }
}

static inline void unlock(Mutex *m)
{
    if (pthread_mutex_unlock(m)) {
        abort();
    }
}

static inline void lock_free(Mutex *m)
{
    pthread_mutex_destroy(m);
}

static inline bool cond_init(Condition *c)
{
    return pthread_cond_init(c, NULL) == 0;
}

static inline void cond_wait(Condition *c, Mutex *m)
{
    if (pthread_cond_wait(c, m)) {
        abort();
    }
}

static inline void wake(Condition *c)
{
    pthread_cond_broadcast(c);
}

static inline void cond_free(Condition *c)
{
    pthread_cond_destroy(c);
}
#endif

// A std.async external wait: completing it wakes the thread that waits on it. Its first
// member is all this runtime needs; rhat_net.c owns the rest.
typedef struct RhWatch {
    void (*complete)(struct RhWatch *);
} RhWatch;
#endif
