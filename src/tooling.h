#ifndef RHAT_TOOLING_H
#define RHAT_TOOLING_H
#include "rhat.h"
/* Optional per-thread allocation and diagnostic boundary for compiler clients.
 */
typedef struct {
  void *(*allocate)(size_t);
  void *(*resize)(void *, size_t);
  void (*release)(void *);
  void (*error)(Loc, const char *);
} CompilerHooks;
/* NULL for the command-line compiler; installed by an embedded checker. */
extern _Thread_local CompilerHooks *compiler_hooks;
/* Release compiler memory through its current allocation boundary. */
void mem_free(void *pointer);
/* One source buffer, optionally replacing the corresponding file on disk. */
typedef struct {
  const char *path;
  char *text;
} ToolSource;
/* Parameters have an internal kind; other kinds follow LSP SymbolKind. */
enum { TOOL_PARAMETER = 100 };
/* One compiler-recorded identifier resolution. */
typedef struct {
  Loc use, declaration;
  const char *name;
  Type *type;
  Function *function;
  Def *owner;
  int kind;
  bool definition;
} ToolRef;
/* Path comparison for source overlays, including Windows case folding. */
bool source_same_path(const char *a, const char *b);
/* Resolve an editor buffer without changing the on-disk source. */
const char *source_override(Program *p, const char *path);
/* Record a checked use or declaration when tooling is enabled. */
void tool_record(Program *p, Loc use, Loc declaration, const char *name,
                 Type *type, Function *function, Def *owner, int kind,
                 bool definition);
#endif
