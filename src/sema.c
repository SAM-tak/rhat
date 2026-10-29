#include "sema.h"
#include "tooling.h"
#include <errno.h>
#include <limits.h>

bool guarded_chain(Expr *e)
{
    return e &&
           (e->kind == E_MEMBER || e->kind == E_INDEX || e->kind == E_SLICE || e->kind == E_CALL) &&
           (e->optional || guarded_chain(e->a));
}

// A method name is not a first-class value; keep it with its call.
Expr *chain_parts(Expr *e, List *steps)
{
    if (e->kind == E_MEMBER || e->kind == E_INDEX || e->kind == E_SLICE ||
        (e->kind == E_CALL && e->a->kind == E_MEMBER)) {
        Expr *base = chain_parts(e->kind == E_CALL ? e->a->a : e->a, steps);
        add(steps, e);
        return base;
    }
    return e;
}

bool step_optional(Expr *e)
{
    return e->kind == E_CALL ? e->a->optional : e->optional;
}

Expr step_expression(Expr *step, Expr *receiver, Expr *member)
{
    Expr copy = *step;
    copy.optional = false;
    if (step->kind == E_CALL) {
        *member = *step->a;
        member->optional = false;
        member->a = receiver;
        copy.a = member;
    }
    else {
        copy.a = receiver;
    }
    return copy;
}

Instr *emit(Context *c, int op, Loc l)
{
    Instr *i = mem(sizeof(*i));
    i->op = op;
    i->loc = l;
    i->dst = i->a = i->b = -1;
    add(&c->in->code, i);
    return i;
}

int slot(Context *c, Type *t, bool alias)
{
    layout(t, c->in->fn->loc);
    Slot *s = mem(sizeof(*s));
    s->type = t;
    s->alias = alias;
    int id = c->in->slots.n;
    s->owner = id;
    add(&c->in->slots, s);
    return id;
}

Value value(Context *c, Type *t)
{
    Value v = {0};
    v.id = slot(c, t, false);
    v.type = t;
    return v;
}

Value abrupt(Context *c)
{
    return (Value){.id = -1, .type = type_builtin(c->p, T_NEVER)};
}

bool dead(Value v)
{
    return v.type->kind == T_NEVER;
}

uint64_t root(Context *c, int depth, bool external, Loc l)
{
    if (c->root_count == 64) {
        fail(l, "function exceeds 64 provenance roots; split the function");
    }
    int n = c->root_count++;
    c->root_depth[n] = depth;
    c->root_external[n] = external;
    return UINT64_C(1) << n;
}

Var *lookup(Context *c, const char *name)
{
    for (int i = c->vars.n - 1; i >= 0; i--) {
        Var *v = c->vars.v[i];
        if (v->hidden) {
            continue;
        }
        if (!strcmp(v->name, name)) {
            return v;
        }
    }
    return NULL;
}

Var *selected(Context *c, Expr *e)
{
    if (!e->scope_kind) {
        Var *pending = NULL;
        for (int j = c->vars.n - 1; j >= 0; j--) {
            Var *v = c->vars.v[j];
            if (v->hidden) {
                continue;
            }
            if (strcmp(v->name, e->text)) {
                continue;
            }
            if (!v->pending) {
                return v;
            }
            pending = v;
        }
        return pending;
    }
    int target = e->scope_kind == 1 ? e->scope_count + 1 : c->lexical - e->scope_count;
    if (e->scope_kind != 3 && (target < 1 || target > c->lexical)) {
        fail(e->loc, "scope selector is outside the current function");
    }
    int skip = e->scope_count;
    for (int j = c->vars.n - 1; j >= 0; j--) {
        Var *v = c->vars.v[j];
        if (v->hidden) {
            continue;
        }
        if (strcmp(v->name, e->text)) {
            continue;
        }
        if (e->scope_kind == 3) {
            if (skip-- == 0) {
                return v;
            }
        }
        else if (v->lexical <= target) {
            return v;
        }
    }
    return NULL;
}

void forget_roots(List *vars, uint64_t roots, bool all)
{
    for (int j = 0; j < vars->n; j++) {
        Var *v = vars->v[j];
        bool direct = all || (v->root & roots);
        if (direct) {
            v->version++;
            v->narrowed = NULL;
        }
        if (direct || (v->path_sensitive && (v->deps & roots))) {
            v->path_version++;
            v->paths = (List){0};
        }
    }
}

void forget_paths(List *vars)
{
    for (int j = 0; j < vars->n; j++) {
        Var *v = vars->v[j];
        if (v->path_sensitive) {
            v->path_version++;
            v->paths = (List){0};
        }
    }
}

List save_facts(List *vars)
{
    List facts = {0};
    for (int j = 0; j < vars->n; j++) {
        Var *v = vars->v[j];
        NilFact *f = mem(sizeof(*f));
        f->type = v->narrowed;
        f->version = v->version;
        f->path_version = v->path_version;
        f->paths = v->paths;
        add(&facts, f);
    }
    return facts;
}

void restore_facts(List *vars, List facts)
{
    for (int j = 0; j < facts.n; j++) {
        Var *v = vars->v[j];
        NilFact *f = facts.v[j];
        v->narrowed = v->version == f->version ? f->type : NULL;
        v->paths = v->path_version == f->path_version ? f->paths : (List){0};
    }
}

// Compile alternate branches from the same input, then merge mutations.
void rewind_facts(List *vars, List facts)
{
    for (int j = 0; j < facts.n; j++) {
        Var *v = vars->v[j];
        NilFact *f = facts.v[j];
        v->version = f->version;
        v->path_version = f->path_version;
        v->narrowed = f->type;
        v->paths = f->paths;
    }
}

void merge_facts(List *vars, List before, List yes)
{
    for (int j = 0; j < before.n; j++) {
        Var *v = vars->v[j];
        NilFact *old = before.v[j], *a = yes.v[j];
        bool changed = v->version != old->version || a->version != old->version;
        bool paths_changed =
            v->path_version != old->path_version || a->path_version != old->path_version;
        if (changed) {
            v->version = (v->version > a->version ? v->version : a->version) + 1;
        }
        v->narrowed = changed ? NULL : old->type;
        if (paths_changed) {
            v->path_version =
                (v->path_version > a->path_version ? v->path_version : a->path_version) + 1;
        }
        v->paths = paths_changed ? (List){0} : old->paths;
    }
}

bool stable_path(Context *c, Expr *e, Var **root, char **path)
{
    if (e->kind == E_NAME || e->kind == E_SELF) {
        *root = selected(c, e);
        *path = str("");
        return *root != NULL;
    }
    if (e->kind == E_MEMBER && !e->optional && stable_path(c, e->a, root, path)) {
        *path = fmt("%s.%zu:%s", *path, strlen(e->text), e->text);
        return true;
    }
    return false;
}

Type *path_type(Var *v, const char *path)
{
    if (!*path) {
        return v->narrowed;
    }
    for (int j = v->paths.n - 1; j >= 0; j--) {
        PathFact *f = v->paths.v[j];
        if (!strcmp(f->path, path)) {
            return f->type;
        }
    }
    return NULL;
}

void remember_test(NilFlow *flow, Context *context, Expr *test, Expr *value, Type *yes, Type *no)
{
    Var *v = NULL;
    char *path = NULL;
    stable_path(context, value, &v, &path);
    do {
        if (v && path && *path) {
            v->path_sensitive = true;
        }
        NilGuard *guard = NULL;
        for (int j = 0; j < flow->guards.n; j++) {
            NilGuard *g = flow->guards.v[j];
            if (g->test == test && g->var == v) {
                guard = g;
                break;
            }
        }
        if (!guard) {
            guard = mem(sizeof(*guard));
            add(&flow->guards, guard);
        }
        *guard = (NilGuard){.test = test,
                            .var = v,
                            .path = path,
                            .version = v ? (*path ? v->path_version : v->version) : 0,
                            .yes = yes,
                            .no = no,
                            .known = !yes  ? -1
                                     : !no ? 1
                                           : 0};
        v = v && v->source && v->source->version == v->source_version ? v->source : NULL;
    } while (v);
}

void remember_guard(NilFlow *flow, Context *context, Expr *test, Expr *value, bool positive)
{
    Var *v = NULL;
    char *path = NULL;
    if (!stable_path(context, value, &v, &path) || *path || v->type->kind != T_UNION ||
        !nullable(v->type)) {
        return;
    }
    Type *type = v->narrowed ? v->narrowed : v->type;
    Type *yes = without_nil(context->p, type, test->loc);
    Type *no = nullable(type) ? type_builtin(context->p, T_NIL) : NULL;
    remember_test(flow, context, test, value, positive ? yes : no, positive ? no : yes);
}

int condition_truth(NilFlow *flow, Expr *e)
{
    if (e->kind == E_SAVED && e->a) {
        return condition_truth(flow, e->a);
    }
    if (e->kind == E_UNARY && !strcmp(e->text, "!")) {
        return -condition_truth(flow, e->a);
    }
    if (e->kind == E_BIN && (!strcmp(e->text, "and^") || !strcmp(e->text, "or^"))) {
        int a = condition_truth(flow, e->a), b = condition_truth(flow, e->b);
        if (!strcmp(e->text, "and^")) {
            return a < 0 || b < 0 ? -1 : a > 0 && b > 0 ? 1 : 0;
        }
        return a > 0 || b > 0 ? 1 : a < 0 && b < 0 ? -1 : 0;
    }
    // Keep existing non-fits condition checking unchanged.
    if (e->kind == E_FITS) {
        for (int j = flow->guards.n - 1; j >= 0; j--) {
            NilGuard *g = flow->guards.v[j];
            if (g->test == e) {
                return g->known;
            }
        }
    }
    return 0;
}

bool nil_name(Expr *e)
{
    return e && e->kind == E_NAME && !strcmp(e->text, "nil^");
}

void refine_condition(Program *p, List *vars, NilFlow *flow, Expr *e, bool truth)
{
    if (e->kind == E_SAVED && e->a) {
        refine_condition(p, vars, flow, e->a, truth);
        return;
    }
    if (e->kind == E_UNARY && !strcmp(e->text, "!")) {
        refine_condition(p, vars, flow, e->a, !truth);
        return;
    }
    if (e->kind == E_BIN &&
        ((!strcmp(e->text, "and^") && truth) || (!strcmp(e->text, "or^") && !truth))) {
        refine_condition(p, vars, flow, e->a, truth);
        refine_condition(p, vars, flow, e->b, truth);
        return;
    }
    for (int j = 0; j < flow->guards.n; j++) {
        NilGuard *g = flow->guards.v[j];
        if (g->test != e || !g->var ||
            g->version != (*g->path ? g->var->path_version : g->var->version)) {
            continue;
        }
        for (int k = 0; k < vars->n; k++) {
            if (vars->v[k] == g->var) {
                Type *type = truth ? g->yes : g->no;
                if (!type) {
                    break;
                }
                if (!*g->path) {
                    g->var->narrowed = type;
                }
                else {
                    PathFact *fact = mem(sizeof(*fact));
                    *fact = (PathFact){g->path, type};
                    List copy = {0};
                    for (int q = 0; q < g->var->paths.n; q++) {
                        add(&copy, g->var->paths.v[q]);
                    }
                    add(&copy, fact);
                    g->var->paths = copy;
                }
                break;
            }
        }
    }
}

Value place_expression(Context *c, Expr *e)
{
    bool raw = c->raw_name;
    Expr *old_place = c->raw_place;
    c->raw_place = e;
    c->raw_name = e->kind == E_NAME;
    Value v = expression(c, e, NULL);
    c->raw_name = raw;
    c->raw_place = old_place;
    return v;
}

Value variable(Var *v)
{
    return (Value){v->slot, v->type, v->type->refs ? v->deps : 0, v->root, v, true};
}

static void access_check(Context *c, bool priv, Def *owner, Loc l, const char *name)
{
    if (priv && c->access != owner) {
        fail(l, "private member '%s' belongs to '%s'", name, owner->name);
    }
}

Value deref(Context *c, Value v, Loc l)
{
    if (dead(v)) {
        return v;
    }
    if (v.type->kind != T_REF) {
        return v;
    }
    Value r = {0};
    r.type = v.type->elem;
    r.id = slot(c, r.type, true);
    r.storage = v.deps;
    r.deps = r.type->refs ? v.deps : 0;
    r.addressable = true;
    Instr *i = emit(c, I_DEREF, l);
    i->dst = r.id;
    i->a = v.id;
    i->type = r.type;
    return r;
}

void store_check(Context *c, Value dst, Value src, Loc l)
{
    if (dst.var && !dst.var->mut) {
        fail(l, "cannot modify let^ binding '%s'", dst.var->name);
    }
    for (int n = 0; n < c->root_count; n++) {
        if (dst.storage & (UINT64_C(1) << n)) {
            if (c->root_external[n] && !c->in->fn->procedure &&
                !(c->in->fn->mutable_self && (c->self_roots & (UINT64_C(1) << n)))) {
                fail(l, "f^ cannot mutate borrowed storage; use p^ or mutable^self^");
            }
            for (int j = 0; j < c->root_count; j++) {
                if (src.deps & (UINT64_C(1) << j)) {
                    if (c->root_depth[j] > c->root_depth[n]) {
                        fail(l, "reference would outlive its owner (scope %d into scope %d)",
                             c->root_depth[j], c->root_depth[n]);
                    }
                    if (c->root_external[n]) {
                        fail(l, "cannot store nonstatic references through borrowed storage");
                    }
                }
            }
        }
    }
    for (int n = 0; n < c->vars.n; n++) {
        Var *v = c->vars.v[n];
        if (v->type->refs && (v->root & dst.storage)) {
            v->deps |= src.deps;
        }
    }
    if (dst.var && dst.var->type->refs) {
        dst.var->deps |= src.deps;
    }
}

