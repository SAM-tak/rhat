#include "sema.h"
#include "tooling.h"

bool callable_type(Type *t)
{
    return t && (t->kind == T_FUNCTION || t->kind == T_INTERSECTION);
}

bool capturing_callable(Type *t)
{
    if (!callable_type(t)) {
        return false;
    }
    if (t->kind == T_FUNCTION) {
        return t->function && t->function->environment;
    }
    for (int j = 0; j < t->arms.n; j++) {
        if (capturing_callable(t->arms.v[j])) {
            return true;
        }
    }
    return false;
}

Var *captured_variable(Context *c, Expr *e)
{
    if (e->capture_index >= c->in->fn->captures.n) {
        fail(e->loc, "invalid lexical capture");
    }
    Param *param = c->in->fn->captures.v[e->capture_index];
    Var *v = lookup(c, param->name);
    if (!v) {
        fail(e->loc, "capture '%s' requires its enclosing call", e->text);
    }
    return v;
}

Type *closure_type(Context *c, Function *f)
{
    if (!f->captures.n || f->environment) {
        return callable_declaration(c->p, f);
    }
    if (f->params.n + f->captures.n > 64) {
        fail(f->loc, "capturing functions support at most 64 written and hidden parameters");
    }
    List captures = {0}, types = {0};
    for (int j = 0; j < f->captures.n; j++) {
        Param *source = f->captures.v[j];
        Expr *e = source->capture_source;
        Type *type = expression_type(c, e, NULL);
        if (!type) {
            return NULL;
        }
        Var *v = e->kind == E_CAPTURE ? captured_variable(c, e) : selected(c, e);
        Param *param = mem(sizeof(*param));
        Stmt *binding = mem(sizeof(*binding));
        *binding = *source->capture;
        binding->mut = e->kind == E_CAPTURE ? !v->captured_let : v->mut;
        binding->loc = v->declaration.file ? v->declaration : binding->loc;
        *param = *source;
        param->capture = binding;
        param->inferred = type_wrap(c->p, T_REF, type, 0);
        add(&captures, param);
        add(&types, param->inferred);
    }
    for (int j = 0; j < c->p->functions.n; j++) {
        Function *clone = c->p->functions.v[j];
        if (clone->closure_origin != f || clone->captures.n != captures.n) {
            continue;
        }
        bool same = true;
        for (int k = 0; k < captures.n; k++) {
            Param *a = captures.v[k], *b = clone->captures.v[k];
            same &= a->inferred == b->inferred && a->capture->mut == b->capture->mut;
        }
        if (same) {
            return callable_declaration(c->p, clone);
        }
    }
    Function *clone = mem(sizeof(*clone));
    *clone = *f;
    clone->closure_origin = f;
    clone->captures = captures;
    clone->instances = (List){0};
    clone->environment = type_tuple(c->p, &types, f->loc);
    clone->environment->symbol = fmt("%%rh_capture_%d", clone->environment->id);
    add(&c->p->functions, clone);
    return callable_declaration(c->p, clone);
}

Type *callable_signature(Program *p, List *params, Type *result, bool procedure, Function *function)
{
    for (int j = 0; j < p->types.n; j++) {
        Type *t = p->types.v[j];
        if (t->kind != T_FUNCTION || t->result != result || t->procedure != procedure ||
            t->function != function || t->params.n != params->n) {
            continue;
        }
        bool same = true;
        for (int k = 0; k < params->n; k++) {
            same &= t->params.v[k] == params->v[k];
        }
        if (same) {
            return t;
        }
    }
    Type *t = mem(sizeof(*t));
    t->kind = T_FUNCTION;
    t->id = p->types.n;
    t->params = *params;
    t->result = result;
    t->procedure = procedure;
    t->function = function;
    t->size = t->align = 1;
    if (function && function->environment) {
        t->size = function->environment->size;
        t->align = function->environment->align;
        t->refs = true;
    }
    add(&p->types, t);
    return t;
}

