#ifndef RHAT_SEMA_H
#define RHAT_SEMA_H
#include "rhat.h"

/* The interface shared by semantic IR construction (sema.c and the files
 * split from it) and type inference (check.c). */
/* Local bindings and branch facts shared by inference and IR construction. */
typedef struct Var Var;
typedef struct {
  char *path;
  Type *type;
} PathFact;
struct Var {
  Loc declaration; /* Source binding used by editor resolutions. */
  char *name;
  Type *type;
  int slot, depth, lexical;
  uint64_t root, deps;
  bool mut, parameter, unavailable, pending, hidden;
  unsigned version, path_version;
  Var *alias;     /* Inference-only direct reference target. */
  Type *narrowed; /* A branch-local view; the storage type never changes. */
  List paths;     /* Stable member paths proven within the current branch. */
  bool path_sensitive; /* Guards on this root can depend on aliased storage. */
  Var *source; /* Match snapshots only: valid until the original changes. */
  unsigned source_version;
};
/* A checked or inference-only saved value with borrow provenance. */
typedef struct {
  int id;
  Type *type;
  uint64_t deps, storage;
  Var *var;
  bool addressable, guarded_copy;
  bool held; /* An owned part of a value no binding releases: copying it moves it. */
} Value;
/* Loop targets and scope boundaries used by control-flow analysis. */
typedef struct {
  int end, update, bindings, phase, outside;
  List focuses, saved, first;
} Loop; /* Jump targets carry the ownership cleanup boundary. */
/* A predicate tied to the mutation version of its refined storage. */
typedef struct {
  Expr *test;
  Var *var;
  unsigned version;
  char *path;
  Type *yes, *no;
  int known; /* One for true, minus one for false, zero for a runtime test. */
} NilGuard;
/* Predicate facts local to a semantic traversal. */
typedef struct {
  List guards;
} NilFlow;
/* A saved binding state at a branch or loop boundary. */
typedef struct {
  Type *type;
  unsigned version, path_version;
  List paths;
} NilFact;
/* The error domain and cleanup boundary of an enclosing catch clause. */
typedef struct {
  Type *errors;
  int slot, label, bindings, pending, checkpoint;
  uint64_t deps;
  List facts;
} ErrorTarget;
/* IR-construction state; inference only reads snapshots of this context. */
typedef struct {
  Program *p;
  Instance *in;
  Def *access;
  List vars;
  int depth, root_count, lexical;
  int root_depth[64];
  bool root_external[64];
  bool terminated;
  uint64_t self_roots;
  List default_stack, loops;
  List handlers, pending_owners;
  bool building_result;
  NilFlow flow;
  bool raw_name;
  Expr *raw_place;
  bool in_default;
  Expr *operator_target; /* The operator constructing an owning destination. */
  int operator_destination; /* Valid only when operator_target is non-NULL. */
  List inflight; /* Values a statement still uses while evaluating later ones. */
  uint64_t param_deps[64]; /* The external root of each reference parameter. */
  int result_part; /* One past the slot of the owned result part built in place. */
} Context;

/* Type inference and concrete-instance registry, implemented in check.c. */

/* Find a non-receiver parameter that needs a concrete call shape. */
Param *inferred_parameter(Function *f);

/* Require a body-derived signature or an explicit generic export contract. */
void inferred_export(Function *f);

/* Enforce the module boundary for ordinary call-shape inference. */
void inferred_call(Function *caller, Function *callee, Loc loc);

/* Diagnose unresolved ordinary definitions without materialized calls. */
void inferred_unused(Program *p, Function *f);

/* Solve return types to a fixed point over all known call instances. */
void infer_results(Program *p);

/* Infer the error alternatives transferred from a handled block. */
Type *handler_errors(Context *c, Stmt *s);

/* Infer the value type of a nil-propagating postfix chain. */
Type *chain_type(Context *c, Expr *e);

/* Infer the storage type targeted by a guarded assignment. */
Type *chain_place_type(Context *c, Expr *e);

/* Infer an expression from a snapshot without generating IR. */
Type *expression_type(Context *c, Expr *e, Type *expected);

/* Infer the persistent bindings reserved for a loop first section. */
List first_types(Context *c, Stmt *s);

/* Find or register a concrete call instance without generating its IR. */
Instance *instance_for(Program *p, Function *f, Def *self, List *actual,
                       Loc at);