Value copy_to(Context *c, Value dst, Value src, Loc l, bool init)
{
    if (dead(src)) {
        return src;
    }
    if (dst.type != src.type && sum_accepts(dst.type, src.type)) {
        src = convert_value(c, src, dst.type, -1, l);
    }
    if (dst.type != src.type) {
        fail(l, "expected '%s', found '%s'", type_name(dst.type), type_name(src.type));
    }
    if (src.type->owned && !src.held) {
        fail(l, "owned region values cannot be copied; construct at the "
                "destination or borrow with 'ref^'");
    }
    if (!init) {
        store_check(c, dst, src, l);
        forget_roots(&c->vars, dst.storage, false);
    }
    Instr *i = emit(c, I_COPY, l);
    i->dst = dst.id;
    i->a = src.id;
    i->type = dst.type;
    dst.deps = src.deps;
    if (dst.var && dst.var->type->refs) {
        dst.var->deps |= src.deps;
    }
    return dst;
}

Value snapshot(Context *c, Value src, Loc l)
{
    if (dead(src) || src.type->kind == T_VOID || src.type->owned) {
        return src;
    }
    Value dst = value(c, src.type);
    return copy_to(c, dst, src, l, true);
}

// A constant of builtin kind k.
Value constant(Context *c, int k, const char *text, Loc loc)
{
    Value v = value(c, type_builtin(c->p, k));
    Instr *i = emit(c, I_CONST, loc);
    i->dst = v.id;
    i->type = v.type;
    i->text = (char *)text;
    return v;
}

// a op b over a's type; a comparison answers bool^.
Value operate(Context *c, const char *op, Value a, Value b, Loc loc)
{
    bool compare = strchr("<=>!", op[0]) != NULL;
    Value v = value(c, compare ? type_builtin(c->p, T_BOOL) : a.type);
    Instr *i = emit(c, I_BIN, loc);
    i->dst = v.id;
    i->a = a.id;
    i->b = b.id;
    i->type = a.type;
    i->text = (char *)op;
    return v;
}

// The place of union u's alternative arm, setting the tag when it writes.
int case_at(Context *c, Value u, Type *arm, bool write, Loc loc)
{
    int id = slot(c, arm, true);
    Instr *i = emit(c, I_CASE_AT, loc);
    i->dst = id;
    i->a = u.id;
    i->b = write;
    i->type = u.type;
    i->label = union_alternative(u.type, arm);
    return id;
}

// An owned operand of try^, catch^ or ?? is built on the data stack of dst,
// which then takes the value it holds without a copy.
Value held_operand(Context *c, Expr *e, int dst)
{
    Type *t = expression_type(c, e, NULL);
    if (t && t->kind != T_NEVER) {
        layout(t, e->loc);
    }
    if (!t || !t->owned || e->kind == E_NAME) {
        return snapshot(c, expression(c, e, NULL), e->loc);
    }
    if (dst < 0) {
        fail(e->loc, "an owned value needs a construction destination");
    }
    int id = slot(c, t, false);
    ((Slot *)c->in->slots.v[id])->owner = dst;
    Value v = construct(c, e, t, id);
    v.held = true;
    return v;
}

Value case_value(Context *c, Value src, Type *arm, Loc loc)
{
    layout(arm, loc);
    if (arm->owned) {
        // An owned alternative is used where it lies.
        return (Value){.id = case_at(c, src, arm, false, loc),
                       .type = arm,
                       .deps = arm->refs ? src.deps : 0,
                       .storage = src.storage,
                       .var = src.var,
                       .addressable = src.addressable,
                       .held = src.held};
    }
    Value out = value(c, arm);
    out.deps = arm->refs ? src.deps : 0;
    out.guarded_copy = arm->kind == T_STRUCT || arm->kind == T_ARRAY;
    Instr *i = emit(c, I_CASE_GET, loc);
    i->dst = out.id;
    i->a = src.id;
    i->type = src.type;
    i->label = union_alternative(src.type, arm);
    if (i->label < 0) {
        fail(loc, "internal: invalid union projection");
    }
    return out;
}

// This also projects a proven subset for nil narrowing. Callers validate all
// ordinary implicit conversions with sum_accepts before entering here.
Value convert_value(Context *c, Value src, Type *to, int dst, Loc loc)
{
    layout(to, loc);
    if (src.type == to && dst < 0) {
        return src;
    }
    Value out = {
        .id = dst >= 0 ? dst : slot(c, to, false), .type = to, .deps = to->refs ? src.deps : 0};
    if (src.type == to) {
        return copy_to(c, out, src, loc, true);
    }
    Type *pointer = c_function_arm(to, src.type);
    if (pointer) {
        // A C-ABI declaration named as a value is the address of its symbol.
        Value address = pointer == to ? out : value(c, pointer);
        Instr *i = emit(c, I_CONST, loc);
        i->dst = address.id;
        i->type = pointer;
        i->text = fmt("@%s", c_function_symbol(src.type->function));
        return pointer == to ? out : convert_value(c, address, to, out.id, loc);
    }
    if (src.type->kind == T_DYN) {
        // A fits^ test on the dyn^ value proved its concrete type or composer.
        Instr *i = emit(c, to->kind == T_DYN ? I_DYNCAST : I_DYNDATA, loc);
        i->dst = out.id;
        i->a = src.id;
        i->type = to;
        return out;
    }
    int tag = union_alternative(to, src.type);
    if (tag >= 0) {
        if (src.type->owned && !src.held) {
            fail(loc, "owned region values cannot be copied; construct at the destination");
        }
        Instr *i = emit(c, I_UNION, loc);
        i->dst = out.id;
        i->a = src.id;
        i->type = to;
        i->label = tag;
        out.held = src.held;
        return out;
    }
    if (src.type->kind != T_UNION) {
        fail(loc, "expected '%s', found '%s'", type_name(to), type_name(src.type));
    }
    if (dst < 0 && to->owned && union_alternative(src.type, to) >= 0) {
        // A narrowed owned alternative is used where it lies.
        return case_value(c, src, to, loc);
    }
    List selected = {0};
    for (int j = 0; j < src.type->def->alternatives.n; j++) {
        Type *arm = src.type->def->alternatives.v[j];
        if (arm == to || union_alternative(to, arm) >= 0) {
            add(&selected, arm);
        }
    }
    if (!selected.n) {
        fail(loc, "cannot project '%s' to '%s'", type_name(src.type), type_name(to));
    }
    int end = ++c->in->labels;
    for (int j = 0; j < selected.n; j++) {
        Type *arm = selected.v[j];
        int next = 0;
        if (j + 1 < selected.n) {
            Value test = value(c, type_builtin(c->p, T_BOOL));
            Instr *i = emit(c, I_CASE_IS, loc);
            i->dst = test.id;
            i->a = src.id;
            i->type = src.type;
            i->label = union_alternative(src.type, arm);
            int yes = ++c->in->labels;
            next = ++c->in->labels;
            i = emit(c, I_CBR, loc);
            i->a = test.id;
            i->label = yes;
            i->other = next;
            i = emit(c, I_LABEL, loc);
            i->label = yes;
        }
        Value part = case_value(c, src, arm, loc);
        convert_value(c, part, to, out.id, loc);
        Instr *i = emit(c, I_BR, loc);
        i->label = end;
        if (next) {
            i = emit(c, I_LABEL, loc);
            i->label = next;
        }
    }
    Instr *i = emit(c, I_LABEL, loc);
    i->label = end;
    out.guarded_copy = to->kind == T_STRUCT || to->kind == T_ARRAY;
    out.held = src.held && to->owned;
    return out;
}

static Value nil_base(Context *c, Value value, Loc loc)
{
    return value.type->kind == T_REF && value.type->elem->kind == T_UNION ? deref(c, value, loc)
                                                                          : value;
}

Value present_value(Context *c, Value src, Loc loc)
{
    if (dead(src)) {
        return src;
    }
    if (src.type->kind == T_VOID) {
        fail(loc, "presence test requires a value");
    }
    Value out = value(c, type_builtin(c->p, T_BOOL));
    if (src.type->kind != T_UNION || !nullable(src.type)) {
        Instr *i = emit(c, I_CONST, loc);
        i->dst = out.id;
        i->type = out.type;
        i->text = src.type->kind == T_NIL ? "0" : "1";
        return out;
    }
    Value absent = value(c, out.type);
    Instr *i = emit(c, I_CASE_IS, loc);
    i->dst = absent.id;
    i->a = src.id;
    i->type = src.type;
    i->label = union_alternative(src.type, type_builtin(c->p, T_NIL));
    i = emit(c, I_UNARY, loc);
    i->dst = out.id;
    i->a = absent.id;
    i->type = out.type;
    i->text = "!";
    return out;
}

Expr *saved_value(Value value, Loc loc)
{
    Expr *e = mem(sizeof(*e));
    e->kind = E_SAVED;
    e->loc = loc;
    Value *v = mem(sizeof(*v));
    *v = value;
    e->compiler_value = v;
    return e;
}

void nil_branch(Context *c, Value condition, int yes, int no, Loc loc)
{
    Instr *i = emit(c, I_CBR, loc);
    i->a = condition.id;
    i->label = yes;
    i->other = no;
}

void nil_label(Context *c, int label, Loc loc)
{
    Instr *i = emit(c, I_LABEL, loc);
    i->label = label;
}

void nil_jump(Context *c, int label, Loc loc)
{
    Instr *i = emit(c, I_BR, loc);
    i->label = label;
}

Value nil_literal(Context *c, Loc loc)
{
    Expr e = {.kind = E_NAME, .loc = loc, .text = "nil^"};
    return expression(c, &e, NULL);
}

Type *dyn_fits_target(Program *p, Spec *s, Loc loc)
{
    bool dynamic = s->kind == SPEC_DYN;
    Type *t = type_spec(p, dynamic ? s->elem : s);
    if (t->kind == T_REF) {
        t = t->elem;
    }
    if (t->kind != T_STRUCT) {
        fail(loc, "fits^ on a dyn^ value needs a def^ type, found '%s'", type_name(t));
    }
    if (dynamic) {
        return type_wrap(p, T_DYN, t, 0);
    }
    if (t->def->abstract) {
        fail(loc, "no value has exactly the abstract type '%s'; write fits^ dyn^ %s", t->def->name,
             t->def->name);
    }
    return type_wrap(p, T_REF, t, 0);
}

Value fits_value(Context *c, Expr *e)
{
    Value src = expression(c, e->a, NULL);
    if (dead(src)) {
        return src;
    }
    if (src.type->kind == T_DYN) {
        // Only the yes branch narrows; the remaining types are not expressible.
        Type *yes = dyn_fits_target(c->p, e->test_spec, e->loc);
        remember_test(&c->flow, c, e, e->a, yes, src.type);
        Value out = value(c, type_builtin(c->p, T_BOOL));
        Instr *i = emit(c, I_DYNIS, e->loc);
        i->dst = out.id;
        i->a = src.id;
        i->type = yes;
        return out;
    }
    Type *target = type_spec(c->p, e->test_spec);
    if (src.type->kind == T_VOID) {
        fail(e->loc, "fits^ requires a value type");
    }
    if (!callable_type(target)) {
        layout(target, e->loc);
    }
    Type *yes = fits_subset(c->p, src.type, target, true, e->loc);
    Type *no = fits_subset(c->p, src.type, target, false, e->loc);
    remember_test(&c->flow, c, e, e->a, yes, no);
    Value out = value(c, type_builtin(c->p, T_BOOL));
    if (!yes || !no) {
        Instr *i = emit(c, I_CONST, e->loc);
        i->dst = out.id;
        i->type = out.type;
        i->text = yes ? "1" : "0";
        return out;
    }
    bool first = true;
    for (int j = 0; j < src.type->def->alternatives.n; j++) {
        Type *arm = src.type->def->alternatives.v[j];
        if (!fits_type(arm, target)) {
            continue;
        }
        Value test = value(c, out.type);
        Instr *i = emit(c, I_CASE_IS, e->loc);
        i->dst = test.id;
        i->a = src.id;
        i->type = src.type;
        i->label = j;
        if (first) {
            copy_to(c, out, test, e->loc, true);
        }
        else {
            // Both operands are pure tag predicates, so a boolean OR is sufficient.
            i = emit(c, I_BIN, e->loc);
            i->dst = out.id;
            i->a = out.id;
            i->b = test.id;
            i->type = out.type;
            i->text = "bitor^";
        }
        first = false;
    }
    return out;
}

Value chain_walk(Context *c, Expr *e, int absent, bool *never)
{
    List steps = {0};
    Expr *root = chain_parts(e, &steps);
    Value base = expression(c, root, NULL);
    *never = false;
    if (dead(base)) {
        return base;
    }
    for (int j = 0; j < steps.n; j++) {
        Expr *step = steps.v[j];
        if (step_optional(step)) {
            base = nil_base(c, base, step->loc);
            Type *plain = without_nil(c->p, base.type, step->loc);
            if (!plain) {
                nil_jump(c, absent, step->loc);
                *never = true;
                break;
            }
            if (nullable(base.type)) {
                int yes = ++c->in->labels;
                nil_branch(c, present_value(c, base, step->loc), yes, absent, step->loc);
                nil_label(c, yes, step->loc);
                base = convert_value(c, base, plain, -1, step->loc);
            }
        }
        Expr member = {0}, copy = step_expression(step, saved_value(base, step->loc), &member);
        base = expression(c, &copy, NULL);
        if (dead(base)) {
            return base;
        }
    }
    return base;
}

static Value chain_value(Context *c, Expr *e)
{
    Type *result = chain_type(c, e);
    if (!result) {
        fail(e->loc, "cannot infer nil-propagating chain");
    }
    layout(result, e->loc);
    Value out = {.id = result->kind == T_VOID || result->kind == T_NEVER ? -1
                                                                         : slot(c, result, false),
                 .type = result};
    int absent = ++c->in->labels, end = ++c->in->labels;
    bool never = false;
    Value base = chain_walk(c, e, absent, &never);
    if (dead(base) && result->kind == T_NEVER) {
        return base;
    }
    if (!never && !c->terminated) {
        if (result->kind != T_VOID) {
            out.deps |= convert_value(c, base, result, out.id, e->loc).deps;
        }
        nil_jump(c, end, e->loc);
    }
    c->terminated = false;
    nil_label(c, absent, e->loc);
    if (result->kind != T_VOID) {
        convert_value(c, nil_literal(c, e->loc), result, out.id, e->loc);
    }
    nil_jump(c, end, e->loc);
    nil_label(c, end, e->loc);
    return out;
}

