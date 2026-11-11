#include "sema.h"

// A reference that a function stores or returns: part of parameter from reaches part of parameter
// to, or the result when to is negative.
enum { FLOW_WHOLE, FLOW_TARGET, FLOW_HELD };

typedef struct {
    int from, from_part, to, to_part;
} Flow;

static bool flow_site(Context *c, Root *r, int *param, int *part)
{
    if (r == c->p->state_root) {
        *param = -2;
        *part = FLOW_WHOLE;
        return true;
    }
    for (int k = 0; k < c->in->params.n && k < 64; k++) {
        Roots roots = c->param_deps[k];
        for (int j = 0; j < roots.n; j++) {
            if (roots.v[j] == r) {
                *param = k;
                *part = !r->inner ? FLOW_WHOLE : FLOW_TARGET;
                if (j && roots.v[0]->inner == r) {
                    *part = FLOW_HELD;
                }
                return true;
            }
        }
    }
    return false;
}

static void flow_add(List *flows, Flow flow)
{
    for (int j = 0; j < flows->n; j++) {
        if (!memcmp(flows->v[j], &flow, sizeof(flow))) {
            return;
        }
    }
    Flow *saved = mem(sizeof(*saved));
    *saved = flow;
    add(flows, saved);
}

bool flow_store(Context *c, Root *owner, Root *src)
{
    if (owner == c->p->state_root && src == c->p->state_root) {
        return true;
    }
    Flow flow;
    if (!flow_site(c, owner, &flow.to, &flow.to_part) ||
        !flow_site(c, src, &flow.from, &flow.from_part)) {
        return false;
    }
    flow_add(&c->in->flows, flow);
    return true;
}

void read_return_check(Context *c, Spec *spec, Roots deps, Loc loc)
{
    if (!spec || !spec->read_from.n) {
        return;
    }
    for (int j = 0; j < deps.n; j++) {
        int from, part;
        bool matched = false;
        if (flow_site(c, deps.v[j], &from, &part) && from >= 0 && from < c->in->fn->params.n) {
            char *name = ((Param *)c->in->fn->params.v[from])->name;
            for (int k = 0; k < spec->read_from.n; k++) {
                matched |= !strcmp(name, spec->read_from.v[k]);
            }
        }
        if (!matched) {
            fail(loc, "returned reference has an origin not listed in read^(...)");
        }
    }
}

void flow_return(Context *c, Roots deps)
{
    read_return_check(c, c->in->fn->result, deps, c->in->fn->loc);
    for (int j = 0; j < deps.n; j++) {
        Flow flow = {.to = -1};
        if (flow_site(c, deps.v[j], &flow.from, &flow.from_part)) {
            flow_add(&c->in->returns, flow);
        }
        else {
            c->in->returns_unknown = true;
        }
    }
}

// Whether t, or anything its references lead to, can keep a reference into storage of type elem.
// Text and byte views count as able to: bytesof^ views any plain data.
static bool flow_reaches(Type *t, Type *elem, List *seen)
{
    if (!t || !t->refs) {
        return false;
    }
    for (int j = 0; j < seen->n; j++) {
        if (seen->v[j] == t) {
            return false;
        }
    }
    add(seen, t);
    if (t->kind == T_STRING || t->kind == T_DYN || (t->kind == T_SLICE && t->elem->kind == T_U8) ||
        ((t->kind == T_REF || t->kind == T_SLICE) && may_reference(t, elem)) ||
        flow_reaches(t->elem, elem, seen)) {
        return true;
    }
    List *parts = t->kind == T_UNION ? &t->def->alternatives : NULL;
    for (int j = 0; parts && j < parts->n; j++) {
        if (flow_reaches(parts->v[j], elem, seen)) {
            return true;
        }
    }
    for (int j = 0; t->kind == T_STRUCT && j < t->def->fields.n; j++) {
        if (flow_reaches(((Field *)t->def->fields.v[j])->type, elem, seen)) {
            return true;
        }
    }
    return false;
}

// Whether a store through an argument of type to could keep a reference from one of type from.
// Only an argument pointing at reference-free data can be told apart by its type.
static bool flow_possible(Type *from, Type *to)
{
    Type *holder = to->kind == T_REF || to->kind == T_SLICE ? to->elem : to;
    if (!holder->refs || !from->refs) {
        return false;
    }
    if ((from->kind != T_REF && from->kind != T_SLICE) || from->elem->refs) {
        return true;
    }
    List seen = {0};
    return flow_reaches(holder, from->elem, &seen);
}

static Roots flow_part(Roots target, Roots held, int part)
{
    return part == FLOW_TARGET ? target : part == FLOW_HELD ? held : roots_union(target, held);
}

static Roots call_part(Context *c, Roots *target, Roots *held, int index, int part)
{
    return index == -2 ? state_roots(c->p) : flow_part(target[index], held[index], part);
}

