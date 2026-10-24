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

struct Spec {
  Loc loc;
  char *name;
  Spec *elem;
  int kind, count;
};
enum { SPEC_NAME, SPEC_REF, SPEC_ARRAY, SPEC_GROW, SPEC_SLICE };
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
  E_IF
};
struct Expr {
  int kind;
  Loc loc;
  char *text;
  Expr *a, *b, *c;
  Function *function;
  List args, names;
};
enum { S_BIND, S_ASSIGN, S_EXPR, S_RETURN, S_IF, S_WHILE, S_BLOCK };
struct Stmt {
  int kind;
  Loc loc;
  char *name;
  bool mut;
  Spec *spec;
  Expr *a, *b;
  List body, other;
};
typedef struct {
  Loc loc;
  char *name;
  Spec *spec;
  bool self;
} Param;
struct Function {
  Loc loc;
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
  Loc loc;
  char *name;
  List base_names, bases, own_fields, own_methods;
  List union_names, alternatives;
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
  T_STRUCT,
  T_REF,
  T_ARRAY,
  T_GROW,
  T_SLICE,
  T_UNION
};
struct Type {
  int kind, id, count;
  Type *elem;
  Def *def;
  size_t size, align;
  size_t payload_align, payload_bytes, payload_offset;
  bool refs, owned;
};
typedef struct {
  List defs, functions, types, instances, strings;
  Function *entry;
  int stack_count;
  char *source;
  const char *path;
} Program;
void parse(Program *p, const char *path);
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
  I_STACK_BEGIN
};
typedef struct {
  int op;
  Loc loc;
  int dst, a, b, label, other;
  char *text;
  Type *type;
  Instance *callee;
  int stack_index;
  bool push_at_top;
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
  bool may_shrink;
};
void plan_stacks(Program *p);
void analyze_array_growth(Program *p);
void report_stacks(Program *p);
void emit_llvm(Program *p, const char *path);
void dump_ir(Program *p, const char *path);
char *llvm_type(Type *t);
#endif