static Value coalesce_value(Context *c, Expr *e, int dst)
{
    Value base = held_operand(c, e->a, dst);
    if (dead(base)) {
        return base;
    }
    if (!nullable(base.type)) {
        fail(e->loc, "?? requires a nil alternative");
    }
    Type *result = expression_type(c, e, NULL);
    Type *plain = without_nil(c->p, base.type, e->loc);
    if (!plain) {
        return construct(c, e->b, result, -1);
    }
    layout(result, e->loc);
    Value out = dst >= 0 ? (Value){.id = dst, .type = result} : value(c, result);
    int yes = ++c->in->labels, no = ++c->in->labels, end = ++c->in->labels;
    nil_branch(c, present_value(c, base, e->loc), yes, no, e->loc);
    nil_label(c, yes, e->loc);
    out.deps |=
        convert_value(c, convert_value(c, base, plain, -1, e->loc), result, out.id, e->loc).deps;
    nil_jump(c, end, e->loc);
    nil_label(c, no, e->loc);
    out.deps |= construct(c, e->b, result, out.id).deps;
    if (!c->terminated) {
        nil_jump(c, end, e->loc);
    }
    c->terminated = false;
    nil_label(c, end, e->loc);
    return out;
}

Value field_value(Context *c, Value base, Field *f, Loc l)
{
    access_check(c, f->priv, f->owner, l, f->name);
    Value r = {0};
    r.type = f->type;
    r.id = slot(c, r.type, true);
    r.storage = base.storage;
    r.deps = r.type->refs ? base.deps : 0;
    r.var = base.var;
    r.addressable = base.addressable;
    r.guarded_copy = base.guarded_copy;
    Instr *i = emit(c, I_FIELD, l);
    i->dst = r.id;
    i->a = base.id;
    i->type = base.type;
    i->label = f->index;
    return r;
}

// Where a binding of type want builds the owned result: the result, its
// owned alternative, or that one's owned last part; -1 if it cannot.
int result_place(Context *c, Type *want, Loc loc)
{
    Value v = {.id = c->in->result_slot, .type = c->in->result};
    if (v.type->kind == T_UNION) {
        Type *arm = NULL;
        for (int j = 0; j < v.type->def->alternatives.n; j++) {
            Type *a = v.type->def->alternatives.v[j];
            Type *part = a->kind == T_STRUCT && a->def->tuple
                             ? ((Field *)a->def->fields.v[a->def->fields.n - 1])->type
                             : a;
            if (a->owned && part == want) {
                arm = a;
            }
        }
        if (!arm) {
            return -1;
        }
        v = (Value){.id = case_at(c, v, arm, true, loc), .type = arm};
    }
    if (v.type->kind == T_STRUCT && v.type->def->tuple) {
        v.id = field_value(c, v, v.type->def->fields.v[v.type->def->fields.n - 1], loc).id;
    }
    if (v.id != c->in->result_slot) {
        c->result_part = v.id + 1;
    }
    return v.id;
}

static void release(Context *c, Type *t, int id, Loc l)
{
    Instr *i = emit(c, I_RELEASE, l);
    i->a = id;
    i->type = t;
}

// Whether v owns its storage, rather than being a result's part in place.
static bool owner_var(Context *c, Var *v)
{
    return v->type->owned && v->slot != c->in->result_slot &&
           !((Slot *)c->in->slots.v[v->slot])->alias;
}

void cleanup(Context *c, int from, Loc l)
{
    for (int j = c->vars.n - 1; j >= from; j--) {
        Var *v = c->vars.v[j];
        if (owner_var(c, v)) {
            release(c, v->type, v->slot, l);
        }
    }
}

void escape_cleanup(Context *c, int bindings, int pending, Loc loc)
{
    List ids = {0};
    for (int j = bindings; j < c->vars.n; j++) {
        Var *v = c->vars.v[j];
        if (owner_var(c, v)) {
            add(&ids, (void *)(intptr_t)(v->slot + 1));
        }
    }
    for (int j = pending; j < c->pending_owners.n; j++) {
        add(&ids, c->pending_owners.v[j]);
    }
    for (int j = 1; j < ids.n; j++) {
        void *id = ids.v[j];
        int k = j;
        while (k && (intptr_t)ids.v[k - 1] < (intptr_t)id) {
            ids.v[k] = ids.v[k - 1];
            k--;
        }
        ids.v[k] = id;
    }
    for (int j = 0; j < ids.n; j++) {
        if (j && ids.v[j] == ids.v[j - 1]) {
            continue;
        }
        int id = (int)(intptr_t)ids.v[j] - 1;
        release(c, ((Slot *)c->in->slots.v[id])->type, id, loc);
    }
}

static Value literal_int(Context *c, Expr *e, Type *expected)
{
    Type *t = expected;
    if (t && kind_float(t->kind)) {
        Value v = value(c, t);
        Instr *i = emit(c, I_CONST, e->loc);
        i->dst = v.id;
        i->type = t;
        i->text = float_constant(e->text, t->kind, e->loc);
        return v;
    }
    if (!t || !kind_integer(t->kind)) {
        t = type_builtin(c->p, T_I32);
    }
    if (!integer_fits(e->text, t->kind)) {
        fail(e->loc, "integer literal does not fit '%s'", type_name(t));
    }
    Value v = value(c, t);
    Instr *i = emit(c, I_CONST, e->loc);
    i->dst = v.id;
    i->type = t;
    i->text = e->text;
    return v;
}

// this^ selects a declaration, independently of its runtime activation.
Function *this_function(Program *p, Instance *in, Expr *target)
{
    if (target->kind != E_NAME || strncmp(target->text, "this^", 5)) {
        return NULL;
    }
    Function *f = in->fn;
    for (const char *q = target->text + 5; *q; q++) {
        if (*q != '^') {
            return NULL;
        }
        f = f ? f->enclosing : NULL;
    }
    if (!f || f == p->entry) {
        fail(target->loc, "%s requires an enclosing function or procedure at that nesting depth",
             target->text);
    }
    return f;
}

Def *outer_receiver(Function *f, Type *type, Loc loc)
{
    if (type->kind != T_REF || type->elem->kind != T_STRUCT ||
        !derives(type->elem->def, f->owner)) {
        fail(loc, "outer method call requires an explicit 'ref^ %s' receiver", f->owner->name);
    }
    return type->elem->def;
}

void check_callable(Program *p, Type *want, Type *actual, Loc loc)
{
    if (!callable_accepts(want, actual)) {
        fail(loc, "callable '%s' does not satisfy '%s'", type_name(actual), type_name(want));
    }
    if (want->kind == T_INTERSECTION) {
        for (int j = 0; j < want->arms.n; j++) {
            check_callable(p, want->arms.v[j], actual, loc);
        }
        return;
    }
    if (want->function) {
        return;
    }
    List candidates = callable_candidates(actual, want->params.n);
    Function *f = overload_select(p, &candidates, &want->params, loc, true);
    Type *signature = callable_declaration(p, f);
    if (f->mutable_self) {
        fail(loc, "a mutable^ method cannot satisfy a pure callable contract");
    }
    bool known = signature->result != NULL;
    for (int j = 0; j < signature->params.n; j++) {
        known &= signature->params.v[j] != NULL;
    }
    if (known) {
        return;
    }
    Def *self = NULL;
    for (int j = 0; j < f->params.n; j++) {
        if (((Param *)f->params.v[j])->self) {
            self = outer_receiver(f, want->params.v[j], loc);
        }
        if (callable_type(want->params.v[j])) {
            fail(loc, "generic callable contracts require concrete callback arguments");
        }
    }
    Instance *in = instantiate(p, f, self, &want->params, loc);
    if (want->result && !parameter_accepts(want->result, in->result)) {
        fail(loc, "callable result '%s' does not satisfy '%s'", type_name(in->result),
             type_name(want->result));
    }
}

static Function *select_call(Context *c, List *candidates, Expr *e, Type *receiver)
{
    if (candidates->n == 1) {
        return candidates->v[0];
    }
    if (!candidates->n) {
        return NULL;
    }
    List actual = {0};
    if (receiver) {
        add(&actual, receiver);
    }
    for (int j = 0; j < e->args.n; j++) {
        Type *hint = overload_hint(c->p, candidates, j + (receiver != NULL), e->args.v[j]);
        add(&actual, expression_type(c, e->args.v[j], hint));
    }
    return overload_select(c->p, candidates, &actual, e->loc, true);
}

// Whether a value of type part could be stored inside a value of type whole.
static bool contains(Type *whole, Type *part)
{
    if (whole == part) {
        return true;
    }
    if (whole->kind == T_ARRAY) {
        return contains(whole->elem, part);
    }
    if (whole->kind == T_UNION) {
        for (int j = 0; j < whole->def->alternatives.n; j++) {
            if (contains(whole->def->alternatives.v[j], part)) {
                return true;
            }
        }
    }
    if (whole->kind == T_STRUCT) {
        for (int j = 0; j < whole->def->fields.n; j++) {
            if (contains(((Field *)whole->def->fields.v[j])->type, part)) {
                return true;
            }
        }
    }
    return false;
}

// Whether a value of type t may refer to a location inside an element of
// type elem.
static bool may_reference(Type *t, Type *elem)
{
    if (!t->refs) {
        return false;
    }
    if (t->kind == T_REF || t->kind == T_SLICE) {
        return contains(elem, t->elem);
    }
    if (t->kind == T_STRING) {
        return false; // Strings view literals or other strings, never arrays.
    }
    if (t->kind == T_DYN) {
        return elem->kind == T_STRUCT; // Any composer of the dyn^ def.
    }
    if (t->kind == T_ARRAY || t->kind == T_GROW) {
        return may_reference(t->elem, elem);
    }
    if (t->kind == T_UNION) {
        for (int j = 0; j < t->def->alternatives.n; j++) {
            if (may_reference(t->def->alternatives.v[j], elem)) {
                return true;
            }
        }
        return false;
    }
    if (t->kind == T_STRUCT) {
        for (int j = 0; j < t->def->fields.n; j++) {
            if (may_reference(((Field *)t->def->fields.v[j])->type, elem)) {
                return true;
            }
        }
    }
    return false;
}

// The element type of the flex^ storage an argument of type t leads a
// callee to (the array itself or a struct's flex^ tail), or NULL.
static Type *flex_elements(Type *t)
{
    if (t->kind == T_REF) {
        t = t->elem;
    }
    if (t->kind == T_GROW) {
        return t->count ? t->elem : NULL;
    }
    if (t->kind == T_STRUCT) {
        for (int j = 0; j < t->def->fields.n; j++) {
            Type *field = ((Field *)t->def->fields.v[j])->type;
            if (field->kind == T_GROW && field->count) {
                return field->elem;
            }
        }
    }
    return NULL;
}

// Shrinking flex^ storage returns its tail to the data stack and the OS, so
// no variable in scope or value still in use may refer into it. skip is the
// shrinking argument itself.
static void shrink_check(Context *c, uint64_t storage, Type *elem, Value *skip, Loc loc)
{
    for (int j = 0; j < c->vars.n; j++) {
        Var *v = c->vars.v[j];
        if (!(v->root & storage) && (v->deps & storage) && may_reference(v->type, elem)) {
            fail(loc,
                 "cannot shrink flex^ storage while '%s' may refer into it; end its scope first",
                 v->name);
        }
    }
    for (int j = 0; j < c->inflight.n; j++) {
        Value *v = c->inflight.v[j];
        if (v != skip && (v->deps & storage) && may_reference(v->type, elem)) {
            fail(loc, "cannot shrink flex^ storage while an earlier operand may refer into it");
        }
    }
    for (int k = 0; k < c->in->params.n && k < 64; k++) {
        if (c->param_deps[k] & storage) {
            c->in->shrinks |= UINT64_C(1) << k;
        }
    }
}

// Why method f of iface cannot occupy a dyn^ table entry, or NULL. Entries
// share one signature across every concrete type.
static const char *dyn_unsafe(Program *p, Def *iface, Function *f)
{
    if (f->templ || f->operator_member || f->right_receiver || !f->params.n ||
        !((Param *)f->params.v[0])->self) {
        return "it is not an ordinary self^ method";
    }
    int named = 0;
    for (int k = 0; k < iface->methods.n; k++) {
        named += !strcmp(((Function *)iface->methods.v[k])->name, f->name);
    }
    if (named > 1) {
        return "it is overloaded";
    }
    for (int j = 1; j < f->params.n; j++) {
        Type *t = parameter_type(p, f->params.v[j]);
        Type *base = t && t->kind == T_REF ? t->elem : t;
        if (!t || callable_type(t) || (base->kind == T_STRUCT && base->def->abstract)) {
            return "its parameters need concrete type annotations";
        }
    }
    if (f->result && type_spec(p, f->result)->owned) {
        return "it returns an owned value";
    }
    if (!f->result && !f->procedure) {
        return "its result needs a type annotation";
    }
    return NULL;
}

// An omitted result means the method returns nothing through dyn^.
static Type *dyn_result(Program *p, Function *f)
{
    return f->result ? type_spec(p, f->result) : type_builtin(p, T_VOID);
}