Type *callable_declaration(Program *p, Function *f)
{
    if (f->invalid) {
        tool_abort();
    }
    List params = {0};
    for (int j = 0; j < f->params.n; j++) {
        Param *a = f->params.v[j];
        add(&params, a->self ? type_wrap(p, T_REF, f->owner->type, 0) : parameter_type(p, a));
    }
    Type *result = f->result ? type_spec(p, f->result) : f->required_result;
    bool known = !f->templ && !f->abstract && (!f->captures.n || f->environment);
    for (int j = 0; j < params.n; j++) {
        known &= params.v[j] != NULL;
    }
    if (known && !result) {
        result = instance_for(p, f, f->owner, &params, f->loc)->result;
    }
    for (int j = 0; known && !result && j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->fn == f) {
            result = in->result;
        }
    }
    return callable_signature(p, &params, result, f->procedure, f);
}

static void intersection_arms(List *arms, Type *t)
{
    if (t->kind == T_INTERSECTION) {
        for (int j = 0; j < t->arms.n; j++) {
            intersection_arms(arms, t->arms.v[j]);
        }
        return;
    }
    for (int j = 0; j < arms->n; j++) {
        if (arms->v[j] == t) {
            return;
        }
    }
    add(arms, t);
}

Type *callable_intersection(Program *p, Type *a, Type *b, Loc loc)
{
    if (capturing_callable(a) || capturing_callable(b)) {
        fail(loc, "capturing functions cannot form callable intersections");
    }
    if (!callable_type(a) || !callable_type(b)) {
        fail(loc, "intersection '&' requires function or procedure types");
    }
    List arms = {0};
    intersection_arms(&arms, a);
    intersection_arms(&arms, b);
    for (int j = 1; j < arms.n; j++) {
        Type *t = arms.v[j];
        int k = j;
        while (k && ((Type *)arms.v[k - 1])->id > t->id) {
            arms.v[k] = arms.v[k - 1];
            k--;
        }
        arms.v[k] = t;
    }
    if (arms.n == 1) {
        return arms.v[0];
    }
    for (int j = 0; j < p->types.n; j++) {
        Type *t = p->types.v[j];
        if (t->kind != T_INTERSECTION || t->arms.n != arms.n) {
            continue;
        }
        bool same = true;
        for (int k = 0; k < arms.n; k++) {
            same &= arms.v[k] == t->arms.v[k];
        }
        if (same) {
            return t;
        }
    }
    Type *t = mem(sizeof(*t));
    t->kind = T_INTERSECTION;
    t->id = p->types.n;
    t->arms = arms;
    t->size = t->align = 1;
    add(&p->types, t);
    return t;
}

bool callable_accepts(Type *want, Type *actual)
{
    if (!callable_type(want) || !callable_type(actual)) {
        return false;
    }
    if (want == actual) {
        return true;
    }
    if (want->kind == T_INTERSECTION) {
        for (int j = 0; j < want->arms.n; j++) {
            if (!callable_accepts(want->arms.v[j], actual)) {
                return false;
            }
        }
        return true;
    }
    if (actual->kind == T_INTERSECTION) {
        for (int j = 0; j < actual->arms.n; j++) {
            if (callable_accepts(want, actual->arms.v[j])) {
                return true;
            }
        }
        return false;
    }
    if (actual->function && actual->function->mutable_self &&
        (!want->function || !want->function->mutable_self)) {
        return false;
    }
    if (want->procedure != actual->procedure || want->params.n != actual->params.n) {
        return false;
    }
    if (want->result && actual->result && !parameter_accepts(want->result, actual->result)) {
        return false;
    }
    for (int j = 0; j < want->params.n; j++) {
        Type *a = actual->params.v[j], *w = want->params.v[j];
        if (a && w && !parameter_accepts(a, w)) {
            return false;
        }
    }
    return true;
}

static Type *returned(Type *t)
{
    return t && t->kind != T_VOID ? t : NULL;
}

