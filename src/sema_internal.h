#ifndef RHAT_SEMA_INTERNAL_H
#define RHAT_SEMA_INTERNAL_H
#include "rhat.h"

/* Private interface between semantic IR construction and type inference.
 * This is not part of the compiler's public API. */
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

#endif
