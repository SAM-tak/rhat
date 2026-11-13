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

Roots root(Context *c, const void *key, int depth, bool external)
{
    Root *r = NULL;
    for (int j = 0; !r && j < c->root_keys.n; j++) {
        r = c->root_keys.v[j] == key ? c->root_list.v[j] : NULL;
    }
    if (!r) {
        r = mem(sizeof(*r));
        add(&c->root_keys, (void *)key);
        add(&c->root_list, r);
    }
    r->depth = depth;
    r->external = external;
    Root **v = mem(sizeof(*v));
    v[0] = r;
    return (Roots){v, 1};
}

Roots state_roots(Program *p)
{
    if (!p->state_root) {
        p->state_root = mem(sizeof(*p->state_root));
        p->state_root->external = true;
    }
    return (Roots){&p->state_root, 1};
}

static bool roots_has(Roots a, Root *r)
{
    for (int j = 0; j < a.n; j++) {
        if (a.v[j] == r) {
            return true;
        }
    }
    return false;
}

Roots roots_union(Roots a, Roots b)
{
    int extra = 0;
    for (int j = 0; j < b.n; j++) {
        extra += !roots_has(a, b.v[j]);
    }
    if (!extra) {
        return a;
    }
    if (!a.n) {
        return b;
    }
    Roots out = {mem(((size_t)a.n + (size_t)extra) * sizeof(*out.v)), a.n};
    memcpy(out.v, a.v, (size_t)a.n * sizeof(*out.v));
    for (int j = 0; j < b.n; j++) {
        if (!roots_has(a, b.v[j])) {
            out.v[out.n++] = b.v[j];
        }
    }
    return out;
}

bool roots_meet(Roots a, Roots b)
{
    for (int j = 0; j < a.n; j++) {
        if (roots_has(b, a.v[j])) {
            return true;
        }
        if (a.v[j]->inner && roots_has(b, a.v[j]->inner)) {
            return true;
        }
        for (int k = 0; k < b.n; k++) {
            if (b.v[k]->inner == a.v[j]) {
                return true;
            }
        }
    }
    return false;
}

Roots roots_inner(Roots a)
{
    Roots out = {0};
    for (int j = 0; j < a.n; j++) {
        Roots one = {a.v[j]->inner ? &a.v[j]->inner : &a.v[j], 1};
        out = roots_union(out, one);
    }
    return out;
}

bool roots_equal(Roots a, Roots b)
{
    return a.n == b.n && roots_union(a, b).n == a.n;
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
    if (e->invalid) {
        tool_abort();
    }
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
                if (v->invalid) {
                    tool_abort();
                }
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
                if (v->invalid) {
                    tool_abort();
                }
                return v;
            }
        }
        else if (v->lexical <= target) {
            if (v->invalid) {
                tool_abort();
            }
            return v;
        }
    }
    return NULL;
}

void tool_failed_binding(Program *p, List *vars, Stmt *s, int depth, int lexical)
{
    if (s->kind == S_BIND_MANY) {
        for (int j = 0; j < s->body.n; j++) {
            tool_failed_binding(p, vars, s->body.v[j], depth, lexical);
        }
    }
    if (s->kind != S_BIND || !strcmp(s->name, "_^")) {
        return;
    }
    for (int j = vars->n - 1; j >= 0; j--) {
        Var *v = vars->v[j];
        if (v->lexical == lexical && !strcmp(v->name, s->name)) {
            return;
        }
    }
    Var *v = mem(sizeof(*v));
    v->invalid = true;
    v->mut = s->mut;
    v->name = s->name;
    v->declaration = s->loc;
    v->type = type_builtin(p, T_VOID);
    v->depth = depth;
    v->lexical = lexical;
    add(vars, v);
    // An invalid initializer does not invalidate an already resolved annotation.
    Type *annotated = s->spec && s->spec->resolved ? s->spec->type : NULL;
    tool_record(p, s->loc, s->loc, s->name, annotated, NULL, NULL, s->mut ? 13 : 14, true);
}

// Distinct external roots can name the same caller storage.
static bool external_roots(Roots roots)
{
    for (int j = 0; j < roots.n; j++) {
        if (roots.v[j]->external) {
            return true;
        }
    }
    return false;
}