/* Shared type, name and flow operations, implemented in sema.c. */

/* Collect the declarations represented by a callable name and receiver. */
List overload_candidates(Program *p, Def *owner, const char *name, int arity);
/* Find the common contextual type without choosing an overload arm. */
Type *overload_hint(Program *p, List *candidates, int index, Expr *arg);
/* Select the sole compatible arm; unresolved inference may return NULL. */
Function *overload_select(Program *p, List *candidates, List *actual, Loc loc,
                          bool strict);
/* Whether two declarations admit any common call, ignoring return types. */
bool overload_overlap(Program *p, Function *a, Function *b);
/* Reject overlapping declarations in one callable group. */
void overload_validate(Program *p, List *functions);
/* Intern callable signatures, declaration types and their intersections. */
Type *callable_signature(Program *p, List *params, Type *result, bool procedure,
                         Function *function);
Type *callable_declaration(Program *p, Function *f);
Type *callable_intersection(Program *p, Type *a, Type *b, Loc loc);
Type *callable_group(Program *p, Def *owner, const char *name);
/* Recover static implementations from an unbound callable value. */
List callable_candidates(Type *type, int arity);
/* Include declaration identity in specializations receiving callable values. */
char *callable_identity(Type *type);
/* Read a callable contract during body inference without requiring a body. */
Type *callable_hint(Type *type, int arity, int index);
Type *callable_contract(Type *type, List *actual);
/* Identify callable types and check their structural contracts. */
bool callable_type(Type *t);
bool callable_accepts(Type *want, Type *actual);
/* The C function pointer type in want (itself or a union arm) that the
 * C-ABI declaration actual can be passed as, or NULL. */
Type *c_function_arm(Type *want, Type *actual);
/* Select an operator member; a missing member leaves built-in handling intact. */
Function *operator_select(Program *p, const char *op, Type *a, Type *b, Loc loc);
/* Contextualize a literal operand only when all operator arms agree. */
Type *operator_hint(Program *p, const char *op, Type *receiver, bool right, Expr *arg);
/* Infer the result of a selected operator with its explicit receiver. */
Type *operator_result(Program *p, Function *f, Type *a, Type *b, Loc loc);

/* Split a postfix chain without separating a method from its call. */
Expr *chain_parts(Expr *e, List *steps);

/* Read a statically established predicate result. */
int condition_truth(NilFlow *flow, Expr *e);

/* Join error or result sets, accepting an absent initial set. */
Type *error_join(Program *p, Type *a, Type *b, Loc loc);

/* Collect the leaf error types used to route propagation. */
Type *error_leaves(Program *p, Type *t, Loc loc);

/* Look up a composed nominal field. */
Field *field(Def *d, const char *n);

/* Compute the alternatives selected by a type-conformance branch. */
Type *fits_subset(Program *p, Type *from, Type *to, bool yes, Loc loc);

/* Invalidate member-path refinements after possible aliasing writes. */
void forget_paths(List *vars);

/* Invalidate refinements on roots that may have changed. */
void forget_roots(List *vars, uint64_t roots, bool all);

/* Recognize a postfix chain that contains a nil guard. */
bool guarded_chain(Expr *e);

/* Test whether a type contains a declared error alternative. */
bool has_error(Type *t);

/* Intersect the facts of normally completing control-flow paths. */
List join_exit_facts(List left, List right);

/* Resolve and validate the complete storage layout of a type. */
void layout(Type *t, Loc l);

/* Resolve a fully qualified nominal declaration. */
Def *lookup_def(Program *p, const char *n);

/* Merge branch mutations conservatively into the preceding facts. */
void merge_facts(List *vars, List before, List yes);

/* Look up a composed nominal method. */
Function *method(Def *d, const char *n);

/* Recognize the literal nil value in a comparison. */
bool nil_name(Expr *e);

/* Select a possibly nullable referent or the value itself. */
Type *nil_subject(Type *t);

/* Test whether a type admits nil. */
bool nullable(Type *t);

/* Test whether all alternatives are declared errors. */
bool only_errors(Type *t);

/* Validate an explicitly supplied receiver of an outer method. */
Def *outer_receiver(Function *f, Type *type, Loc loc);

/* Read the refined type of a stable member path. */
Type *path_type(Var *v, const char *path);

