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
  List c_headers; /* includeC^ headers its externC^ shims read. */
  Function *entry;
  bool root;
  int state;
};

struct Spec {
  bool resolved;
  Loc loc;
  char *name;
  Spec *elem, *other; /* Binary type operands; elem holds a callable result. */
  int kind, count;
  List params; /* Parameter specifications of a callable signature. */
  bool procedure; /* A p^ signature carries the procedure effect. */
  bool foreign;   /* externC^ before a signature: a C function pointer. */
};
enum {
  SPEC_NAME,
  SPEC_REF,
  SPEC_ARRAY,
  SPEC_GROW,
  SPEC_SLICE,
  SPEC_UNION, /* An inline set of alternative types. */
  SPEC_FUNCTION, /* A callable signature ending in a semicolon. */
  SPEC_INTERSECTION, /* A value satisfying every callable signature. */
  SPEC_DYN, /* dyn^ D, valid only as ref^ dyn^ D. */
  SPEC_TUPLE /* -> (A, B): multiple results, params holding each. */
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
  E_FITS,     /* Type conformance predicate; its right operand is a Spec. */
  E_TRY,      /* Unwrap success or transfer an explicitly returned error. */
  E_CATCH,    /* Replace an error value with a lazily evaluated expression. */
  E_COALESCE, /* Lazy nil replacement, below unary and above binary operators.
               */
  E_CHAIN,    /* Comparisons share operands and short-circuit left to right. */
  E_SLICE,    /* A range index: text is "::" or "to^"; b/c are optional bounds. */
  E_PACK,     /* Compiler-only: trailing arguments of a variadic call, or the
                 single forwarded sequence in a. */
  E_PANIC,    /* panic^ a: end the process; it produces no value. */
  E_RANGE     /* a :: b or a to^ b (text): a range^ value; a or b may be omitted. */
};
struct Expr {
  Loc name_loc; /* Exact member spelling, distinct from receiver diagnostics. */
  Spec *test_spec; /* The type operand of fits^, or the type of an empty
                      grow^ T[] value; never evaluated as a value. */
  Spec *constant_spec;
  bool optional;        /* This postfix link starts a nil-propagating guard. */
  bool tuple; /* return^ a, b: a record of the multiple result type. */
  bool spread;          /* A trailing ... forwards this sequence to a variadic
                           parameter. */
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
  S_INIT_MANY,
  S_HANDLE /* A physical block body with ordered error handling arms. */
};
/* One ordered, typed or default arm of a physical block's catch^ clause. */
typedef struct {
  Loc loc;    /* Source location of the catch^ arm. */
  Spec *spec; /* Error type filter, or NULL for the final default arm. */
  List body;  /* Statements evaluated with the caught error as it^. */
} CatchArm;
struct Stmt {
  List handlers;     /* Ordered CatchArm pointers for S_HANDLE. */
  bool loop_handler; /* A loop catches main^ only, with outer names visible. */
  bool scoped_handlers; /* If arms share the if^ body's lexical scope depth. */
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
  bool flow_alias; /* A compiler-generated match snapshot shares type facts. */
  bool internal; /* A compiler-generated write to a read-only focus. */
  int depth;     /* A jump counts enclosing loops, starting at one. */
};
typedef struct {
  Loc loc;
  char *name;
  Spec *spec;
  bool self;
  Type *inferred; /* Body-derived constraint, independent of call sites. */
} Param;
struct Function {
  Loc declaration;     /* Name token for editor navigation. */
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
  char *external; /* C symbol of an externC^ declaration, which has no body. */
  bool intrinsic; /* externLLVM^ binds an LLVM intrinsic instead of a C symbol. */
  bool procedure, templ, abstract, priv, override, mutable_self;
  bool overload, operator_member, right_receiver; /* Explicit member roles. */
  bool exported; /* exportC^: callable from C under its own name. */
  bool lhat_export; /* exportL^: registered as an L^ host function. */
  bool variadic; /* The last parameter "..." collects trailing arguments as a
                    slice. */
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
  char *c_name; /* externC^ def^: the C type of this name it lays out as. */
  bool opaque;  /* externC^ def^ Name; with no body, usable only as ref^. */
  bool lhat_value; /* exportL^ def^: an L^ host value type, its methods members. */
  bool lhat_data;  /* exportL^ ref^ def^: an L^ host data type over ref^ to it. */
  bool tuple; /* The anonymous struct of multiple results, fields 0, 1, ... */
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
  T_UNION,
  T_NEVER, /* Compiler-only type for an expression with no normal value path. */
  T_FUNCTION, /* A callable signature, optionally tied to a static declaration. */
  T_INTERSECTION, /* A canonical set of callable signatures. */
  T_DYN, /* ref^ dyn^ D: data reference and method table of a type composing
           elem's def. */
  T_CFUNC /* externC^ f^ ...;: a C function pointer with elem's signature. */
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
  List params, arms; /* Callable parameters or intersection members. */
  Type *result; /* The result of a callable signature. */
  Function *function; /* A noncapturing declaration known at compile time. */
  bool procedure; /* The callable may have procedure effects. */
};
typedef struct {
  bool library_root; /* Check a standalone module using library rules. */
  bool tooling; /* Retain source and checked resolutions for editor clients. */
  List sources, overlays, resolutions; /* ToolSource and ToolRef records. */
  List modules, module_paths;
  Module *emitting;
  const char *stdlib_dir;
  List defs, functions, types, instances, strings;
  List dyn_tables; /* DynTable records, one per converted concrete type and
                      dyn^ def. */
  Function *entry;
  int stack_count;
  bool callbacks; /* Some exportC^ function may be entered from C. */
  bool crt_dll;   /* --crt-dll: build and link against the C runtime DLL. */
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
  I_STACK_SAVE,   /* Save the caller-owned result stack frontier. */
  I_STACK_REWIND, /* Restore a result construction checkpoint after an error. */
  I_RANGE_GUARD,
  I_TERMINATE, /* Exit with the string in slot a; label 1 marks panic^. */
  I_DYN,       /* Pair reference a with method table label. */
  I_DYNCALL,   /* Call method table entry label through args[0]. */
  I_DYNIS,     /* Test whether dyn^ a holds type: exactly ref^ T, or ref^ dyn^ D's composers. */
  I_DYNCAST,   /* Swap dyn^ a's table for the ref^ dyn^ type of the same value. */
  I_DYNDATA,   /* The data reference of dyn^ a, proven to be type. */
  I_CASE_AT,   /* Place dst at union a's payload for alternative label; b also sets the tag. */
  I_SITE       /* dst = the static record of where an error is built: its source, line,
                  column and message; text is the source (length bytes), a NUL, the message. */
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
  List targets; /* I_DYNCALL: every Instance a table holds at its entry. */
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
  int callback_need; /* exportC^ entries: stacks used before any callback. */
  bool may_shrink, emitted, cross_module;
  uint64_t shrinks; /* Parameters through which this may shrink flex^ storage. */
};
/* The methods of def iface specialized for concrete, by iface method index;
 * entries are NULL for methods that cannot be called through dyn^. */
typedef struct {
  Def *concrete, *iface;
  List entries;
  int id;
} DynTable;
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
/* Compile each module and link them with the runtime and C libraries named
 * by --link-lib. */
int build_objects(Program *p, const char *compiler, const char *clang,
                  const char *runtime, const char *opt, const char *out,
                  const char *cache, bool reuse, bool verbose,
                  List *libraries, List *c_includes);
void dump_ir(Program *p, const char *path);
/* Write the C shims for externC^ functions that take or return a def^ by
 * value, or that come from modules with includeC^ headers (headers selects
 * which); false when the program needs none. */
bool emit_c_shims(Program *p, const char *path, bool headers);
/* The C symbol of an externC^ or exportC^ function (the latter's name
 * without its module path). */
const char *c_function_symbol(Function *f);
/* The exportL^ ref^ def^ that t, a ref^ (or a ref^ | nil^), refers to. */
Def *lhat_data_def(Type *t);
char *llvm_type(Type *t);
#endif