// The table of concrete's implementations of iface's methods.
DynTable *dyn_table(Program *p, Def *concrete, Def *iface, Loc loc)
{
    for (int j = 0; j < p->dyn_tables.n; j++) {
        DynTable *t = p->dyn_tables.v[j];
        if (t->concrete == concrete && t->iface == iface) {
            return t;
        }
    }
    // A dyn^ call has no shrink summary, so it must not reach flex^ storage.
    if (flex_elements(concrete->type)) {
        fail(loc, "'%s' has a flex^ field and cannot be used through dyn^", concrete->name);
    }
    DynTable *table = mem(sizeof(*table));
    *table = (DynTable){.concrete = concrete, .iface = iface, .id = p->dyn_tables.n};
    add(&p->dyn_tables, table);
    for (int k = 0; k < iface->methods.n; k++) {
        Function *f = iface->methods.v[k];
        Instance *in = NULL;
        if (!dyn_unsafe(p, iface, f)) {
            Function *impl = method(concrete, f->name);
            List actual = {0};
            add(&actual, type_wrap(p, T_REF, concrete->type, 0));
            for (int j = 1; j < impl->params.n; j++) {
                add(&actual, parameter_type(p, impl->params.v[j]));
            }
            in = instantiate(p, impl, concrete, &actual, loc);
            if (in->result != dyn_result(p, f)) {
                fail(loc, "'%s.%s' returns '%s', but dyn^ %s expects '%s'; annotate its result",
                     concrete->name, f->name, type_name(in->result), iface->name,
                     type_name(dyn_result(p, f)));
            }
        }
        add(&table->entries, in);
    }
    return table;
}

// Pair a reference to a concrete composing type with its method table.
static Value dyn_value(Context *c, Value v, Type *dyn, Loc loc)
{
    if (dead(v) || v.type == dyn) {
        return v;
    }
    Type *t = v.type->kind == T_REF ? v.type->elem : NULL;
    if (!t || t->kind != T_STRUCT || t->def->abstract || !derives(t->def, dyn->elem->def)) {
        fail(loc, "'%s' cannot become '%s'; pass a reference to a concrete type composing '%s'",
             type_name(v.type), type_name(dyn), dyn->elem->def->name);
    }
    DynTable *table = dyn_table(c->p, t->def, dyn->elem->def, loc);
    Value out = {.id = slot(c, dyn, false), .type = dyn, .deps = v.deps};
    Instr *i = emit(c, I_DYN, loc);
    i->dst = out.id;
    i->a = v.id;
    i->label = table->id;
    i->type = dyn;
    return out;
}

// Call a method through the table carried by a ref^ dyn^ receiver.
static Value dyn_call(Context *c, Expr *e, Value receiver, Type *expected, int destination)
{
    Program *p = c->p;
    Def *iface = receiver.type->elem->def;
    int index = -1;
    for (int k = 0; k < iface->methods.n && index < 0; k++) {
        if (!strcmp(((Function *)iface->methods.v[k])->name, e->a->text)) {
            index = k;
        }
    }
    if (index < 0) {
        fail(e->loc, "'%s' has no method '%s'", iface->name, e->a->text);
    }
    Function *f = iface->methods.v[index];
    access_check(c, f->priv, f->owner, e->loc, f->name);
    const char *why = dyn_unsafe(p, iface, f);
    if (why) {
        fail(e->loc, "'%s' cannot be called through dyn^: %s", f->name, why);
    }
    if (f->procedure && !c->in->fn->procedure) {
        fail(e->loc, "f^ cannot call p^ '%s'", f->name);
    }
    if (f->params.n != e->args.n + 1) {
        fail(e->loc, "'%s' expects %d arguments", f->name, f->params.n - 1);
    }
    if (f->procedure || f->mutable_self) {
        // The same mutation rules as a static call to a receiver in that storage.
        store_check(c, (Value){.storage = receiver.deps}, (Value){0}, e->loc);
    }
    Instr *i = mem(sizeof(*i));
    i->op = I_DYNCALL;
    i->loc = e->loc;
    add(&i->args, (void *)(intptr_t)(receiver.id + 1));
    uint64_t deps = receiver.deps;
    for (int j = 0; j < e->args.n; j++) {
        Type *want = parameter_type(p, f->params.v[j + 1]);
        Value v = expression(c, e->args.v[j], want);
        if (c->terminated) {
            return abrupt(c);
        }
        // Every table entry was specialized for exactly the declared types.
        if (v.type != want) {
            fail(e->loc, "argument to '%s' through dyn^ must be '%s', found '%s'", f->name,
                 type_name(want), type_name(v.type));
        }
        v = snapshot(c, v, e->loc);
        deps |= v.deps;
        add(&i->args, (void *)(intptr_t)(v.id + 1));
    }
    Type *t = dyn_result(p, f);
    if (expected && expected != t) {
        fail(e->loc, "call returns '%s', expected '%s'", type_name(t), type_name(expected));
    }
    Value out = {.type = t,
                 .id = t->kind == T_VOID  ? -1
                       : destination >= 0 ? destination
                                          : slot(c, t, false),
                 .deps = t->refs ? deps : 0};
    i->dst = out.id;
    i->type = t;
    i->label = index;
    add(&c->in->code, i);
    if (f->procedure || f->mutable_self) {
        forget_roots(&c->vars, deps, false);
    }
    return out;
}

static Value call(Context *c, Expr *e, Type *expected, int destination)
{
    Program *p = c->p;
    Expr *target = e->a;
    if (target->kind == E_MEMBER && !strcmp(target->text, "contains") && e->args.n == 1 &&
        range_type(expression_type(c, target->a, NULL))) {
        Value r = deref(c, expression(c, target->a, NULL), e->loc);
        Value i = dead(r) ? r : expression(c, e->args.v[0], type_builtin(p, T_I64));
        if (dead(i)) {
            return i;
        }
        if (i.type->kind != T_I64) {
            fail(e->loc, "range^ contains an i64^");
        }
        return range_contains(c, r, i, e->loc);
    }
    if (target->kind == E_MEMBER &&
        error_messages(p, target, e->args.n, expression_type(c, target->a, NULL))) {
        Value err = deref(c, expression(c, target->a, NULL), e->loc);
        if (dead(err)) {
            return err;
        }
        Value out = error_message(c, err, e->loc);
        return destination >= 0
                   ? copy_to(c, (Value){.id = destination, .type = out.type}, out, e->loc, true)
                   : out;
    }
    if (target->kind == E_NAME) {
        for (int k = T_U8; k <= T_F64; k++) {
            if (strcmp(target->text, builtin_name(k))) {
                continue;
            }
            if (e->args.n != 1) {
                fail(e->loc, "numeric conversion expects one argument");
            }
            Type *t = type_builtin(p, k);
            Value v = expression(c, e->args.v[0], NULL);
            if (c->terminated) {
                return abrupt(c);
            }
            if (!kind_integer(v.type->kind) && !kind_float(v.type->kind) &&
                !(v.type->kind == T_CHAR && kind_integer(k))) {
                fail(e->loc,
                     v.type->kind == T_STRING
                         ? "%s(...) converts numbers; read a string^ with %s.parse(s)"
                         : "numeric conversion requires a number",
                     builtin_name(k), builtin_name(k));
            }
            if (expected && expected != t) {
                fail(e->loc, "conversion result is '%s', expected '%s'", type_name(t),
                     type_name(expected));
            }
            Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t};
            Instr *i = emit(c, I_CONVERT, e->loc);
            i->dst = out.id;
            i->a = v.id;
            i->type = t;
            return out;
        }
    }
    if (target->kind == E_NAME && !strcmp(target->text, "char^")) {
        return char_cast(c, e, destination);
    }
    if (target->kind == E_NAME && !strcmp(target->text, "string^")) {
        return text_cast(c, e, destination);
    }
    if (target->kind == E_MEMBER && !strcmp(target->text, "parse") && target->a->kind == E_NAME &&
        numeric_kind(target->a->text) >= 0) {
        return number_parse(c, e, destination);
    }
    if (target->kind == E_MEMBER && !strcmp(target->text, "append") && e->args.n == 1 &&
        ((Expr *)e->args.v[0])->kind == E_INTERP) {
        Value into = deref(c, expression(c, target->a, NULL), e->loc);
        if (dead(into)) {
            return into;
        }
        if (into.type->kind != T_STRUCT || strcmp(into.type->def->name, "std.text.Builder")) {
            fail(e->loc, "$\"...\" is appended to a std.text.Builder, not '%s'",
                 type_name(into.type));
        }
        interp_into(c, e->args.v[0], into);
        return (Value){.id = -1, .type = type_builtin(p, T_VOID)};
    }
    if (target->kind == E_MEMBER &&
        (!strcmp(target->text, "tostring") || !strcmp(target->text, "write_text")) &&
        textual(expression_type(c, target->a, NULL))) {
        return value_text(c, e, destination);
    }
    if (target->kind == E_MEMBER && text_method_type(p, target->text, e->loc)) {
        Type *base = expression_type(c, target->a, NULL);
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        if (base && base->kind == T_STRING) {
            return text_method(c, e, destination);
        }
    }
    if (target->kind == E_NAME &&
        (!strcmp(target->text, "print^") || !strcmp(target->text, "assert^"))) {
        bool print = !strcmp(target->text, "print^");
        if (print && !c->in->fn->procedure) {
            fail(e->loc, "print^ requires p^");
        }
        if (!print && e->args.n != 1) {
            fail(e->loc, "assert^ expects one bool");
        }
        Instr *i = mem(sizeof(*i));
        i->op = print ? I_PRINT : I_ASSERT;
        i->loc = e->loc;
        i->dst = -1;
        List temps = {0};
        for (int j = 0; j < e->args.n; j++) {
            Expr *arg = e->args.v[j];
            Value v = print && arg->kind == E_INTERP
                          ? interp_print(c, arg, &temps)
                          : expression(c, arg, print ? NULL : type_builtin(p, T_BOOL));
            if (c->terminated) {
                return abrupt(c);
            }
            if (print && !kind_integer(v.type->kind) && !kind_float(v.type->kind) &&
                v.type->kind != T_STRING && v.type->kind != T_BOOL && v.type->kind != T_CHAR) {
                fail(e->loc, "print^ does not support '%s'", type_name(v.type));
            }
            if (!print && v.type->kind != T_BOOL) {
                fail(e->loc, "assert^ requires bool");
            }
            v = snapshot(c, v, e->loc);
            add(&i->args, (void *)(intptr_t)(v.id + 1));
        }
        add(&c->in->code, i);
        for (int j = temps.n - 1; j >= 0; j--) {
            release(c, lookup_def(p, "std.text.Builder")->type, (int)(intptr_t)temps.v[j], e->loc);
            c->pending_owners.n--;
        }
        if (!print) {
            refine_condition(p, &c->vars, &c->flow, e->args.v[0], true);
        }
        return (Value){.id = -1, .type = type_builtin(p, T_VOID)};
    }
    if (target->kind == E_MEMBER && target->a->kind == E_NAME && !strcmp(target->text, "new")) {
        Def *d = lookup_def(p, target->a->text);
        if (d) {
            if (e->args.n) {
                fail(e->loc, "new() takes no arguments; use Type{ field = value }");
            }
            Expr r = {0};
            r.kind = E_RECORD;
            r.loc = e->loc;
            r.text = d->name;
            return construct(c, &r, d->type, destination);
        }
    }
    Function *f = NULL;
    Def *self = NULL;
    List actual = {0};
    List values = {0};
    if (target->kind == E_MEMBER) {
        Value receiver = expression(c, target->a, NULL);
        if (dead(receiver)) {
            return receiver;
        }
        if (c->terminated) {
            return abrupt(c);
        }
        Value base = deref(c, receiver, e->loc);
        if (base.type->kind == T_GROW && !strcmp(target->text, "truncate")) {
            if (e->args.n != 1) {
                fail(e->loc, "truncate expects one new length");
            }
            Value len = expression(c, e->args.v[0], type_builtin(p, T_I64));
            if (c->terminated) {
                return abrupt(c);
            }
            if (len.type->kind != T_I64) {
                fail(e->loc, "truncate length must be i64^");
            }
            store_check(c, base, (Value){0}, e->loc);
            if (base.type->count) {
                shrink_check(c, base.storage, base.type->elem, NULL, e->loc);
            }
            Type *t = type_builtin(p, T_VOID);
            if (expected && expected != t) {
                fail(e->loc, "truncate has no result");
            }
            Instr *i = emit(c, I_TRUNCATE, e->loc);
            i->a = base.id;
            i->b = len.id;
            i->type = base.type;
            return (Value){.id = -1, .type = t};
        }
        if (base.type->kind == T_GROW && !strcmp(target->text, "append")) {
            if (e->args.n != 1) {
                fail(e->loc, "append expects one slice");
            }
            Value src = expression(c, e->args.v[0], NULL);
            if (c->terminated) {
                return abrupt(c);
            }
            if (!((src.type->kind == T_SLICE && src.type->elem == base.type->elem) ||
                  (src.type->kind == T_STRING && base.type->elem->kind == T_U8))) {
                fail(e->loc, "append needs a slice with the same element type");
            }
            Value dependencies = {.deps = base.type->elem->refs ? src.deps : 0};
            store_check(c, base, dependencies, e->loc);
            forget_roots(&c->vars, base.storage | base.deps, false);
            Type *t = type_builtin(p, T_VOID);
            if (expected && expected != t) {
                fail(e->loc, "append has no result");
            }
            Instr *i = emit(c, I_APPEND, e->loc);
            i->a = base.id;
            i->b = src.id;
            i->type = base.type;
            return (Value){.id = -1, .type = t};
        }
        if (base.type->kind == T_GROW &&
            (!strcmp(target->text, "pop") || !strcmp(target->text, "clear"))) {
            bool pop = !strcmp(target->text, "pop");
            if (e->args.n) {
                fail(e->loc, "%s expects no arguments", target->text);
            }
            store_check(c, base, (Value){0}, e->loc);
            if (base.type->count) {
                shrink_check(c, base.storage, base.type->elem, NULL, e->loc);
            }
            Type *t = pop ? base.type->elem : type_builtin(p, T_VOID);
            if (expected && expected != t) {
                fail(e->loc, "%s returns '%s', expected '%s'", target->text, type_name(t),
                     type_name(expected));
            }
            Value out = {.id = pop ? (destination >= 0 ? destination : slot(c, t, false)) : -1,
                         .type = t,
                         .deps = t->refs ? base.storage | base.deps : 0};
            Instr *i = emit(c, pop ? I_POP : I_CLEAR, e->loc);
            i->dst = out.id;
            i->a = base.id;
            i->type = base.type;
            return out;
        }
        if (base.type->kind == T_GROW && !strcmp(target->text, "push")) {
            if (e->args.n != 1) {
                fail(e->loc, "push expects one element");
            }
            Value v = expression(c, e->args.v[0], base.type->elem);
            if (c->terminated) {
                return abrupt(c);
            }
            if (v.type != base.type->elem) {
                fail(e->loc, "push element type mismatch");
            }
            store_check(c, base, v, e->loc);
            forget_roots(&c->vars, base.storage | base.deps, false);
            Type *t = type_wrap(p, T_REF, base.type->elem, 0);
            if (expected && expected != t) {
                fail(e->loc, "push returns '%s', expected '%s'", type_name(t), type_name(expected));
            }
            Value out = {.id = destination >= 0 ? destination : slot(c, t, false),
                         .type = t,
                         .deps = base.storage | base.deps | v.deps};
            Instr *i = emit(c, I_PUSH, e->loc);
            i->dst = out.id;
            i->a = base.id;
            i->b = v.id;
            i->type = base.type;
            return out;
        }
        if (base.type->kind == T_DYN) {
            return dyn_call(c, e, snapshot(c, base, e->loc), expected, destination);
        }
        if (base.type->kind != T_STRUCT) {
            fail(e->loc, "method call requires a structure");
        }
        self = base.type->def;
        List candidates = overload_candidates(p, self, target->text, e->args.n + 1);
        f = select_call(c, &candidates, e, type_wrap(p, T_REF, base.type, 0));
        if (!f) {
            f = method(self, target->text);
        }
        if (!f) {
            fail(e->loc, "'%s' has no method '%s'", self->name, target->text);
        }
        access_check(c, f->priv, f->owner, e->loc, f->name);
        if (base.guarded_copy && (f->procedure || f->mutable_self)) {
            fail(e->loc, "mutable nil-propagating receiver requires an optional reference");
        }
        if (f->procedure || f->mutable_self) {
            store_check(c, base, (Value){0}, e->loc);
        }
        Value ref = value(c, type_wrap(p, T_REF, base.type, 0));
        Instr *i = emit(c, I_ADDR, e->loc);
        i->dst = ref.id;
        i->a = base.id;
        i->type = ref.type;
        ref.deps = base.storage | base.deps;
        if (!base.addressable) {
            ref.deps |= root(c, c->depth + 1, false, e->loc);
        }
        Value *saved = mem(sizeof(*saved));
        *saved = ref;
        add(&values, saved);
        add(&actual, ref.type);
    }
    else if (target->kind == E_FUNCTION) {
        f = target->function;
    }
    else if ((f = this_function(p, c->in, target))) {
        self = f == c->in->fn ? c->in->self : NULL;
        if (self) {
            Var *receiver = lookup(c, "self^");
            Value ref = snapshot(c, variable(receiver), e->loc);
            Value *saved = mem(sizeof(*saved));
            *saved = ref;
            add(&values, saved);
            add(&actual, ref.type);
        }
    }
    else if (target->kind == E_NAME) {
        Var *binding = selected(c, target);
        List candidates = binding ? callable_candidates(binding->type, e->args.n)
                                  : overload_candidates(p, NULL, target->text, e->args.n);
        f = select_call(c, &candidates, e, NULL);
        if (!f) {
            fail(e->loc, "unknown function '%s'", target->text);
        }
    }
    else {
        Value target_value = expression(c, target, NULL);
        List candidates = callable_candidates(target_value.type, e->args.n);
        f = select_call(c, &candidates, e, NULL);
        if (!f) {
            fail(e->loc, "callable has no statically selected implementation");
        }
    }
    if (f->abstract) {
        fail(e->loc, "cannot call abstract method '%s'", f->name);
    }
    Var *alias = target->kind == E_NAME ? selected(c, target) : NULL;
    if (alias && callable_type(alias->type)) {
        tool_record(p, target->loc, alias->declaration, alias->name, alias->type, NULL, NULL, 13,
                    false);
    }
    else {
        tool_record(p, target->name_loc.file ? target->name_loc : target->loc,
                    f->declaration.file ? f->declaration : f->loc, f->name, NULL, f, f->owner,
                    f->owner ? 6 : 12, false);
    }
    inferred_call(c->in->fn, f, e->loc);
    int offset = values.n;
    Value *right_self = offset && f->right_receiver ? values.v[0] : NULL;
    if (right_self) {
        values.n = actual.n = 0;
    }
    e = pack_arguments(f, e, offset);
    if (f->params.n != e->args.n + offset) {
        fail(e->loc, "'%s' expects %s%d arguments", f->name, f->variadic ? "at least " : "",
             f->params.n - offset - f->variadic);
    }
    if (f->procedure && !c->in->fn->procedure) {
        fail(e->loc, "f^ cannot call p^ '%s'", f->name);
    }
    // Arguments evaluated so far stay in use while later ones may shrink.
    int inflight = c->inflight.n;
    for (int j = 0; j < values.n; j++) {
        add(&c->inflight, values.v[j]);
    }
    for (int j = 0; j < e->args.n; j++) {
        Param *param = f->params.v[right_self ? j : j + offset];
        Type *want = parameter_type(p, param);
        Value v = expression(c, e->args.v[j], parameter_hint(p, param, e->args.v[j]));
        if (c->terminated) {
            c->inflight.n = inflight;
            return abrupt(c);
        }
        if (v.type->kind == T_VOID) {
            fail(e->loc, "void argument");
        }
        if (want) {
            if (callable_type(want)) {
                check_callable(p, want, v.type, e->loc);
            }
            Type *a = v.type, *b = want;
            if (a != b) {
                bool okay = parameter_accepts(b, a);
                if (!okay) {
                    fail(e->loc, "argument to '%s' requires nominal '%s', found '%s'", f->name,
                         type_name(want), type_name(v.type));
                }
            }
        }
        if (!self && f->owner && param->self) {
            self = outer_receiver(f, v.type, e->loc);
            if (f->procedure || f->mutable_self) {
                store_check(c, deref(c, v, e->loc), (Value){0}, e->loc);
            }
        }
        if (v.type->owned) {
            fail(e->loc, "pass owned values by reference with 'ref^'");
        }
        v = snapshot(c, v, e->loc);
        Value *saved = mem(sizeof(*saved));
        *saved = v;
        add(&values, saved);
        add(&c->inflight, saved);
        add(&actual, v.type);
    }
    if (right_self) {
        add(&values, right_self);
        add(&actual, right_self->type);
    }
    Instance *in = instantiate(p, f, self, &actual, e->loc);
    // A callee still being lowered has no summary yet; assume it shrinks.
    for (int j = 0; j < values.n && j < 64; j++) {
        Value *v = values.v[j];
        Type *elem = flex_elements(v->type);
        if (elem && (in->state != 2 || ((in->shrinks >> j) & 1)) && v->deps) {
            shrink_check(c, v->deps, elem, v, e->loc);
        }
    }
    c->inflight.n = inflight;
    Type *t = in->result;
    if (expected && expected != t) {
        fail(e->loc, "call returns '%s', expected '%s'", type_name(t), type_name(expected));
    }
    Value out = {0};
    out.type = t;
    out.id = t->kind == T_VOID ? -1 : destination >= 0 ? destination : slot(c, t, false);
    uint64_t deps = 0;
    for (int j = 0; j < values.n; j++) {
        Value *v = values.v[j];
        deps |= v->deps;
    }
    if (t->refs) {
        out.deps = deps;
    }
    if (t->owned && destination < 0) {
        fail(e->loc, "owned return requires a binding or return destination");
    }
    Instr *i = emit(c, I_CALL, e->loc);
    i->dst = out.id;
    i->callee = in;
    i->type = t;
    for (int j = 0; j < values.n; j++) {
        Value *v = values.v[j];
        add(&i->args, (void *)(intptr_t)(v->id + 1));
    }
    // C may write through every pointer it receives.
    if (f->procedure || f->mutable_self || f->external) {
        forget_roots(&c->vars, deps, false);
    }
    return out;
}