/* Replace a variadic call's trailing arguments with one E_PACK argument;
 * offset counts receivers already bound to leading parameters. */
Expr *pack_arguments(Function *f, Expr *e, int offset);

/* The narrowed type of fits^ spec s on a ref^ dyn^ value: ref^ dyn^ D for
 * fits^ dyn^ D, otherwise a reference to exactly the named concrete def. */
Type *dyn_fits_target(Program *p, Spec *s, Loc loc);

/* Reject an exportC^ function whose signature C cannot call. */
void export_validate(Program *p, Function *f);
/* Check an exportL^ def^ as an L^ host value type and mark its methods. */
void lhat_def_validate(Program *p, Def *d);

/* Reject an externC^ signature without a plain C shape or a usable symbol. */
void external_validate(Program *p, Function *f);

/* Apply the true or false facts of a previously checked condition. */
void refine_condition(Program *p, List *vars, NilFlow *flow, Expr *e,
                      bool truth);

/* Record refinements established by a nil-presence predicate. */
void remember_guard(NilFlow *flow, Context *context, Expr *test, Expr *value,
                    bool positive);

/* Record the subsets and mutation versions proven by a predicate. */
void remember_test(NilFlow *flow, Context *context, Expr *test, Expr *value,
                   Type *yes, Type *no);

/* Restore refinements whose mutation versions still match. */
void restore_facts(List *vars, List facts);

/* Restore an input state before analyzing an alternate branch. */
void rewind_facts(List *vars, List facts);

/* Snapshot the current binding refinements and mutation versions. */
List save_facts(List *vars);

/* Resolve a local binding with explicit lexical or focus selection. */
Var *selected(Context *c, Expr *e);

/* Identify a binding and member path suitable for flow refinement. */
bool stable_path(Context *c, Expr *e, Var **root, char **path);

/* Rebuild one postfix operation around a supplied receiver. */
Expr step_expression(Expr *step, Expr *receiver, Expr *member);

/* Test whether a chain link starts a nil guard. */
bool step_optional(Expr *e);

/* Remove error alternatives while preserving nominal success values. */
Type *success_type(Program *p, Type *t, Loc loc);

/* Test whether every source alternative is admitted by a destination. */
bool sum_accepts(Type *to, Type *from);

/* Resolve the lexical declaration selected by this^ and its outer forms. */
Function *this_function(Program *p, Instance *in, Expr *target);

/* Intern an anonymous union while preserving named alternatives. */
Type *type_union(Program *p, Type *a, Type *b, Loc loc);
/* The multiple result type of elems, one per distinct element list. */
Type *type_tuple(Program *p, List *elems, Loc loc);
/* The built-in range^: [start, end) over i64^, an open end held as the
 * minimum i64^. */
Type *type_range(Program *p);
/* Whether t is range^. */
bool range_type(Type *t);
/* What char^(x) casts to: char^ | std.text.Error.Invalid. */
Type *char_cast_type(Program *p, Loc loc);
/* What string^(bytes) casts to: string^ | std.text.Error.Invalid. */
Type *text_cast_type(Program *p, Loc loc);
/* Whether t writes itself as text: a number, bool^, char^ or string^. */
bool textual(Type *t);
/* The type of string^ method name, or NULL when string^ has none. */
Type *text_method_type(Program *p, const char *name, Loc loc);
/* Reject destructuring t into a number of names other than its results. */
void results_check(Type *t, int names, Loc loc);
/* For member name of t: the first variant when t is a union of errors and
 * name is a member all variants share at the same place, else t. */
Type *error_view(Type *t, const char *name);
/* Whether target(...) calls message() on base, a union of errors, which
 * each variant answers. */
bool error_messages(Program *p, Expr *target, int arguments, Type *base);
/* Which of source, line, column and the recorded message name is on error
 * variant t, or -1. */
int error_site(Type *t, const char *name);

/* Select an unambiguous contextual type for a literal. */
Type *union_literal_hint(Type *t, Expr *e);

/* Compute the successful non-nil alternatives of a type. */
Type *without_nil(Program *p, Type *t, Loc loc);