Type *c_function_arm(Type *want, Type *actual)
{
    if (want->kind == T_UNION) {
        for (int j = 0; j < want->def->alternatives.n; j++) {
            Type *arm = c_function_arm(want->def->alternatives.v[j], actual);
            if (arm) {
                return arm;
            }
        }
        return NULL;
    }
    Function *f = actual->kind == T_FUNCTION ? actual->function : NULL;
    if (want->kind != T_CFUNC || !f || !(f->exported || (f->external && !f->intrinsic))) {
        return NULL;
    }
    Type *signature = want->elem;
    bool same = signature->procedure == actual->procedure &&
                signature->params.n == actual->params.n &&
                returned(signature->result) == returned(actual->result);
    for (int j = 0; same && j < actual->params.n; j++) {
        same = signature->params.v[j] == actual->params.v[j];
    }
    return same ? want : NULL;
}

const char *c_function_symbol(Function *f)
{
    const char *dot = strrchr(f->name, '.');
    return f->external ? f->external : dot ? dot + 1 : f->name;
}

Type *callable_group(Program *p, Def *owner, const char *name)
{
    List *functions = owner ? &owner->methods : &p->functions;
    Type *type = NULL;
    for (int j = 0; j < functions->n; j++) {
        Function *f = functions->v[j];
        if (!strcmp(f->name, name)) {
            Type *arm = callable_declaration(p, f);
            type = type ? callable_intersection(p, type, arm, f->loc) : arm;
        }
    }
    return type;
}

// A variadic function is never overloaded, so it stays a candidate at any
// arity and the call reports missing fixed arguments.
static bool accepts_arity(Function *f, int arity)
{
    return f->params.n == arity || f->variadic;
}

List callable_candidates(Type *type, int arity)
{
    List found = {0};
    if (!callable_type(type)) {
        return found;
    }
    if (type->kind == T_FUNCTION) {
        if (type->function && accepts_arity(type->function, arity)) {
            add(&found, type->function);
        }
        return found;
    }
    for (int j = 0; j < type->arms.n; j++) {
        List arm = callable_candidates(type->arms.v[j], arity);
        for (int k = 0; k < arm.n; k++) {
            bool duplicate = false;
            for (int n = 0; n < found.n; n++) {
                duplicate |= found.v[n] == arm.v[k];
            }
            if (!duplicate) {
                add(&found, arm.v[k]);
            }
        }
    }
    return found;
}

char *callable_identity(Type *type)
{
    if (!type) {
        return "?";
    }
    char *identity = type_name(type);
    if (type->elem && type->kind != T_STRUCT) {
        identity = fmt("%s:%s", identity, callable_identity(type->elem));
    }
    if (type->kind == T_INTERSECTION) {
        for (int j = 0; j < type->arms.n; j++) {
            identity = fmt("%s:%s", identity, callable_identity(type->arms.v[j]));
        }
    }
    if (type->function) {
        Function *f = type->function;
        identity = fmt("%s:%s:%s:%d:%d", identity, f->loc.file, f->name, f->loc.line, f->loc.col);
        for (int j = 0; j < f->captures.n; j++) {
            Param *capture = f->captures.v[j];
            identity = fmt("%s:capture:%s:%d", identity, callable_identity(capture->inferred),
                           capture->capture->mut);
        }
    }
    return identity;
}

Type *callable_hint(Type *type, int arity, int index)
{
    if (!callable_type(type)) {
        return NULL;
    }
    if (type->kind == T_FUNCTION) {
        return type->params.n == arity && index < arity ? type->params.v[index] : NULL;
    }
    Type *hint = NULL;
    bool found = false;
    for (int j = 0; j < type->arms.n; j++) {
        Type *arm = type->arms.v[j];
        if (arm->params.n == arity) {
            Type *next = callable_hint(arm, arity, index);
            if (found && next != hint) {
                return NULL;
            }
            hint = next;
            found = true;
        }
    }
    return hint;
}