Type *union_literal_hint(Type *t, Expr *e)
{
    if (!t || t->kind != T_UNION || e->constant_spec) {
        return NULL;
    }
    Expr *literal = e;
    if (literal->kind == E_UNARY && !strcmp(literal->text, "-")) {
        literal = literal->a;
    }
    if (literal->kind != E_INT && literal->kind != E_FLOAT && literal->kind != E_ARRAY &&
        (literal->kind != E_RECORD || literal->text)) {
        return NULL;
    }
    Type *hint = NULL;
    for (int j = 0; j < t->def->alternatives.n; j++) {
        Type *a = t->def->alternatives.v[j];
        if ((literal->kind == E_INT && kind_integer(a->kind)) ||
            (literal->kind == E_FLOAT && kind_float(a->kind)) ||
            (literal->kind == E_RECORD && a->kind == T_STRUCT && a->def->tuple == literal->tuple &&
             (!literal->tuple || a->def->fields.n == literal->args.n)) ||
            (literal->kind == E_ARRAY && (a->kind == T_ARRAY || a->kind == T_GROW))) {
            // A mixed result must not implicitly choose an error constructor.
            if (literal->kind == E_RECORD && a->def->error_variant) {
                return NULL;
            }
            if (hint) {
                return NULL;
            }
            hint = a;
        }
    }
    return hint;
}