/* Collect parameter constraints from declaration bodies before instantiation. */
void infer_parameters(Program *p);
/* Resolve a declared or body-inferred parameter constraint. */
Type *parameter_type(Program *p, Param *param);
/* Contextualize literals without boxing inferred union members. */
Type *parameter_hint(Program *p, Param *param, Expr *arg);
/* Test a parameter constraint, including nominal composition. */
bool parameter_accepts(Type *want, Type *actual);
/* Avoid redundant declaration instances for locally specialized bodies. */
bool declaration_instance_needed(Program *p, Function *f);

/* Types (types.c), shared with IR construction. */
/* The index of alternative arm in union sum, or -1. */
int union_alternative(Type *sum, Type *arm);
/* Whether every value of from passes fits^ to: an alternative, a callable it
 * satisfies, or a def^ (or ref^ to one) composing to's. */
bool fits_type(Type *from, Type *to);
/* Whether error type to admits every error from may be. */
bool error_accepts(Type *to, Type *from);
/* Whether some error from may be is one that to admits. */
bool error_overlap(Type *to, Type *from);
/* Gather d's fields and methods from its composition bases and its own
 * declaration, checking overrides, overloads and union alternatives. */
void compose(Program *p, Def *d);

/* IR construction (sema.c), shared with the files split from it. */
/* Append an instruction of op at l to the instance under construction. */
Instr *emit(Context *c, int op, Loc l);
/* A new slot of type t; an alias slot is a place another instruction sets. */
int slot(Context *c, Type *t, bool alias);
/* A value of type t in a new slot. */
Value value(Context *c, Type *t);
/* The value of an expression whose evaluation does not go on. */
Value abrupt(Context *c);
/* Whether v is abrupt: evaluation stopped before producing it. */
bool dead(Value v);
/* A new borrow root at lexical depth, or for storage from outside. */
uint64_t root(Context *c, int depth, bool external, Loc l);
/* The value v refers to when it is a ref^, else v. */
Value deref(Context *c, Value v, Loc l);
/* Copy src into dst, converting to dst's union; init skips the store
 * checks of a first write. */
Value copy_to(Context *c, Value dst, Value src, Loc l, bool init);
/* A constant of builtin kind k. */
Value constant(Context *c, int k, const char *text, Loc loc);
/* a op b over a's type; a comparison answers bool^. */
Value operate(Context *c, const char *op, Value a, Value b, Loc loc);
/* The place of union u's alternative arm, setting the tag when it writes. */
int case_at(Context *c, Value u, Type *arm, bool write, Loc loc);
/* An owned operand of try^, catch^ or ?? is built on the data stack of dst,
 * which then takes the value it holds without a copy. */
Value held_operand(Context *c, Expr *e, int dst);
/* The alternative arm of union src, which flow has proven it holds. */
Value case_value(Context *c, Value src, Type *arm, Loc loc);
/* src as type to, into dst unless it is negative: wrapping into a union,
 * projecting a proven subset of one, or taking a C function's address. */
Value convert_value(Context *c, Value src, Type *to, int dst, Loc loc);
/* Branch to yes or no on bool^ condition. */
void nil_branch(Context *c, Value condition, int yes, int no, Loc loc);
/* Place label here. */
void nil_label(Context *c, int label, Loc loc);
/* Jump to label. */
void nil_jump(Context *c, int label, Loc loc);
/* The value nil^. */
Value nil_literal(Context *c, Loc loc);
/* The place of field f of base. */
Value field_value(Context *c, Value base, Field *f, Loc l);
/* Release, innermost first, the owners from bindings and pending owners from
 * pending onward, before leaving their scope early. */
void escape_cleanup(Context *c, int bindings, int pending, Loc loc);
/* Build e as type t into slot dst, or into a new slot when dst is negative. */
Value construct(Context *c, Expr *e, Type *t, int dst);
/* Call operator or method f on a and b (b is a for a unary one). */
Value invoke_operator(Context *c, Function *f, Value a, Value b, Expr *origin);
/* The value of e, typed as expected when that is given. */
Value expression(Context *c, Expr *e, Type *expected);

/* Errors (errors.c). */
/* src as error type to, into dst unless it is negative. */
Value pack_error(Context *c, Value src, Type *to, int dst, Loc loc);
/* The source an error site names: under std/ for the standard library,
 * else as the compiler was given it, with / between directories. */
char *site_source(Program *p, const char *file);
/* The message() of error err, by the variant it holds when it is a union. */
Value error_message(Context *c, Value err, Loc loc);
/* End the program on subject: a panic^ message, or an error that no catch^
 * took, with its message() and where it was built. */