Roots flow_call(Context *c, Instance *in, List *values, Loc loc)
{
    Function *f = in->fn;
    Roots all = {0}, target[64], held[64];
    int count = values->n < 64 ? values->n : 64;
    for (int j = 0; j < values->n; j++) {
        Value *v = values->v[j];
        all = roots_union(all, v->deps);
        if (j < 64) {
            // A reference copied or merged since it was made no longer tells the two apart.
            bool told = v->target.n && roots_equal(roots_union(v->target, v->contents), v->deps);
            target[j] = told ? v->target : v->deps;
            held[j] = told ? v->contents : roots_inner(v->deps);
        }
    }
    if (f->external) {
        return all;
    }
    // A callee still being lowered has no summary yet; assume it stores all it can.
    bool known = in->state == 2 && !in->invalid;
    List assumed = {0};
    for (int to = 0; !known && to < count; to++) {
        Param *param = f->params.v[to];
        if ((f->procedure || (f->mutable_self && param->self)) &&
            type_writes(((Value *)values->v[to])->type)) {
            for (int from = 0; from < count; from++) {
                if (flow_possible(((Value *)values->v[from])->type,
                                  ((Value *)values->v[to])->type)) {
                    Flow flow = {from, FLOW_WHOLE, to, FLOW_WHOLE};
                    flow_add(&assumed, flow);
                }
            }
        }
    }
    List *flows = known ? &in->flows : &assumed;
    if (flows->n && values->n > 64) {
        fail(loc, "reference flow analysis supports calls with at most 64 parameters");
    }
    // Arguments may share storage, and a callee's stores may happen in any order, so what one
    // store adds is visible to every other part of the call until nothing more changes.
    for (bool changed = true; changed;) {
        changed = false;
        for (int j = 0; j < flows->n; j++) {
            Flow *flow = flows->v[j];
            Roots src = call_part(c, target, held, flow->from, flow->from_part);
            Roots dst = call_part(c, target, held, flow->to, flow->to_part);
            for (int y = 0; y < count; y++) {
                if (roots_meet(dst, roots_union(target[y], held[y]))) {
                    Roots grown = roots_union(held[y], src);
                    changed |= grown.n != held[y].n;
                    held[y] = grown;
                }
            }
        }
    }
    for (int j = 0; j < flows->n; j++) {
        Flow *flow = flows->v[j];
        store_check(c, (Value){.storage = call_part(c, target, held, flow->to, flow->to_part)},
                    (Value){.deps = call_part(c, target, held, flow->from, flow->from_part)}, loc);
    }
    Roots out = {0};
    if (!known || in->returns_unknown || values->n > 64) {
        out = roots_union(out, all);
        if (c->p->state_fields.n) {
            out = roots_union(out, state_roots(c->p));
        }
        for (int j = 0; j < count; j++) {
            out = roots_union(out, held[j]);
        }
        return out;
    }
    for (int j = 0; j < in->returns.n; j++) {
        Flow *flow = in->returns.v[j];
        out = roots_union(out, call_part(c, target, held, flow->from, flow->from_part));
    }
    return out;
}

bool read_reference(Type *t)
{
    return (t->kind == T_REF || t->kind == T_DYN || t->kind == T_SLICE) && t->count != 0;
}

static bool capability(Type *t, bool write, List *seen)
{
    if (!t || !t->refs) {
        return false;
    }
    if (t->kind == T_SLICE && read_reference(t)) {
        return !write;
    }
    if (write && t->kind == T_SLICE) {
        return true;
    }
    if (t->kind == T_REF || t->kind == T_DYN) {
        if (write) {
            return !read_reference(t);
        }
        if (read_reference(t)) {
            return true;
        }
        // The outer reference describes access to the holder itself.
        return false;
    }
    for (int j = 0; j < seen->n; j++) {
        if (seen->v[j] == t) {
            return false;
        }
    }
    add(seen, t);
    if (t->kind == T_FUNCTION && t->function && t->function->environment) {
        Function *f = t->function;
        for (int j = 0; j < f->captures.n; j++) {
            Param *capture = f->captures.v[j];
            if (write && f->procedure && capture->capture->mut) {
                return true;
            }
            if ((!write || f->procedure) && capability(capture->inferred->elem, write, seen)) {
                return true;
            }
        }
    }
    if (t->elem && capability(t->elem, write, seen)) {
        return true;
    }
    if (t->kind == T_STRUCT) {
        for (int j = 0; j < t->def->fields.n; j++) {
            if (capability(((Field *)t->def->fields.v[j])->type, write, seen)) {
                return true;
            }
        }
    }
    if (t->kind == T_UNION) {
        for (int j = 0; j < t->def->alternatives.n; j++) {
            if (capability(t->def->alternatives.v[j], write, seen)) {
                return true;
            }
        }
    }
    return false;
}

bool type_reads(Type *t)
{
    List seen = {0};
    return capability(t, false, &seen);
}

bool type_writes(Type *t)
{
    List seen = {0};
    return capability(t, true, &seen);
}

void mutation_check(Context *c, Roots storage, Loc loc)
{
    for (int j = 0; j < c->pending_disposals.n; j++) {
        Value *pending = c->pending_disposals.v[j];
        if (pending->row_reservation && roots_meet(storage, pending->storage)) {
            fail(loc, "cannot modify an array while constructing one of its owned rows");
        }
    }
}

void flow_validate(Program *p)
{
    bool state_changed;
    do {
        state_changed = false;
        for (int j = 0; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            for (int k = 0; !in->uses_state && k < in->code.n; k++) {
                Instr *i = in->code.v[k];
                if (i->op == I_CALL && i->callee->uses_state) {
                    in->uses_state = state_changed = true;
                }
                for (int n = 0; i->op == I_DYNCALL && n < p->dyn_tables.n; n++) {
                    DynTable *table = p->dyn_tables.v[n];
                    Instance *target =
                        i->label < table->entries.n ? table->entries.v[i->label] : NULL;
                    if (target && target->uses_state) {
                        in->uses_state = state_changed = true;
                    }
                }
            }
        }
    } while (state_changed);
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if ((in->thread_entry || in->fn->exported || in->fn->lhat_export) && in->uses_state &&
            !in->invalid) {
            fail(in->fn->loc,
                 "thread entry or exported function cannot access task-owned static variables");
        }
        if ((in->indirect || in->fn->exported) && in->flows.n && !in->invalid) {
            fail(in->fn->loc,
                 "'%s' stores a reference through an argument, so it cannot be called through "
                 "dyn^, a function value or exportC^",
                 in->fn->name);
        }
    }
}