Value construct(Context *c, Expr *e, Type *t, int dst)
{
    if (c->terminated) {
        return abrupt(c);
    }
    if (e->kind == E_CATCH) {
        return catch_value(c, e, t, dst);
    }
    if (e->kind == E_COALESCE && t && t->owned && t == expression_type(c, e, NULL)) {
        return coalesce_value(c, e, dst);
    }
    if (e->kind == E_TRY) {
        bool literal = e->a->kind == E_INT || e->a->kind == E_FLOAT || e->a->kind == E_RECORD ||
                       e->a->kind == E_ARRAY;
        Type *source = expression_type(c, e->a, literal ? t : NULL);
        if (source && !has_error(source)) {
            return construct(c, e->a, t, dst);
        }
        Value src = try_value(c, e, dst);
        if (dead(src) || !t) {
            return src;
        }
        Value out = {.id = dst >= 0 ? dst : slot(c, t, false), .type = t};
        return copy_to(c, out, src, e->loc, true);
    }
    if (e->kind == E_INTERP) {
        Def *builder = lookup_def(c->p, "std.text.Builder");
        if (t && t != builder->type) {
            fail(e->loc, "$\"...\" builds a std.text.Builder, expected '%s'", type_name(t));
        }
        if (dst < 0) {
            fail(e->loc,
                 "$\"...\" builds a std.text.Builder; bind it, or pass it to print^ or append");
        }
        Expr empty = {.kind = E_RECORD, .loc = e->loc, .text = builder->name};
        Value out = construct(c, &empty, builder->type, dst);
        interp_into(c, e, out);
        return c->terminated ? abrupt(c) : out;
    }
    if (e->kind == E_RANGE) {
        Type *range = type_range(c->p);
        if (t && t != range) {
            fail(e->loc, "a range is 'range^', expected '%s'", type_name(t));
        }
        Value out = {.id = dst >= 0 ? dst : slot(c, range, false), .type = range};
        Value ends[2];
        for (int j = 0; j < 2; j++) {
            Expr *end = j ? e->b : e->a;
            ends[j] = end ? expression(c, end, type_builtin(c->p, T_I64))
                          : constant(c, T_I64, j ? RANGE_OPEN : "0", e->loc);
            if (c->terminated) {
                return abrupt(c);
            }
            if (ends[j].type->kind != T_I64) {
                fail(end->loc, "range bounds must be i64^");
            }
        }
        if (!strcmp(e->text, "to^")) {
            int bad = ++c->in->labels, good = ++c->in->labels;
            nil_branch(
                c,
                operate(c, "=", ends[1], constant(c, T_I64, "9223372036854775807", e->loc), e->loc),
                bad, good, e->loc);
            nil_label(c, bad, e->loc);
            Expr text = {.kind = E_STRING,
                         .loc = e->loc,
                         .text = "the end of a to^ range overflows",
                         .length = strlen("the end of a to^ range overflows")};
            terminate(c, expression(c, &text, type_builtin(c->p, T_STRING)), true, e->loc);
            c->terminated = false;
            nil_label(c, good, e->loc);
            ends[1] = operate(c, "+", ends[1], constant(c, T_I64, "1", e->loc), e->loc);
        }
        for (int j = 0; j < 2; j++) {
            copy_to(c, field_value(c, out, range->def->fields.v[j], e->loc), ends[j], e->loc, true);
        }
        return out;
    }
    if (e->kind == E_SCOPE) {
        int begin = c->vars.n, depth = c->depth++;
        c->lexical++;
        statements(c, &e->bindings, false);
        Value out = construct(c, e->a, t, dst);
        if (dead(out)) {
            c->vars.n = begin;
            c->depth = depth;
            c->lexical--;
            return out;
        }
        for (int j = 0; j < c->root_count; j++) {
            if ((out.deps & (UINT64_C(1) << j)) && c->root_depth[j] > depth) {
                fail(e->loc, "scoped expression cannot escape a local owner");
            }
        }
        if (dst < 0) {
            out = snapshot(c, out, e->loc);
        }
        cleanup(c, begin, e->loc);
        c->vars.n = begin;
        c->depth = depth;
        c->lexical--;
        return out;
    }
    if (t && t->kind == T_UNION && e->kind != E_IF) {
        if (e->kind == E_RECORD && e->text && !strcmp(e->text, t->def->name)) {
            fail(e->loc, "construct union '%s' from a named alternative", type_name(t));
        }
        if (e->kind == E_RECORD && !e->text && !union_literal_hint(t, e)) {
            fail(e->loc, "contextual construction requires a single nominal struct "
                         "type; write Type{field=value}");
        }
        Type *hint = union_literal_hint(t, e), *built = hint ? hint : expression_type(c, e, NULL);
        if (built && built->kind != T_NEVER) {
            layout(built, e->loc);
        }
        bool forwarded = built == t && e->kind == E_CALL && !guarded_chain(e);
        if (built && built->owned &&
            (forwarded || (built != t && union_alternative(t, built) >= 0))) {
            if (dst < 0) {
                fail(e->loc, "owned construction requires a destination");
            }
            Value u = {.id = dst, .type = t};
            Value v = forwarded ? call(c, e, t, dst)
                                : construct(c, e, built, case_at(c, u, built, true, e->loc));
            u.deps = v.deps;
            return dead(v) ? v : u;
        }
        Value arm = expression(c, e, hint);
        if (dead(arm)) {
            return arm;
        }
        if (!sum_accepts(t, arm.type) && !c_function_arm(t, arm.type)) {
            fail(e->loc, "'%s' is not an alternative of nominal union '%s'", type_name(arm.type),
                 type_name(t));
        }
        return convert_value(c, arm, t, dst, e->loc);
    }
    if (e->kind == E_CALL && !guarded_chain(e)) {
        return call(c, e, t, dst);
    }
    if (e->kind == E_IF) {
        if (!t) {
            t = expression_type(c, e, NULL);
        }
        if (!t || t->kind == T_VOID) {
            fail(e->loc, "if^ expression must produce a value on every branch");
        }
        Value out = t->kind == T_NEVER
                        ? abrupt(c)
                        : (Value){.id = dst >= 0 ? dst : slot(c, t, false), .type = t};
        if (t->owned && dst < 0) {
            fail(e->loc, "owned conditional requires a construction destination");
        }
        Value condition = expression(c, e->a, type_builtin(c->p, T_BOOL));
        if (dead(condition)) {
            return condition;
        }
        if (condition.type->kind != T_BOOL) {
            fail(e->loc, "condition requires bool");
        }
        int known = condition_truth(&c->flow, e->a);
        if (known) {
            List facts = save_facts(&c->vars);
            refine_condition(c->p, &c->vars, &c->flow, e->a, known > 0);
            Value result = construct(c, known > 0 ? e->b : e->c, t, out.id);
            restore_facts(&c->vars, facts);
            return result;
        }
        int yes = ++c->in->labels, no = ++c->in->labels, end = ++c->in->labels;
        Instr *i = emit(c, I_CBR, e->loc);
        i->a = condition.id;
        i->label = yes;
        i->other = no;
        i = emit(c, I_LABEL, e->loc);
        i->label = yes;
        List facts = save_facts(&c->vars);
        refine_condition(c->p, &c->vars, &c->flow, e->a, true);
        Value a = construct(c, e->b, t, out.id);
        bool a_end = c->terminated;
        List after_yes = save_facts(&c->vars);
        rewind_facts(&c->vars, facts);
        if (!a_end) {
            i = emit(c, I_BR, e->loc);
            i->label = end;
        }
        c->terminated = false;
        i = emit(c, I_LABEL, e->loc);
        i->label = no;
        refine_condition(c->p, &c->vars, &c->flow, e->a, false);
        Value b = construct(c, e->c, t, out.id);
        bool b_end = c->terminated;
        merge_facts(&c->vars, facts, after_yes);
        if (!b_end) {
            i = emit(c, I_BR, e->loc);
            i->label = end;
        }
        c->terminated = a_end && b_end;
        if (!c->terminated) {
            i = emit(c, I_LABEL, e->loc);
            i->label = end;
        }
        out.deps = a.deps | b.deps;
        return c->terminated ? abrupt(c) : out;
    }
    if (e->kind == E_RECORD) {
        if (e->text) {
            Def *d = lookup_def(c->p, e->text);
            if (!d) {
                fail(e->loc, "unknown constructor '%s'", e->text);
            }
            if (t && t != d->type) {
                fail(e->loc, "constructor has nominal type '%s', expected '%s'", d->name,
                     type_name(t));
            }
            t = d->type;
        }
        bool results = t && t->kind == T_STRUCT && t->def->tuple;
        if (e->tuple && !results) {
            fail(e->loc, "multiple values go only where multiple results (A, B) are expected, "
                         "as in return^ a, b or catch^ (a, b)");
        }
        if (e->tuple && e->args.n != t->def->fields.n) {
            fail(e->loc, "%d values returned for '%s'", e->args.n, type_name(t));
        }
        if (!t || t->kind != T_STRUCT) {
            fail(e->loc, "contextual construction requires a nominal struct type; "
                         "write Type{field=value}");
        }
        Module *scope = c->in_default && c->access ? c->access->module : c->in->fn->module;
        if (!e->text && t->def->module && t->def->module != scope && !t->def->public) {
            fail(e->loc, "contextual construction requires a public type outside "
                         "its module");
        }
        if (e->text) {
            tool_record(c->p, e->loc, t->def->loc, t->def->name, t, NULL, t->def, 23, false);
        }
        if (t->def->abstract) {
            fail(e->loc, "cannot construct abstract def^ '%s'", t->def->name);
        }
        if (dst < 0 && t->owned) {
            fail(e->loc, "owned construction requires a destination");
        }
        Value out = {0};
        out.type = t;
        out.id = dst >= 0 ? dst : slot(c, t, false);
        // Fields built so far stay in use while later ones are evaluated.
        int inflight = c->inflight.n;
        add(&c->inflight, &out);
        if (!t->def->fields.n) {
            Instr *z = emit(c, I_ZERO, e->loc);
            z->dst = out.id;
            z->type = t;
        }
        for (int j = 0; j < e->names.n; j++) {
            char *name = e->names.v[j];
            Field *f = field(t->def, name);
            if (!f && t->def->error_variant && !strcmp(name, "message")) {
                continue;
            }
            if (!f) {
                fail(e->loc, "unknown field '%s' in '%s'", name, t->def->name);
            }
            access_check(c, f->priv, f->owner, e->loc, name);
            for (int k = 0; k < j; k++) {
                if (!strcmp(name, e->names.v[k])) {
                    fail(e->loc, "duplicate field initializer '%s'", name);
                }
            }
        }
        for (int j = 0; j < t->def->fields.n; j++) {
            Field *f = t->def->fields.v[j];
            Expr *init = f->init;
            for (int k = 0; k < e->names.n; k++) {
                if (!strcmp(f->name, e->names.v[k])) {
                    init = e->args.v[k];
                }
            }
            Def *old = c->access; // Default initializers retain their declaration's
                                  // access context.
            bool old_default = c->in_default;
            bool default_value = init && init == f->init;
            if (default_value) {
                for (int k = 0; k < c->default_stack.n; k++) {
                    if (c->default_stack.v[k] == f->init) {
                        fail(e->loc, "recursive default construction for '%s'", f->name);
                    }
                }
                add(&c->default_stack, f->init);
                c->in_default = true;
            }
            if (init == f->init) {
                c->access = f->initializer_owner;
            }
            Value dest = {0};
            dest.type = f->type;
            dest.id = slot(c, f->type, true);
            Instr *a = emit(c, I_FIELD, e->loc);
            a->dst = dest.id;
            a->a = out.id;
            a->type = t;
            a->label = f->index;
            Value v = {0};
            if (t->def->error_variant && !f->index) {
                // The hidden site records where this error is built and its message.
                char *message = t->def->name;
                for (int k = 0; k < e->names.n; k++) {
                    Expr *given = e->args.v[k];
                    if (!strcmp(e->names.v[k], "message") && given->kind != E_STRING) {
                        fail(given->loc,
                             "an error's message is a string literal; pass details as fields");
                    }
                    message = strcmp(e->names.v[k], "message") ? message : given->text;
                }
                char *source = site_source(c->p, e->loc.file);
                Instr *where = emit(c, I_SITE, e->loc);
                where->dst = dest.id;
                where->type = f->type;
                where->length = strlen(source);
                where->text = mem(where->length + strlen(message) + 2);
                memcpy(where->text, source, where->length);
                strcpy(where->text + where->length + 1, message);
            }
            else if (init) {
                v = construct(c, init, f->type, dest.id);
            }
            else if (f->type->kind == T_REF || f->type->kind == T_STRING ||
                     f->type->kind == T_SLICE || f->type->kind == T_UNION ||
                     (f->type->kind == T_ARRAY && f->type->refs)) {
                fail(e->loc, "field '%s' needs an initializer", f->name);
            }
            else if (f->type->kind == T_STRUCT) {
                Expr empty = {.kind = E_RECORD, .loc = e->loc, .text = f->type->def->name};
                v = construct(c, &empty, f->type, dest.id);
            }
            else if (f->type->kind == T_GROW) {
                Expr empty = {.kind = E_ARRAY, .loc = e->loc};
                v = construct(c, &empty, f->type, dest.id);
            }
            else {
                Instr *z = emit(c, I_ZERO, e->loc);
                z->dst = dest.id;
                z->type = dest.type;
            }
            c->access = old;
            c->in_default = old_default;
            if (default_value) {
                c->default_stack.n--;
            }
            if (c->terminated) {
                c->inflight.n = inflight;
                return abrupt(c);
            }
            out.deps |= v.deps;
        }
        c->inflight.n = inflight;
        return out;
    }
    if (e->kind == E_ARRAY) {
        bool grow = e->text != NULL; // grow^/flex^ [...] or grow^/flex^ T[]
        int flex = grow && !strcmp(e->text, "flex^");
        if (e->test_spec) {
            Type *named = type_spec(c->p, e->test_spec);
            if (t && t != named) {
                fail(e->loc, "'%s' cannot initialize '%s'", type_name(named), type_name(t));
            }
            t = named;
        }
        if (grow && t && (t->kind != T_GROW || t->count != flex)) {
            fail(e->loc, "%s array literal cannot initialize '%s'", e->text, type_name(t));
        }
        Value first = {0};
        if (!t) {
            if (!e->args.n) {
                fail(e->loc, "empty array needs a type annotation");
            }
            first = expression(c, e->args.v[0], NULL);
        }
        if (!t && grow) {
            t = type_wrap(c->p, T_GROW, first.type, flex);
        }
        if (!t) {
            t = type_wrap(c->p, T_ARRAY, first.type, e->args.n);
            layout(t, e->loc); // Re-evaluation of inference expressions is avoided
                               // below.
            Value out = value(c, t);
            for (int j = 0; j < e->args.n; j++) {
                Value v = j ? expression(c, e->args.v[j], t->elem) : first;
                Value d = {0};
                d.type = t->elem;
                d.id = slot(c, d.type, true);
                Instr *ix = emit(c, I_INDEX, e->loc);
                ix->dst = d.id;
                ix->a = out.id;
                ix->type = t;
                ix->label = j;
                ix->b = -1;
                out.deps |= copy_to(c, d, v, e->loc, true).deps;
            }
            return out;
        }
        if (t->kind != T_ARRAY && t->kind != T_GROW) {
            fail(e->loc, "array literal requires array type");
        }
        if (t->kind == T_ARRAY && e->args.n != t->count) {
            fail(e->loc, "fixed array requires exactly %d elements", t->count);
        }
        Value out = {0};
        out.type = t;
        out.id = dst >= 0 ? dst : slot(c, t, false);
        if (t->kind == T_GROW) {
            layout(t->elem, e->loc);
            if (t->elem->owned || !t->elem->size) {
                fail(e->loc, "growable elements must be nonempty fixed-size values");
            }
            Instr *a = emit(c, I_REGION, e->loc);
            a->dst = out.id;
            a->type = t;
        }
        int inflight = c->inflight.n;
        add(&c->inflight, &out);
        for (int j = 0; j < e->args.n; j++) {
            if (t->kind == T_GROW) {
                Value v = j || !first.type ? expression(c, e->args.v[j], t->elem) : first;
                if (v.type != t->elem) {
                    fail(e->loc, "array element type mismatch");
                }
                out.deps |= v.deps;
                Instr *a = emit(c, I_PUSH, e->loc);
                a->a = out.id;
                a->b = v.id;
                a->type = t;
            }
            else {
                int d = slot(c, t->elem, true);
                Instr *ix = emit(c, I_INDEX, e->loc);
                ix->dst = d;
                ix->a = out.id;
                ix->type = t;
                ix->b = -1;
                ix->label = j;
                Value v = construct(c, e->args.v[j], t->elem, d);
                if (c->terminated) {
                    c->inflight.n = inflight;
                    return abrupt(c);
                }
                out.deps |= v.deps;
            }
        }
        c->inflight.n = inflight;
        return out;
    }
    Expr *old_target = c->operator_target;
    int old_destination = c->operator_destination;
    if (t && t->owned && dst >= 0 && (e->kind == E_BIN || e->kind == E_UNARY)) {
        c->operator_target = e;
        c->operator_destination = dst;
    }
    Value v = expression(c, e, t);
    c->operator_target = old_target;
    c->operator_destination = old_destination;
    if (dst >= 0) {
        if (v.id == dst) {
            return v;
        }
        Value d = {0};
        d.id = dst;
        d.type = t;
        return copy_to(c, d, v, e->loc, true);
    }
    return v;
}

static Value callable_value(Context *c, Type *type, Type *expected, Loc loc)
{
    bool pointer = expected && c_function_arm(expected, type);
    Type *bare = expected ? without_nil(c->p, expected, loc) : NULL;
    Function *f = type->kind == T_FUNCTION ? type->function : NULL;
    if (bare && bare->kind == T_CFUNC && f && !f->exported && !f->external) {
        fail(loc, "only externC^ and exportC^ functions have a C address for '%s'",
             type_name(bare));
    }
    if (expected && !pointer && (!callable_type(expected) || !callable_accepts(expected, type))) {
        fail(loc, "callable '%s' does not satisfy '%s'", type_name(type), type_name(expected));
    }
    Value out = value(c, type);
    Instr *i = emit(c, I_CONST, loc);
    i->dst = out.id;
    i->type = type;
    i->text = "0";
    return pointer ? convert_value(c, out, expected, -1, loc) : out;
}

void operator_contract(Function *f, Type *result, Loc loc)
{
    if (!strcmp(f->name, "op^=") && result->kind != T_BOOL) {
        fail(loc, "op^= must return bool^");
    }
    if (!strcmp(f->name, "op^<=>") &&
        (!kind_integer(result->kind) || kind_unsigned(result->kind))) {
        fail(loc, "op^<=> must return a signed integer");
    }
}