Type *callable_contract(Type *type, List *actual)
{
    if (!callable_type(type)) {
        return NULL;
    }
    if (type->kind == T_INTERSECTION) {
        Type *found = NULL;
        for (int j = 0; j < type->arms.n; j++) {
            Type *arm = callable_contract(type->arms.v[j], actual);
            if (arm && found) {
                return NULL;
            }
            if (arm) {
                found = arm;
            }
        }
        return found;
    }
    if (actual->n != type->params.n) {
        return NULL;
    }
    for (int j = 0; j < actual->n; j++) {
        Type *want = type->params.v[j], *arg = actual->v[j];
        if (want && arg && !parameter_accepts(want, arg)) {
            return NULL;
        }
    }
    return type;
}

List overload_candidates(Program *p, Def *owner, const char *name, int arity)
{
    List found = {0};
    List *functions = owner ? &owner->methods : &p->functions;
    for (int j = 0; j < functions->n; j++) {
        Function *f = functions->v[j];
        if (!strcmp(f->name, name) && accepts_arity(f, arity)) {
            add(&found, f);
        }
    }
    return found;
}

Type *overload_hint(Program *p, List *candidates, int index, Expr *arg)
{
    // A number literal can only become a number, so other arms cast no vote.
    Expr *number = arg && arg->kind == E_UNARY && !strcmp(arg->text, "-") ? arg->a : arg;
    bool literal =
        number && !number->constant_spec && (number->kind == E_INT || number->kind == E_FLOAT);
    Type *hint = NULL;
    bool first = true;
    for (int j = 0; j < candidates->n; j++) {
        Function *f = candidates->v[j];
        Param *a = index < f->params.n ? f->params.v[index] : NULL;
        Type *t = a && a->self ? type_wrap(p, T_REF, f->owner->type, 0) : parameter_hint(p, a, arg);
        if (literal && t && !kind_integer(t->kind) && !kind_float(t->kind) && t->kind != T_UNION) {
            continue;
        }
        if (!first && t != hint) {
            return NULL;
        }
        hint = t;
        first = false;
    }
    return hint;
}

// A typed constant or cast is already a value, even when its AST contains a
// literal. Only unspecialized numeric syntax can take a candidate's type.
static bool literal_argument(Program *p, Type *want, Expr *arg, Type **actual)
{
    if (!arg || arg->constant_spec || !want) {
        return true;
    }
    Expr *number = arg;
    bool negative = arg->kind == E_UNARY && !strcmp(arg->text, "-");
    if (negative) {
        number = arg->a;
    }
    if (number->constant_spec || (number->kind != E_INT && number->kind != E_FLOAT)) {
        return true;
    }
    Type *hint = want->kind == T_UNION ? union_literal_hint(want, arg) : want;
    if (!hint) {
        hint = type_builtin(p, number->kind == E_INT ? T_I32 : T_F64);
        if (!parameter_accepts(want, hint)) {
            return false;
        }
    }
    const char *text = negative ? fmt("-%s", number->text) : number->text;
    bool fits = kind_float(hint->kind)                              ? float_fits(text, hint->kind)
                : kind_integer(hint->kind) && number->kind == E_INT ? integer_fits(text, hint->kind)
                                                                    : false;
    if (fits) {
        *actual = hint;
    }
    return fits;
}

// What f returns when declared or already inferred; never makes an instance.
static Type *known_result(Program *p, Function *f)
{
    if (f->templ) {
        return NULL;
    }
    if (f->result || f->required_result) {
        return f->result ? type_spec(p, f->result) : f->required_result;
    }
    for (int j = 0; j < f->instances.n; j++) {
        Instance *in = f->instances.v[j];
        if (in->result) {
            return in->result;
        }
    }
    return NULL;
}

static Expr *call_argument(Expr *call, Function *f, int offset, int index)
{
    int arg = index - (offset && !f->right_receiver ? offset : 0);
    return call && arg >= 0 && arg < call->args.n ? call->args.v[arg] : NULL;
}

