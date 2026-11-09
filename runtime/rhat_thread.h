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

// Internal format only; never a serialization or foreign ABI contract.
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
#endif