void forget_roots(List *vars, Roots roots, bool all)
{
    bool external = external_roots(roots);
    for (int j = 0; j < vars->n; j++) {
        Var *v = vars->v[j];
        bool direct = all || roots_meet(v->root, roots);
        if (direct) {
            v->version++;
            v->narrowed = NULL;
        }
        if (direct || (v->path_sensitive &&
                       (roots_meet(v->deps, roots) || (external && external_roots(v->deps))))) {
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
    if (e->kind == E_FITS || type_comparison(e)) {
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
    if (v->invalid) {
        tool_abort();
    }
    return (Value){v->slot, v->type, v->type->refs ? v->deps : (Roots){0}, v->root, v, true};
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
    r.deps = r.type->refs ? roots_inner(v.deps) : (Roots){0};
    r.addressable = true;
    r.readonly = v.readonly || read_reference(v.type);
    Instr *i = emit(c, I_DEREF, l);
    i->dst = r.id;
    i->a = v.id;
    i->type = r.type;
    return r;
}

void store_check(Context *c, Value dst, Value src, Loc l)
{
    if (dst.readonly) {
        fail(l, "cannot modify through a read^ reference");
    }
    mutation_check(c, dst.storage, l);
    if (dst.var && !dst.var->mut) {
        fail(l, "cannot modify let^ binding '%s'", dst.var->name);
    }
    for (int n = 0; n < dst.storage.n; n++) {
        Root *owner = dst.storage.v[n];
        if (owner->readonly) {
            fail(l, "cannot modify let^ binding '%s'", owner->readonly);
        }
        if (owner->external && !c->in->fn->procedure &&
            !(c->in->fn->mutable_self && roots_has(c->self_roots, owner))) {
            fail(l, "f^ cannot mutate borrowed storage; use p^ or mutable^self^");
        }
        if (roots_has(c->self_contents, owner)) {
            c->in->writes_reached = true;
        }
        for (int j = 0; j < src.deps.n; j++) {
            if (src.deps.v[j]->depth > owner->depth) {
                fail(l, "reference would outlive its owner (scope %d into scope %d)",
                     src.deps.v[j]->depth, owner->depth);
            }
            if (owner->external && !flow_store(c, owner, src.deps.v[j])) {
                fail(l, "cannot store nonstatic references through borrowed storage");
            }
        }
    }
    for (int n = 0; n < c->vars.n; n++) {
        Var *v = c->vars.v[n];
        if (v->type->refs && roots_meet(v->root, dst.storage)) {
            v->deps = roots_union(v->deps, src.deps);
        }
    }
    if (dst.var && dst.var->type->refs) {
        dst.var->deps = roots_union(dst.var->deps, src.deps);
    }
}

// An enum^ value seen as its backing integer.
static Value enum_integer(Context *c, Value v, Loc l)
{
    Value out = value(c, v.type->elem);
    Instr *i = emit(c, I_CONVERT, l);
    i->dst = out.id;
    i->a = v.id;
    i->type = out.type;
    return out;
}

// Roots as long-lived as these that say nothing about writing: a reference read out of a let^
// container may still write what it refers to, though the container must outlive it.
static Roots lifetime_roots(Context *c, Roots storage)
{
    Roots out = {0};
    for (int j = 0; j < storage.n; j++) {
        Root *r = storage.v[j];
        Roots one = {&storage.v[j], 1};
        out = roots_union(out, r->readonly ? root(c, r, r->depth, r->external) : one);
    }
    return out;
}

// Locate a channel handle responsible for noncopyability, including through aggregate members.
static Type *channel_copy_cause(Type *t, char **path)
{
    if (!t->disposable) {
        return NULL;
    }
    if (t->kind == T_CHANNEL) {
        return t;
    }
    List *parts = t->kind == T_STRUCT  ? &t->def->fields
                  : t->kind == T_UNION ? &t->def->alternatives
                                       : NULL;
    if (parts) {
        for (int j = 0; j < parts->n; j++) {
            Field *field = t->kind == T_STRUCT ? parts->v[j] : NULL;
            Type *cause = channel_copy_cause(field ? field->type : parts->v[j], path);
            if (cause) {
                if (field) {
                    *path = *path ? fmt("%s%s%s", field->name, **path == '[' ? "" : ".", *path)
                                  : field->name;
                }
                return cause;
            }
        }
    }
    else if (t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_BOUNDED) {
        Type *cause = channel_copy_cause(t->elem, path);
        if (cause) {
            *path = *path ? fmt("[]%s%s", **path == '[' ? "" : ".", *path) : "[]";
        }
        return cause;
    }
    return NULL;
}

static char *noncopy_reason(Type *t)
{
    char *path = NULL;
    Type *cause = channel_copy_cause(t, &path);
    if (cause) {
        return path ? fmt("contains noncopyable '%s' at '%s'", type_name(cause), path)
                    : fmt("is a noncopyable '%s'", type_name(cause));
    }
    return "is Disposable";
}

// A place keeps its noncopyable value, so only a temporary, which nothing else names, or the binding
// a return^ hands over may move out.
static void disposable_check(Context *c, Value src, Loc l)
{
    layout(src.type, l);
    bool handed = c->moving && src.var == c->moving &&
                  (src.id == c->moving->slot || c->moving->narrowed == src.type);
    if (src.type->disposable && src.addressable && !handed) {
        fail(l,
             "'%s' %s and cannot be copied; store a temporary directly, or keep it "
             "in a T[grow^] and borrow with 'ref^'",
             type_name(src.type), noncopy_reason(src.type));
    }
}

// A temporary with no binding has nothing to dispose it, so it cannot be used up in passing.
static void temporary_check(Value v, Loc l)
{
    if (!dead(v)) {
        layout(v.type, l);
        if (v.type->disposable && !v.addressable) {
            fail(l, "bind this '%s' first: a Disposable temporary has nothing to dispose it",
                 type_name(v.type));
        }
    }
}

void disposable_argument(Type *t, Loc l)
{
    layout(t, l);
    if (t->disposable) {
        fail(l,
             "'%s' %s and cannot be passed by value; take 'ref^', and keep the "
             "values in a T[grow^] that others refer to",
             type_name(t), noncopy_reason(t));
    }
}

Value copy_to(Context *c, Value dst, Value src, Loc l, bool init)
{
    if (dead(src)) {
        return src;
    }
    if (enum_backs(dst.type, src.type)) {
        src = enum_integer(c, src, l);
    }
    if (dst.type != src.type && sum_accepts(dst.type, src.type)) {
        src = convert_value(c, src, dst.type, -1, l);
    }
    if (src.readonly && type_writes(src.type)) {
        fail(l, "cannot copy writable references through a read^ access; use a read^ reference");
    }
    if (dst.type != src.type) {
        fail(l, "expected '%s', found '%s'", type_name(dst.type), type_name(src.type));
    }
    if (src.type->owned && !src.held) {
        fail(l, "owned region values cannot be copied; construct at the "
                "destination or borrow with 'ref^'");
    }
    disposable_check(c, src, l);
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
        dst.var->deps = roots_union(dst.var->deps, src.deps);
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
    if (arm->owned || arm->disposable) {
        // An owned or Disposable alternative is used where it lies.
        return (Value){.id = case_at(c, src, arm, false, loc),
                       .type = arm,
                       .deps = arm->refs ? src.deps : (Roots){0},
                       .storage = src.storage,
                       .var = src.var,
                       .addressable = src.addressable,
                       .readonly = src.readonly,
                       .held = src.held};
    }
    Value out = value(c, arm);
    out.deps = arm->refs ? src.deps : (Roots){0};
    out.readonly = src.readonly;
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
    if (read_reference(src.type) && (to->kind == T_REF || to->kind == T_DYN) &&
        !read_reference(to)) {
        fail(loc, "cannot drop read^ from a reference; expected '%s', found '%s'", type_name(to),
             type_name(src.type));
    }
    if (src.type == to && dst < 0) {
        return src;
    }
    Value out = {.id = dst >= 0 ? dst : slot(c, to, false),
                 .type = to,
                 .deps = to->refs ? src.deps : (Roots){0}};
    if (src.type == to) {
        return copy_to(c, out, src, loc, true);
    }
    if (range_type(to) && range_type(src.type)) {
        for (int j = 0; j < 2; j++) {
            copy_to(c, field_value(c, out, to->def->fields.v[j], loc),
                    field_value(c, src, src.type->def->fields.v[j], loc), loc, true);
        }
        return out;
    }
    if (read_reference(to) && src.type->kind == to->kind && src.type->elem == to->elem) {
        out.deps = src.deps;
        Instr *i = emit(c, I_COPY, loc);
        i->dst = out.id;
        i->a = src.id;
        i->type = to;
        return out;
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
    if (src.type->kind == T_DYN && (to->kind == T_DYN || to->kind == T_REF)) {
        // A fits^ test on the dyn^ value proved its concrete type or composer.
        Instr *i = emit(c, to->kind == T_DYN ? I_DYNCAST : I_DYNDATA, loc);
        i->dst = out.id;
        i->a = src.id;
        i->type = to;
        return out;
    }
    if (to->kind == T_UNION && src.type->kind != T_UNION && union_alternative(to, src.type) < 0) {
        for (int j = 0; j < to->def->alternatives.n; j++) {
            Type *arm = to->def->alternatives.v[j];
            if (sum_accepts(arm, src.type)) {
                src = convert_value(c, src, arm, -1, loc);
                out.deps = src.deps;
                break;
            }
        }
    }
    int tag = union_alternative(to, src.type);
    if (tag >= 0) {
        if (src.type->owned && !src.held) {
            fail(loc, "owned region values cannot be copied; construct at the destination");
        }
        disposable_check(c, src, loc);
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
    if (dst < 0 && (to->owned || to->disposable) && union_alternative(src.type, to) >= 0) {
        // A narrowed owned or Disposable alternative is used where it lies.
        return case_value(c, src, to, loc);
    }
    List selected = {0};
    for (int j = 0; j < src.type->def->alternatives.n; j++) {
        Type *arm = src.type->def->alternatives.v[j];
        if (sum_accepts(to, arm)) {
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
        Value converted = convert_value(c, part, to, out.id, loc);
        out.deps = roots_union(out.deps, converted.deps);
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

// u = v and u != v for a union u and a value v of one of its alternatives: u
// holds that alternative and the alternative equals v.
static Value union_equal(Context *c, Expr *e, Value u, Value v)
{
    Value out = value(c, type_builtin(c->p, T_BOOL));
    Value held = value(c, out.type);
    int yes = ++c->in->labels, no = ++c->in->labels, end = ++c->in->labels;
    Instr *i = emit(c, I_CASE_IS, e->loc);
    i->dst = held.id;
    i->a = u.id;
    i->type = u.type;
    i->label = union_alternative(u.type, v.type);
    nil_branch(c, held, yes, no, e->loc);
    nil_label(c, yes, e->loc);
    Value payload = case_value(c, u, v.type, e->loc);
    Expr left = {.kind = E_SAVED, .loc = e->loc, .compiler_value = &payload};
    Expr right = {.kind = E_SAVED, .loc = e->loc, .compiler_value = &v};
    Expr equal = {.kind = E_BIN, .loc = e->loc, .text = "=", .a = &left, .b = &right};
    copy_to(c, out, expression(c, &equal, NULL), e->loc, true);
    nil_jump(c, end, e->loc);
    nil_label(c, no, e->loc);
    i = emit(c, I_CONST, e->loc);
    i->dst = out.id;
    i->type = out.type;
    i->text = "0";
    nil_jump(c, end, e->loc);
    nil_label(c, end, e->loc);
    if (!strcmp(e->text, "!=")) {
        Expr result = {.kind = E_SAVED, .loc = e->loc, .compiler_value = &out};
        Expr negate = {.kind = E_UNARY, .loc = e->loc, .text = "!", .a = &result};
        return expression(c, &negate, NULL);
    }
    return out;
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
    if (t->kind == T_DYN) {
        dynamic = true;
        t = t->elem;
    }
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
    temporary_check(src, e->loc);
    if (src.type->kind == T_DYN) {
        // Only the yes branch narrows; the remaining types are not expressible.
        Type *yes = dyn_fits_target(c->p, e->test_spec, e->loc);
        if (read_reference(src.type)) {
            yes = type_wrap(c->p, yes->kind, yes->elem, 1);
        }
        remember_test(&c->flow, c, e, e->a, yes, src.type);
        Value out = value(c, type_builtin(c->p, T_BOOL));
        Instr *i = emit(c, I_DYNIS, e->loc);
        i->dst = out.id;
        i->a = src.id;
        i->type = yes;
        return out;
    }
    Type *target = context_type(c, e->test_spec);
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
            out.deps = roots_union(out.deps, convert_value(c, base, result, out.id, e->loc).deps);
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
    out.deps = roots_union(
        out.deps,
        convert_value(c, convert_value(c, base, plain, -1, e->loc), result, out.id, e->loc).deps);
    nil_jump(c, end, e->loc);
    nil_label(c, no, e->loc);
    out.deps = roots_union(out.deps, construct(c, e->b, result, out.id).deps);
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
    r.deps = r.type->refs ? base.deps : (Roots){0};
    r.var = base.var;
    r.addressable = base.addressable;
    r.guarded_copy = base.guarded_copy;
    r.readonly = base.readonly;
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

void dispose_elements(Context *c, Value array, Value from, Loc l)
{
    layout(array.type, l);
    Type *elem = array.type->elem;
    if (!elem->disposable && !elem->owned) {
        return;
    }
    Type *integer = type_builtin(c->p, T_I64);
    Value at = value(c, integer);
    if (array.type->kind == T_ARRAY && !owning_rows(array.type)) {
        Instr *i = emit(c, I_CONST, l);
        i->dst = at.id;
        i->type = integer;
        i->text = fmt("%d", array.type->count);
    }
    else {
        Instr *i = emit(c, I_LENGTH, l);
        i->dst = at.id;
        i->a = array.id;
        i->type = array.type;
    }
    if (from.id < 0) {
        from = value(c, integer);
        Instr *i = emit(c, I_CONST, l);
        i->dst = from.id;
        i->type = integer;
        i->text = "0";
    }
    Value one = value(c, integer);
    Instr *constant = emit(c, I_CONST, l);
    constant->dst = one.id;
    constant->type = integer;
    constant->text = "1";
    int test = ++c->in->labels, body = ++c->in->labels, end = ++c->in->labels;
    nil_jump(c, test, l);
    nil_label(c, test, l);
    nil_branch(c, operate(c, ">", at, from, l), body, end, l);
    nil_label(c, body, l);
    Value before = operate(c, "-", at, one, l);
    Instr *step = emit(c, I_COPY, l);
    step->dst = at.id;
    step->a = before.id;
    step->type = integer;
    Value item = {.id = slot(c, elem, true), .type = elem, .addressable = true};
    Instr *index = emit(c, I_INDEX, l);
    index->dst = item.id;
    index->a = array.id;
    index->b = at.id;
    index->type = array.type;
    dispose_value(c, item, l);
    if (elem->owned) {
        Instr *drop = emit(c, I_ROW_DROP, l);
        drop->a = array.id;
        drop->b = at.id;
        drop->type = array.type;
    }
    nil_jump(c, test, l);
    nil_label(c, end, l);
}

void dispose_value(Context *c, Value v, Loc l)
{
    if (v.row_reservation) {
        Instr *drop = emit(c, I_ROW_DROP, l);
        drop->a = v.row_reservation->a;
        drop->b = v.id;
        drop->type = v.type;
        return;
    }
    layout(v.type, l);
    Type *t = v.type;
    if (!t->disposable) {
        return;
    }
    if (!c->in->fn->procedure) {
        fail(l, "'%s' needs p^: disposing it calls dispose()", type_name(t));
    }
    if (t->kind == T_FRAME) {
        Value ref = value(c, type_wrap(c->p, T_REF, t, 0));
        Instr *address = emit(c, I_ADDR, l);
        address->dst = ref.id;
        address->a = v.id;
        address->type = ref.type;
        Instr *drop = emit(c, I_FRAME_DISPOSE, l);
        drop->a = ref.id;
        drop->callee = t->coroutine;
        drop->dst = -1;
        return;
    }
    if (t->kind == T_CHANNEL) {
        Instr *drop = emit(c, I_THREAD, l);
        drop->dst = -1;
        drop->label = THREAD_DROP;
        drop->a = v.id;
        drop->type = t;
        return;
    }
    if (t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_BOUNDED) {
        dispose_elements(c, v, (Value){.id = -1}, l);
        return;
    }
    Value place = {.id = v.id, .type = t, .addressable = true};
    if (t->kind == T_UNION) {
        for (int j = 0; j < t->def->alternatives.n; j++) {
            Type *arm = t->def->alternatives.v[j];
            if (!arm->disposable) {
                continue;
            }
            Value test = value(c, type_builtin(c->p, T_BOOL));
            Instr *is = emit(c, I_CASE_IS, l);
            is->dst = test.id;
            is->a = v.id;
            is->type = t;
            is->label = j;
            int here = ++c->in->labels, next = ++c->in->labels;
            nil_branch(c, test, here, next, l);
            nil_label(c, here, l);
            Value part = {.id = case_at(c, place, arm, false, l), .type = arm, .addressable = true};
            dispose_value(c, part, l);
            nil_jump(c, next, l);
            nil_label(c, next, l);
        }
        return;
    }
    Def *access = c->access;
    if (t->def->disposable) {
        Function *own = method(t->def, "dispose");
        c->access = own->owner;
        Expr *origin = mem(sizeof(*origin));
        origin->kind = E_NAME;
        origin->loc = l;
        origin->text = "dispose";
        invoke_operator(c, own, place, (Value){0}, origin);
    }
    for (int j = t->def->fields.n - 1; j >= 0; j--) {
        Field *f = t->def->fields.v[j];
        if (f->type->disposable) {
            c->access = f->owner;
            dispose_value(c, field_value(c, place, f, l), l);
        }
    }
    c->access = access;
}

void dispose_pending(Context *c, Value part)
{
    layout(part.type, c->in->fn->loc);
    if (part.type->disposable) {
        Value *kept = mem(sizeof(*kept));
        *kept = part;
        add(&c->pending_disposals, kept);
    }
}

void dispose_all(Context *c, int bindings, int disposals, Loc loc)
{
    for (int j = c->pending_disposals.n - 1; !c->disposed && j >= disposals; j--) {
        dispose_value(c, *(Value *)c->pending_disposals.v[j], loc);
    }
    for (int j = c->vars.n - 1; !c->disposed && j >= bindings; j--) {
        Var *v = c->vars.v[j];
        if (v->dispose) {
            dispose_value(c, variable(v), loc);
        }
    }
}

void cleanup(Context *c, int from, Loc l)
{
    for (int j = c->vars.n - 1; j >= from; j--) {
        Var *v = c->vars.v[j];
        if (v->dispose && !c->disposed) {
            dispose_value(c, variable(v), l);
        }
        if (owner_var(c, v)) {
            release(c, v->type, v->slot, l);
        }
    }
}

void escape_cleanup(Context *c, int bindings, int pending, int disposals, Loc loc)
{
    dispose_all(c, bindings, disposals, loc);
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

Def *static_owner(Program *p, Expr *target)
{
    if (target->kind != E_STATIC) {
        return NULL;
    }
    Type *t = type_spec(p, target->a->type_expr);
    if (t->kind != T_STRUCT) {
        fail(target->loc, "'%s' has no static member '%s'", type_name(t), target->text);
    }
    return t->def;
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
    Function *f = overload_select(p, &candidates, &want->params, NULL, 0, NULL, loc, true);
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
    in->indirect = true;
    if (want->result && !parameter_accepts(want->result, in->result)) {
        fail(loc, "callable result '%s' does not satisfy '%s'", type_name(in->result),
             type_name(want->result));
    }
}

static Function *select_call(Context *c, List *candidates, Expr *e, Type *receiver, Type *expected)
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
    return overload_select(c->p, candidates, &actual, e, receiver != NULL, expected, e->loc, true);
}

// Whether a value of type part could be stored inside a value of type whole.
static bool contains(Type *whole, Type *part)
{
    if (whole == part) {
        return true;
    }
    if (whole->kind == T_ARRAY || whole->kind == T_BOUNDED || whole->kind == T_GROW) {
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
bool may_reference(Type *t, Type *elem)
{
    if (!t->refs) {
        return false;
    }
    if (capturing_callable(t)) {
        Function *f = t->function;
        for (int j = 0; j < f->captures.n; j++) {
            Param *capture = f->captures.v[j];
            if (may_reference(capture->inferred->elem, elem)) {
                return true;
            }
        }
        return false;
    }
    if (t->kind == T_REF || t->kind == T_SLICE) {
        // bytesof^ can borrow any plain representation, not only u8 elements.
        return (t->kind == T_SLICE && t->elem->kind == T_U8 && plain_data(elem)) ||
               contains(elem, t->elem);
    }
    if (t->kind == T_STRING) {
        return false; // Strings view literals or other strings, never arrays.
    }
    if (t->kind == T_DYN) {
        return elem->kind == T_STRUCT; // Any composer of the dyn^ def.
    }
    if (t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_BOUNDED) {
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

// Storage reachable through t whose shrink, disposal or replacement can invalidate
// interior references; NULL when there is no such storage.
static Type *invalidated_elements(Type *t)
{
    if (t->kind == T_REF) {
        t = t->elem;
    }
    if (t->kind == T_GROW) {
        return t->count == ARRAY_FLEX || t->elem->disposable || t->elem->owned ? t->elem : NULL;
    }
    if (t->kind == T_UNION && t->disposable) {
        return t;
    }
    if (t->kind == T_BOUNDED && (t->elem->owned || t->elem->disposable)) {
        return t->elem;
    }
    if (t->kind == T_ARRAY && (owning_rows(t) || t->elem->disposable)) {
        return invalidated_elements(t->elem);
    }
    if (t->kind == T_STRUCT) {
        for (int j = 0; j < t->def->fields.n; j++) {
            Type *field = ((Field *)t->def->fields.v[j])->type;
            Type *elem = invalidated_elements(field);
            if (elem) {
                return elem;
            }
        }
    }
    return NULL;
}

// Shrinking flex^ storage returns its tail to the data stack and the OS, so
// no variable in scope or value still in use may refer into it. skip is the
// shrinking argument itself.
static void shrink_check(Context *c, Roots storage, Type *elem, Value *skip, Loc loc)
{
    bool external = external_roots(storage);
    if (c->in->params.n > 64 && external) {
        fail(loc, "storage invalidation analysis supports functions with at most 64 parameters");
    }
    if (c->p->state_fields.n && roots_meet(storage, state_roots(c->p))) {
        c->in->state_shrinks = true;
    }
    for (int j = 0; j < c->vars.n; j++) {
        Var *v = c->vars.v[j];
        if ((!roots_meet(v->root, storage) || v->type->refs) &&
            (roots_meet(v->deps, storage) || (external && external_roots(v->deps))) &&
            may_reference(v->type, elem)) {
            fail(loc,
                 "cannot shrink flex^ storage or dispose elements while '%s' may refer into it; "
                 "end its scope first",
                 v->name);
        }
    }
    for (int j = 0; j < c->inflight.n; j++) {
        Value *v = c->inflight.v[j];
        if (v != skip && (roots_meet(v->deps, storage) || (external && external_roots(v->deps))) &&
            may_reference(v->type, elem)) {
            fail(loc, "cannot shrink flex^ storage or dispose elements while an earlier operand "
                      "may refer into it");
        }
    }
    for (int k = 0; k < c->in->params.n && k < 64; k++) {
        if (roots_meet(c->param_deps[k], storage)) {
            c->in->shrinks |= UINT64_C(1) << k;
        }
    }
}

// Replacing a Disposable union may change the payload's type. Whole-union references
// remain valid; references into any of its alternatives must no longer be live.
static void replace_union(Context *c, Roots storage, Type *type, Value *skip, Loc loc)
{
    for (int j = 0; j < type->def->alternatives.n; j++) {
        shrink_check(c, storage, type->def->alternatives.v[j], skip, loc);
    }
}

static void replacement_parts(Context *c, Roots storage, Type *type, Value *skip, Loc loc)
{
    if (!type->disposable) {
        return;
    }
    if (type->kind == T_UNION) {
        replace_union(c, storage, type, skip, loc);
    }
    else if (type->kind == T_STRUCT) {
        for (int j = 0; j < type->def->fields.n; j++) {
            replacement_parts(c, storage, ((Field *)type->def->fields.v[j])->type, skip, loc);
        }
    }
    else if (type->kind == T_ARRAY) {
        replacement_parts(c, storage, type->elem, skip, loc);
    }
}

void replacement_check(Context *c, Value dst, Loc loc)
{
    replacement_parts(c, dst.storage, dst.type, NULL, loc);
}

static void shrink_argument(Context *c, Type *type, Roots deps, Value *skip, Loc loc)
{
    if (type->kind == T_REF) {
        type = type->elem;
    }
    if (type->kind == T_FUNCTION && type->function && type->function->environment) {
        for (int j = 0; j < type->function->captures.n; j++) {
            Param *capture = type->function->captures.v[j];
            shrink_argument(c, capture->inferred, deps, skip, loc);
        }
    }
    else if (type->kind == T_STRUCT) {
        for (int j = 0; j < type->def->fields.n; j++) {
            shrink_argument(c, ((Field *)type->def->fields.v[j])->type, deps, skip, loc);
        }
    }
    else if (type->kind == T_ARRAY && (owning_rows(type) || type->elem->disposable)) {
        shrink_argument(c, type->elem, deps, skip, loc);
    }
    else if (type->kind == T_UNION && type->disposable) {
        replace_union(c, deps, type, skip, loc);
    }
    else {
        Type *elem = invalidated_elements(type);
        if (elem) {
            shrink_check(c, deps, elem, skip, loc);
        }
    }
}

// Whether a result of type t can hold references from the arguments. An error's own record of
// where it was made is static text, so only fields declared on the error count.
static bool result_holds_arguments(Type *t)
{
    if (t->kind == T_UNION) {
        for (int j = 0; j < t->def->alternatives.n; j++) {
            if (result_holds_arguments(t->def->alternatives.v[j])) {
                return true;
            }
        }
        return false;
    }
    if (t->kind != T_STRUCT || !t->def->group) {
        return t->refs;
    }
    for (int j = 0; j < t->def->fields.n; j++) {
        Field *field = t->def->fields.v[j];
        if (field->name[0] != '\001' && field->type->refs) {
            return true;
        }
    }
    return false;
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
    // A dyn^ call has no invalidation summary, so it must not reach reclaimable storage.
    if (invalidated_elements(concrete->type)) {
        fail(loc, "'%s' has a flex^ field or replaceable payload and cannot be used through dyn^",
             concrete->name);
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
            in->indirect = true;
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

// One method of a frame's def: it passes straight to the frame's own member of that name,
// handing in its value parameter when the coroutine receives one.
static void frame_method(Def *d, Instance *co, const char *name, bool advance, Type *value,
                         Type *result)
{
    Function *f = mem(sizeof(*f));
    f->name = str(name);
    f->owner = d;
    f->module = co->fn->module;
    f->loc = f->declaration = co->fn->loc;
    f->lexical = 1;
    f->resolved = true;
    f->procedure = advance && co->fn->procedure;
    f->mutable_self = advance && !co->fn->procedure;
    f->required_result = result; // What the coroutine type answers, widened from the frame's.
    Param *self = mem(sizeof(*self));
    *self = (Param){.loc = f->loc, .name = "self^", .self = true};
    add(&f->params, self);
    Expr *receiver = mem(sizeof(*receiver));
    *receiver = (Expr){.kind = E_SELF, .loc = f->loc, .text = "self^"};
    Expr *target = mem(sizeof(*target));
    *target = (Expr){.kind = E_MEMBER, .loc = f->loc, .text = f->name, .a = receiver};
    Expr *call = mem(sizeof(*call));
    *call = (Expr){.kind = E_CALL, .loc = f->loc, .a = target};
    if (value) {
        Param *handed = mem(sizeof(*handed));
        *handed = (Param){.loc = f->loc, .name = "value", .inferred = value};
        add(&f->params, handed);
        Expr *arg = mem(sizeof(*arg));
        *arg = (Expr){.kind = E_NAME, .loc = f->loc, .text = "value"};
        add(&call->args, arg);
    }
    Stmt *ret = mem(sizeof(*ret));
    *ret = (Stmt){.kind = S_RETURN, .loc = f->loc, .a = call};
    add(&f->body, ret);
    add(&d->methods, f);
}

// The def a coroutine frame stands in for when it goes into a dyn^ of iface, one of std.sys's
// coroutine types: its resume takes what the coroutine receives, if anything, and its answers
// widen to iface's.
static Def *frame_def(Program *p, Type *frame, Def *iface)
{
    Instance *co = frame->coroutine;
    for (int j = 0; j < co->frame_defs.n; j += 2) {
        if (co->frame_defs.v[j] == iface) {
            return co->frame_defs.v[j + 1];
        }
    }
    Type **a = iface->coroutine_args;
    Type *turn = iface->coroutine_params > 1 ? type_union(p, a[1], a[2], co->fn->loc) : NULL;
    Def *d = mem(sizeof(*d));
    d->name = type_name(frame);
    d->module = co->fn->module;
    d->loc = co->fn->loc;
    d->type = frame;
    d->state = 2;
    d->resolved = true;
    d->id = p->defs.n;
    frame_method(d, co, "start", true, NULL, turn);
    frame_method(d, co, "resume", true, co->received, turn);
    frame_method(d, co, "done", false, NULL, NULL);
    frame_method(d, co, "result", false, NULL, a[2]);
    add(&co->frame_defs, iface);
    add(&co->frame_defs, d);
    return d;
}

// Pair a reference to a concrete composing type with its method table.
static Value dyn_value(Context *c, Value v, Type *dyn, Loc loc)
{
    if (dead(v) || v.type == dyn) {
        return v;
    }
    if ((v.readonly || read_reference(v.type)) && !read_reference(dyn)) {
        fail(loc, "cannot drop read^ from a reference");
    }

    if (v.type->kind == T_DYN && v.type->elem == dyn->elem) {
        return convert_value(c, v, dyn, -1, loc);
    }
    Type *t = v.type->kind == T_REF ? v.type->elem : NULL;
    bool frame = t && t->kind == T_FRAME && frame_conforms(t, dyn->elem->def);
    if (!frame &&
        (!t || t->kind != T_STRUCT || t->def->abstract || !derives(t->def, dyn->elem->def))) {
        fail(loc, "'%s' cannot become '%s'; pass a reference to a concrete type composing '%s'",
             type_name(v.type), type_name(dyn), dyn->elem->def->name);
    }
    DynTable *table =
        dyn_table(c->p, frame ? frame_def(c->p, t, dyn->elem->def) : t->def, dyn->elem->def, loc);
    Value out = {.id = slot(c, dyn, false), .type = dyn, .deps = v.deps};
    Instr *i = emit(c, I_DYN, loc);
    i->dst = out.id;
    i->a = v.id;
    i->label = table->id;
    i->type = dyn;
    return out;
}

// Convert argument capabilities without changing the lifetime of the referent.
static Value call_argument_value(Context *c, Expr *arg, Type *want, Type *hint)
{
    if (want && want->kind == T_DYN) {
        hint = arg->kind == E_UNARY && !strcmp(arg->text, "ref^")
                   ? type_wrap(c->p, T_REF, want->elem, want->count)
                   : NULL;
    }
    if (want && type_reads(want)) {
        hint = want;
    }
    Value v = expression(c, arg, hint);
    if (dead(v)) {
        return v;
    }

    if (want && want->kind == T_DYN) {
        v = dyn_value(c, v, want, arg->loc);
    }
    else if (want && v.type != want && type_reads(want) && sum_accepts(want, v.type)) {
        v = convert_value(c, v, want, -1, arg->loc);
    }
    else if (want && read_reference(want) && v.type->kind == T_REF &&
             parameter_accepts(want, v.type)) {
        v = convert_value(c, v, type_wrap(c->p, T_REF, v.type->elem, 1), -1, arg->loc);
    }

    return v;
}

// A p^ may write what a writable argument refers to, so it may not refer into a let^.
static void writable_argument(Value v, Param *param, Function *f, Loc loc)
{
    if (f->procedure && !param->self && !param->capture && type_writes(v.type)) {
        for (int j = 0; j < v.deps.n; j++) {
            if (v.deps.v[j]->readonly) {
                fail(loc, "cannot modify let^ binding '%s'", v.deps.v[j]->readonly);
            }
        }
    }
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
        fail(e->a->name_loc.file ? e->a->name_loc : e->loc, "'%s' has no method '%s'", iface->name,
             e->a->text);
    }
    Function *f = iface->methods.v[index];
    access_check(c, f->priv, f->owner, e->loc, f->name);
    const char *why = dyn_unsafe(p, iface, f);
    if (why) {
        fail(e->loc, "'%s' cannot be called through dyn^: %s", f->name, why);
    }
    if (procedure_call(f) && !c->in->fn->procedure) {
        fail(e->loc, "f^ cannot call p^ '%s'", f->name);
    }
    if (f->params.n != e->args.n + 1) {
        fail(e->loc, "'%s' expects %d arguments", f->name, f->params.n - 1);
    }
    if (f->procedure || f->mutable_self) {
        // The same mutation rules as a static call to a receiver in that storage.
        store_check(c, (Value){.storage = receiver.deps, .readonly = read_reference(receiver.type)},
                    (Value){0}, e->loc);
    }
    Instr *i = mem(sizeof(*i));
    i->op = I_DYNCALL;
    i->loc = e->loc;
    add(&i->args, (void *)(intptr_t)(receiver.id + 1));
    Roots deps = receiver.deps;
    List writable = {0}, arguments = {0};
    add(&arguments, &receiver);
    if ((f->procedure || f->mutable_self) && !read_reference(receiver.type)) {
        add(&writable, &receiver);
    }
    for (int j = 0; j < e->args.n; j++) {
        Param *param = f->params.v[j + 1];
        Type *want =
            function_parameter(p, f, param, function_argument_type(c, f, param, e->args.v[j]));
        Value v = call_argument_value(c, e->args.v[j], want, want);
        if (c->terminated) {
            return abrupt(c);
        }
        writable_argument(v, f->params.v[j + 1], f, e->loc);
        if (c->terminated) {
            return abrupt(c);
        }
        // Every table entry was specialized for exactly the declared types.
        if (v.type != want) {
            fail(e->loc, "argument to '%s' through dyn^ must be '%s', found '%s'", f->name,
                 type_name(want), type_name(v.type));
        }
        v = snapshot(c, v, e->loc);
        Value *saved = mem(sizeof(*saved));
        *saved = v;
        add(&arguments, saved);
        if (f->procedure && type_writes(v.type)) {
            add(&writable, saved);
        }
        deps = roots_union(deps, v.deps);
        add(&i->args, (void *)(intptr_t)(v.id + 1));
    }
    Instance contract = {.fn = f};
    for (int j = 0; j < arguments.n; j++) {
        add(&contract.params, ((Value *)arguments.v[j])->type);
    }
    Type *t = f->result ? function_result(p, &contract, f->result) : dyn_result(p, f);
    if (p->state_fields.n) {
        Roots state = state_roots(p);
        if (f->procedure) {
            mutation_check(c, state, e->loc);
            for (int j = 0; j < p->state_fields.n; j++) {
                Type *elem = invalidated_elements(state_field_type(p, p->state_fields.v[j]));
                if (elem) {
                    for (int k = 0; k < arguments.n; k++) {
                        Value *arg = arguments.v[k];
                        if (roots_meet(arg->deps, state) && may_reference(arg->type, elem)) {
                            fail(e->loc,
                                 "cannot shrink flex^ storage while an argument may refer into it");
                        }
                    }
                    shrink_argument(c, state_field_type(p, p->state_fields.v[j]), state, NULL,
                                    e->loc);
                }
            }
            forget_roots(&c->vars, state, false);
        }
        deps = roots_union(deps, state);
    }
    deps = result_holds_arguments(t) ? deps : (Roots){0};
    if (!f->procedure && type_writes(t) && !read_contract(f->result)) {
        for (int j = 0; j < arguments.n; j++) {
            if (type_reads(((Value *)arguments.v[j])->type)) {
                fail(
                    e->loc,
                    "cannot infer reference result capability through dyn^ with read^ arguments; add read^(parameter) or read^ to the method result");
            }
        }
    }
    for (int j = 0; j < writable.n; j++) {
        mutation_check(c, ((Value *)writable.v[j])->deps, e->loc);
    }

    if (expected && expected != t && !(read_reference(expected) && sum_accepts(expected, t))) {
        fail(e->loc, "call returns '%s', expected '%s'", type_name(t), type_name(expected));
    }
    Value out = {.type = t,
                 .id = t->kind == T_VOID  ? -1
                       : destination >= 0 ? destination
                                          : slot(c, t, false),
                 .deps = t->refs ? deps : (Roots){0}};

    i->dst = out.id;
    i->type = t;
    i->label = index;
    add(&c->in->code, i);
    if (f->procedure || f->mutable_self) {
        forget_roots(&c->vars, deps, false);
    }
    return out;
}

// x.bits and T.from_bits(n): the same bits seen as another number type of the same width.
static Value bit_cast(Context *c, Value v, int kind, Loc loc, int destination)
{
    Type *t = type_builtin(c->p, kind);
    Value out = destination >= 0 ? (Value){.id = destination, .type = t} : value(c, t);
    Instr *i = emit(c, I_CONVERT, loc);
    i->dst = out.id;
    i->a = v.id;
    i->type = out.type;
    i->text = "bits";
    return out;
}

// Construct owned rows directly in stable storage; no owned descriptor is copied.
static Value push_owned_row(Context *c, Value base, Expr *element, Loc loc)
{
    Type *elem = base.type->elem;
    int dst = slot(c, elem, true);
    Value index = value(c, type_builtin(c->p, T_I64));
    Instr *length = emit(c, I_LENGTH, loc);
    length->dst = index.id;
    length->a = base.id;
    length->type = base.type;
    Instr *reserve = emit(c, I_ROW_SLOT, loc);
    reserve->a = base.id;
    reserve->dst = dst;
    reserve->type = base.type;
    reserve->label = 1;
    int pending = c->pending_disposals.n;
    Value *rollback = mem(sizeof(*rollback));
    *rollback = (Value){
        .id = index.id, .type = base.type, .storage = base.storage, .row_reservation = reserve};
    add(&c->pending_disposals, rollback);
    Value row = construct(c, element, elem, dst);
    c->pending_disposals.n = pending;
    if (c->terminated) {
        return abrupt(c);
    }
    store_check(c, base, row, loc);
    Instr *commit = emit(c, I_ROW_COMMIT, loc);
    commit->a = base.id;
    commit->type = base.type;
    Value result = value(c, type_wrap(c->p, T_REF, elem, 0));
    Instr *address = emit(c, I_ADDR, loc);
    address->a = dst;
    address->dst = result.id;
    address->type = result.type;
    result.deps = roots_union(roots_union(base.storage, base.deps), row.deps);
    return result;
}

// The frame's turn seen as the part a caller wants, what it yielded or what it returned, in
// dst (a new slot when dst < 0). The turn is Y | T flattened into one union, so a part that is
// itself a union is taken alternative by alternative and retagged as that part.
static Value turn_part(Context *c, Value turn, Type *part, int dst, Loc l)
{
    if (turn.type == part && dst < 0) {
        return turn;
    }
    Value out = {.type = part, .deps = part->refs ? turn.deps : (Roots){0}};
    out.id = dst >= 0 ? dst : slot(c, part, false);
    if (turn.type == part || part->kind != T_UNION) {
        Value v = turn.type == part ? turn : case_value(c, turn, part, l);
        Instr *copy = emit(c, I_COPY, l);
        copy->dst = out.id;
        copy->a = v.id;
        copy->type = part;
        return out;
    }
    int end = ++c->in->labels;
    for (int j = 0; j < part->def->alternatives.n; j++) {
        Type *arm = part->def->alternatives.v[j];
        Value test = value(c, type_builtin(c->p, T_BOOL));
        Instr *is = emit(c, I_CASE_IS, l);
        is->dst = test.id;
        is->a = turn.id;
        is->type = turn.type;
        is->label = union_alternative(turn.type, arm);
        int here = ++c->in->labels, next = ++c->in->labels;
        nil_branch(c, test, here, next, l);
        nil_label(c, here, l);
        convert_value(c, case_value(c, turn, arm, l), part, out.id, l);
        nil_jump(c, end, l);
        nil_label(c, next, l);
    }
    nil_jump(c, end, l);
    nil_label(c, end, l);
    return out;
}

// Advancing the frame of a mutable^self^ method writes what its receiver reference points at,
// so from an f^ that has to be the f^'s own, as for a mutable^self^ call (or derived from its
// own self when it is mutable^self^ itself).
static void frame_reach_check(Context *c, Instance *co, Roots reached, Loc l)
{
    if (!co->fn->mutable_self || c->in->fn->procedure) {
        return;
    }
    for (int n = 0; n < reached.n; n++) {
        Root *owner = reached.v[n];
        if (owner->external && !(c->in->fn->mutable_self && roots_has(c->self_roots, owner))) {
            fail(l, "f^ cannot advance the coroutine of a mutable^self^ method on borrowed "
                    "storage; use p^");
        }
    }
}

// start, resume, done and result of a coroutine frame. Advancing a frame runs its body, so it
// carries the body's effect and writes the frame, which must be the caller's to write.
static Value frame_member(Context *c, Expr *e, Expr *target, Value base, int destination)
{
    Instance *co = base.type->coroutine;
    const char *name = target->text;
    // for^ in^ walks a frame with next: started or resumed as its state calls for, answering
    // what it yielded unless it finished.
    bool next = !strcmp(name, "\001next");
    bool advance = next || !strcmp(name, "start") || !strcmp(name, "resume");
    bool done = !strcmp(name, "done");
    if (!advance && !done && strcmp(name, "result")) {
        fail(target->loc, "a coroutine frame has start, resume, done and result, not '%s'", name);
    }
    // A resume hands in what yield^ receives, if anything does; start and the rest take nothing.
    bool hands = !strcmp(name, "resume") && co->received;
    if (e->args.n != (hands ? 1 : 0)) {
        fail(e->loc,
             hands ? "this coroutine receives '%s'; resume it with one value"
                   : "'%s' takes no arguments",
             hands ? type_name(co->received) : name);
    }
    if (advance) {
        if (co->fn->procedure && !c->in->fn->procedure) {
            fail(e->loc, "f^ cannot advance a p^ coroutine; '%s' runs its body", name);
        }
        store_check(c, base, (Value){0}, e->loc);
        frame_reach_check(c, co, base.deps, e->loc);
    }
    Value ref = value(c, type_wrap(c->p, T_REF, base.type, base.readonly));
    Instr *address = emit(c, I_ADDR, e->loc);
    address->dst = ref.id;
    address->a = base.id;
    address->type = ref.type;
    // result reads the last turn, which a finished frame's return^ wrote; the final type is
    // one alternative of it unless the two are the same.
    bool project = !advance && !done && co->final != co->turn;
    bool yielded = next && co->yielded != co->turn;
    Type *t = done ? type_builtin(c->p, T_BOOL) : co->turn;
    Value out = {.type = t};
    out.id = destination >= 0 && !project && !yielded ? destination : slot(c, t, false);
    if (t->refs) {
        out.deps = base.deps;
    }
    Value handed = {.id = -1};
    if (hands) {
        handed = construct(c, e->args.v[0], co->received, slot(c, co->received, false));
        if (c->terminated) {
            return abrupt(c);
        }
        store_check(c, base, handed, e->loc);
    }
    Instr *i = emit(c, advance ? I_RESUME : done ? I_FRAME_DONE : I_FRAME_RESULT, e->loc);
    i->dst = out.id;
    i->a = ref.id;
    i->type = t;
    i->callee = co;
    if (hands) {
        add(&i->args, (void *)(intptr_t)(handed.id + 1));
    }
    i->label = next ? 2 : strcmp(name, "start") != 0;
    if (advance && co->fn->procedure) {
        forget_roots(&c->vars, base.deps, false);
    }
    if (yielded) {
        // Only a frame that has not finished holds what it yielded.
        Value finished = value(c, type_builtin(c->p, T_BOOL));
        Instr *test = emit(c, I_FRAME_DONE, e->loc);
        test->dst = finished.id;
        test->a = ref.id;
        test->type = finished.type;
        test->callee = co;
        int y = destination >= 0 ? destination : slot(c, co->yielded, false);
        int skip = ++c->in->labels, take = ++c->in->labels;
        nil_branch(c, finished, skip, take, e->loc);
        nil_label(c, take, e->loc);
        Value part = turn_part(c, out, co->yielded, y, e->loc);
        nil_jump(c, skip, e->loc);
        nil_label(c, skip, e->loc);
        return part;
    }
    return project ? turn_part(c, out, co->final, destination, e->loc) : out;
}

static Value call(Context *c, Expr *e, Type *expected, int destination)
{
    Expr *target = e->a;
    bool numeric = target->kind == E_NAME && numeric_kind(target->text) >= 0;
    if ((c->type_constant || c->in->fn->constant_value) && !numeric) {
        fail(e->loc, "type constant cannot call a function, including in field defaults");
    }
    Program *p = c->p;
    settle_overload(p, target);
    int thread = thread_operation(target);
    if (thread) {
        return thread_call(c, e, thread, destination);
    }
    if (target->kind == E_MEMBER && !strcmp(target->text, "contains") && e->args.n == 1 &&
        range_type(expression_type(c, target->a, NULL))) {
        Value r = deref(c, expression(c, target->a, NULL), e->loc);
        Type *bound = range_bound(p, r.type);
        Value i = dead(r) ? r : expression(c, e->args.v[0], bound);
        if (dead(i)) {
            return i;
        }
        if (i.type != bound) {
            fail(e->loc, "%s contains a %s", type_name(r.type), type_name(bound));
        }
        return range_contains(c, r, range_widen(c, i, e->loc), e->loc);
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
    // A cast that can fail answers a union with its error, which must be
    // handled before it reaches a destination of the plain type.
    bool parse = target->kind == E_MEMBER && !strcmp(target->text, "parse") &&
                 target->a->kind == E_NAME && numeric_kind(target->a->text) >= 0;
    Type *cast = target->kind == E_TYPE ? enum_cast_type(p, type_spec(p, target->type_expr), e->loc)
                 : parse                ? parse_type(p, numeric_kind(target->a->text), e->loc)
                 : target->kind != E_NAME           ? NULL
                 : !strcmp(target->text, "char^")   ? char_cast_type(p, e->loc)
                 : !strcmp(target->text, "string^") ? text_cast_type(p, e->loc)
                                                    : NULL;
    if (cast && expected && expected != cast) {
        fail(e->loc, "conversion result is '%s', expected '%s'", type_name(cast),
             type_name(expected));
    }
    if (target->kind == E_TYPE) {
        return enum_cast(c, e, destination);
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
                !((v.type->kind == T_CHAR || v.type->kind == T_ENUM) && kind_integer(k))) {
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
    if (parse) {
        return number_parse(c, e, destination);
    }
    if (target->kind == E_MEMBER && !strcmp(target->text, "from_bits") &&
        target->a->kind == E_NAME && numeric_kind(target->a->text) >= 0) {
        int k = numeric_kind(target->a->text);
        if (!kind_float(k)) {
            fail(e->loc, "from_bits is defined for float types, not '%s'", builtin_name(k));
        }
        Type *bits = type_builtin(p, bits_kind(k));
        if (e->args.n != 1) {
            fail(e->loc, "%s.from_bits takes one %s", builtin_name(k), type_name(bits));
        }
        Value n = expression(c, e->args.v[0], bits);
        if (dead(n)) {
            return n;
        }
        if (n.type != bits) {
            fail(e->loc, "%s.from_bits takes a %s, found '%s'", builtin_name(k), type_name(bits),
                 type_name(n.type));
        }
        return bit_cast(c, n, k, e->loc, destination);
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
    if (target->kind == E_MEMBER &&
        (!strcmp(target->text, "trim") || !strcmp(target->text, "split"))) {
        Expr *library = text_library_call(p, e, expression_type(c, target->a, NULL));
        if (library) {
            return call(c, library, expected, destination);
        }
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
                v.type->kind != T_STRING && v.type->kind != T_BOOL && v.type->kind != T_CHAR &&
                v.type->kind != T_ENUM) {
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
    Value closure = {0};
    Def *self = NULL;
    List actual = {0};
    List values = {0};
    if ((self = static_owner(p, target))) {
        List candidates = overload_candidates(p, self, target->text, e->args.n);
        f = select_call(c, &candidates, e, NULL, expected);
        if (!f) {
            f = method(self, target->text);
        }
        if (!f || !f->static_member) {
            fail(target->name_loc.file ? target->name_loc : e->loc,
                 "'%s' has no static member '%s'", self->name, target->text);
        }
        access_check(c, f->priv, f->owner, e->loc, f->name);
    }
    else if (target->kind == E_MEMBER) {
        Value receiver = expression(c, target->a, NULL);
        if (dead(receiver)) {
            return receiver;
        }
        if (c->terminated) {
            return abrupt(c);
        }
        Value base = deref(c, receiver, e->loc);
        temporary_check(base, e->loc);
        if (base.type->kind == T_FRAME) {
            return frame_member(c, e, target, base, destination);
        }
        Type *held = base.type->kind == T_DYN ? base.type->elem : base.type;
        if (!strcmp(target->text, "dispose") && held->kind == T_STRUCT && held->def->disposable) {
            fail(e->loc, "dispose is called when the value's scope ends; it cannot be called "
                         "directly");
        }
        if (base.type->kind == T_GROW && base.type->count == ARRAY_SIZED &&
            (!strcmp(target->text, "push") || !strcmp(target->text, "pop") ||
             !strcmp(target->text, "append") || !strcmp(target->text, "clear") ||
             !strcmp(target->text, "truncate"))) {
            fail(e->loc, "'%s' has a fixed length after construction; %s is not allowed",
                 type_name(base.type), target->text);
        }
        bool growable = base.type->kind == T_GROW || base.type->kind == T_BOUNDED;
        bool flex = base.type->kind == T_GROW && base.type->count == ARRAY_FLEX;
        if (growable && !strcmp(target->text, "truncate")) {
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
            layout(base.type, e->loc);
            if (flex || base.type->elem->disposable || base.type->elem->owned) {
                shrink_check(c, base.storage, base.type->elem, NULL, e->loc);
            }
            Type *t = type_builtin(p, T_VOID);
            if (expected && expected != t) {
                fail(e->loc, "truncate has no result");
            }
            dispose_elements(c, base, len, e->loc);
            Instr *i = emit(c, I_TRUNCATE, e->loc);
            i->a = base.id;
            i->b = len.id;
            i->type = base.type;
            return (Value){.id = -1, .type = t};
        }
        if (growable && !strcmp(target->text, "append")) {
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
            layout(base.type->elem, e->loc);
            if (base.type->elem->owned) {
                fail(e->loc, "append cannot copy owned rows; push a freshly constructed row");
            }
            if (base.type->elem->disposable) {
                fail(e->loc, "append would copy Disposable '%s'; push temporaries instead",
                     type_name(base.type->elem));
            }
            Value dependencies = {.deps = base.type->elem->refs ? src.deps : (Roots){0}};
            store_check(c, base, dependencies, e->loc);
            forget_roots(&c->vars, roots_union(base.storage, base.deps), false);
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
        if (growable && (!strcmp(target->text, "pop") || !strcmp(target->text, "clear"))) {
            bool pop = !strcmp(target->text, "pop");
            if (e->args.n) {
                fail(e->loc, "%s expects no arguments", target->text);
            }
            store_check(c, base, (Value){0}, e->loc);
            layout(base.type, e->loc);
            if (flex || base.type->elem->disposable || base.type->elem->owned) {
                shrink_check(c, base.storage, base.type->elem, NULL, e->loc);
            }
            if (pop && base.type->elem->owned) {
                Instr *last = emit(c, I_ROW_SLOT, e->loc);
                last->a = base.id;
                last->dst = slot(c, base.type->elem, true);
                last->type = base.type;
                Value length = value(c, type_builtin(p, T_I64));
                Instr *len = emit(c, I_LENGTH, e->loc);
                len->dst = length.id;
                len->a = base.id;
                len->type = base.type;
                Value one = constant(c, T_I64, "1", e->loc);
                Value remaining = operate(c, "-", length, one, e->loc);
                dispose_elements(c, base, remaining, e->loc);
                Instr *truncate = emit(c, I_TRUNCATE, e->loc);
                truncate->a = base.id;
                truncate->b = remaining.id;
                truncate->type = base.type;
                return (Value){.id = -1, .type = type_builtin(p, T_VOID)};
            }
            if (!pop) {
                dispose_elements(c, base, (Value){.id = -1}, e->loc);
            }
            Type *t = pop ? base.type->elem : type_builtin(p, T_VOID);
            if (expected && expected != t) {
                fail(e->loc, "%s returns '%s', expected '%s'", target->text, type_name(t),
                     type_name(expected));
            }
            Value out = {.id = pop ? (destination >= 0 ? destination : slot(c, t, false)) : -1,
                         .type = t,
                         .deps = t->refs ? roots_union(base.storage, base.deps) : (Roots){0}};
            Instr *i = emit(c, pop ? I_POP : I_CLEAR, e->loc);
            i->dst = out.id;
            i->a = base.id;
            i->type = base.type;
            return out;
        }
        if (growable && !strcmp(target->text, "push")) {
            if (e->args.n != 1) {
                fail(e->loc, "push expects one element");
            }
            if (base.type->elem->owned) {
                store_check(c, base, (Value){0}, e->loc);
                Value row = push_owned_row(c, base, e->args.v[0], e->loc);
                forget_roots(&c->vars, roots_union(base.storage, base.deps), false);
                return row;
            }
            Value v = expression(c, e->args.v[0], base.type->elem);
            if (c->terminated) {
                return abrupt(c);
            }
            if (v.type != base.type->elem) {
                fail(e->loc, "push element type mismatch");
            }
            disposable_check(c, v, e->loc);
            store_check(c, base, v, e->loc);
            forget_roots(&c->vars, roots_union(base.storage, base.deps), false);
            Type *t = type_wrap(p, T_REF, base.type->elem, 0);
            if (expected && expected != t) {
                fail(e->loc, "push returns '%s', expected '%s'", type_name(t), type_name(expected));
            }
            Value out = {.id = destination >= 0 ? destination : slot(c, t, false),
                         .type = t,
                         .deps = roots_union(roots_union(base.storage, base.deps), v.deps)};
            Instr *i = emit(c, I_PUSH, e->loc);
            i->dst = out.id;
            i->a = base.id;
            i->b = v.id;
            i->type = base.type;
            return out;
        }
        if (base.type->kind == T_DYN) {
            if (base.readonly && !read_reference(base.type)) {
                // Reached through read^, it can only be read, whatever the element's own type.
                Value read = value(c, type_wrap(p, T_DYN, base.type->elem, 1));
                read.deps = base.deps;
                Instr *i = emit(c, I_COPY, e->loc);
                i->dst = read.id;
                i->a = base.id;
                i->type = read.type;
                return dyn_call(c, e, read, expected, destination);
            }
            return dyn_call(c, e, snapshot(c, base, e->loc), expected, destination);
        }
        if (base.type->kind != T_STRUCT) {
            fail(e->loc, "method call requires a structure");
        }
        self = base.type->def;
        List candidates = overload_candidates(p, self, target->text, e->args.n + 1);
        f = select_call(c, &candidates, e, type_wrap(p, T_REF, base.type, 0), expected);
        if (!f) {
            f = method(self, target->text);
        }
        if (!f) {
            fail(target->name_loc.file ? target->name_loc : e->loc, "'%s' has no method '%s'",
                 self->name, target->text);
        }
        if (f->static_member) {
            fail(e->loc, "'%s' is a static member; call it as %s.%s(...)", f->name, self->name,
                 f->name);
        }
        access_check(c, f->priv, f->owner, e->loc, f->name);
        if (base.guarded_copy && (f->procedure || f->mutable_self)) {
            fail(e->loc, "mutable nil-propagating receiver requires an optional reference");
        }
        if (f->procedure || f->mutable_self) {
            store_check(c, base, (Value){0}, e->loc);
        }
        Value ref = value(c, type_wrap(p, T_REF, base.type, base.readonly));
        Instr *i = emit(c, I_ADDR, e->loc);
        i->dst = ref.id;
        i->a = base.id;
        i->type = ref.type;
        ref.deps = roots_union(base.storage, base.deps);
        ref.target = base.storage;
        ref.contents = base.deps;
        if (!base.addressable) {
            Roots temporary = root(c, e, c->depth + 1, false);
            ref.deps = roots_union(ref.deps, temporary);
            ref.target = roots_union(ref.target, temporary);
        }
        Value *saved = mem(sizeof(*saved));
        *saved = ref;
        add(&values, saved);
        add(&actual, ref.type);
    }
    else if (target->kind == E_FUNCTION) {
        closure = expression(c, target, NULL);
        f = closure.type->function;
    }
    else if ((f = this_function(p, c->in, target))) {
        self = f == c->in->fn ? c->in->self : NULL;
        if (self && !f->static_member) {
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
        f = select_call(c, &candidates, e, NULL, expected);
        if (!f) {
            fail(e->loc, "unknown function '%s'", target->text);
        }
        if (binding && f->environment) {
            closure = snapshot(c, variable(binding), e->loc);
        }
    }
    else {
        Value target_value = expression(c, target, NULL);
        closure = target_value;
        List candidates = callable_candidates(target_value.type, e->args.n);
        f = select_call(c, &candidates, e, NULL, expected);
        if (!f) {
            fail(e->loc, "callable has no statically selected implementation");
        }
    }
    if (f->abstract) {
        fail(e->loc, "cannot call abstract method '%s'", f->name);
    }
    Var *alias = target->kind == E_NAME ? selected(c, target) : NULL;
    if (alias && callable_type(alias->type)) {
        tool_record(p, target->loc, alias->declaration, alias->name, alias->type, NULL, NULL,
                    alias->parameter ? TOOL_PARAMETER
                    : alias->mut     ? 13
                                     : 14,
                    false);
    }
    // Direct function targets also implement implicit disposal, error messages
    // and interpolation. Only operator syntax has a written reference there.
    else if (target->kind != E_FUNCTION || f->operator_member) {
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
    if (procedure_call(f) && !c->in->fn->procedure) {
        fail(e->loc, "f^ cannot call p^ '%s'", f->name);
    }
    // Arguments evaluated so far stay in use while later ones may shrink.
    int inflight = c->inflight.n;
    for (int j = 0; j < values.n; j++) {
        add(&c->inflight, values.v[j]);
    }
    for (int j = 0; j < e->args.n; j++) {
        Param *param = f->params.v[right_self ? j : j + offset];
        Type *natural = function_argument_type(c, f, param, e->args.v[j]);
        Type *want = function_parameter(p, f, param, natural);
        Value v =
            call_argument_value(c, e->args.v[j], want, parameter_hint(p, param, e->args.v[j]));
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
                if (!okay && want->kind == T_SLICE && v.type->kind == T_SLICE) {
                    fail(e->loc,
                         "'%s' may write this argument, which is '%s'; declare it read^ if it only "
                         "reads",
                         f->name, type_name(v.type));
                }
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
        disposable_argument(v.type, e->loc);
        writable_argument(v, param, f, e->loc);
        if (argument_pack(v.type) && (!f->heterogeneous || strcmp(param->name, "..."))) {
            fail(e->loc, "argument packs must be forwarded to a variadic parameter");
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
    // Check after all arguments too: a later read argument may overlap an
    // earlier writable receiver or argument. Pure calls do not mutate them.
    for (int j = 0; j < values.n; j++) {
        Param *param = f->params.v[j];
        Value *arg = values.v[j];
        if ((f->procedure || (f->mutable_self && param->self)) && type_writes(arg->type)) {
            mutation_check(c, arg->deps, e->loc);
        }
    }
    for (int j = 0; j < f->captures.n; j++) {
        Param *capture = f->captures.v[j];
        Value ref;
        if (!closure.type && c->in->fn == f) {
            ref = variable(lookup(c, capture->name));
        }
        else if (!closure.type) {
            fail(e->loc, "capturing function requires its lexical environment");
        }
        else {
            ref = (Value){.id = slot(c, capture->inferred, true),
                          .type = capture->inferred,
                          .deps = closure.deps,
                          .target = closure.deps,
                          .contents = closure.deps};
            Instr *field = emit(c, I_FIELD, e->loc);
            field->dst = ref.id;
            field->a = closure.id;
            field->type = f->environment;
            field->label = j;
        }
        Value *saved = mem(sizeof(*saved));
        *saved = ref;
        add(&values, saved);
        add(&c->inflight, saved);
        if (f->procedure && (capture->capture->mut || type_writes(ref.type->elem))) {
            mutation_check(c, ref.deps, e->loc);
        }
    }
    Instance *in = instantiate(p, f, self, &actual, e->loc);
    if (f->yields && in->state == 1) {
        fail(e->loc, "coroutine '%s' cannot hold a frame of itself", f->name);
    }
    // A mutable^self^ method that writes what its receiver refers to (or has no summary yet)
    // reaches past the receiver: from an f^ those targets have to be its own as well.
    bool reaches = f->mutable_self && (in->state != 2 || in->writes_reached);
    for (int j = 0; reaches && j < values.n && j < f->params.n; j++) {
        Value *v = values.v[j];
        if (!((Param *)f->params.v[j])->self) {
            continue;
        }
        bool told = v->target.n && roots_equal(roots_union(v->target, v->contents), v->deps);
        Roots reached = told ? v->contents : roots_inner(v->deps);
        for (int n = 0; n < reached.n; n++) {
            Root *owner = reached.v[n];
            if (roots_has(c->self_contents, owner)) {
                c->in->writes_reached = true;
            }
            if (owner->external && !c->in->fn->procedure &&
                !(c->in->fn->mutable_self && roots_has(c->self_roots, owner))) {
                fail(e->loc, "f^ cannot call a mutable^self^ method on a value referring to "
                             "borrowed storage; use p^");
            }
        }
    }
    if (p->state_fields.n && !f->external) {
        Roots state = state_roots(p);
        if (f->procedure) {
            mutation_check(c, state, e->loc);
            forget_roots(&c->vars, state, false);
        }
        if (in->state != 2 || in->state_shrinks) {
            for (int j = 0; j < p->state_fields.n; j++) {
                Type *elem = invalidated_elements(state_field_type(p, p->state_fields.v[j]));
                if (elem) {
                    shrink_argument(c, state_field_type(p, p->state_fields.v[j]), state, NULL,
                                    e->loc);
                }
            }
        }
    }

    // A callee still being lowered has no summary yet; assume it shrinks.
    for (int j = 0; j < values.n; j++) {
        Value *v = values.v[j];
        if ((in->state != 2 || j >= 64 || ((in->shrinks >> j) & 1)) && v->deps.n) {
            shrink_argument(c, v->type, v->deps, v, e->loc);
        }
    }
    c->inflight.n = inflight;
    Type *t = in->result;
    if (expected && expected != t && !(read_reference(expected) && sum_accepts(expected, t))) {
        fail(e->loc, "call returns '%s', expected '%s'", type_name(t), type_name(expected));
    }
    Value out = {0};
    out.type = t;
    out.id = t->kind == T_VOID ? -1 : destination >= 0 ? destination : slot(c, t, false);
    Roots deps = {0};
    for (int j = 0; j < values.n; j++) {
        Value *v = values.v[j];
        deps = roots_union(deps, v->deps);
    }
    Roots returned = flow_call(c, in, &values, e->loc);
    if (t->refs) {
        // A frame holds its arguments until it runs, as a record built from them would.
        out.deps = f->yields ? deps : returned;
    }

    if (t->owned && destination < 0) {
        fail(e->loc, "owned return requires a binding or return destination");
    }
    Instr *i = emit(c, f->yields ? I_FRAME : I_CALL, e->loc);
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
            (literal->kind == E_ARRAY &&
             (a->kind == T_ARRAY || a->kind == T_GROW || a->kind == T_BOUNDED))) {
            // A mixed result must not implicitly choose an error constructor.
            if (literal->kind == E_RECORD && a->def->group) {
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

static bool array_storage(Type *t)
{
    return t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_BOUNDED || t->kind == T_SLICE;
}

// Copies rebuild owned array rows in their destination regions; references in
// leaf elements retain the source's borrow dependencies.
static Value copy_array(Context *c, Expr *e, Type *t, int dst)
{
    Value source = deref(c, expression(c, e->args.v[0], NULL), e->loc);
    if (dead(source)) {
        return source;
    }
    Type *s = source.type;
    if (!array_storage(s) || !array_storage(t) || t->kind == T_SLICE ||
        (t->kind == T_GROW && t->count == ARRAY_CAPPED)) {
        fail(e->loc, "source... requires an array or slice and an owning array destination");
    }
    layout(t, e->loc);
    if (t->elem->disposable || s->elem->disposable) {
        fail(e->loc, "source... would copy Disposable elements");
    }
    bool recursive =
        (t->elem->owned || t->elem != s->elem) && array_storage(t->elem) && array_storage(s->elem);
    if ((!recursive && t->elem != s->elem) || (t->elem->owned && !recursive)) {
        fail(e->loc, "source... cannot copy '%s' elements into '%s'", type_name(s->elem),
             type_name(t->elem));
    }
    if (source.readonly && !recursive && type_writes(t->elem)) {
        fail(e->loc,
             "cannot copy writable references through a read^ access; use a read^ reference");
    }
    if (t->kind == T_ARRAY && s->kind == T_ARRAY && t->count != s->count) {
        fail(e->loc, "fixed array copy requires exactly %d elements, found %d", t->count, s->count);
    }
    Value length = value(c, type_builtin(c->p, T_I64));
    Instr *len = emit(c, I_LENGTH, e->loc);
    len->dst = length.id;
    len->a = source.id;
    len->type = s;
    if (t->kind == T_ARRAY && s->kind != T_ARRAY) {
        Expr number = {.kind = E_INT, .loc = e->loc, .text = fmt("%d", t->count)};
        Value count = expression(c, &number, length.type);
        Value equal = operate(c, "=", length, count, e->loc);
        Instr *check = emit(c, I_ASSERT, e->loc);
        add(&check->args, (void *)(intptr_t)(equal.id + 1));
    }
    Value out = {.id = dst >= 0 ? dst : slot(c, t, false), .type = t};
    out.deps = t->elem->refs ? source.deps : (Roots){0};
    if (t->kind == T_GROW || owning_rows(t)) {
        Instr *region = emit(c, I_REGION, e->loc);
        region->dst = out.id;
        region->type = t;
    }
    else if (t->kind == T_BOUNDED) {
        Instr *clear = emit(c, I_CLEAR, e->loc);
        clear->a = out.id;
        clear->type = t;
    }
    // Flat owners keep the existing bulk-copy path.
    if (t->kind == T_GROW && !recursive) {
        Value view = value(c, type_wrap(c->p, T_SLICE, t->elem, 0));
        Instr *slice = emit(c, I_SLICE, e->loc);
        slice->dst = view.id;
        slice->a = source.id;
        slice->b = slice->other = -1;
        slice->type = s;
        Instr *append = emit(c, I_APPEND, e->loc);
        append->a = out.id;
        append->b = view.id;
        append->type = t;
        return out;
    }
    int inflight = c->inflight.n, pending = c->pending_disposals.n;
    add(&c->inflight, &out);
    if (t->kind != T_ARRAY || owning_rows(t)) {
        dispose_pending(c, out);
    }
    Expr zero = {.kind = E_INT, .loc = e->loc, .text = "0"};
    Expr one = {.kind = E_INT, .loc = e->loc, .text = "1"};
    Value at = expression(c, &zero, length.type), step = expression(c, &one, length.type);
    int test = ++c->in->labels, body = ++c->in->labels, end = ++c->in->labels;
    nil_jump(c, test, e->loc);
    nil_label(c, test, e->loc);
    nil_branch(c, operate(c, "<", at, length, e->loc), body, end, e->loc);
    nil_label(c, body, e->loc);
    Value item = {.id = slot(c, s->elem, true),
                  .type = s->elem,
                  .deps = source.deps,
                  .readonly = source.readonly,
                  .addressable = true};
    Instr *index = emit(c, I_INDEX, e->loc);
    index->dst = item.id;
    index->a = source.id;
    index->b = at.id;
    index->type = s;
    Expr copy = {.kind = E_ARRAY, .loc = e->loc, .text = "copy"};
    add(&copy.args, saved_value(item, e->loc));
    if (owning_rows(t)) {
        push_owned_row(c, out, &copy, e->loc);
    }
    else if (t->kind == T_GROW || t->kind == T_BOUNDED) {
        Value element = recursive ? construct(c, &copy, t->elem, -1) : item;
        Instr *push = emit(c, I_PUSH, e->loc);
        push->a = out.id;
        push->b = element.id;
        push->type = t;
    }
    else {
        Value place = {.id = slot(c, t->elem, true), .type = t->elem};
        index = emit(c, I_INDEX, e->loc);
        index->dst = place.id;
        index->a = out.id;
        index->b = at.id;
        index->type = t;
        if (recursive) {
            construct(c, &copy, t->elem, place.id);
        }
        else {
            copy_to(c, place, item, e->loc, true);
        }
    }
    Value next = operate(c, "+", at, step, e->loc);
    Instr *advance = emit(c, I_COPY, e->loc);
    advance->dst = at.id;
    advance->a = next.id;
    advance->type = at.type;
    nil_jump(c, test, e->loc);
    nil_label(c, end, e->loc);
    c->inflight.n = inflight;
    c->pending_disposals.n = pending;
    return out;
}

// Construct repeated elements, copied arrays, or reserved capacity.
static Value sized_array(Context *c, Expr *e, Type *t, int dst)
{
    if (!strcmp(e->text, "copy")) {
        return copy_array(c, e, t, dst);
    }
    bool reserve = !strcmp(e->text, "reserve"), repeat = !strcmp(e->text, "repeat");
    if (!t) {
        fail(e->loc, "array construction needs a type; write T[...]{...} or annotate the binding");
    }
    bool fixed = repeat && t->kind == T_ARRAY;
    if (e->repeat_count == -1 && !fixed) {
        fail(e->loc, "repeat^max^ requires a fixed-length array type");
    }
    if (!fixed && (t->kind != T_GROW || reserve != (t->count == ARRAY_CAPPED))) {
        fail(e->loc,
             reserve ? "{max^ capacity} builds a T[max^], not '%s'"
                     : "repeat^ builds T[N], T[] and T[grow^], not '%s'",
             type_name(t));
    }
    if (fixed && ((e->repeat_count != -1 && e->repeat_count != t->count) || t->elem->owned)) {
        fail(e->loc,
             t->elem->owned ? "repeat^ cannot copy owned elements into '%s'"
                            : "'%s' is built by repeat^ with the constant count %d",
             type_name(t), t->count);
    }
    layout(t, e->loc);
    if (t->elem->disposable && !reserve) {
        fail(e->loc, "repeat^ would copy Disposable '%s'", type_name(t->elem));
    }
    Value out = {.id = dst >= 0 ? dst : slot(c, t, false), .type = t};
    Value count = {0}, source = {0};
    if (repeat) {
        source = snapshot(c, expression(c, e->args.v[0], t->elem), e->loc);
        if (dead(source)) {
            return source;
        }
        if (source.type != t->elem) {
            fail(e->loc, "array initial value must be '%s'", type_name(t->elem));
        }
    }
    if ((repeat && !fixed) || reserve) {
        count = snapshot(c, expression(c, e->args.v[repeat], type_builtin(c->p, T_I64)), e->loc);
        if (dead(count)) {
            return count;
        }
        if (count.type->kind != T_I64) {
            fail(e->loc, "array %s must be i64^", reserve ? "capacity" : "length");
        }
    }
    if (reserve) {
        source = count;
    }
    if (dead(source)) {
        return source;
    }
    out.deps = t->elem->refs && !reserve ? source.deps : (Roots){0};
    if (fixed) {
        Instr *fill = emit(c, I_FILL, e->loc);
        fill->type = t;
        fill->dst = out.id;
        fill->b = source.id;
        return out;
    }
    Instr *region = emit(c, I_REGION, e->loc);
    region->dst = out.id;
    region->type = t;
    Instr *fill = emit(c, I_FILL, e->loc);
    fill->type = t;
    fill->b = reserve ? -1 : source.id;
    fill->dst = out.id;
    fill->a = count.id;
    return out;
}

Type *repeat_type(Program *p, Expr *e, Type *elem)
{
    if (!elem || strcmp(e->text, "repeat")) {
        return NULL;
    }
    if (e->repeat_count == -1) {
        fail(e->loc, "repeat^max^ requires a fixed-length array type");
    }
    return e->repeat_count && !elem->owned ? type_wrap(p, T_ARRAY, elem, e->repeat_count)
                                           : type_wrap(p, T_GROW, elem, ARRAY_SIZED);
}

Type *array_form_hint(Program *p, Expr *e, Type *t, int index)
{
    if (!strcmp(e->text, "copy")) {
        return NULL;
    }
    if (!strcmp(e->text, "repeat") && !index) {
        return t ? t->elem : NULL;
    }
    return type_builtin(p, T_I64);
}

// Explicit ref^ attributes borrow stable places, leaving existing references unchanged.
static Value array_element(Context *c, Expr *array, Expr *element, Type *expected)
{
    if (array->array_refs) {
        Type *actual = expression_type(c, element, NULL);
        if (actual && actual->kind != T_REF && actual->kind != T_DYN) {
            Expr borrow = {.kind = E_UNARY, .loc = element->loc, .text = "ref^", .a = element};
            return expression(c, &borrow, expected);
        }
    }
    return expression(c, element, expected);
}

void empty_array_braces(Expr *e, Type *t)
{
    if (e->kind == E_RECORD && !e->text && !e->type_expr && !e->args.n && !e->tuple && t &&
        (t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_BOUNDED)) {
        e->kind = E_ARRAY;
    }
}

static bool zero_default(Type *t)
{
    if (t->kind == T_ARRAY) {
        return zero_default(t->elem);
    }
    return kind_integer(t->kind) || kind_float(t->kind) || t->kind == T_BOOL || t->kind == T_CHAR ||
           t->kind == T_NIL || t->kind == T_ENUM;
}

Value construct(Context *c, Expr *e, Type *t, int dst)
{
    if (e->kind == E_DEFAULT) {
        if (t->kind == T_STRUCT || t->kind == T_ARRAY || t->kind == T_GROW ||
            t->kind == T_BOUNDED) {
            Expr empty = {.kind = t->kind == T_STRUCT ? E_RECORD : E_ARRAY,
                          .loc = e->loc,
                          .text = t->kind == T_STRUCT ? t->def->name : NULL};
            return construct(c, &empty, t, dst);
        }
        if (t->refs) {
            if (e->state_field) {
                fail(e->loc, "static variable '%s' needs an initializer", e->state_field->name);
            }
            fail(e->loc, "array element type '%s' needs an initializer", type_name(t));
        }
        if (!e->state_field && !kind_integer(t->kind) && !kind_float(t->kind) &&
            t->kind != T_BOOL && t->kind != T_CHAR && t->kind != T_NIL && t->kind != T_ENUM) {
            fail(e->loc, "array element type '%s' needs an initializer", type_name(t));
        }
        Value out = {.id = dst >= 0 ? dst : slot(c, t, false), .type = t};
        Instr *i = emit(c, I_ZERO, e->loc);
        i->dst = out.id;
        i->type = t;
        return out;
    }
    if (e->kind == E_LOOP_PART) {
        return construct(c, loop_part(c->p, e, expression_type(c, e->a, NULL)), t, dst);
    }
    empty_array_braces(e, t);
    if (e->constant_owner) {
        Expr copy = *e;
        copy.constant_owner = NULL;
        Def *access = c->access;
        bool constant = c->type_constant;
        c->access = e->constant_owner;
        c->type_constant = true;
        Value result = construct(c, &copy, t, dst);
        c->access = access;
        c->type_constant = constant;
        return result;
    }
    if (e->invalid) {
        tool_abort();
    }
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
        if (t && !range_type(t)) {
            fail(e->loc, "a range is 'range^', expected '%s'", type_name(t));
        }
        Type *natural = expression_type(c, e, t), *bound = range_bound(c->p, natural);
        Type *range = t ? t : natural;
        Value out = {.id = dst >= 0 ? dst : slot(c, range, false), .type = range};
        Value ends[2];
        for (int j = 0; j < 2; j++) {
            Expr *end = j ? e->b : e->a;
            ends[j] = end ? deref(c, expression(c, end, bound), e->loc)
                          : constant(c, T_I64, j ? RANGE_OPEN : "0", e->loc);
            if (c->terminated) {
                return abrupt(c);
            }
            if (end && ends[j].type != bound) {
                fail(end->loc, "range bounds must both be %s, found '%s'", type_name(bound),
                     type_name(ends[j].type));
            }
        }
        if (!strcmp(e->text, "to^")) {
            int bad = ++c->in->labels, good = ++c->in->labels;
            nil_branch(c,
                       operate(c, "=", ends[1],
                               constant(c, bound->kind, integer_max(bound->kind), e->loc), e->loc),
                       bad, good, e->loc);
            nil_label(c, bad, e->loc);
            Expr text = {.kind = E_STRING,
                         .loc = e->loc,
                         .text = "the end of a to^ range overflows",
                         .length = strlen("the end of a to^ range overflows")};
            terminate(c, expression(c, &text, type_builtin(c->p, T_STRING)), true, e->loc);
            c->terminated = false;
            nil_label(c, good, e->loc);
            ends[1] = range_widen(c, ends[1], e->loc);
            ends[1] = operate(c, "+", ends[1], constant(c, T_I64, "1", e->loc), e->loc);
        }
        for (int j = 0; j < 2; j++) {
            copy_to(c, field_value(c, out, range->def->fields.v[j], e->loc),
                    range_widen(c, ends[j], e->loc), e->loc, true);
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
        for (int j = 0; j < out.deps.n; j++) {
            if (out.deps.v[j]->depth > depth) {
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
        out.deps = roots_union(a.deps, b.deps);
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
        if ((c->type_constant || c->in->fn->constant_value) && t->owned) {
            fail(e->loc, "type constant cannot own a data stack region");
        }
        if ((c->type_constant || c->in->fn->constant_value) && t->def->disposable) {
            fail(e->loc, "type constant cannot be Disposable");
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
        int inflight = c->inflight.n, pending = c->pending_disposals.n;
        add(&c->inflight, &out);
        if (!t->def->fields.n) {
            Instr *z = emit(c, I_ZERO, e->loc);
            z->dst = out.id;
            z->type = t;
        }
        for (int j = 0; j < e->names.n; j++) {
            char *name = e->names.v[j];
            Field *f = field(t->def, name);
            if (!f && t->def->group && !strcmp(name, "message")) {
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
            if (t->def->group && !f->index) {
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
                     f->type->kind == T_SLICE || f->type->kind == T_UNION) {
                fail(e->loc, "field '%s' needs an initializer", f->name);
            }
            else if (f->type->kind == T_STRUCT) {
                Expr empty = {.kind = E_RECORD, .loc = e->loc, .text = f->type->def->name};
                v = construct(c, &empty, f->type, dest.id);
            }
            else if (f->type->kind == T_ARRAY || f->type->kind == T_GROW ||
                     f->type->kind == T_BOUNDED) {
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
                c->pending_disposals.n = pending;
                return abrupt(c);
            }
            dispose_pending(c, dest);
            out.deps = roots_union(out.deps, v.deps);
        }
        c->inflight.n = inflight;
        c->pending_disposals.n = pending;
        return out;
    }
    if (e->kind == E_ARRAY) {
        if (e->text) {
            Type *source = !strcmp(e->text, "copy") ? expression_type(c, e->args.v[0], NULL) : NULL;
            Type *named = array_literal_type(c->p, e, t, source);
            if (!named && !t) {
                named = repeat_type(c->p, e, expression_type(c, e->args.v[0], NULL));
            }
            if (t && named && t != named) {
                fail(e->loc, "'%s' cannot initialize '%s'", type_name(named), type_name(t));
            }
            return sized_array(c, e, named, dst);
        }
        if (t && t->kind == T_GROW && t->count == ARRAY_CAPPED) {
            fail(e->loc, "'%s' is built with {max^ capacity}", type_name(t));
        }
        Type *shape = array_literal_type(c->p, e, t, NULL);
        Value first = {0};
        if (!shape) {
            if (!e->args.n) {
                fail(e->loc, "empty array needs a type annotation");
            }
            first = array_element(c, e, e->args.v[0], NULL);
            if (dead(first)) {
                return first;
            }
            shape = array_literal_type(c->p, e, t, first.type);
        }
        if (t && t != shape) {
            fail(e->loc, "array literal of '%s' cannot initialize '%s'", type_name(shape),
                 type_name(t));
        }
        t = shape;
        layout(t, e->loc);
        if (t->kind != T_ARRAY && t->kind != T_GROW && t->kind != T_BOUNDED) {
            fail(e->loc, "array literal requires array type");
        }
        bool defaults = t->kind == T_ARRAY && !e->args.n;
        if (t->kind == T_ARRAY && !defaults && e->args.n != t->count) {
            fail(e->loc, "fixed array requires exactly %d elements", t->count);
        }
        if (t->kind == T_BOUNDED && e->args.n > t->count) {
            fail(e->loc, "'%s' holds at most %d elements", type_name(t), t->count);
        }
        Value out = {0};
        out.type = t;
        out.id = dst >= 0 ? dst : slot(c, t, false);
        if (defaults && zero_default(t)) {
            Instr *zero = emit(c, I_ZERO, e->loc);
            zero->dst = out.id;
            zero->type = t;
            return out;
        }
        if (t->kind == T_GROW) {
            layout(t->elem, e->loc);
            if (t->elem->owned || !t->elem->size) {
                fail(e->loc, "owning array elements must be nonempty fixed-size values");
            }
            Instr *a = emit(c, I_REGION, e->loc);
            a->dst = out.id;
            a->type = t;
        }
        if (owning_rows(t)) {
            Instr *region = emit(c, I_REGION, e->loc);
            region->dst = out.id;
            region->type = t;
        }
        else if (t->kind == T_BOUNDED) {
            // Only the length is set; elements past it are never read.
            layout(t, e->loc);
            Instr *a = emit(c, I_CLEAR, e->loc);
            a->a = out.id;
            a->type = t;
        }
        int inflight = c->inflight.n, pending = c->pending_disposals.n;
        add(&c->inflight, &out);
        if (t->kind == T_GROW || t->kind == T_BOUNDED || owning_rows(t)) {
            // Its length covers what has been constructed so far.
            dispose_pending(c, out);
        }
        int count = defaults ? t->count : e->args.n;
        for (int j = 0; j < count; j++) {
            Expr initial = {.kind = E_DEFAULT, .loc = e->loc};
            Expr *element = defaults ? &initial : e->args.v[j];
            if (owning_rows(t)) {
                Value row = push_owned_row(c, out, element, e->loc);
                if (c->terminated) {
                    c->inflight.n = inflight;
                    c->pending_disposals.n = pending;
                    return abrupt(c);
                }
                out.deps = roots_union(out.deps, row.deps);
            }
            else if (t->kind == T_GROW || t->kind == T_BOUNDED) {
                Value v = j || !first.type ? array_element(c, e, e->args.v[j], t->elem) : first;
                if (dead(v)) {
                    c->inflight.n = inflight;
                    c->pending_disposals.n = pending;
                    return v;
                }
                if (v.type != t->elem && read_reference(t->elem) && sum_accepts(t->elem, v.type)) {
                    v = convert_value(c, v, t->elem, -1, e->loc);
                }
                if (v.type != t->elem) {
                    fail(e->loc, "array element type mismatch");
                }
                disposable_check(c, v, e->loc);
                out.deps = roots_union(out.deps, v.deps);
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
                Value v;
                if ((!j && first.type) || (e->array_refs && !defaults)) {
                    Value source =
                        !j && first.type ? first : array_element(c, e, e->args.v[j], t->elem);
                    Value target = {.id = d, .type = t->elem};
                    v = copy_to(c, target, source, e->loc, true);
                }
                else {
                    v = construct(c, element, t->elem, d);
                }
                if (c->terminated) {
                    c->inflight.n = inflight;
                    c->pending_disposals.n = pending;
                    return abrupt(c);
                }
                dispose_pending(c, (Value){.id = d, .type = t->elem});
                out.deps = roots_union(out.deps, v.deps);
            }
        }
        c->inflight.n = inflight;
        c->pending_disposals.n = pending;
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
    if (f && f->environment) {
        if (pointer) {
            fail(loc, "capturing functions cannot have a C address");
        }
        for (int j = 0; j < f->captures.n; j++) {
            Param *capture = f->captures.v[j];
            Value ref;
            if (c->in->fn == f) {
                ref = variable(lookup(c, capture->name));
            }
            else {
                Value source = expression(c, capture->capture_source, NULL);
                if (!source.addressable) {
                    fail(loc, "capture '%s' requires a local binding", capture->capture->name);
                }
                ref = value(c, capture->inferred);
                Instr *address = emit(c, I_ADDR, loc);
                address->dst = ref.id;
                address->a = source.id;
                address->type = ref.type;
                ref.deps = roots_union(source.storage, source.deps);
            }
            out.deps = roots_union(out.deps, lifetime_roots(c, ref.deps));
            int part = slot(c, ref.type, true);
            Instr *field = emit(c, I_FIELD, loc);
            field->dst = part;
            field->a = out.id;
            field->type = f->environment;
            field->label = j;
            Instr *copy = emit(c, I_COPY, loc);
            copy->dst = part;
            copy->a = ref.id;
            copy->type = ref.type;
        }
        return out;
    }
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
    Value ref = value(c, type_wrap(c->p, T_REF, receiver.type, receiver.readonly));
    Instr *address = emit(c, I_ADDR, loc);
    address->dst = ref.id;
    address->a = receiver.id;
    address->type = ref.type;
    ref.deps = roots_union(receiver.storage, receiver.deps);
    if (!receiver.addressable) {
        ref.deps = roots_union(ref.deps, root(c, origin, c->depth + 1, false));
    }
    Expr target = {.kind = E_FUNCTION, .loc = loc, .function = f};
    Expr invoke = {.kind = E_CALL, .loc = loc, .a = &target};
    for (int j = 0; j < f->params.n; j++) {
        Param *param = f->params.v[j];
        Value arg = param->self ? ref : f->right_receiver ? a : b;
        Type *adapted =
            param->self ? arg.type : operator_argument(c->p, parameter_type(c->p, param), arg.type);
        if (adapted != arg.type) {
            Value borrowed = value(c, adapted);
            Instr *i = emit(c, I_ADDR, loc);
            i->dst = borrowed.id;
            i->a = arg.id;
            i->type = adapted;
            borrowed.deps = roots_union(arg.storage, arg.deps);
            if (!arg.addressable || (arg.var && !arg.var->mut)) {
                borrowed.deps = roots_union(borrowed.deps, root(c, origin, c->depth + 1, false));
            }
            arg = borrowed;
        }
        add(&invoke.args, saved_value(arg, loc));
    }
    int destination = c->operator_target == origin ? c->operator_destination : -1;
    Value result = call(c, &invoke, NULL, destination);
    operator_contract(f, result.type, loc);
    return result;
}

// A range index borrows a view: [lo .. hi] excludes hi, [lo to^ hi] includes
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
             "string^ has no []; slice it with s.slice(a .. b), or its bytes with s.bytes[a .. b]");
    }
    bool inline_storage =
        base.type->kind == T_ARRAY || base.type->kind == T_GROW || base.type->kind == T_BOUNDED;
    if (!inline_storage && base.type->kind != T_SLICE) {
        fail(e->loc, "slicing requires an array or slice");
    }
    if (inline_storage && !base.addressable) {
        fail(e->loc, "slice source needs a binding; cannot borrow a temporary array");
    }
    int bounds[2] = {-1, -1};
    Expr *ends[2] = {e->b, e->c};
    bool ranged = e->kind == E_INDEX;
    if (ranged) {
        Value r = expression(c, e->b, type_range(p, NULL, e->loc));
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
        Value v =
            snapshot(c, deref(c, expression(c, ends[j], type_builtin(p, T_I64)), e->loc), e->loc);
        if (c->terminated) {
            return abrupt(c);
        }
        if (!range_element(v.type)) {
            fail(ends[j]->loc, "slice bounds must be integers that fit i64^");
        }
        bounds[j] = range_widen(c, v, e->loc).id;
    }
    // A slice of what cannot be written is read^; a read^ destination converts a writable one.
    bool read = base.readonly || read_reference(base.type);
    if (inline_storage) {
        read |= base.var && !base.var->mut;
        // Other storage of a let^, reached through a reference, is checked when it is written.
        for (int j = 0; j < base.storage.n; j++) {
            read |= base.storage.v[j]->readonly && base.storage.v[j]->external;
        }
    }
    Type *t = type_wrap(p, T_SLICE, base.type->elem, read);
    if (expected && !sum_accepts(expected, t)) {
        fail(e->loc,
             read && expected->kind == T_SLICE
                 ? "slice is '%s', expected '%s'; its source is read-only, so var^ it or take read^"
                 : "slice is '%s', expected '%s'",
             type_name(t), type_name(expected));
    }
    Value out = {.id = slot(c, t, false),
                 .type = t,
                 .deps = inline_storage ? roots_union(base.deps, base.storage) : base.deps};
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
    if (!expected) {
        expected = expression_type(c, e, NULL);
        if (!expected) {
            fail(e->loc, "cannot infer heterogeneous argument types");
        }
    }
    if (argument_pack(expected) || (e->a && argument_pack(expression_type(c, e->a, NULL)))) {
        Value source = {0};
        List args = e->args;
        if (e->a) {
            source = snapshot(c, deref(c, expression(c, e->a, NULL), e->loc), e->loc);
            if (dead(source)) {
                return source;
            }
            if (source.type == expected) {
                return source;
            }
            args = (List){0};
            int count = inline_length(source.type, e->loc);
            if (count > 4096) {
                fail(e->loc, "heterogeneous argument pack exceeds 4096 elements");
            }
            for (int j = 0; j < count; j++) {
                Value item;
                if (argument_pack(source.type)) {
                    item = field_value(c, source, source.type->def->fields.v[j], e->loc);
                }
                else {
                    Expr base = {.kind = E_SAVED, .loc = e->loc, .compiler_value = &source};
                    Expr index = {.kind = E_INT, .loc = e->loc, .text = fmt("%d", j)};
                    Expr at = {
                        .kind = E_INDEX, .loc = e->loc, .a = &base, .b = &index, .index_count = -1};
                    item = expression(c, &at, NULL);
                }
                add(&args, saved_value(item, e->loc));
            }
        }
        if (!argument_pack(expected)) {
            Expr packed = {.kind = E_PACK, .loc = e->loc, .args = args};
            return pack_value(c, &packed, expected);
        }
        Value out = value(c, expected);
        Instr *zero = emit(c, I_ZERO, e->loc);
        zero->dst = out.id;
        zero->type = expected;
        for (int j = 0; j < args.n; j++) {
            Field *field = expected->def->fields.v[j];
            Value dest = field_value(c, out, field, e->loc);
            Value item = construct(c, args.v[j], field->type, dest.id);
            if (dead(item)) {
                return item;
            }
            out.deps = roots_union(out.deps, item.deps);
        }
        return out;
    }
    if (e->a) {
        Expr view = {.kind = E_SLICE, .loc = e->a->loc, .a = e->a, .text = ".."};
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
                 .deps = roots_union(items.deps, root(c, e, c->depth + 1, false))};
    Instr *i = emit(c, I_SLICE, e->loc);
    i->dst = out.id;
    i->a = items.id;
    i->b = bounds;
    i->other = bounds;
    i->type = array;
    return out;
}

// Suspend at a new resume point. A frame dropped while suspended there enters at its dispose
// point instead, which disposes what is live and finishes the frame.
static void suspend(Context *c, Loc l)
{
    int point = ++c->in->yield_points;
    Instr *i = emit(c, I_YIELD, l);
    i->label = point;
    bool live = c->pending_disposals.n > 0;
    for (int j = 0; j < c->vars.n && !live; j++) {
        live = ((Var *)c->vars.v[j])->dispose;
    }
    if (live) {
        add(&c->in->dispose_points, (void *)(intptr_t)point);
        emit(c, I_DISPOSE_POINT, l)->label = point;
        dispose_all(c, 0, 0, l);
        emit(c, I_RETURN, l)->type = c->in->result;
    }
    emit(c, I_RESUME_POINT, l)->label = point;
}

// yield^ ends one activation of a coroutine: nothing of the body's own data stacks, nothing
// it is still building and no reference into its own locals may be left to the next resume.
static void suspension_check(Context *c, Loc l)
{
    if (c->pending_owners.n) {
        fail(l, "yield^ cannot suspend while an owned value is being built");
    }
    for (int j = 0; j < c->vars.n; j++) {
        Var *v = c->vars.v[j];
        if (v->hidden || v->unavailable) {
            continue;
        }
        if (v->type->owned) {
            fail(l,
                 "'%s' owns data stack storage, which cannot live across yield^; end its "
                 "scope first or borrow storage owned by the caller",
                 v->name);
        }
        if (((Slot *)c->in->slots.v[v->slot])->alias) {
            fail(l, "reference binding '%s' cannot live across yield^ yet", v->name);
        }
        for (int k = 0; v->type->refs && k < v->deps.n; k++) {
            if (!v->deps.v[k]->external) {
                fail(l,
                     "'%s' refers to a local of this coroutine, which cannot be kept across "
                     "yield^",
                     v->name);
            }
            c->in->keeps_borrow |= !v->parameter;
        }
    }
}

// yield^ a: put a into the turn this resume answers and suspend at a new resume point. Where
// its value is used, it is what the next resume hands in.
static Value yield_point(Context *c, Expr *e)
{
    suspension_check(c, e->loc);
    Expr none = {.kind = E_NAME, .text = "nil^", .loc = e->loc};
    Value v = construct(c, e->a ? e->a : &none, c->in->result, c->in->result_slot);
    if (c->terminated) {
        return abrupt(c);
    }
    if (v.type->owned) {
        fail(e->loc, "yield^ cannot hand out an owned value");
    }
    suspend(c, e->loc);
    Type *r = c->in->received;
    if (!r) {
        return (Value){.id = -1, .type = type_builtin(c->p, T_VOID)};
    }
    Value out = value(c, r);
    Instr *copy = emit(c, I_COPY, e->loc);
    copy->dst = out.id;
    copy->a = c->in->received_slot;
    copy->type = r;
    return out;
}

// A fresh reference to the frame held at holder: its address if holder is the frame, else the
// reference holder already is. Taken anew after each suspension, so no pointer into this
// coroutine's own frame is kept across one.
static int frame_reference(Context *c, Value holder, Loc l)
{
    if (holder.type->kind == T_REF) {
        return holder.id;
    }
    Value ref = value(c, type_wrap(c->p, T_REF, holder.type, 0));
    Instr *address = emit(c, I_ADDR, l);
    address->dst = ref.id;
    address->a = holder.id;
    address->type = ref.type;
    return ref.id;
}

// await^ a: run frame a to its end through this coroutine. Each of its suspensions suspends
// this one with what it yielded, and each resume of this one resumes it with what was handed
// in; its result is the value.
static Value await_point(Context *c, Expr *e)
{
    Value holder = expression(c, e->a, NULL);
    if (dead(holder) || c->terminated) {
        return c->terminated ? abrupt(c) : holder;
    }
    Type *frame = holder.type->kind == T_REF ? holder.type->elem : holder.type;
    if (frame->kind != T_FRAME) {
        fail(e->loc, "await^ needs a coroutine frame, found '%s'", type_name(holder.type));
    }
    Instance *co = frame->coroutine;
    if (co->fn->procedure && !c->in->fn->procedure) {
        fail(e->loc, "f^ cannot advance a p^ coroutine; await^ runs its body");
    }
    Value place = holder.type->kind == T_REF ? deref(c, holder, e->loc) : holder;
    store_check(c, place, (Value){0}, e->loc);
    frame_reach_check(c, co, place.deps, e->loc);
    // A frame borrowing this coroutine's locals is lent them again at each resume, at wherever
    // this frame has moved; it may keep only the parameters themselves across its suspensions.
    bool lends = false;
    for (int k = 0; k < place.deps.n; k++) {
        lends |= !place.deps.v[k]->external;
        c->in->keeps_borrow |= place.deps.v[k]->external;
    }
    for (int k = 0; lends && k < co->params.n; k++) {
        Type *param = co->params.v[k];
        if (co->keeps_borrow || (param->refs && param->kind != T_REF && param->kind != T_SLICE)) {
            fail(e->loc,
                 "await^ of a coroutine that refers to a local of this coroutine needs it to "
                 "keep only its ref^ and slice parameters across suspensions");
        }
    }
    c->in->relends |= lends;
    int disposals = c->pending_disposals.n;
    if (holder.type->kind != T_REF && !holder.var) {
        dispose_pending(c, holder);
    }
    Value turn = value(c, co->turn);
    int ref = frame_reference(c, holder, e->loc);
    Instr *start = emit(c, I_RESUME, e->loc);
    start->dst = turn.id;
    start->a = ref;
    start->type = co->turn;
    start->callee = co;
    start->label = 2;
    int top = ++c->in->labels, end = ++c->in->labels, more = ++c->in->labels;
    nil_jump(c, top, e->loc);
    nil_label(c, top, e->loc);
    Value done = value(c, type_builtin(c->p, T_BOOL));
    ref = frame_reference(c, holder, e->loc);
    Instr *test = emit(c, I_FRAME_DONE, e->loc);
    test->dst = done.id;
    test->a = ref;
    test->type = done.type;
    test->callee = co;
    nil_branch(c, done, end, more, e->loc);
    nil_label(c, more, e->loc);
    suspension_check(c, e->loc);
    convert_value(c, turn_part(c, turn, co->yielded, -1, e->loc), c->in->result, c->in->result_slot,
                  e->loc);
    suspend(c, e->loc);
    ref = frame_reference(c, holder, e->loc);
    if (lends) {
        Instr *rebase = emit(c, I_REBASE, e->loc);
        rebase->a = ref;
        rebase->callee = co;
    }
    Instr *again = emit(c, I_RESUME, e->loc);
    again->dst = turn.id;
    again->a = ref;
    again->type = co->turn;
    again->callee = co;
    again->label = 1;
    if (co->received) {
        add(&again->args, (void *)(intptr_t)(c->in->received_slot + 1));
    }
    nil_jump(c, top, e->loc);
    nil_label(c, end, e->loc);
    c->pending_disposals.n = disposals;
    Value out = turn_part(c, turn, co->final, slot(c, co->final, false), e->loc);
    // Its references come from what the frame borrows, if its returns take any from there.
    bool borrowed = co->returns.n || co->returns_unknown;
    out.deps = co->final->refs && borrowed ? place.deps : (Roots){0};
    return out;
}

Value expression(Context *c, Expr *e, Type *expected)
{
    settle_overload(c->p, e);
    if (e->kind == E_STATE) {
        Field *f = e->state_field;
        c->in->uses_state = true;
        if (e->type_expr) {
            Type *type = type_spec(c->p, e->type_expr);
            Def *d = type->def->generic ? type->def->generic : type->def;
            tool_record(c->p, e->type_expr->loc, d->loc, d->name, type, NULL, d, 23, false);
        }
        if (c->type_constant || c->in->fn->constant_value) {
            fail(e->loc, "type constant cannot read a static variable");
        }
        Value out = {
            .type = state_field_type(c->p, f), .addressable = true, .storage = state_roots(c->p)};
        if (f->binding && !f->binding->mut) {
            Roots immutable = root(c, f, 0, true);
            immutable.v[0]->readonly = f->name;
            out.storage = roots_union(out.storage, immutable);
        }
        out.id = slot(c, out.type, true);
        out.deps = out.type->refs ? state_roots(c->p) : (Roots){0};
        Instr *i = emit(c, I_STATE_REF, e->loc);
        i->dst = out.id;
        i->label = f->index;
        i->type = out.type;
        tool_record(c->p, e->loc, f->loc, f->name, out.type, NULL, f->owner,
                    f->binding && !f->binding->mut ? 14 : 13, false);
        return out;
    }
    if (e->kind == E_LOOP_PART) {
        return expression(c, loop_part(c->p, e, expression_type(c, e->a, NULL)), expected);
    }
    if (e->constant_owner) {
        Expr copy = *e;
        copy.constant_owner = NULL;
        Def *access = c->access;
        bool constant = c->type_constant;
        c->access = e->constant_owner;
        c->type_constant = true;
        Value result = expression(c, &copy, expected);
        c->access = access;
        c->type_constant = constant;
        return result;
    }
    if (type_signature(e)) {
        char *text = type_name(context_type(c, e->a->type_expr));
        Value out = value(c, type_builtin(c->p, T_STRING));
        Instr *i = emit(c, I_STRING, e->loc);
        i->dst = out.id;
        i->type = out.type;
        i->text = text;
        i->length = strlen(text);
        return out;
    }
    if (type_comparison(e)) {
        Value out = value(c, type_builtin(c->p, T_BOOL));
        Instr *i = emit(c, I_CONST, e->loc);
        i->dst = out.id;
        i->type = out.type;
        i->text = compare_types(c, &c->flow, e) ? "1" : "0";
        return out;
    }
    if (e->kind == E_LAYOUT) {
        Type *type = context_type(c, e->type_expr);
        layout(type, e->loc);
        return constant(c, T_I64,
                        fmt("%zu", !strcmp(e->text, "sizeof^") ? type->size : type->align), e->loc);
    }
    if (e->kind == E_BYTESOF) {
        Value base = place_expression(c, e->a);
        while (!dead(base) && base.type->kind == T_REF) {
            base = deref(c, base, e->loc);
        }
        if (dead(base)) {
            return base;
        }
        Type *view = byte_view_type(c->p, base.type, e->loc);
        if (base.type->kind != T_SLICE && !base.addressable) {
            fail(e->loc, "bytesof^ source needs a binding; cannot borrow a temporary");
        }
        Value out = {.id = slot(c, view, false),
                     .type = view,
                     .deps = base.type->kind != T_SLICE ? roots_union(base.deps, base.storage)
                                                        : base.deps};
        Instr *i = emit(c, I_SLICE, e->loc);
        i->dst = out.id;
        i->a = base.id;
        i->b = -1;
        i->other = -1;
        i->text = "bytes";
        i->type = base.type;
        return out;
    }
    if (e->kind == E_TYPE) {
        fail(e->loc, "'%s' names a type, not a value",
             e->type_expr->name ? e->type_expr->name : "typeof^");
    }
    if (e->kind == E_STATIC) {
        fail(e->loc, "static member '%s' must be called", e->text);
    }
    if (e->invalid) {
        tool_abort();
    }
    if (c->terminated) {
        return abrupt(c);
    }
    if (e->spread) {
        fail(e->loc, "'...' forwards only into a variadic parameter");
    }
    if (e->kind == E_PACK) {
        return pack_value(c, e, expected);
    }
    if (e->kind == E_YIELD) {
        return yield_point(c, e);
    }
    if (e->kind == E_AWAIT) {
        return await_point(c, e);
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
        if (t->kind == T_ENUM) {
            Value member = value(c, t);
            Instr *i = emit(c, I_CONST, e->loc);
            i->dst = member.id;
            i->type = t;
            i->text = e->text;
            if (expected && enum_backs(expected, t)) {
                return enum_integer(c, member, e->loc);
            }
            if (expected && expected != t) {
                fail(e->loc, "constant type mismatch");
            }
            return member;
        }
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
        Type *type = closure_type(c, e->function);
        if (!type) {
            fail(e->loc, "cannot infer lexical capture types");
        }
        return callable_value(c, type, expected, e->loc);
    }
    if (e->kind == E_CAPTURE) {
        Var *v = captured_variable(c, e);
        Param *capture = c->in->fn->captures.v[e->capture_index];
        Value captured = deref(c, variable(v), e->loc);
        tool_record(p, e->loc, capture->capture->loc, e->text, captured.type, NULL, NULL,
                    capture->capture->mut ? 13 : 14, false);
        return captured;
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
            fail(e->loc, "unknown value '%s'", e->text);
        }
        if (v->unavailable) {
            fail(e->loc, "first^ binding '%s' is not definitely initialized here", v->name);
        }
        if (!e->untracked) {
            tool_record(p, e->loc, v->declaration, v->name,
                        v->narrowed && !c->raw_name ? v->narrowed : v->type, NULL, NULL,
                        v->parameter ? TOOL_PARAMETER
                        : v->mut     ? 13
                                     : 14,
                        false);
        }
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
        if (argument_pack(base.type)) {
            if (strcmp(e->text, "len")) {
                fail(e->loc,
                     "argument packs only support .len, constant indexing and inline^ for^");
            }
            return constant(c, T_I64, fmt("%d", base.type->def->fields.n), e->loc);
        }
        if ((base.type->kind == T_ARRAY || base.type->kind == T_GROW ||
             base.type->kind == T_BOUNDED || base.type->kind == T_STRING ||
             base.type->kind == T_SLICE) &&
            !strcmp(e->text, "len")) {
            out = value(c, type_builtin(p, T_I64));
            Instr *i = emit(c, I_LENGTH, e->loc);
            i->dst = out.id;
            i->a = base.id;
            i->type = base.type;
            return out;
        }
        if (range_type(base.type)) {
            return range_member(c, base, e->text, e->loc);
        }
        if (kind_float(base.type->kind) && !strcmp(e->text, "bits")) {
            return bit_cast(c, base, bits_kind(base.type->kind), e->loc, -1);
        }
        if (base.type->kind == T_STRING) {
            // s.bytes views the same bytes; string^ itself has no len.
            if (strcmp(e->text, "bytes")) {
                fail(e->loc,
                     "string^ has no %s; use s.bytes for bytes, or s.decode(at), s.slice(r) and "
                     "s.count_chars()",
                     e->text);
            }
            Value bytes = {.id = slot(c, type_wrap(p, T_SLICE, type_builtin(p, T_U8), 1), false),
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
        temporary_check(base, e->loc);
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
            fail(e->name_loc.file ? e->name_loc : e->loc, "'%s' has no field '%s'",
                 base.type->def->name, e->text);
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
        temporary_check(base, e->loc);
        if (argument_pack(base.type)) {
            int index = argument_index(&c->vars, base.type, e->b, e->loc);
            return field_value(c, base, base.type->def->fields.v[index], e->loc);
        }
        if (base.type->kind == T_STRING) {
            fail(e->loc,
                 "string^ has no []; read bytes with s.bytes[i], or characters with s.decode(at)");
        }
        if (base.type->kind != T_ARRAY && base.type->kind != T_GROW &&
            base.type->kind != T_BOUNDED && base.type->kind != T_SLICE) {
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
        out.deps = !out.type->refs ? (Roots){0}
                   : view          ? base.deps
                                   : roots_union(base.deps, lifetime_roots(c, base.storage));
        // A slice is a handle: what its element allows does not depend on how it is bound.
        out.var = view ? NULL : base.var;
        out.addressable = view || base.addressable;
        out.readonly = base.readonly || read_reference(base.type);
        out.guarded_copy = base.guarded_copy;
        Instr *i = emit(c, I_INDEX, e->loc);
        i->dst = out.id;
        i->a = base.id;
        i->b = index.id;
        i->type = base.type;
        if (out.type->owned && c->raw_place != e) {
            bool read = out.readonly || (base.var && !base.var->mut);
            Value ref = value(c, type_wrap(p, T_REF, out.type, read));
            Instr *addr = emit(c, I_ADDR, e->loc);
            addr->a = out.id;
            addr->dst = ref.id;
            addr->type = ref.type;
            ref.deps = roots_union(out.storage, out.deps);
            ref.target = out.storage;
            ref.contents = out.deps;
            return ref;
        }
        return out;
    }
    if (e->kind == E_UNARY) {
        if (!strcmp(e->text, "-") && e->a->kind == E_INT) {
            Expr neg = *e->a;
            neg.text = fmt("-%s", neg.text);
            if (neg.constant_spec) {
                Type *t = type_spec(c->p, neg.constant_spec);
                if (expected && expected != t) {
                    fail(e->loc, "constant type mismatch");
                }
                expected = t;
            }
            return literal_int(c, &neg, expected);
        }
        bool borrow = !strcmp(e->text, "ref^") || !strcmp(e->text, "read^");
        Value a = borrow
                      ? place_expression(c, e->a)
                      : expression(c, e->a,
                                   (!strcmp(e->text, "-") || !strcmp(e->text, "bitnot^")) ? expected
                                                                                          : NULL);
        if (dead(a)) {
            return a;
        }
        if (borrow) {
            if (argument_pack(a.type)) {
                fail(e->loc, "cannot take a reference to an argument pack");
            }
            if (!a.addressable) {
                fail(e->loc, "cannot borrow a temporary; bind it first");
            }
            bool reading = !strcmp(e->text, "read^") || (expected && read_reference(expected));
            if (a.var && !a.var->mut && !e->top_binding && !reading) {
                fail(e->loc, "borrowing storage requires var^, found let^ '%s'", a.var->name);
            }
            for (int n = 0; n < a.storage.n && !e->top_binding; n++) {
                if (a.storage.v[n]->readonly && !reading) {
                    fail(e->loc, "borrowing storage requires var^, found let^ '%s'",
                         a.storage.v[n]->readonly);
                }
            }
            out = value(c, type_wrap(p, T_REF, a.type, a.readonly || !strcmp(e->text, "read^")));
            out.deps = roots_union(a.storage, a.deps);
            out.target = a.storage;
            out.contents = a.deps;
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
            !e->a->constant_spec &&
            (e->a->kind == E_INT || e->a->kind == E_FLOAT ||
             (e->a->kind == E_UNARY && !strcmp(e->a->text, "-") && e->a->a->kind == E_INT));
        Type *right_type = !logic && literal_left ? expression_type(c, e->b, NULL) : NULL;
        if (expected && (kind_integer(expected->kind) || kind_float(expected->kind)) &&
            right_type && (kind_integer(right_type->kind) || kind_float(right_type->kind))) {
            right_type = NULL;
        }
        Type *left_hint = right_type ? operator_hint(p, e->text, right_type, true, e->a) : NULL;
        // 4 + n takes n's numeric type, as n + 4 does; 40 + 2 keeps the expected one.
        bool literal_right =
            !e->b->constant_spec && (e->b->kind == E_INT || e->b->kind == E_FLOAT ||
                                     (e->b->kind == E_UNARY && !strcmp(e->b->text, "-") &&
                                      (e->b->a->kind == E_INT || e->b->a->kind == E_FLOAT)));
        if (!left_hint && right_type && !shift && !literal_right &&
            ((e->a->kind == E_FLOAT && kind_float(right_type->kind)) ||
             (e->a->kind != E_FLOAT && kind_integer(right_type->kind)))) {
            left_hint = right_type;
        }
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
                                   operand_hint ? operand_hint
                                   : shift || nil_rhs || a.type->kind == T_STRUCT ||
                                           a.type->kind == T_REF ||
                                           (comparison && a.type->kind == T_UNION)
                                       ? NULL
                                       : a.type);
        if (dead(b)) {
            return b;
        }
        if (enum_backs(b.type, a.type)) {
            a = enum_integer(c, a, e->loc);
        }
        else if (enum_backs(a.type, b.type)) {
            b = enum_integer(c, b, e->loc);
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
        bool equality = !strcmp(e->text, "=") || !strcmp(e->text, "!=");
        if (equality && a.type->kind == T_UNION && b.type->kind != T_UNION &&
            union_alternative(a.type, b.type) >= 0) {
            return union_equal(c, e, a, b);
        }
        if (equality && b.type->kind == T_UNION && a.type->kind != T_UNION &&
            union_alternative(b.type, a.type) >= 0) {
            return union_equal(c, e, b, a);
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
            !(cmp && (a.type->kind == T_BOOL || a.type->kind == T_ENUM) &&
              strchr("=!", e->text[0])) &&
            !(cmp && a.type->kind == T_CHAR)) {
            fail(e->loc, "operator '%s' is not defined for '%s'", e->text, type_name(a.type));
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
        if (!strcmp(e->text, "//") || !strcmp(e->text, "**") || !strcmp(e->text, "::") ||
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