Function *overload_select(Program *p, List *candidates, List *actual, Expr *call, int offset,
                          Type *expected, Loc loc, bool strict)
{
    Function *found = NULL, *ambiguous = NULL, *preferred = NULL;
    int preferences = 0;
    bool pending = false;
    for (int j = 0; j < candidates->n; j++) {
        Function *f = candidates->v[j];
        if (f->params.n != actual->n) {
            continue;
        }
        bool match = true;
        for (int k = 0; k < actual->n && match; k++) {
            Param *a = f->params.v[k];
            Type *t = actual->v[k], *want = parameter_type(p, a);
            if (!a->self && !literal_argument(p, want, call_argument(call, f, offset, k), &t)) {
                match = false;
            }
            else if (!a->self && t && want) {
                match =
                    parameter_accepts(want, f->operator_member ? operator_argument(p, want, t) : t);
            }
        }
        if (match) {
            ambiguous = found ? f : NULL;
            found = found ? found : f;
            // Literal arguments fit several arms; the expected result picks one.
            // An arm whose result is still being inferred decides on a later pass.
            Type *result = expected ? known_result(p, f) : NULL;
            pending |= expected && !result;
            if (expected && result == expected) {
                preferred = f;
                preferences++;
            }
        }
    }
    if (ambiguous) {
        if (preferences == 1) {
            found = preferred;
        }
        else if (!strict || pending) {
            return NULL;
        }
        else {
            fail(loc, "ambiguous overload '%s'; annotate the argument types", ambiguous->name);
        }
    }
    if (!found && strict) {
        fail(loc, "no matching overload%s%s", candidates->n ? " for '" : "",
             candidates->n ? fmt("%s'", ((Function *)candidates->v[0])->name) : "");
    }
    for (int k = 0; found && k < actual->n; k++) {
        Param *param = found->params.v[k];
        Type *t = actual->v[k];
        if (!param->self) {
            literal_argument(p, parameter_type(p, param), call_argument(call, found, offset, k),
                             &t);
            actual->v[k] = t;
        }
    }
    return found;
}

static bool domains_overlap(Program *p, Type *a, Type *b)
{
    if (!a || !b || a == b || parameter_accepts(a, b) || parameter_accepts(b, a)) {
        return true;
    }
    if (a->kind == T_UNION || b->kind == T_UNION) {
        Type *sum = a->kind == T_UNION ? a : b, *other = sum == a ? b : a;
        for (int j = 0; j < sum->def->alternatives.n; j++) {
            if (domains_overlap(p, sum->def->alternatives.v[j], other)) {
                return true;
            }
        }
        return false;
    }
    if (a->kind == T_REF && b->kind == T_REF) {
        return domains_overlap(p, a->elem, b->elem);
    }
    if (a->kind == T_STRUCT && b->kind == T_STRUCT) {
        for (int j = 0; j < p->defs.n; j++) {
            Def *d = p->defs.v[j];
            if (derives(d, a->def) && derives(d, b->def)) {
                return true;
            }
        }
    }
    return callable_type(a) && callable_type(b);
}

bool overload_overlap(Program *p, Function *a, Function *b)
{
    if (a->params.n != b->params.n || a->right_receiver != b->right_receiver) {
        return false;
    }
    for (int j = 0; j < a->params.n; j++) {
        Param *x = a->params.v[j], *y = b->params.v[j];
        if (!x->self && !y->self) {
            Type *xt = parameter_type(p, x), *yt = parameter_type(p, y);
            bool overlap = domains_overlap(p, xt, yt);
            if (a->operator_member && b->operator_member) {
                xt = xt && xt->kind == T_REF ? xt->elem : xt;
                yt = yt && yt->kind == T_REF ? yt->elem : yt;
            }
            if (!overlap && !domains_overlap(p, xt, yt)) {
                return false;
            }
        }
    }
    return true;
}

void overload_validate(Program *p, List *functions)
{
    for (int j = 0; j < functions->n; j++) {
        Function *f = functions->v[j];
        for (int k = 0; k < j; k++) {
            Function *other = functions->v[k];
            if (f->invalid || other->invalid) {
                continue;
            }
            if (f != other && !strcmp(f->name, other->name) && (f->variadic || other->variadic)) {
                TOOL_CHECK(p,
                           fail(f->loc, "variadic function '%s' cannot be overloaded", f->name););
            }
            if (f != other && !strcmp(f->name, other->name) && overload_overlap(p, f, other)) {
                TOOL_CHECK(p, fail(f->loc, "overlapping overload '%s'; duplicate function domains",
                                   f->name););
            }
        }
    }
}

