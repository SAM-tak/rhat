// Shared runtime ABI for independent data-stack shards and typed transfer.
#ifndef RHAT_THREAD_H
#define RHAT_THREAD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Stable-address storage owned by one R^ execution thread.
typedef struct {
    unsigned char *base, *top, *limit, *committed;
} RhStack;

// TLS-owned execution state. Code and immutable type descriptors are shared.
typedef struct {
    RhStack *stacks;
    uint64_t count, callback_sp;
} RhThreadContext;

// One field or union alternative in an internal transfer descriptor.
typedef struct RhTransferType RhTransferType;

typedef struct {
    uint64_t offset;
    const RhTransferType *type;
} RhTransferField;

// Compiler/runtime transfer ABI for this R^ version; not a serialization format.
struct RhTransferType {
    uint64_t kind, size, align, count, offset;
    const RhTransferType *element;
    const RhTransferField *fields;
};

// Allocate a fresh shard, undoing partial reservation on failure.
RhThreadContext *rh_context_new(uint64_t count);
// Release a shard after its owner has stopped executing R^ code.
void rh_context_free(RhThreadContext *context);
// Attach this thread to its sole execution context.
void rh_context_attach(RhThreadContext *context);
// Allocate aligned bytes on a receiver-owned stack; exhaustion is fatal.
void *rh_transfer_alloc(RhStack *stack, uint64_t bytes, uint64_t align);
// Copy arguments before returning; run entry in a fresh attached context. Zero means failure.
int64_t rh_thread_spawn(void (*entry)(void *), const RhTransferType *type, void *args,
                        uint64_t stacks);
// Wait for a successful spawn and release its handle; zero is a no-op.
void rh_thread_join(int64_t handle);
// Block the caller for finite seconds; nonpositive durations return immediately.
void rh_thread_sleep(double seconds);
// Return the calling thread's positive, process-local logical ID, allocating it lazily.
int64_t rh_thread_current_id(void);
// Read a live spawn handle's logical ID; zero has no ID. Call before joining.
int64_t rh_thread_id(int64_t handle);
#endif
