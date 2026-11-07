#include "sema.h"

// The boundary is carried alongside the actual roots so existing escape checks
// also reject a read reference stored outside its protecting lexical scope.
typedef struct {
    Root boundary;
    Roots targets;
    int scope;
    Expr *call;
    Loc loc;
} ReadBorrow;

// Only parameter relations cross a call boundary; no runtime borrow state is needed.
typedef struct {
    Instance *callee;
    Def *iface;
    int method, count;
    uint64_t origins[64], aliases[64];
    bool overflow;
    Loc loc;
} ReadCall;

// A reference that a function stores or returns: part of parameter from reaches part of parameter
// to, or the result when to is negative.
enum { FLOW_WHOLE, FLOW_TARGET, FLOW_HELD };

typedef struct {
    int from, from_part, to, to_part;
} Flow;

static bool flow_site(Context *c, Root *r, int *param, int *part)
{
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
    Flow flow;
    if (src->read_borrow || !flow_site(c, owner, &flow.to, &flow.to_part) ||
        !flow_site(c, src, &flow.from, &flow.from_part)) {
        return false;
    }
    flow_add(&c->in->flows, flow);
    return true;
}

void flow_return(Context *c, Roots deps)
{
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
// Text and byte views count as able to: x.bytes views any plain data.
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
            Roots src = flow_part(target[flow->from], held[flow->from], flow->from_part);
            Roots dst = flow_part(target[flow->to], held[flow->to], flow->to_part);
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
        store_check(
            c, (Value){.storage = flow_part(target[flow->to], held[flow->to], flow->to_part)},
            (Value){.deps = flow_part(target[flow->from], held[flow->from], flow->from_part)}, loc);
    }
    Roots out = {0};
    if (!known || in->returns_unknown || values->n > 64) {
        out = all;
        for (int j = 0; j < count; j++) {
            out = roots_union(out, held[j]);
        }
        return out;
    }
    for (int j = 0; j < in->returns.n; j++) {
        Flow *flow = in->returns.v[j];
        out = roots_union(out, flow_part(target[flow->from], held[flow->from], flow->from_part));
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
        // A writable pointer to a holder does not itself borrow the holder
        // read-only. Read loans in its stored contents are tracked separately.
        return false;
    }
    for (int j = 0; j < seen->n; j++) {
        if (seen->v[j] == t) {
            return false;
        }
    }
    add(seen, t);
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

Value read_borrow(Context *c, Value v, Loc loc)
{
    if (c->in->params.n > 64) {
        fail(loc, "read^ borrowing supports functions with at most 64 parameters");
    }
    if (!v.deps.n) {
        return v;
    }
    // Forwarding a loan preserves its original boundary, including at returns.
    Roots covered = {0};
    for (int j = 0; j < v.deps.n; j++) {
        if (v.deps.v[j]->read_borrow) {
            ReadBorrow *existing = (ReadBorrow *)v.deps.v[j];
            covered = roots_union(covered, existing->targets);
        }
    }
    bool inherited = covered.n != 0;
    for (int j = 0; j < v.deps.n; j++) {
        Roots one = {&v.deps.v[j], 1};
        if (!v.deps.v[j]->read_borrow && !roots_meet(covered, one)) {
            inherited = false;
        }
    }
    if (inherited) {
        return v;
    }
    ReadBorrow *loan = mem(sizeof(*loan));
    loan->boundary.read_borrow = true;
    loan->boundary.external = c->read_return;
    for (int j = 0; j < v.deps.n; j++) {
        loan->boundary.external &= v.deps.v[j]->external;
    }
    loan->scope = c->depth;
    loan->boundary.depth = loan->boundary.external ? 0 : c->depth;
    loan->targets = v.deps;
    loan->call = c->read_call;
    loan->loc = loc;
    add(&c->read_borrows, loan);
    Roots boundary = {mem(sizeof(Root *)), 1};
    boundary.v[0] = &loan->boundary;
    v.deps = roots_union(v.deps, boundary);
    return v;
}

void read_write_check(Context *c, Roots storage, Loc loc)
{
    for (int j = 0; j < c->read_borrows.n; j++) {
        ReadBorrow *loan = c->read_borrows.v[j];
        if (roots_meet(storage, loan->targets)) {
            fail(loc,
                 "cannot modify storage during a read^ borrow (started at %s:%d; "
                 "protected until its scope ends)",
                 loan->loc.file, loan->loc.line);
        }
        // Distinct reference parameters can still alias at a caller. Record
        // the forbidden pairs rather than assuming they refer to different data.
        for (int a = 0; a < c->in->params.n && a < 64; a++) {
            if (!roots_meet(loan->targets, c->param_deps[a])) {
                continue;
            }
            for (int b = 0; b < c->in->params.n && b < 64; b++) {
                if (roots_meet(storage, c->param_deps[b])) {
                    c->in->read_conflicts[a] |= UINT64_C(1) << b;
                }
            }
        }
    }
}