static Function *operator_on(Program *p, Type *receiver, Type *other, const char *op, bool right,
                             Loc loc)
{
    if (receiver && receiver->kind == T_REF) {
        receiver = receiver->elem;
    }
    if (!receiver || receiver->kind != T_STRUCT) {
        return NULL;
    }
    List candidates = overload_candidates(p, receiver->def, fmt("op^%s", op), other ? 2 : 1);
    List filtered = {0}, actual = {0};
    for (int j = 0; j < candidates.n; j++) {
        Function *f = candidates.v[j];
        if (f->operator_member && f->right_receiver == right) {
            add(&filtered, f);
        }
    }
    Type *self = type_wrap(p, T_REF, receiver, 0);
    if (right) {
        add(&actual, other);
    }
    add(&actual, self);
    if (!right && other) {
        add(&actual, other);
    }
    return overload_select(p, &filtered, &actual, NULL, 0, NULL, loc, false);
}

Type *operator_hint(Program *p, const char *op, Type *receiver, bool right, Expr *arg)
{
    if (receiver && receiver->kind == T_REF) {
        receiver = receiver->elem;
    }
    if (!receiver || receiver->kind != T_STRUCT) {
        return NULL;
    }
    bool comparison = strchr("=!<>", op[0]) != NULL;
    const char *name = comparison && strcmp(op, "=") && strcmp(op, "!=") ? "<=>" : op;
    if (!strcmp(name, "!=")) {
        name = "=";
    }
    List candidates = overload_candidates(p, receiver->def, fmt("op^%s", name), 2);
    if (!candidates.n && comparison) {
        candidates = overload_candidates(p, receiver->def, "op^<=>", 2);
    }
    List filtered = {0};
    for (int j = 0; j < candidates.n; j++) {
        Function *f = candidates.v[j];
        if (f->right_receiver == right && f->operator_member) {
            add(&filtered, f);
        }
    }
    Type *hint = overload_hint(p, &filtered, right ? 0 : 1, arg);
    if (hint && hint->kind == T_REF) {
        return arg && (arg->kind == E_RECORD || arg->kind == E_ARRAY || arg->kind == E_INT ||
                       arg->kind == E_FLOAT)
                   ? hint->elem
                   : NULL;
    }
    return hint;
}

Function *operator_select(Program *p, const char *op, Type *a, Type *b, Loc loc)
{
    bool comparison = strchr("=!<>", op[0]) != NULL;
    const char *name = !strcmp(op, "!=") ? "=" : op;
    Function *f = operator_on(p, a, b, name, false, loc);
    if (!f && b) {
        f = operator_on(p, b, a, name, true, loc);
    }
    if (!f && b && comparison && strcmp(name, "<=>")) {
        f = operator_on(p, a, b, "<=>", false, loc);
        if (!f) {
            f = operator_on(p, b, a, "<=>", true, loc);
        }
    }
    return f;
}

Type *operator_result(Program *p, Function *f, Type *a, Type *b, Loc loc)
{
    Type *base = f->right_receiver ? b : a;
    bool read = read_reference(base);
    if (base->kind == T_REF) {
        base = base->elem;
    }
    List actual = {0};
    for (int j = 0; j < f->params.n; j++) {
        Param *param = f->params.v[j];
        add(&actual, param->self ? type_wrap(p, T_REF, base, read)
                                 : operator_argument(p, parameter_type(p, param),
                                                     f->right_receiver ? a : b));
    }
    return instance_for(p, f, base->def, &actual, loc)->result;
}

Type *operator_argument(Program *p, Type *want, Type *actual)
{
    if (want && actual && want->kind == T_REF && actual->kind != T_REF &&
        parameter_accepts(want->elem, actual)) {
        return type_wrap(p, T_REF, actual, want->count);
    }
    return actual;
}
