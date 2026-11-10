#ifndef RHAT_TOOLING_H
#define RHAT_TOOLING_H
#include "rhat.h"
#include <setjmp.h>

// Nested editor-only recovery boundaries; command-line compilation stays fatal.
typedef struct ToolRecovery ToolRecovery;

struct ToolRecovery {
    jmp_buf escape;
    ToolRecovery *previous;
    Instance *instance; // Diagnostic context, inherited by nested recovery boundaries.
};

// Push/pop a boundary in the same stack frame that calls setjmp.
void tool_enter(ToolRecovery *recovery);
void tool_leave(ToolRecovery *recovery);
// Abandon a dependent unit without reporting a second, cascading error.
_Noreturn void tool_abort(void);

// One concrete specialization requested by another instance at a source location.
typedef struct {
    Instance *caller, *callee;
    Loc loc;
} ToolInstantiation;

// The concrete body currently being inferred or checked, if any.
Instance *tool_current_instance(void);
// Record a call before checking a new or cached specialization.
void tool_instance_use(Instance *instance, Loc loc);
// Collect template instantiation requests and their callers, without recursive cycles.
List tool_instantiation_trace(Instance *instance);

// Snapshot mutable analysis state, preserving live list buffers on rollback.
typedef struct ToolState ToolState;
ToolState *tool_save(void *target, size_t size, const size_t *lists, size_t count);
void tool_restore(ToolState *state);
// Snapshot the objects referenced by a list (bindings carry mutable flow facts).
ToolState *tool_save_objects(List *objects, size_t size);

// Run an independent phase/unit; the body must not return, break or continue.
#define TOOL_CHECK(p, ...)                                                                         \
    do {                                                                                           \
        if (!(p)->tooling) {                                                                       \
            __VA_ARGS__                                                                            \
        }                                                                                          \
        else {                                                                                     \
            ToolRecovery tool_boundary;                                                            \
            tool_enter(&tool_boundary);                                                            \
            if (setjmp(tool_boundary.escape) == 0) {                                               \
                __VA_ARGS__                                                                        \
            }                                                                                      \
            tool_leave(&tool_boundary);                                                            \
        }                                                                                          \
    } while (0)

// Optional per-thread allocation and diagnostic boundary for compiler clients.
typedef struct {
    void *(*allocate)(size_t);
    void *(*resize)(void *, size_t);
    void (*release)(void *);
    void (*error)(Loc, const char *);
    void (*abandon)(void);
} CompilerHooks;

// NULL for the command-line compiler; installed by an embedded checker.
extern _Thread_local CompilerHooks *compiler_hooks;

// Release client-managed compiler memory; CLI chunks live until process exit.
void mem_free(void *pointer);

// One source buffer, optionally replacing the corresponding file on disk.
typedef struct {
    const char *path;
    char *text;
} ToolSource;

// Parameters have an internal kind; other kinds follow LSP SymbolKind.
enum { TOOL_PARAMETER = 100, TOOL_TRY = 101 };

// Parser extents for outline declarations; the header ends before the first body brace.
typedef struct {
    Loc start, end, header_end;
} ToolSpan;

// One compiler-recorded identifier resolution.
typedef struct {
    Loc use, declaration;
    const char *name;
    Type *type;
    Function *function;
    Def *owner;
    int kind;
    bool definition;
    const char *detail; // What hovering says, when it is not the name's type.
} ToolRef;

// Path comparison for source overlays, including Windows case folding.
bool source_same_path(const char *a, const char *b);

// Resolve an editor buffer without changing the on-disk source.
const char *source_override(Program *p, const char *path);

// Record a checked use or declaration when tooling is enabled.
void tool_record(Program *p, Loc use, Loc declaration, const char *name, Type *type,
                 Function *function, Def *owner, int kind, bool definition);

// Record where the try^ at use sends what it finds: detail for hovering, and the first
// destination for going to its definition.
void tool_record_try(Program *p, Loc use, Loc destination, Type *errors, const char *detail);
#endif