void read_call_end(Context *c, Expr *call, bool escapes)
{
    int keep = 0;
    for (int j = 0; j < c->read_borrows.n; j++) {
        ReadBorrow *loan = c->read_borrows.v[j];
        if (loan->call == call) {
            if (!escapes) {
                continue;
            }
            loan->call = NULL;
        }
        c->read_borrows.v[keep++] = loan;
    }
    c->read_borrows.n = keep;
}

void read_scope_end(Context *c, int depth)
{
    int keep = 0;
    for (int j = 0; j < c->read_borrows.n; j++) {
        ReadBorrow *loan = c->read_borrows.v[j];
        if (loan->scope <= depth) {
            c->read_borrows.v[keep++] = loan;
        }
    }
    c->read_borrows.n = keep;
}

static ReadCall *read_call_record(Context *c, List *values, Loc loc)
{
    bool references = false;
    for (int j = 0; j < values->n; j++) {
        references |= ((Value *)values->v[j])->deps.n != 0;
    }
    if (!references) {
        return NULL;
    }
    ReadCall *call = mem(sizeof(*call));
    call->count = values->n < 64 ? values->n : 64;
    call->overflow = c->in->params.n > 64 || values->n > 64;
    call->loc = loc;
    for (int a = 0; a < call->count; a++) {
        Value *arg = values->v[a];
        for (int b = 0; b < c->in->params.n && b < 64; b++) {
            if (roots_meet(arg->deps, c->param_deps[b])) {
                call->origins[a] |= UINT64_C(1) << b;
            }
        }
        for (int b = 0; b < call->count; b++) {
            if (roots_meet(arg->deps, ((Value *)values->v[b])->deps)) {
                call->aliases[a] |= UINT64_C(1) << b;
            }
        }
    }
    add(&c->in->read_calls, call);
    return call;
}

void read_call_check(Context *c, Instance *callee, List *values, Loc loc)
{
    ReadCall *call = read_call_record(c, values, loc);
    if (call) {
        call->callee = callee;
    }
}

void read_dyn_call_check(Context *c, Def *iface, int method, List *values, Loc loc)
{
    ReadCall *call = read_call_record(c, values, loc);
    if (call) {
        call->iface = iface;
        call->method = method;
    }
}

static bool propagate(Instance *caller, ReadCall *call, Instance *callee)
{
    if (callee->invalid) {
        return false;
    }
    bool changed = false;
    for (int a = 0; a < call->count; a++) {
        uint64_t conflicts = callee->read_conflicts[a];
        if (!conflicts) {
            continue;
        }
        if (conflicts && call->overflow) {
            fail(call->loc, "read^ conflict analysis supports calls with at most 64 parameters");
        }
        if (conflicts & call->aliases[a]) {
            fail(call->loc, "call aliases storage borrowed with read^ and storage it may modify");
        }
        for (int b = 0; b < call->count; b++) {
            if (!(conflicts & (UINT64_C(1) << b))) {
                continue;
            }
            for (int origin = 0; origin < 64; origin++) {
                if (call->origins[a] & (UINT64_C(1) << origin)) {
                    uint64_t before = caller->read_conflicts[origin];
                    caller->read_conflicts[origin] |= call->origins[b];
                    changed |= before != caller->read_conflicts[origin];
                }
            }
        }
    }
    return changed;
}

void read_validate(Program *p)
{
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if ((in->indirect || in->fn->exported) && in->flows.n && !in->invalid) {
            fail(in->fn->loc,
                 "'%s' stores a reference through an argument, so it cannot be called through "
                 "dyn^, a function value or exportC^",
                 in->fn->name);
        }
    }
    bool changed;
    do {
        changed = false;
        for (int j = 0; j < p->instances.n; j++) {
            Instance *caller = p->instances.v[j];
            if (caller->invalid) {
                continue;
            }
            for (int k = 0; k < caller->read_calls.n; k++) {
                ReadCall *call = caller->read_calls.v[k];
                if (call->callee) {
                    changed |= propagate(caller, call, call->callee);
                }
                else {
                    for (int n = 0; n < p->dyn_tables.n; n++) {
                        DynTable *table = p->dyn_tables.v[n];
                        if (table->iface == call->iface && call->method < table->entries.n &&
                            table->entries.v[call->method]) {
                            changed |= propagate(caller, call, table->entries.v[call->method]);
                        }
                    }
                }
            }
        }
    } while (changed);
}