Value invoke_operator(Context *c, Function *f, Value a, Value b, Expr *origin)
{
    Loc loc = origin->loc;
    Value receiver = deref(c, f->right_receiver ? b : a, loc);
    access_check(c, f->priv, f->owner, loc, f->name);
    Value ref = value(c, type_wrap(c->p, T_REF, receiver.type, 0));
    Instr *address = emit(c, I_ADDR, loc);
    address->dst = ref.id;
    address->a = receiver.id;
    address->type = ref.type;
    ref.deps = receiver.storage | receiver.deps;
    if (!receiver.addressable) {
        ref.deps |= root(c, c->depth + 1, false, loc);
    }
    Expr target = {.kind = E_FUNCTION, .loc = loc, .function = f};
    Expr invoke = {.kind = E_CALL, .loc = loc, .a = &target};
    for (int j = 0; j < f->params.n; j++) {
        Param *param = f->params.v[j];
        add(&invoke.args, saved_value(param->self ? ref : f->right_receiver ? a : b, loc));
    }
    int destination = c->operator_target == origin ? c->operator_destination : -1;
    Value result = call(c, &invoke, NULL, destination);
    operator_contract(f, result.type, loc);
    return result;
}

// A range index borrows a view: [lo :: hi] excludes hi, [lo to^ hi] includes
// it, and an omitted bound is the start or the length.
static Value slice_view(Context *c, Expr *e, Type *expected)
{
    Program *p = c->p;
    Value base = deref(c, expression(c, e->a, NULL), e->loc);
    if (dead(base)) {
        return base;
    }
    if (c->terminated) {
        return abrupt(c);
    }
    if (base.type->kind == T_STRING) {
        fail(e->loc,
             "string^ has no []; slice it with s.slice(a :: b), or its bytes with s.bytes[a :: b]");
    }
    if (base.type->kind != T_ARRAY && base.type->kind != T_GROW && base.type->kind != T_SLICE) {
        fail(e->loc, "slicing requires an array or slice");
    }
    if ((base.type->kind == T_ARRAY || base.type->kind == T_GROW) && !base.addressable) {
        fail(e->loc, "slice source needs a binding; cannot borrow a temporary array");
    }
    int bounds[2] = {-1, -1};
    Expr *ends[2] = {e->b, e->c};
    bool ranged = e->kind == E_INDEX;
    if (ranged) {
        Value r = expression(c, e->b, type_range(p));
        if (c->terminated) {
            return abrupt(c);
        }
        for (int j = 0; j < 2; j++) {
            bounds[j] = field_value(c, r, r.type->def->fields.v[j], e->loc).id;
            ends[j] = NULL;
        }
    }
    for (int j = 0; j < 2; j++) {
        if (!ends[j]) {
            continue;
        }
        Value v = snapshot(c, expression(c, ends[j], type_builtin(p, T_I64)), e->loc);
        if (c->terminated) {
            return abrupt(c);
        }
        if (v.type->kind != T_I64) {
            fail(ends[j]->loc, "slice bounds must be i64^");
        }
        bounds[j] = v.id;
    }
    Type *elem = base.type->kind == T_STRING ? type_builtin(p, T_U8) : base.type->elem;
    Type *t = type_wrap(p, T_SLICE, elem, 0);
    if (expected && expected != t) {
        fail(e->loc, "slice is '%s', expected '%s'", type_name(t), type_name(expected));
    }
    Value out = {
        .id = slot(c, t, false),
        .type = t,
        .deps = base.deps |
                ((base.type->kind == T_ARRAY || base.type->kind == T_GROW) ? base.storage : 0)};
    Instr *i = emit(c, I_SLICE, e->loc);
    i->dst = out.id;
    i->a = base.id;
    i->b = bounds[0];
    i->other = bounds[1];
    i->label = !ranged && !strcmp(e->text, "to^");
    i->text = ranged ? "open" : NULL;
    i->type = base.type;
    return out;
}

// Collect trailing arguments into a temporary array borrowed for one call,
// or view a forwarded sequence.
static Value pack_value(Context *c, Expr *e, Type *expected)
{
    Program *p = c->p;
    if (e->a) {
        Expr view = {.kind = E_SLICE, .loc = e->a->loc, .a = e->a, .text = "::"};
        return slice_view(c, &view, expected);
    }
    layout(expected->elem, e->loc);
    if (expected->elem->owned) {
        fail(e->loc, "variadic elements cannot own storage");
    }
    Type *array = type_wrap(p, T_ARRAY, expected->elem, e->args.n ? e->args.n : 1);
    Value items = {0};
    int bounds = -1;
    if (e->args.n) {
        Expr literal = {.kind = E_ARRAY, .loc = e->loc, .args = e->args};
        items = expression(c, &literal, array);
        if (c->terminated) {
            return abrupt(c);
        }
    }
    else {
        // No arguments: an empty view of one zeroed element.
        items = value(c, array);
        Instr *z = emit(c, I_ZERO, e->loc);
        z->dst = items.id;
        z->type = array;
        Expr zero = {.kind = E_INT, .loc = e->loc, .text = "0"};
        bounds = expression(c, &zero, type_builtin(p, T_I64)).id;
    }
    Value out = {.id = slot(c, expected, false),
                 .type = expected,
                 .deps = items.deps | root(c, c->depth + 1, false, e->loc)};
    Instr *i = emit(c, I_SLICE, e->loc);
    i->dst = out.id;
    i->a = items.id;
    i->b = bounds;
    i->other = bounds;
    i->type = array;
    return out;
}

