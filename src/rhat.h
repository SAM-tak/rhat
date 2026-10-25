#ifndef RHAT_H
#define RHAT_H
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  void **v;
  int n, cap;
} List;
void *mem(size_t n);
char *str(const char *s);
char *fmt(const char *s, ...);
void add(List *a, void *v);
typedef struct {
  const char *file;
  int line, col;
} Loc;
_Noreturn void fail(Loc l, const char *s, ...);
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Def Def;
typedef struct Function Function;
typedef struct Type Type;
typedef struct Spec Spec;
typedef struct Instance Instance;
typedef struct Field Field;
typedef struct Module Module;
typedef struct {
  char *name, *alias;
  Loc loc;
  Module *target;
} Import;
typedef struct {
  char *name;
  Stmt *binding;
  bool public;
  int state;
} Constant;
struct Module {
  char *name, *path;
  List imports, defs, functions, constants;
  Function *entry;
  bool root;
  int state;
};

struct Spec {
  bool resolved;
  Loc loc;
  char *name;
  Spec *elem, *other; /* Union specifications retain both alternative types. */
  int kind, count;
};
enum {
  SPEC_NAME,
  SPEC_REF,
  SPEC_ARRAY,
  SPEC_GROW,
  SPEC_SLICE,
  SPEC_UNION /* An inline set of alternative types. */
};
enum {
  E_INT,
  E_FLOAT,
  E_STRING,
  E_BOOL,
  E_NAME,
  E_SELF,
  E_BIN,
  E_UNARY,
  E_CALL,
  E_MEMBER,
  E_INDEX,
  E_RECORD,
  E_ARRAY,
  E_FUNCTION,
  E_IF,
  E_SAVED,    /* A compiler-owned value snapshot, never parsed from source. */
  E_SCOPE,    /* A lexical focus scope producing one value. */
  E_EXPECT,   /* Infer a contextual numeric type without evaluating the hint. */
  E_PRESENT,  /* Postfix nil presence test; it does not unwrap a value. */
  E_COALESCE, /* Lazy nil replacement, below unary and above binary operators.
               */
  E_CHAIN     /* Comparisons share operands and short-circuit left to right. */
};
struct Expr {
  Spec *constant_spec;
  bool error_constructor;
  bool optional;        /* This postfix link starts a nil-propagating guard. */
  void *compiler_value; /* Internal full Value, retaining borrow provenance. */
  int kind;
  Loc loc;
  char *text;
  Expr *a, *b, *c;
  Function *function;
  List args, names;
  int saved_slot; /* Scalar slot referenced by compiler-generated E_SAVED. */
  int scope_kind,
      scope_count; /* Explicit lexical scope or enclosing it^ selection. */
  size_t length; /* Byte length of a string literal, including embedded NULs. */
  List bindings; /* Lexical declarations preceding a scoped expression. */
};
/* Statements include parallel declarations, loop jumps, and reserved first^
 * initialization. */