void terminate(Context *c, Value subject, bool panic, Loc loc);
/* Hand error to the innermost catch^, or return it from the function. */
void route_error(Context *c, Value error, Loc loc);
/* Bind the caught error as it^ for a catch^ arm. */
void error_focus(Context *c, Value error, Loc loc);
/* try^ e: its success, or the error routed out; into dst when owned. */
Value try_value(Context *c, Expr *e, int dst);
/* e catch^ replacement, typed as expected, into dst. */
Value catch_value(Context *c, Expr *e, Type *expected, int dst);

/* Built-in text, characters and ranges (text.c). */
/* r.end (i64^ | nil^, nil^ when open) or r.len. */
Value range_member(Context *c, Value r, const char *name, Loc loc);
/* r.contains(i): start <= i and, unless r is open, i < end. */
Value range_contains(Context *c, Value r, Value i, Loc loc);
/* char^(x): the one character of string^ x, or integer x as a scalar
 * value, else std.text.Error.Invalid. */
Value char_cast(Context *c, Expr *e, int destination);
/* dst = the runtime function name applied to args, or no result when k is
 * T_VOID. */
Value helper(Context *c, const char *name, Value *args, int n, int k, Loc loc);
/* string^(bytes): the bytes as a string^ when they are UTF-8, else
 * std.text.Error.Invalid. */
Value text_cast(Context *c, Expr *e, int destination);
/* x.write_text(out) appends x to Builder out; x.tostring() builds a new
 * Builder at the destination with it. */
Value value_text(Context *c, Expr *e, int destination);
/* Append the text and holes of $"..." e to the Builder into. */
void interp_into(Context *c, Expr *e, Value into);
/* $"..." for print^: built in a Builder living until the print, which
 * shows its text. */
Value interp_print(Context *c, Expr *e, List *temps);
/* The built-in methods of string^, which address it by byte offsets. */
Value text_method(Context *c, Expr *e, int destination);
/* The open end of a range^: the minimum i64^. */
#define RANGE_OPEN "-9223372036854775808"

/* More of IR construction (sema.c), used by statements. */
/* The innermost visible binding named name, or NULL. */
Var *lookup(Context *c, const char *name);
/* e evaluated as a place to write, its name read without narrowing. */
Value place_expression(Context *c, Expr *e);
/* The value of binding v, in its slot. */
Value variable(Var *v);
/* Reject storing src into dst when a borrow in src would outlive dst. */
void store_check(Context *c, Value dst, Value src, Loc l);
/* src copied into a new slot, unless it is owned or void. */
Value snapshot(Context *c, Value src, Loc l);
/* Whether src, which may be nil^, holds a value. */
Value present_value(Context *c, Value src, Loc loc);
/* An expression standing for the already computed value. */
Expr *saved_value(Value value, Loc loc);
/* The bool^ of a fits^ test e. */
Value fits_value(Context *c, Expr *e);
/* Walk optional chain e, jumping to absent when a guarded link is nil^;
 * never says whether the chain cannot be nil^. */
Value chain_walk(Context *c, Expr *e, int absent, bool *never);
/* Where a binding of type want builds the owned result: the result, its
 * owned alternative, or that one's owned last part; -1 if it cannot. */
int result_place(Context *c, Type *want, Loc loc);
/* Release, innermost first, the owning bindings from index from on. */
void cleanup(Context *c, int from, Loc l);
/* Reject a callable actual that cannot stand for want without a statically
 * selected implementation. */
void check_callable(Program *p, Type *want, Type *actual, Loc loc);
/* The table of concrete's implementations of iface's methods. */
DynTable *dyn_table(Program *p, Def *concrete, Def *iface, Loc loc);
/* Check what op^= and op^<=> must return. */
void operator_contract(Function *f, Type *result, Loc loc);

/* Statements (statements.c). */
/* A block with ordered catch^ arms. */
void handle_statements(Context *c, Stmt *s);
/* The statements of body, in a scope of their own when scope is set. */
void statements(Context *c, List *body, bool scope);
/* The instance of f, or of method f of self, for argument types actual,
 * its IR built on first use. */
Instance *instantiate(Program *p, Function *f, Def *self, List *actual, Loc at);

#endif