Value expression(Context *c, Expr *e, Type *expected)
{
    if (c->terminated) {
        return abrupt(c);
    }
    if (e->spread) {
        fail(e->loc, "'...' forwards only into a variadic parameter");
    }
    if (e->kind == E_PACK) {
        return pack_value(c, e, expected);
    }
    if (e->kind == E_PANIC) {
        Value v = snapshot(c, expression(c, e->a, NULL), e->loc);
        if (c->terminated) {
            return abrupt(c);
        }
        if (v.type->kind != T_STRING && !only_errors(v.type)) {
            fail(e->loc, "panic^ takes a string^ or an error value, found '%s'", type_name(v.type));
        }
        terminate(c, v, true, e->loc);
        return abrupt(c);
    }
    if (e->kind == E_TRY) {
        return construct(c, e, expected, -1);
    }
    if (e->kind == E_CATCH) {
        return catch_value(c, e, expected, -1);
    }
    if (expected && expected->kind == T_DYN) {
        return dyn_value(c, expression(c, e, NULL), expected, e->loc);
    }
    if (expected && expected->kind == T_UNION) {
        return construct(c, e, expected, -1);
    }
    Program *p = c->p;
    if (e->constant_spec) {
        Expr copy = *e;
        copy.constant_spec = NULL;
        Type *t = type_spec(p, e->constant_spec);
        Value v = expression(c, &copy, t);
        if (v.type != t || (expected && expected != t)) {
            fail(e->loc, "constant type mismatch");
        }
        return v;
    }
    Value out = {0};
    if (e->kind == E_EXPECT) {
        return expression(c, e->a, expression_type(c, e->b, NULL));
    }
    if (e->kind == E_SAVED) {
        if (e->compiler_value) {
            return *(Value *)e->compiler_value;
        }
        out.id = e->saved_slot;
        out.type = ((Slot *)c->in->slots.v[out.id])->type;
        return out;
    }
    if (e->kind == E_CHAIN) {
        Value previous = snapshot(c, expression(c, e->args.v[0], NULL), e->loc);
        if (dead(previous)) {
            return previous;
        }
        out = value(c, type_builtin(p, T_BOOL));
        int end = ++c->in->labels;
        for (int j = 0; j < e->names.n; j++) {
            Expr *operand = e->args.v[j + 1];
            bool fits = !strcmp((char *)e->names.v[j], "fits^");
            Value current = previous, test;
            if (fits) {
                Expr pair = *operand;
                pair.a = saved_value(previous, e->loc);
                test = fits_value(c, &pair);
            }
            else {
                Type *hint = operator_hint(p, e->names.v[j], previous.type, false, operand);
                current = snapshot(c, expression(c, operand, hint ? hint : previous.type), e->loc);
                if (dead(current)) {
                    return current;
                }
                Expr left = {.kind = E_SAVED, .loc = e->loc, .saved_slot = previous.id};
                Expr right = {.kind = E_SAVED, .loc = e->loc, .saved_slot = current.id};
                Expr pair = {
                    .kind = E_BIN, .loc = e->loc, .text = e->names.v[j], .a = &left, .b = &right};
                test = expression(c, &pair, NULL);
            }
            copy_to(c, out, test, e->loc, true);
            if (j + 1 < e->names.n) {
                int next = ++c->in->labels;
                Instr *br = emit(c, I_CBR, e->loc);
                br->a = test.id;
                br->label = next;
                br->other = end;
                Instr *lab = emit(c, I_LABEL, e->loc);
                lab->label = next;
            }
            previous = current;
        }
        Instr *br = emit(c, I_BR, e->loc);
        br->label = end;
        Instr *lab = emit(c, I_LABEL, e->loc);
        lab->label = end;
        return out;
    }
    if (e->kind == E_PRESENT) {
        Value src = e->a->kind == E_NAME ? place_expression(c, e->a) : expression(c, e->a, NULL);
        Value result = present_value(c, src, e->loc);
        remember_guard(&c->flow, c, e, e->a, true);
        return result;
    }
    if (e->kind == E_FITS) {
        return fits_value(c, e);
    }
    if (e->kind == E_COALESCE) {
        return coalesce_value(c, e, -1);
    }
    if (guarded_chain(e)) {
        return chain_value(c, e);
    }
    if (e->kind == E_FUNCTION) {
        return callable_value(c, callable_declaration(p, e->function), expected, e->loc);
    }
    if (e->kind == E_INT) {
        return literal_int(c, e, expected);
    }
    if (e->kind == E_BOOL || e->kind == E_FLOAT || e->kind == E_STRING) {
        Type *t =
            type_builtin(p, e->kind == E_BOOL ? T_BOOL
                            : e->kind == E_FLOAT
                                ? (expected && kind_float(expected->kind) ? expected->kind : T_F64)
                                : T_STRING);
        out = value(c, t);
        Instr *i = emit(c, e->kind == E_STRING ? I_STRING : I_CONST, e->loc);
        i->dst = out.id;
        i->type = t;
        i->length = e->length;
        i->text = e->kind == E_BOOL    ? (!strcmp(e->text, "true^") ? "1" : "0")
                  : e->kind == E_FLOAT ? float_constant(e->text, t->kind, e->loc)
                                       : e->text;
        return out;
    }
    if (e->kind == E_NAME && !strcmp(e->text, "nil^")) {
        Type *t = type_builtin(p, T_NIL);
        Value v = value(c, t);
        Instr *i = emit(c, I_CONST, e->loc);
        i->dst = v.id;
        i->type = t;
        i->text = "0";
        return v;
    }
    if (e->kind == E_NAME || e->kind == E_SELF) {
        if (!strcmp(e->text, "_^")) {
            fail(e->loc, "_^ discards a value and cannot be read");
        }
        if (c->in_default) {
            fail(e->loc, "default initializer cannot capture caller locals or self^");
        }
        Var *v = selected(c, e);
        if (!v && e->kind == E_NAME && !e->scope_kind) {
            Type *type = callable_group(p, NULL, e->text);
            if (type) {
                for (int j = 0; j < p->functions.n; j++) {
                    Function *member = p->functions.v[j];
                    if (!strcmp(member->name, e->text)) {
                        tool_record(p, e->loc, member->declaration, e->text, type, NULL, NULL, 12,
                                    false);
                    }
                }
                return callable_value(c, type, expected, e->loc);
            }
        }
        if (!v) {
            fail(e->loc, "unknown value '%s' (closures and global values are not supported)",
                 e->text);
        }
        if (v->unavailable) {
            fail(e->loc, "first^ binding '%s' is not definitely initialized here", v->name);
        }
        tool_record(p, e->loc, v->declaration, v->name,
                    v->narrowed && !c->raw_name ? v->narrowed : v->type, NULL, NULL,
                    v->parameter ? TOOL_PARAMETER : 13, false);
        Value result = variable(v);
        if (v->narrowed && !c->raw_name) {
            return convert_value(c, result, v->narrowed, -1, e->loc);
        }
        return result;
    }
    if (e->kind == E_CALL) {
        return call(c, e, expected, -1);
    }
    if (e->kind == E_CHAR) {
        Value v = value(c, type_builtin(p, T_CHAR));
        Instr *i = emit(c, I_CONST, e->loc);
        i->dst = v.id;
        i->type = v.type;
        i->text = e->text;
        return v;
    }
    if (e->kind == E_RECORD || e->kind == E_ARRAY || e->kind == E_IF || e->kind == E_SCOPE ||
        e->kind == E_RANGE || e->kind == E_INTERP) {
        return construct(c, e, expected, -1);
    }
    if (e->kind == E_MEMBER) {
        Value base = deref(c, expression(c, e->a, NULL), e->loc);
        if (dead(base)) {
            return base;
        }
        if ((base.type->kind == T_ARRAY || base.type->kind == T_GROW ||
             base.type->kind == T_STRING || base.type->kind == T_SLICE) &&
            !strcmp(e->text, "len")) {
            out = value(c, type_builtin(p, T_I64));
            Instr *i = emit(c, I_LENGTH, e->loc);
            i->dst = out.id;
            i->a = base.id;
            i->type = base.type;
            return out;
        }
        if (range_type(base.type) && strcmp(e->text, "start")) {
            return range_member(c, base, e->text, e->loc);
        }
        if (base.type->kind == T_STRING) {
            // s.bytes views the same bytes; string^ itself has no len.
            if (strcmp(e->text, "bytes")) {
                fail(e->loc,
                     "string^ has no %s; use s.bytes for bytes, or s.decode(at), s.slice(r) and "
                     "s.count_chars()",
                     e->text);
            }
            Value bytes = {.id = slot(c, type_wrap(p, T_SLICE, type_builtin(p, T_U8), 0), false),
                           .deps = base.deps};
            bytes.type = ((Slot *)c->in->slots.v[bytes.id])->type;
            Instr *i = emit(c, I_COPY, e->loc);
            i->dst = bytes.id;
            i->a = base.id;
            i->type = bytes.type;
            return bytes;
        }
        Type *view = error_view(base.type, e->text);
        if (view != base.type) {
            // Every error variant starts with the same fields, so the first one reads them.
            base = (Value){.id = case_at(c, base, view, false, e->loc),
                           .type = view,
                           .deps = base.deps,
                           .storage = base.storage,
                           .var = base.var,
                           .addressable = base.addressable};
        }
        int site = error_site(base.type, e->text);
        if (site >= 0) {
            Value where =
                deref(c, field_value(c, base, base.type->def->fields.v[0], e->loc), e->loc);
            return field_value(c, where, where.type->def->fields.v[site], e->loc);
        }
        if (base.type->kind != T_STRUCT) {
            fail(e->loc, "field access requires a structure");
        }
        Field *f = field(base.type->def, e->text);
        if (!f) {
            Type *type = callable_group(p, base.type->def, e->text);
            if (type) {
                for (int j = 0; j < base.type->def->methods.n; j++) {
                    Function *member = base.type->def->methods.v[j];
                    if (!strcmp(member->name, e->text)) {
                        access_check(c, member->priv, member->owner, e->loc, member->name);
                    }
                }
                return callable_value(c, type, expected, e->loc);
            }
        }
        if (!f) {
            fail(e->loc, "'%s' has no field '%s'", base.type->def->name, e->text);
        }
        Value result = field_value(c, base, f, e->loc);
        Var *root = NULL;
        char *path = NULL;
        if (c->raw_place != e && stable_path(c, e, &root, &path)) {
            Type *type = path_type(root, path);
            if (type) {
                result = convert_value(c, result, type, -1, e->loc);
            }
        }
        tool_record(p, e->name_loc.file ? e->name_loc : e->loc, f->loc, f->name, result.type, NULL,
                    f->owner, 8, false);
        return result;
    }
    if (e->kind == E_SLICE) {
        return slice_view(c, e, expected);
    }
    if (e->kind == E_INDEX && range_type(expression_type(c, e->b, NULL))) {
        return slice_view(c, e, expected);
    }
    if (e->kind == E_INDEX) {
        Value base = deref(c, expression(c, e->a, NULL), e->loc);
        if (dead(base)) {
            return base;
        }
        if (base.type->kind == T_STRING) {
            fail(e->loc,
                 "string^ has no []; read bytes with s.bytes[i], or characters with s.decode(at)");
        }
        if (base.type->kind != T_ARRAY && base.type->kind != T_GROW && base.type->kind != T_SLICE) {
            fail(e->loc, "indexing requires an array");
        }
        Value index = expression(c, e->b, type_builtin(p, T_I64));
        if (dead(index)) {
            return index;
        }
        if (!kind_integer(index.type->kind)) {
            fail(e->loc, "index must be an integer");
        }
        bool view = base.type->kind == T_SLICE || base.type->kind == T_STRING;
        out.type = base.type->kind == T_STRING ? type_builtin(p, T_U8) : base.type->elem;
        out.id = slot(c, out.type, true);
        out.storage = view ? base.deps : base.storage;
        // The container must outlive references loaded from it. This conservative
        // bound also covers aliases and loop-carried updates to element roots.
        out.deps = out.type->refs ? base.deps | (view ? 0 : base.storage) : 0;
        out.var = base.var;
        out.addressable = base.addressable;
        out.guarded_copy = base.guarded_copy;
        Instr *i = emit(c, I_INDEX, e->loc);
        i->dst = out.id;
        i->a = base.id;
        i->b = index.id;
        i->type = base.type;
        return view ? snapshot(c, out, e->loc) : out;
    }
    if (e->kind == E_UNARY) {
        if (!strcmp(e->text, "-") && e->a->kind == E_INT) {
            Expr neg = *e->a;
            neg.text = fmt("-%s", neg.text);
            return literal_int(c, &neg, expected);
        }
        Value a = !strcmp(e->text, "ref^")
                      ? place_expression(c, e->a)
                      : expression(c, e->a,
                                   (!strcmp(e->text, "-") || !strcmp(e->text, "bitnot^")) ? expected
                                                                                          : NULL);
        if (dead(a)) {
            return a;
        }
        if (!strcmp(e->text, "ref^")) {
            if (!a.addressable) {
                fail(e->loc, "cannot borrow a temporary; bind it first");
            }
            if (a.var && !a.var->mut) {
                fail(e->loc, "borrowing storage requires var^, found let^ '%s'", a.var->name);
            }
            out = value(c, type_wrap(p, T_REF, a.type, 0));
            out.deps = a.storage | a.deps;
            Instr *i = emit(c, I_ADDR, e->loc);
            i->dst = out.id;
            i->a = a.id;
            i->type = out.type;
            return out;
        }
        if (!strcmp(e->text, "*")) {
            if (a.type->kind != T_REF) {
                fail(e->loc, "'*' requires a reference");
            }
            return deref(c, a, e->loc);
        }
        if (!strcmp(e->text, "-")) {
            Function *op = operator_select(p, "-", a.type, NULL, e->loc);
            if (op) {
                return invoke_operator(c, op, a, (Value){0}, e);
            }
        }
        if ((!strcmp(e->text, "bitnot^") && !kind_integer(a.type->kind)) ||
            (!strcmp(e->text, "!") && a.type->kind != T_BOOL) ||
            (!strcmp(e->text, "-") &&
             !(kind_float(a.type->kind) ||
               (kind_integer(a.type->kind) && !kind_unsigned(a.type->kind))))) {
            fail(e->loc, "invalid unary operand");
        }
        out = value(c, a.type);
        Instr *i = emit(c, I_UNARY, e->loc);
        i->dst = out.id;
        i->a = a.id;
        i->type = a.type;
        i->text = e->text;
        return out;
    }
    if (e->kind == E_BIN) {
        bool logic = !strcmp(e->text, "and^") || !strcmp(e->text, "or^") ||
                     !strcmp(e->text, "&&") || !strcmp(e->text, "||");
        bool comparison = strchr("=!<>", e->text[0]) != NULL;
        bool shift = !strcmp(e->text, "bitshift^");
        bool bitwise = !strncmp(e->text, "bit", 3);
        Type *rhs_kind = comparison && e->b->kind != E_INT && e->b->kind != E_FLOAT &&
                                 e->b->kind != E_BOOL && e->b->kind != E_STRING
                             ? expression_type(c, e->b, NULL)
                             : NULL;
        bool nil_rhs = nil_name(e->b) || (rhs_kind && rhs_kind->kind == T_NIL);
        bool literal_left =
            e->a->kind == E_INT || e->a->kind == E_FLOAT ||
            (e->a->kind == E_UNARY && !strcmp(e->a->text, "-") && e->a->a->kind == E_INT);
        Type *left_hint =
            !logic && literal_left
                ? operator_hint(p, e->text, expression_type(c, e->b, NULL), true, e->a)
                : NULL;
        Value a = comparison && nil_rhs && e->a->kind == E_NAME
                      ? place_expression(c, e->a)
                      : expression(c, e->a,
                                   logic        ? type_builtin(p, T_BOOL)
                                   : left_hint  ? left_hint
                                   : comparison ? NULL
                                                : expected);
        if (dead(a)) {
            return a;
        }
        if (logic) {
            if (a.type->kind != T_BOOL) {
                fail(e->loc, "logical operands must be bool");
            }
            bool andop = !strcmp(e->text, "and^") || !strcmp(e->text, "&&");
            int known = condition_truth(&c->flow, e->a);
            if (known) {
                if ((known > 0) != andop) {
                    return a;
                }
                List facts = save_facts(&c->vars);
                refine_condition(p, &c->vars, &c->flow, e->a, andop);
                Value b = expression(c, e->b, a.type);
                restore_facts(&c->vars, facts);
                if (dead(b)) {
                    return b;
                }
                if (b.type->kind != T_BOOL) {
                    fail(e->loc, "logical operands must be bool");
                }
                return b;
            }
            out = value(c, a.type);
            copy_to(c, out, a, e->loc, true);
            int rhs = ++c->in->labels, end = ++c->in->labels;
            Instr *br = emit(c, I_CBR, e->loc);
            br->a = a.id;
            br->label = andop ? rhs : end;
            br->other = andop ? end : rhs;
            Instr *lab = emit(c, I_LABEL, e->loc);
            lab->label = rhs;
            List facts = save_facts(&c->vars);
            refine_condition(p, &c->vars, &c->flow, e->a, andop);
            Value b = expression(c, e->b, a.type);
            restore_facts(&c->vars, facts);
            if (!dead(b)) {
                copy_to(c, out, b, e->loc, true);
                br = emit(c, I_BR, e->loc);
                br->label = end;
            }
            c->terminated = false;
            lab = emit(c, I_LABEL, e->loc);
            lab->label = end;
            return out;
        }
        a = snapshot(c, a, e->loc);
        Type *operand_hint = operator_hint(p, e->text, a.type, false, e->b);
        Value b = comparison && nil_name(e->a) && e->b->kind == E_NAME
                      ? place_expression(c, e->b)
                      : expression(c, e->b,
                                   operand_hint       ? operand_hint
                                   : shift || nil_rhs ? NULL
                                                      : a.type);
        if (dead(b)) {
            return b;
        }
        Function *op = operator_select(p, e->text, a.type, b.type, e->loc);
        if (op) {
            Value result = invoke_operator(c, op, a, b, e);
            if (comparison && strcmp(e->text, "<=>")) {
                Expr left = {.kind = E_SAVED, .loc = e->loc, .compiler_value = &result};
                if (!strcmp(op->name, "op^=")) {
                    if (!strcmp(e->text, "!=")) {
                        Expr negate = {.kind = E_UNARY, .loc = e->loc, .text = "!", .a = &left};
                        return expression(c, &negate, NULL);
                    }
                    return result;
                }
                Expr zero = {.kind = E_INT, .loc = e->loc, .text = "0"};
                Expr compare = {
                    .kind = E_BIN, .loc = e->loc, .text = e->text, .a = &left, .b = &zero};
                return expression(c, &compare, NULL);
            }
            return result;
        }
        if (bitwise && (!kind_integer(a.type->kind) || !kind_integer(b.type->kind))) {
            fail(e->loc, "bitwise operands must be integers");
        }
        if (comparison && (a.type->kind == T_NIL || b.type->kind == T_NIL)) {
            if (strcmp(e->text, "=") && strcmp(e->text, "!=")) {
                fail(e->loc, "nil supports only equality comparisons");
            }
            if (nil_name(e->a) || nil_name(e->b)) {
                remember_guard(&c->flow, c, e, nil_name(e->a) ? e->b : e->a,
                               !strcmp(e->text, "!="));
            }
            Value test = present_value(c, a.type->kind == T_NIL ? b : a, e->loc);
            if (!strcmp(e->text, "!=")) {
                return test;
            }
            Value result = value(c, test.type);
            Instr *i = emit(c, I_UNARY, e->loc);
            i->dst = result.id;
            i->a = test.id;
            i->type = test.type;
            i->text = "!";
            return result;
        }
        if (!shift && a.type != b.type) {
            fail(e->loc, "binary operands have different types");
        }
        bool cmp = strchr("=!<>", e->text[0]) != NULL;
        if (cmp && a.type->kind == T_STRING) {
            // Byte order, which UTF-8 makes the order of scalar values.
            Value both[2] = {a, b};
            Value order = helper(c, "rh_text_compare", both, 2, T_I64, e->loc);
            if (strcmp(e->text, "<=>")) {
                return operate(c, e->text, order, constant(c, T_I64, "0", e->loc), e->loc);
            }
            Value sign = value(c, type_builtin(p, T_I32));
            Instr *i = emit(c, I_CONVERT, e->loc);
            i->dst = sign.id;
            i->a = order.id;
            i->type = sign.type;
            return sign;
        }
        if (!kind_integer(a.type->kind) && !kind_float(a.type->kind) &&
            !(cmp && a.type->kind == T_BOOL && strchr("=!", e->text[0])) &&
            !(cmp && a.type->kind == T_CHAR)) {
            fail(e->loc, "operator '%s' is not defined for '%s'", e->text, type_name(a.type));
        }
        if (kind_float(a.type->kind) && !strcmp(e->text, "%")) {
            fail(e->loc, "floating remainder is not supported");
        }
        if (!strcmp(e->text, "<=>")) {
            Expr *left = saved_value(a, e->loc), *right = saved_value(b, e->loc);
            Expr less = {.kind = E_BIN, .loc = e->loc, .text = "<", .a = left, .b = right};
            Expr greater = {.kind = E_BIN, .loc = e->loc, .text = ">", .a = left, .b = right};
            Expr minus = {.kind = E_INT, .loc = e->loc, .text = "-1"};
            Expr plus = {.kind = E_INT, .loc = e->loc, .text = "1"};
            Expr zero = {.kind = E_INT, .loc = e->loc, .text = "0"};
            Expr tail = {.kind = E_IF, .loc = e->loc, .a = &greater, .b = &plus, .c = &zero};
            Expr choice = {.kind = E_IF, .loc = e->loc, .a = &less, .b = &minus, .c = &tail};
            return expression(c, &choice, type_builtin(p, T_I32));
        }
        if (!strcmp(e->text, "//") || !strcmp(e->text, "**") || !strcmp(e->text, "..") ||
            !strcmp(e->text, "cross^") || !strcmp(e->text, "dot^")) {
            fail(e->loc, "operator '%s' requires a user-defined operator member", e->text);
        }
        out = value(c, cmp ? type_builtin(p, T_BOOL) : a.type);
        Instr *i = emit(c, I_BIN, e->loc);
        i->dst = out.id;
        i->a = a.id;
        i->b = b.id;
        i->text = e->text;
        i->type = a.type;
        return out;
    }
    fail(e->loc, "unsupported expression");
}