enum {
  S_BIND,
  S_ASSIGN,
  S_EXPR,
  S_RETURN,
  S_IF,
  S_WHILE,
  S_BLOCK,
  S_BIND_MANY,
  S_BREAK,
  S_NEXT,
  S_INIT,
  S_INIT_MANY
};
struct Stmt {
  int kind;
  Loc loc;
  char *name;
  bool mut;
  Spec *spec;
  Expr *a, *b;
  char *op; /* The arithmetic operator of a compound assignment. */
  List targets,
      values; /* Parallel writes evaluate all addresses/values first. */
  List body, other;
  List update; /* Loop updates also run after a targeted next^. */
  List prolog, pre, first, last, epilog; /* Ordered loop sections. */
  List focuses; /* Header bindings whose last successful values are retained. */
  bool integer; /* A compiler-generated counted-loop integer requirement. */
  bool optional; /* Nil-guarded assignment evaluates only present pairs. */
  bool internal; /* A compiler-generated write to a read-only focus. */
  int depth;     /* A jump counts enclosing loops, starting at one. */
};
typedef struct {
  Loc loc;
  char *name;
  Spec *spec;
  bool self;
} Param;
struct Function {
  Function *enclosing; /* Lexical enclosing function, without a captured
                          activation. */
  Module *module;
  bool public;
  Loc loc;
  int lexical; /* Source lexical depth; noncapturing literals keep scope
                  numbering. */
  char *name;
  Def *owner;
  List params, body;
  Spec *result;
  Type *required_result;
  bool procedure, templ, abstract, priv, override, mutable_self;
};
struct Field {
  Loc loc;
  char *name;
  Spec *spec;
  Type *type;
  Expr *init;
  Def *owner, *initializer_owner;
  bool abstract, priv;
  int index;
};
struct Def {
  Module *module;
  bool public, error_variant;
  Loc loc;
  char *name;
  List base_names, bases, own_fields, own_methods;
  List union_specs,
      alternatives; /* Declared union alternatives and resolved types. */
  List fields, methods;
  int state, id;
  bool abstract;
  Type *type;
};
enum {
  T_VOID,
  T_BOOL,
  T_U8,
  T_U16,
  T_U32,
  T_U64,
  T_U128,
  T_I8,
  T_I16,
  T_I32,
  T_I64,
  T_I128,
  T_F8,
  T_F16,
  T_F32,
  T_F64,
  T_STRING,
  T_NIL,
  T_STRUCT,
  T_REF,
  T_ARRAY,
  T_GROW,
  T_SLICE,
  T_UNION
};
struct Type {
  char *symbol;
  int kind, id, count;
  Type *elem;
  Def *def;
  size_t size, align;
  size_t payload_align, payload_bytes, payload_offset;
  bool refs, owned;
  bool structural; /* Anonymous union identity is its canonical alternative set.
                    */
};
typedef struct {
  List modules, module_paths;
  Module *emitting;
  const char *stdlib_dir;
  List defs, functions, types, instances, strings;
  Function *entry;
  int stack_count;
  char *source;
  const char *path;
} Program;
void parse(Program *p, const char *path);
void parse_file(Program *p, const char *path, Module *module);
void resolve_modules(Program *p);
void analyze(Program *p);
Type *type_spec(Program *p, Spec *s);
Type *type_builtin(Program *p, int kind);
Type *type_wrap(Program *p, int kind, Type *elem, int count);
char *type_name(Type *t);
const char *builtin_name(int kind);
bool kind_integer(int kind);
bool kind_unsigned(int kind);
bool kind_float(int kind);
int numeric_bits(int kind);
bool integer_fits(const char *text, int kind);
char *float_constant(const char *text, int kind, Loc loc);
bool derives(Def *d, Def *base);

/* The IR contains typed storage and explicit construction/region operations. */
enum {
  I_CONST,
  I_STRING,
  I_COPY,
  I_ZERO,
  I_BIN,
  I_UNARY,
  I_ADDR,
  I_DEREF,
  I_FIELD,
  I_INDEX,
  I_CALL,
  I_NATIVE,
  I_PRINT,
  I_ASSERT,
  I_LABEL,
  I_BR,
  I_CBR,
  I_RETURN,
  I_REGION,
  I_RELEASE,
  I_PUSH,
  I_LENGTH,
  I_CONVERT,
  I_POP,
  I_CLEAR,
  I_SLICE,
  I_APPEND,
  I_UNION,
  I_CASE_IS,
  I_CASE_GET,
  I_TRUNCATE,
  I_STACK_BEGIN,
  I_RANGE_GUARD
};
enum {
  RANGE_SLOT,
  RANGE_CONST,
  RANGE_IV,
  RANGE_LENGTH,
  RANGE_ADD,
  RANGE_SUB,
  RANGE_MUL
};
typedef struct RangeExpr RangeExpr;
struct RangeExpr {
  int kind, slot;
  char *text;
  bool indirect;
  Type *type;
  RangeExpr *a, *b;
};
typedef struct {
  int induction, comparison, limit;
  RangeExpr *upper;
  List indices, arrays, data;
} RangePlan;
typedef struct {
  int op;
  Loc loc;
  int dst, a, b, label, other;
  char *text;
  size_t length; /* Explicit byte length for I_STRING. */
  Type *type;
  Instance *callee;
  int stack_index;
  bool push_at_top;
  bool index_proven;
  int index_data;
  RangePlan *range;
  List args;
} Instr;
typedef struct {
  Type *type;
  bool alias;
  int owner;
} Slot;
struct Instance {
  Function *fn;
  Def *self;
  List params, slots, code;
  Type *result;
  char *symbol;
  int id, state, labels, result_slot;
  char *result_local;
  int stack_own, stack_need;
  bool may_shrink, emitted, cross_module;
};
void plan_stacks(Program *p);
void analyze_array_growth(Program *p);
void version_range_loops(Program *p);
void report_stacks(Program *p);
void emit_llvm(Program *p, const char *path);
void emit_llvm_builtins(FILE *f);
char *digest_bytes(const void *data, size_t n);
char *digest_file(const char *path);
char *link_identity(const char *text);
const char *tool_resource(const char *compiler, const char *fallback,
                          const char *relative);
int run_process(char *const *args);
int build_objects(Program *p, const char *compiler, const char *clang,
                  const char *runtime, const char *opt, const char *out,
                  const char *cache, bool reuse, bool verbose);
void dump_ir(Program *p, const char *path);
char *llvm_type(Type *t);
#endif
