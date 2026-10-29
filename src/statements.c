#include "sema.h"
#include "tooling.h"

static void branch_to(Context *c, int target, Loc loc)
{
    Instr *i = emit(c, I_BR, loc);
    i->label = target;
}

static void label_at(Context *c, int label, Loc loc)
{
    Instr *i = emit(c, I_LABEL, loc);
    i->label = label;
}

static void conditional(Context *c, Value value, int yes, int no, Loc loc)
{
    Instr *i = emit(c, I_CBR, loc);
    i->a = value.id;
    i->label = yes;
    i->other = no;
}

static void remember_focus(Context *c, Loop *loop, Loc loc, bool restore)
{
    for (int j = 0; j < loop->focuses.n; j++) {
        Var *v = loop->focuses.v[j];
        Value *save = loop->saved.v[j];
        bool mut = v->mut;
        v->mut = true;
        if (restore) {
            copy_to(c, variable(v), *save, loc, false);
        }
        else {
            *save = copy_to(c, *save, variable(v), loc, true);
        }
        v->mut = mut;
    }
}

static void plain_loop(Context *c, Stmt *s)
{
    int test = ++c->in->labels, body = ++c->in->labels, end = ++c->in->labels;
    int update = s->update.n ? ++c->in->labels : test;
    Loop target = {
        .end = end, .update = update, .bindings = c->vars.n, .outside = c->vars.n, .phase = 1};
    add(&c->loops, &target);
    branch_to(c, test, s->loc);
    int code = c->in->code.n, slots = c->in->slots.n, labels = c->in->labels, roots = c->root_count;
    int bindings = c->vars.n;
    List entry_facts = save_facts(&c->vars);
    for (;;) {
        List turn_facts = save_facts(&c->vars);
        Type *before_fact[64];
        bool before_paths[64];
        for (int j = 0; j < bindings; j++) {
            before_fact[j] = ((Var *)c->vars.v[j])->narrowed;
            before_paths[j] = ((Var *)c->vars.v[j])->paths.n != 0;
        }
        uint64_t before[64];
        for (int j = 0; j < bindings; j++) {
            before[j] = ((Var *)c->vars.v[j])->deps;
        }
        label_at(c, test, s->loc);
        statements(c, &s->pre, false);
        Value cond = expression(c, s->a, type_builtin(c->p, T_BOOL));
        if (dead(cond)) {
            c->loops.n--;
            c->vars.n = bindings;
            return;
        }
        if (cond.type->kind != T_BOOL) {
            fail(s->loc, "condition requires bool");
        }
        conditional(c, cond, body, end, s->loc);
        label_at(c, body, s->loc);
        refine_condition(c->p, &c->vars, &c->flow, s->a, true);
        c->lexical--;
        bool live = condition_truth(&c->flow, s->a) >= 0;
        if (live) {
            statements(c, &s->body, true);
        }
        c->lexical++;
        if (s->update.n) {
            bool targeted = false;
            for (int j = code; j < c->in->code.n; j++) {
                Instr *edge = c->in->code.v[j];
                targeted |= (edge->op == I_BR && edge->label == update) ||
                            (edge->op == I_CBR && (edge->label == update || edge->other == update));
            }
            /* Keep an ordinary header update in the canonical straight-line loop. */
            if (targeted || c->terminated) {
                if (!c->terminated) {
                    branch_to(c, update, s->loc);
                }
                c->terminated = false;
                label_at(c, update, s->loc);
            }
            c->lexical--;
            if (live) {
                statements(c, &s->update, true);
            }
            c->lexical++;
        }
        if (!c->terminated) {
            branch_to(c, test, s->loc);
        }
        c->terminated = false;
        restore_facts(&c->vars, turn_facts);
        bool changed = false;
        for (int j = 0; j < bindings; j++) {
            changed |= before[j] != ((Var *)c->vars.v[j])->deps ||
                       (before_fact[j] && !((Var *)c->vars.v[j])->narrowed) ||
                       (before_paths[j] && !((Var *)c->vars.v[j])->paths.n);
        }
        if (!changed) {
            break;
        }
        c->in->code.n = code;
        c->in->slots.n = slots;
        c->in->labels = labels;
        c->root_count = roots;
    }
    c->loops.n--;
    label_at(c, end, s->loc);
    restore_facts(&c->vars, entry_facts);
}

static void loop_code(Context *c, Stmt *s)
{
    bool simple = !s->prolog.n && !s->first.n && !s->last.n && !s->epilog.n;
    for (int j = 0; j < s->pre.n; j++) {
        simple &= ((Stmt *)s->pre.v[j])->internal;
    }
    if (simple) {
        plain_loop(c, s);
        return;
    }
    int scope = c->vars.n;
    List entry_facts = save_facts(&c->vars);
    c->depth++;
    Loop opening = {.phase = -1};
    add(&c->loops, &opening);
    statements(c, &s->prolog, false);
    c->loops.n--;
    if (c->terminated) {
        c->vars.n = scope;
        c->depth--;
        restore_facts(&c->vars, entry_facts);
        return;
    }
    List first = first_types(c, s);
    for (int j = 0; j < first.n; j++) {
        Var *v = first.v[j];
        if (!v->type) {
            fail(s->loc, "cannot infer first^ binding '%s'", v->name);
        }
        layout(v->type, s->loc);
        v->depth = c->depth;
        v->lexical = c->lexical;
        v->root = root(c, c->depth, false, s->loc);
        v->slot = slot(c, v->type, false);
        v->unavailable = v->pending = true;
        if (v->type->owned) {
            Instr *i = emit(c, I_STACK_BEGIN, s->loc);
            i->dst = v->slot;
            i->type = v->type;
            i->text = v->name;
        }
        add(&c->vars, v);
    }
    Value initial = value(c, type_builtin(c->p, T_BOOL)),
          ran = value(c, type_builtin(c->p, T_BOOL));
    Instr *i = emit(c, I_CONST, s->loc);
    i->dst = initial.id;
    i->type = initial.type;
    i->text = "1";
    i = emit(c, I_CONST, s->loc);
    i->dst = ran.id;
    i->type = ran.type;
    i->text = "0";
    int test = ++c->in->labels, yes = ++c->in->labels, no = ++c->in->labels, once = ++c->in->labels;
    int main = ++c->in->labels, update = ++c->in->labels, finish = ++c->in->labels;
    int last = ++c->in->labels, epilog = ++c->in->labels;
    Loop target = {
        .end = finish, .update = update, .bindings = c->vars.n, .outside = scope, .first = first
    };
    for (int j = 0; j < s->focuses.n; j++) {
        Expr name = {.kind = E_NAME, .loc = s->loc, .text = s->focuses.v[j]};
        Var *v = selected(c, &name);
        if (v && !v->type->owned) {
            add(&target.focuses, v);
            Value *save = mem(sizeof(*save));
            *save = snapshot(c, variable(v), s->loc);
            add(&target.saved, save);
        }
    }
    add(&c->loops, &target);
    branch_to(c, test, s->loc);
    int code = c->in->code.n, slots = c->in->slots.n, labels = c->in->labels, roots = c->root_count;
    int persistent = c->vars.n;
    List loop_inputs = save_facts(&c->vars);
    for (;;) {
        List turn_facts = save_facts(&c->vars);
        Type *before_fact[64];
        bool before_paths[64];
        for (int j = 0; j < persistent; j++) {
            before_fact[j] = ((Var *)c->vars.v[j])->narrowed;
            before_paths[j] = ((Var *)c->vars.v[j])->paths.n != 0;
        }
        uint64_t before[64];
        for (int j = 0; j < persistent; j++) {
            before[j] = ((Var *)c->vars.v[j])->deps;
        }
        for (int j = 0; j < first.n; j++) {
            Var *v = first.v[j];
            v->unavailable = v->pending = true;
        }
        target.phase = 0;
        label_at(c, test, s->loc);
        c->depth++;
        statements(c, &s->pre, false);
        Value cond = {0};
        if (!c->terminated) {
            cond = expression(c, s->a, type_builtin(c->p, T_BOOL));
        }
        if (c->terminated) {
            /* Pre/test may transfer directly to this loop's update or epilog.
             * Such paths never construct first^ values or execute last^. */
            bool exits = false, updates = false;
            for (int j = code; j < c->in->code.n; j++) {
                Instr *edge = c->in->code.v[j];
                if (edge->op == I_BR || edge->op == I_CBR) {
                    exits |= edge->label == finish || (edge->op == I_CBR && edge->other == finish);
                    updates |=
                        edge->label == update || (edge->op == I_CBR && edge->other == update);
                }
            }
            c->vars.n = persistent;
            c->depth--;
            if (updates) {
                c->terminated = false;
                label_at(c, update, s->loc);
                c->lexical--;
                statements(c, &s->update, true);
                c->lexical++;
                if (!c->terminated) {
                    branch_to(c, test, s->loc);
                }
            }
            c->terminated = !exits;
            if (exits) {
                label_at(c, finish, s->loc);
                target.phase = 3;
                c->lexical--;
                statements(c, &s->epilog, true);
                c->lexical++;
                if (!c->terminated) {
                    cleanup(c, scope, s->loc);
                }
            }
            c->vars.n = scope;
            c->depth--;
            c->loops.n--;
            restore_facts(&c->vars, entry_facts);
            return;
        }
        if (cond.type->kind != T_BOOL) {
            fail(s->loc, "condition requires bool");
        }
        bool live = condition_truth(&c->flow, s->a) >= 0;
        conditional(c, cond, yes, no, s->loc);
        label_at(c, yes, s->loc);
        refine_condition(c->p, &c->vars, &c->flow, s->a, true);
        target.phase = 1;
        remember_focus(c, &target, s->loc, false);
        i = emit(c, I_CONST, s->loc);
        i->dst = ran.id;
        i->type = ran.type;
        i->text = "1";
        conditional(c, initial, once, main, s->loc);
        label_at(c, once, s->loc);
        int turn = c->vars.n;
        List pre_vars = {0};
        for (int j = persistent; j < turn; j++) {
            add(&pre_vars, c->vars.v[j]);
        }
        List init = {0};
        for (int j = 0; j < s->first.n; j++) {
            Stmt *item = s->first.v[j], *copy = mem(sizeof(*copy));
            *copy = *item;
            if (copy->kind == S_BIND) {
                copy->kind = S_INIT;
            }
            if (copy->kind == S_BIND_MANY) {
                copy->kind = S_INIT_MANY;
            }
            add(&init, copy);
        }
        /* First-section declarations outlive the per-turn pre/main storage. */
        int depth = c->depth;
        c->depth--;
        if (live) {
            statements(c, &init, false);
        }
        c->depth = depth;
        if (c->vars.n != turn) {
            fail(s->loc, "first^ must declare persistent values at its top level");
        }
        if (!c->terminated) {
            i = emit(c, I_CONST, s->loc);
            i->dst = initial.id;
            i->type = initial.type;
            i->text = "0";
            branch_to(c, main, s->loc);
        }
        c->terminated = false;
        label_at(c, main, s->loc);
        for (int j = 0; j < first.n; j++) {
            ((Var *)first.v[j])->unavailable = false;
        }
        if (live) {
            statements(c, &s->body, false);
        }
        if (!c->terminated) {
            cleanup(c, persistent, s->loc);
            remember_focus(c, &target, s->loc, false);
            branch_to(c, update, s->loc);
        }
        c->vars.n = persistent;
        c->depth--;
        c->terminated = false;
        label_at(c, update, s->loc);
        c->lexical--;
        if (live) {
            statements(c, &s->update, true);
        }
        c->lexical++;
        if (!c->terminated) {
            branch_to(c, test, s->loc);
        }
        c->terminated = false;
        label_at(c, no, s->loc);
        /* The failed test also owns the final pre-section activation. */
        c->depth++;
        int saved = c->vars.n;
        /* Reuse the exact pre variables/slots from the emitted test. */
        for (int j = 0; j < pre_vars.n; j++) {
            add(&c->vars, pre_vars.v[j]);
        }
        cleanup(c, persistent, s->loc);
        c->vars.n = saved;
        c->depth--;
        branch_to(c, finish, s->loc);
        restore_facts(&c->vars, turn_facts);
        bool changed = false;
        for (int j = 0; j < persistent; j++) {
            changed |= before[j] != ((Var *)c->vars.v[j])->deps ||
                       (before_fact[j] && !((Var *)c->vars.v[j])->narrowed) ||
                       (before_paths[j] && !((Var *)c->vars.v[j])->paths.n);
        }
        if (!changed) {
            break;
        }
        c->in->code.n = code;
        c->in->slots.n = slots;
        c->in->labels = labels;
        c->root_count = roots;
    }
    restore_facts(&c->vars, loop_inputs);
    label_at(c, finish, s->loc);
    conditional(c, ran, last, epilog, s->loc);
    label_at(c, last, s->loc);
    target.phase = 2;
    remember_focus(c, &target, s->loc, true);
    c->lexical--;
    statements(c, &s->last, true);
    c->lexical++;
    if (!c->terminated) {
        branch_to(c, epilog, s->loc);
    }
    c->terminated = false;
    label_at(c, epilog, s->loc);
    target.phase = 3;
    for (int j = 0; j < first.n; j++) {
        ((Var *)first.v[j])->unavailable = true;
    }
    c->lexical--;
    statements(c, &s->epilog, true);
    c->lexical++;
    c->loops.n--;
    if (!c->terminated) {
        cleanup(c, scope, s->loc);
    }
    c->vars.n = scope;
    c->depth--;
    restore_facts(&c->vars, entry_facts);
}

typedef struct {
    Value location, pointer, active, old, pending;
    Type *prior_fact;
    bool indirect;
} NilAssignment;

static Value assignment_location(Context *c, NilAssignment *pair, Loc loc)
{
    if (!pair->indirect) {
        return pair->location;
    }
    Value place = deref(c, pair->pointer, loc);
    Value out = pair->location;
    out.id = place.id;
    return out;
}

static void nil_assignment(Context *c, Stmt *s)
{
    int count = s->targets.n ? s->targets.n : 1;
    NilAssignment *pairs = mem((size_t)count * sizeof(*pairs));
    for (int j = 0; j < count; j++) {
        Expr *target = s->targets.n ? s->targets.v[j] : s->a;
        NilAssignment *pair = &pairs[j];
        if (target->kind == E_NAME && !strcmp(target->text, "_^")) {
            fail(target->loc, "discard is not a nil-guarded assignment destination");
        }
        pair->active = value(c, type_builtin(c->p, T_BOOL));
        Instr *zero = emit(c, I_CONST, s->loc);
        zero->dst = pair->active.id;
        zero->type = pair->active.type;
        zero->text = "0";
        int absent = ++c->in->labels, end = ++c->in->labels;
        if (guarded_chain(target)) {
            Type *t = chain_place_type(c, target);
            if (!t) {
                fail(target->loc, "cannot infer nil-guarded assignment destination");
            }
            if (t->kind == T_NEVER) {
                Type *normal = chain_type(c, target);
                bool never = false;
                pair->location = chain_walk(c, target, absent, &never);
                if (normal && normal->kind == T_NEVER) {
                    return;
                }
                c->terminated = false;
                nil_label(c, absent, s->loc);
                nil_jump(c, end, s->loc);
                nil_label(c, end, s->loc);
                continue;
            }
            layout(t, target->loc);
            pair->pointer = value(c, type_wrap(c->p, T_REF, t, 0));
            pair->indirect = true;
            bool never = false;
            pair->location = chain_walk(c, target, absent, &never);
            if (never) {
                fail(target->loc, "nil-only chain has no assignment destination");
            }
            if (!pair->location.addressable) {
                fail(target->loc, "nil-guarded destination must refer to existing "
                                  "storage; use an optional reference");
            }
            Instr *address = emit(c, I_ADDR, target->loc);
            address->dst = pair->pointer.id;
            address->a = pair->location.id;
            address->type = pair->pointer.type;
            pair->pointer.deps = pair->location.storage | pair->location.deps;
        }
        else {
            pair->location = place_expression(c, target);
            if (dead(pair->location)) {
                return;
            }
            if (!pair->location.addressable) {
                fail(target->loc, "assignment destination is not addressable");
            }
        }
        if (pair->location.var && pair->location.type->kind == T_UNION) {
            pair->prior_fact = pair->location.var->narrowed;
        }
        if (pair->location.type->owned) {
            fail(target->loc, "owned values cannot be reassigned while references may exist");
        }
        store_check(c, pair->location, (Value){0}, s->loc);
        Value test = present_value(c, pair->location, s->loc);
        copy_to(c, pair->active, test, s->loc, true);
        if (s->op) {
            Type *plain = without_nil(c->p, pair->location.type, s->loc);
            if (!plain || (!kind_integer(plain->kind) && !kind_float(plain->kind))) {
                fail(s->loc, "nil-guarded compound assignment requires one non-nil "
                             "numeric type");
            }
            int yes = ++c->in->labels;
            nil_branch(c, test, yes, absent, s->loc);
            nil_label(c, yes, s->loc);
            pair->old = snapshot(c, convert_value(c, pair->location, plain, -1, s->loc), s->loc);
        }
        nil_jump(c, end, s->loc);
        nil_label(c, absent, s->loc);
        nil_jump(c, end, s->loc);
        nil_label(c, end, s->loc);
    }
    // Preserve parallel assignment: snapshot every enabled RHS before any write.
    for (int j = 0; j < count; j++) {
        NilAssignment *pair = &pairs[j];
        if (dead(pair->location)) {
            continue;
        }
        int yes = ++c->in->labels, end = ++c->in->labels;
        nil_branch(c, pair->active, yes, end, s->loc);
        nil_label(c, yes, s->loc);
        Expr *rhs = s->values.n ? s->values.v[j] : s->b;
        if (s->op) {
            Value b = expression(c, rhs, pair->old.type);
            Expr binary = {.kind = E_BIN,
                           .loc = s->loc,
                           .text = s->op,
                           .a = saved_value(pair->old, s->loc),
                           .b = saved_value(b, s->loc)};
            pair->pending = dead(b) ? b
                                    : convert_value(c, expression(c, &binary, pair->old.type),
                                                    pair->location.type, -1, s->loc);
        }
        else {
            pair->pending = expression(c, rhs, pair->location.type);
        }
        pair->pending = snapshot(c, pair->pending, s->loc);
        if (!dead(pair->pending)) {
            nil_jump(c, end, s->loc);
        }
        c->terminated = false;
        nil_label(c, end, s->loc);
    }
    for (int j = 0; j < count; j++) {
        NilAssignment *pair = &pairs[j];
        if (dead(pair->location) || dead(pair->pending)) {
            continue;
        }
        int yes = ++c->in->labels, end = ++c->in->labels;
        nil_branch(c, pair->active, yes, end, s->loc);
        nil_label(c, yes, s->loc);
        copy_to(c, assignment_location(c, pair, s->loc), pair->pending, s->loc, false);
        if (s->op && pair->prior_fact && pair->prior_fact->kind != T_NIL && pair->location.var) {
            pair->location.var->narrowed = pair->prior_fact;
        }
        nil_jump(c, end, s->loc);
        nil_label(c, end, s->loc);
    }
}

List join_exit_facts(List left, List right)
{
    if (!left.n) {
        return right;
    }
    for (int j = 0; j < left.n; j++) {
        NilFact *a = left.v[j], *b = right.v[j];
        if (a->type != b->type || a->version != b->version) {
            a->type = NULL;
        }
        if (a->paths.v != b->paths.v || a->path_version != b->path_version) {
            a->paths = (List){0};
        }
        a->version = (a->version > b->version ? a->version : b->version) + 1;
        a->path_version =
            (a->path_version > b->path_version ? a->path_version : b->path_version) + 1;
    }
    return left;
}

void handle_statements(Context *c, Stmt *s)
{
    Stmt local = *s, branch = {0};
    List head_body = {0};
    if (s->a) {
        Value head = snapshot(c, expression(c, s->a, type_builtin(c->p, T_BOOL)), s->loc);
        if (dead(head)) {
            return;
        }
        branch = *(Stmt *)s->body.v[0];
        branch.a = saved_value(head, s->a->loc);
        branch.a->a = s->a;
        add(&head_body, &branch);
        local.body = head_body;
        local.a = NULL;
        s = &local;
    }
    Type *errors = handler_errors(c, s);
    if (!errors) {
        fail(s->loc, "catch^ clause requires an error-producing try^ before its arms");
    }
    int base = c->vars.n, visible = base;
    if (s->loop_handler) {
        visible = ((Loop *)c->loops.v[c->loops.n - 1])->outside;
    }
    List before = save_facts(&c->vars);
    ErrorTarget target = {
        .errors = errors,
        .slot = slot(c, errors, false),
        .label = ++c->in->labels,
        .bindings = base,
        .pending = c->pending_owners.n,
        .checkpoint = -1
    };
    if (c->in->result->owned) {
        target.checkpoint = slot(c, type_builtin(c->p, T_U64), false);
        Instr *i = emit(c, I_STACK_SAVE, s->loc);
        i->a = c->in->result_slot;
        i->dst = target.checkpoint;
        i->type = c->in->result;
    }
    add(&c->handlers, &target);
    statements(c, &s->body, false);
    bool normal = !c->terminated;
    List exits = normal ? save_facts(&c->vars) : (List){0};
    exits.n = normal ? base : 0;
    int end = ++c->in->labels;
    if (normal) {
        cleanup(c, base, s->loc);
        nil_jump(c, end, s->loc);
    }
    c->vars.n = base;
    c->handlers.n--;
    c->terminated = false;
    nil_label(c, target.label, s->loc);
    Value remaining = {.id = target.slot, .type = errors, .deps = target.deps};
    bool *hidden = mem((size_t)(base + 1) * sizeof(*hidden));
    for (int j = 0; j < base; j++) {
        hidden[j] = ((Var *)c->vars.v[j])->hidden;
    }
    for (int k = 0; k < s->handlers.n && remaining.type; k++) {
        CatchArm *arm = s->handlers.v[k];
        Type *asked = arm->spec ? type_spec(c->p, arm->spec) : remaining.type;
        if (!only_errors(asked)) {
            fail(arm->loc, "catch^ arm type must contain only declared errors");
        }
        Type *yes = fits_subset(c->p, remaining.type, asked, true, arm->loc);
        Type *no = fits_subset(c->p, remaining.type, asked, false, arm->loc);
        if (!yes) {
            continue;
        }
        int next = 0;
        if (no) {
            int body = ++c->in->labels;
            next = ++c->in->labels;
            Expr test = {.kind = E_FITS,
                         .loc = arm->loc,
                         .a = saved_value(remaining, arm->loc),
                         .test_spec = arm->spec};
            nil_branch(c, fits_value(c, &test), body, next, arm->loc);
            nil_label(c, body, arm->loc);
        }
        rewind_facts(&c->vars, target.facts.n ? target.facts : before);
        for (int j = visible; j < base; j++) {
            ((Var *)c->vars.v[j])->hidden = true;
        }
        if (s->scoped_handlers) {
            c->depth++;
            c->lexical++;
        }
        error_focus(c, pack_error(c, remaining, yes, -1, arm->loc), arm->loc);
        statements(c, &arm->body, false);
        if (!c->terminated) {
            cleanup(c, base, arm->loc);
            List exit = save_facts(&c->vars);
            exit.n = base;
            exits = normal ? join_exit_facts(exits, exit) : exit;
            normal = true;
            nil_jump(c, end, arm->loc);
        }
        c->vars.n = base;
        if (s->scoped_handlers) {
            c->depth--;
            c->lexical--;
        }
        for (int j = 0; j < base; j++) {
            ((Var *)c->vars.v[j])->hidden = hidden[j];
        }
        c->terminated = false;
        if (next) {
            nil_label(c, next, arm->loc);
            remaining = pack_error(c, remaining, no, -1, arm->loc);
        }
        else {
            remaining.type = NULL;
        }
    }
    if (remaining.type) {
        route_error(c, remaining, s->loc);
    }
    c->vars.n = base;
    c->terminated = !normal;
    if (normal) {
        rewind_facts(&c->vars, exits);
        nil_label(c, end, s->loc);
    }
}

static void binding_name(Context *c, Stmt *s)
{
    if (c->in->fn == c->p->entry && c->depth == 1) {
        if (lookup_def(c->p, s->name)) {
            fail(s->loc, "top-level binding conflicts with def^ '%s'", s->name);
        }
        for (int j = 0; j < c->p->functions.n; j++) {
            Function *f = c->p->functions.v[j];
            if (!strcmp(s->name, f->name)) {
                fail(s->loc, "top-level binding conflicts with function '%s'", s->name);
            }
        }
    }
    for (int j = c->vars.n - 1; j >= 0; j--) {
        Var *v = c->vars.v[j];
        if (v->depth < c->depth) {
            break;
        }
        if (!strcmp(v->name, s->name)) {
            fail(s->loc, "duplicate binding '%s'", s->name);
        }
    }
}

void statements(Context *c, List *body, bool scope)
{
    int start = c->vars.n;
    if (scope) {
        c->depth++;
        c->lexical++;
    }
    for (int n = 0; n < body->n; n++) {
        Stmt *s = body->v[n];
        if (c->terminated) {
            fail(s->loc, "unreachable statement");
        }
        if (s->kind == S_INIT_MANY) {
            for (int j = 0; j < s->body.n; j++) {
                Stmt *b = s->body.v[j];
                Var *v = lookup(c, b->name);
                if (!v || !v->unavailable || !v->pending) {
                    fail(b->loc, "first^ binding conflicts with a pre^ binding");
                }
                Value out = construct(c, b->a, v->type, v->slot);
                if (dead(out)) {
                    break;
                }
                Value dst = variable(v);
                dst.var = NULL;
                store_check(c, dst, out, b->loc);
                v->deps |= out.deps;
            }
            if (c->terminated) {
                break;
            }
            for (int j = 0; j < s->body.n; j++) {
                Var *v = lookup(c, ((Stmt *)s->body.v[j])->name);
                v->unavailable = v->pending = false;
            }
        }
        else if (s->kind == S_INIT) {
            Var *var = lookup(c, s->name);
            if (!var || !var->unavailable) {
                fail(s->loc, "invalid first^ initialization");
            }
            Value v = construct(c, s->a, var->type, var->slot);
            if (dead(v)) {
                break;
            }
            Value dst = variable(var);
            dst.var = NULL;
            store_check(c, dst, v, s->loc);
            var->deps |= v.deps;
            var->unavailable = var->pending = false;
        }
        else if (s->kind == S_HANDLE) {
            handle_statements(c, s);
        }
        else if (s->kind == S_BIND) {
            if (!strcmp(s->name, "_^")) {
                Stmt hidden = *s;
                hidden.name = fmt("\001discard%d", c->in->slots.n);
                List one = {0};
                add(&one, &hidden);
                statements(c, &one, false);
                continue;
            }
            binding_name(c, s);
            Type *t = s->spec ? type_spec(c->p, s->spec) : NULL;
            if (!t) {
                t = expression_type(c, s->a, NULL);
            }
            Value v = {0};
            bool alias_result =
                c->in->result_local && !strcmp(c->in->result_local, s->name) && c->depth == 1;
            int dest =
                alias_result ? result_place(c, t ? t : expression_type(c, s->a, NULL), s->loc) : -1;
            alias_result = dest >= 0;
            if (alias_result && !t) {
                t = ((Slot *)c->in->slots.v[dest])->type;
            }
            if (callable_type(t)) {
                Type *actual = expression_type(c, s->a, t);
                if (!actual || !callable_accepts(t, actual)) {
                    fail(s->loc, "callable does not satisfy '%s'", type_name(t));
                }
                check_callable(c->p, t, actual, s->loc);
                if (s->mut) {
                    fail(s->loc, "statically selected callable bindings require let^");
                }
                t = actual;
            }
            if (t && t->kind == T_NEVER) {
                expression(c, s->a, NULL);
                break;
            }
            if (t) {
                layout(t, s->loc);
                if (t->kind == T_VOID) {
                    fail(s->loc, "binding cannot have void type");
                }
                if (s->depth) {
                    results_check(t, s->depth, s->loc);
                }
                else if (t->kind == T_STRUCT && t->def->tuple) {
                    fail(s->loc, "multiple results '%s' need a name each, as in let^ a, b = ...",
                         type_name(t));
                }
                if (dest < 0) {
                    dest = slot(c, t, false);
                }
                if (t->owned && !alias_result) {
                    Instr *begin = emit(c, I_STACK_BEGIN, s->loc);
                    begin->dst = dest;
                    begin->type = t;
                    begin->text = s->name;
                }
                bool pending = t->owned && !alias_result;
                if (pending) {
                    add(&c->pending_owners, (void *)(intptr_t)(dest + 1));
                }
                bool building = c->building_result;
                c->building_result |= alias_result;
                v = construct(c, s->a, t, dest);
                c->building_result = building;
                if (pending) {
                    c->pending_owners.n--;
                }
                if (dead(v)) {
                    break;
                }
            }
            else {
                v = expression(c, s->a, NULL);
                if (dead(v)) {
                    break;
                }
                t = v.type;
                if (t->kind == T_VOID) {
                    fail(s->loc, "binding cannot have void type");
                }
                int d = slot(c, t, false);
                Value dst = {.id = d, .type = t};
                v = copy_to(c, dst, v, s->loc, true);
            }
            if (s->integer && !kind_integer(t->kind)) {
                fail(s->loc, "counted loops require an integer type");
            }
            Var *var = mem(sizeof(*var));
            var->declaration = s->loc;
            var->name = s->name;
            var->slot = v.id;
            var->type = t;
            var->depth = c->depth;
            var->lexical = c->lexical;
            var->mut = s->mut;
            var->root = root(c, c->depth, false, s->loc);
            var->deps = v.deps;
            if (s->flow_alias && s->a->kind == E_NAME) {
                var->source = selected(c, s->a);
                var->source_version = var->source ? var->source->version : 0;
            }
            Value dst = variable(var);
            dst.var = NULL;
            store_check(c, dst, v, s->loc);
            tool_record(c->p, s->loc, s->loc, s->name, t, NULL, NULL, 13, true);
            add(&c->vars, var);
        }
        else if (s->kind == S_BIND_MANY && s->prolog.n) {
            /* let^ a, b = f(): each name views its part of the results in place,
             * so an owned part is neither copied nor moved. */
            statements(c, &s->prolog, false);
            if (c->terminated) {
                break;
            }
            Var *source = c->vars.v[c->vars.n - 1];
            Type *t = source->type;
            for (int j = 0; j < s->body.n; j++) {
                Stmt *binding = s->body.v[j];
                Field *f = t->def->fields.v[j];
                for (int k = 0; k < j; k++) {
                    if (strcmp(binding->name, "_^") &&
                        !strcmp(binding->name, ((Stmt *)s->body.v[k])->name)) {
                        fail(binding->loc, "duplicate binding '%s'", binding->name);
                    }
                }
                if (binding->spec && type_spec(c->p, binding->spec) != f->type) {
                    fail(binding->loc, "result %d is '%s'", j, type_name(f->type));
                }
                if (!strcmp(binding->name, "_^")) {
                    continue;
                }
                binding_name(c, binding);
                Var *v = mem(sizeof(*v));
                v->declaration = binding->loc;
                v->name = binding->name;
                v->slot = field_value(c, variable(source), f, binding->loc).id;
                v->type = f->type;
                v->depth = c->depth;
                v->lexical = c->lexical;
                v->mut = s->mut;
                v->root = source->root;
                v->deps = source->deps;
                tool_record(c->p, binding->loc, binding->loc, v->name, v->type, NULL, NULL, 13,
                            true);
                add(&c->vars, v);
            }
        }
        else if (s->kind == S_BIND_MANY) {
            int begin = c->vars.n;
            for (int j = 0; j < s->body.n; j++) {
                Stmt *binding = s->body.v[j];
                for (int k = 0; k < j; k++) {
                    if (strcmp(binding->name, "_^") &&
                        !strcmp(binding->name, ((Stmt *)s->body.v[k])->name)) {
                        fail(binding->loc, "duplicate binding '%s'", binding->name);
                    }
                }
                binding_name(c, binding);
                Stmt hidden = *binding;
                hidden.name = fmt("\001parallel%d", c->in->slots.n);
                List one = {0};
                add(&one, &hidden);
                // Keep a parallel declaration's owned result at the caller destination.
                char *result = c->in->result_local;
                if (result && !strcmp(result, binding->name)) {
                    c->in->result_local = hidden.name;
                }
                statements(c, &one, false);
                c->in->result_local = result;
                if (c->terminated) {
                    break;
                }
            }
            if (c->terminated) {
                break;
            }
            for (int j = 0; j < s->body.n; j++) {
                Stmt *binding = s->body.v[j];
                Var *v = c->vars.v[begin + j];
                if (strcmp(binding->name, "_^")) {
                    v->name = binding->name;
                    tool_record(c->p, binding->loc, binding->loc, v->name, v->type, NULL, NULL, 13,
                                true);
                }
            }
        }
        else if (s->kind == S_ASSIGN && s->optional) {
            nil_assignment(c, s);
        }
        else if (s->kind == S_ASSIGN) {
            int count = s->targets.n ? s->targets.n : 1;
            Value *dest = mem(sizeof(*dest) * count), *old = mem(sizeof(*old) * count),
                  *src = mem(sizeof(*src) * count);
            for (int j = 0; j < count; j++) {
                Expr *target = s->targets.n ? s->targets.v[j] : s->a;
                if (target->kind == E_NAME && !strcmp(target->text, "_^")) {
                    if (s->op) {
                        fail(s->loc, "discard cannot be a compound assignment target");
                    }
                    continue;
                }
                dest[j] = place_expression(c, s->targets.n ? s->targets.v[j] : s->a);
                if (dead(dest[j])) {
                    break;
                }
                if (!dest[j].addressable) {
                    fail(s->loc, "assignment destination is not addressable");
                }
                if (dest[j].type->owned) {
                    fail(s->loc, "owned values cannot be reassigned while references may exist");
                }
                if (s->op) {
                    Value previous = dest[j];
                    Var *root = NULL;
                    char *path = NULL;
                    if (previous.type->kind == T_UNION && stable_path(c, target, &root, &path)) {
                        Type *type = path_type(root, path);
                        if (type) {
                            previous = convert_value(c, previous, type, -1, s->loc);
                        }
                    }
                    old[j] = snapshot(c, previous, s->loc);
                }
            }
            if (c->terminated) {
                break;
            }
            for (int j = 0; j < count; j++) {
                if (!dest[j].type) {
                    Stmt discard = {.kind = S_BIND,
                                    .loc = s->loc,
                                    .name = "_^",
                                    .a = s->values.n ? s->values.v[j] : s->b};
                    List one = {0};
                    add(&one, &discard);
                    statements(c, &one, false);
                    continue;
                }
                Expr *rhs = s->values.n ? s->values.v[j] : s->b;
                Type *hint = s->op ? operator_hint(c->p, s->op, old[j].type, false, rhs) : NULL;
                src[j] = snapshot(c,
                                  expression(c, rhs,
                                             hint    ? hint
                                             : s->op ? old[j].type
                                                     : dest[j].type),
                                  s->loc);
                if (s->op) {
                    Expr left = {.kind = E_SAVED, .loc = s->loc, .saved_slot = old[j].id};
                    Expr right = {.kind = E_SAVED, .loc = s->loc, .saved_slot = src[j].id};
                    Expr binary = {
                        .kind = E_BIN, .loc = s->loc, .text = s->op, .a = &left, .b = &right};
                    src[j] = expression(c, &binary, old[j].type);
                }
            }
            if (c->terminated) {
                break;
            }
            if (c->terminated) {
                break;
            }
            for (int j = 0; j < count; j++) {
                if (!dest[j].type) {
                    continue;
                }
                bool mut = dest[j].var && dest[j].var->mut;
                if (s->internal && dest[j].var) {
                    dest[j].var->mut = true;
                }
                copy_to(c, dest[j], src[j], s->loc, false);
                Expr *target = s->targets.n ? s->targets.v[j] : s->a;
                if (s->op && target->kind == E_NAME && dest[j].var &&
                    dest[j].type->kind == T_UNION &&
                    union_alternative(dest[j].type, src[j].type) >= 0) {
                    dest[j].var->narrowed = src[j].type;
                }
                if (s->internal && dest[j].var) {
                    dest[j].var->mut = mut;
                }
            }
        }
        else if (s->kind == S_BREAK || s->kind == S_NEXT) {
            if (s->depth > c->loops.n) {
                fail(s->loc, "jump depth exceeds enclosing loops");
            }
            Loop *loop = c->loops.v[c->loops.n - s->depth];
            if (loop->phase < 0 || loop->phase >= 2) {
                fail(s->loc, "prolog^/last^/epilog^ cannot jump to their own loop");
            }
            if (loop->phase == 1) {
                for (int j = 0; j < loop->first.n; j++) {
                    if (((Var *)loop->first.v[j])->unavailable) {
                        fail(s->loc, "cannot jump before all first^ bindings are initialized");
                    }
                }
            }
            if (loop->phase == 1) {
                remember_focus(c, loop, s->loc, false);
            }
            cleanup(c, loop->bindings, s->loc);
            Instr *br = emit(c, I_BR, s->loc);
            br->label = s->kind == S_BREAK ? loop->end : loop->update;
            c->terminated = true;
        }
        else if (s->kind == S_EXPR) {
            Value v = expression(c, s->a, NULL);
            if (dead(v)) {
                break;
            }
            if (v.type->owned) {
                fail(s->loc, "owned temporary requires a binding");
            }
            if (s->a->kind != E_CALL && s->a->kind != E_TRY && s->a->kind != E_CATCH) {
                fail(s->loc, "only calls can be expression statements");
            }
        }
        else if (s->kind == S_RETURN) {
            Type *t = c->in->result;
            Value v = {.id = -1, .type = type_builtin(c->p, T_VOID)};
            if (s->a) {
                if (t->kind == T_VOID) {
                    Value tested = expression(c, s->a, NULL);
                    if (dead(tested)) {
                        break;
                    }
                    fail(s->loc, "procedure or void function cannot return a value");
                }
                Expr *named = s->a->tuple ? s->a->args.v[s->a->args.n - 1] : s->a;
                bool designated = t->owned && c->in->result_local && named->kind == E_NAME &&
                                  !strcmp(named->text, c->in->result_local);
                if (t->owned && c->in->result_local && t->kind != T_UNION && !designated) {
                    fail(s->loc, "owned result must return the designated construction binding");
                }
                if (designated) {
                    Var *r = lookup(c, named->text);
                    if (!r || (r->slot != c->in->result_slot && r->slot + 1 != c->result_part)) {
                        fail(s->loc, "owned result binding was shadowed");
                    }
                    v = variable(r);
                    /* The owned last part is already in place; build the others. */
                    Value base = {.id = c->in->result_slot, .type = t};
                    for (int j = 0; s->a->tuple && t->kind == T_UNION && j < t->def->alternatives.n;
                         j++) {
                        Type *arm = t->def->alternatives.v[j];
                        if (arm->owned && arm->kind == T_STRUCT && arm->def->tuple) {
                            base = (Value){.id = case_at(c, base, arm, true, s->loc), .type = arm};
                        }
                    }
                    if (s->a->tuple && (base.type->kind != T_STRUCT || !base.type->def->tuple ||
                                        base.type->def->fields.n != s->a->args.n)) {
                        fail(s->loc, "%d values returned for '%s'", s->a->args.n, type_name(t));
                    }
                    for (int j = 0; s->a->tuple && j + 1 < s->a->args.n; j++) {
                        Field *f = base.type->def->fields.v[j];
                        Value part = construct(c, s->a->args.v[j], f->type,
                                               field_value(c, base, f, s->loc).id);
                        if (dead(part)) {
                            v = part;
                            break;
                        }
                        v.deps |= part.deps;
                    }
                }
                else {
                    bool building = c->building_result;
                    c->building_result |= t->owned;
                    v = construct(c, s->a, t, c->in->result_slot);
                    c->building_result = building;
                }
                if (dead(v)) {
                    break;
                }
            }
            else if (t->kind != T_VOID) {
                if (nullable(t) && has_error(t)) {
                    v = construct(c, &(Expr){.kind = E_NAME, .text = "nil^", .loc = s->loc}, t,
                                  c->in->result_slot);
                }
                else {
                    fail(s->loc, "missing return value");
                }
            }
            for (int j = 0; j < c->root_count; j++) {
                if ((v.deps & (UINT64_C(1) << j)) && !c->root_external[j]) {
                    fail(s->loc, "cannot return a reference rooted in a local owner");
                }
            }
            cleanup(c, 0, s->loc);
            Instr *i = emit(c, I_RETURN, s->loc);
            i->type = t;
            c->terminated = true;
        }
        else if (s->kind == S_BLOCK) {
            statements(c, &s->body, true);
        }
        else if (s->kind == S_IF) {
            Value cond = expression(c, s->a, type_builtin(c->p, T_BOOL));
            if (dead(cond)) {
                break;
            }
            if (cond.type->kind != T_BOOL) {
                fail(s->loc, "condition requires bool");
            }
            int known = condition_truth(&c->flow, s->a);
            if (known) {
                List facts = save_facts(&c->vars);
                refine_condition(c->p, &c->vars, &c->flow, s->a, known > 0);
                if (s->internal) {
                    c->lexical--;
                }
                statements(c, known > 0 ? &s->body : &s->other, true);
                if (s->internal) {
                    c->lexical++;
                }
                restore_facts(&c->vars, facts);
                if (c->terminated) {
                    break;
                }
                continue;
            }
            int yes = ++c->in->labels, no = ++c->in->labels, end = ++c->in->labels;
            Instr *i = emit(c, I_CBR, s->loc);
            i->a = cond.id;
            i->label = yes;
            i->other = no;
            i = emit(c, I_LABEL, s->loc);
            i->label = yes;
            if (s->internal) {
                c->lexical--;
            }
            List facts = save_facts(&c->vars);
            refine_condition(c->p, &c->vars, &c->flow, s->a, true);
            statements(c, &s->body, true);
            List after_yes = save_facts(&c->vars);
            rewind_facts(&c->vars, facts);
            if (s->internal) {
                c->lexical++;
            }
            bool a = c->terminated;
            if (!a) {
                i = emit(c, I_BR, s->loc);
                i->label = end;
            }
            c->terminated = false;
            i = emit(c, I_LABEL, s->loc);
            i->label = no;
            if (s->internal) {
                c->lexical--;
            }
            refine_condition(c->p, &c->vars, &c->flow, s->a, false);
            statements(c, &s->other, true);
            bool b = c->terminated;
            if (!a && b) {
                rewind_facts(&c->vars, after_yes);
            }
            else if (!a && !b) {
                merge_facts(&c->vars, facts, after_yes);
            }
            if (s->internal) {
                c->lexical++;
            }
            if (!b) {
                i = emit(c, I_BR, s->loc);
                i->label = end;
            }
            c->terminated = a && b;
            if (!c->terminated) {
                i = emit(c, I_LABEL, s->loc);
                i->label = end;
            }
        }
        else if (s->kind == S_WHILE) {
            loop_code(c, s);
        }
    }
    if (scope) {
        if (!c->terminated) {
            cleanup(c, start, body->n ? ((Stmt *)body->v[body->n - 1])->loc : c->in->fn->loc);
        }
        c->vars.n = start;
        c->depth--;
        c->lexical--;
    }
}

/* The local every return^ gives as the owned result; returns of another
 * alternative of a union result do not take part. */
static void return_names(List *body, char **name, bool *valid, bool others)
{
    for (int j = 0; j < body->n; j++) {
        Stmt *s = body->v[j];
        if (s->kind == S_RETURN) {
            Expr *a = s->a && s->a->tuple ? s->a->args.v[s->a->args.n - 1] : s->a;
            if (!a || a->kind != E_NAME || !strcmp(a->text, "nil^")) {
                *valid &= others;
                continue;
            }
            if (*name && strcmp(*name, a->text)) {
                *valid = false;
            }
            else {
                *name = a->text;
            }
        }
        return_names(&s->body, name, valid, others);
        return_names(&s->other, name, valid, others);
        return_names(&s->prolog, name, valid, others);
        return_names(&s->pre, name, valid, others);
        return_names(&s->first, name, valid, others);
        return_names(&s->last, name, valid, others);
        return_names(&s->epilog, name, valid, others);
        return_names(&s->update, name, valid, others);
        for (int k = 0; k < s->handlers.n; k++) {
            return_names(&((CatchArm *)s->handlers.v[k])->body, name, valid, others);
        }
    }
}

Instance *instantiate(Program *p, Function *f, Def *self, List *actual, Loc at)
{
    Instance *in = instance_for(p, f, self, actual, at);
    if (in->state) {
        return in;
    }
    infer_results(p);
    if (!in->result) {
        fail(f->loc,
             "cannot infer return type of '%s'; add a grounded return or an "
             "explicit result type",
             f->name);
    }
    layout(in->result, f->loc);
    if (f->operator_member) {
        operator_contract(f, in->result, f->loc);
    }
    if (f->external) {
        in->state = 2;
        return in;
    }
    in->state = 1;
    Context c = {0};
    c.p = p;
    c.in = in;
    c.access = f->owner;
    c.depth = 1;
    c.lexical = f->lexical ? f->lexical : 1;
    if (in->result->kind != T_VOID) {
        in->result_slot = slot(&c, in->result, true);
        if (in->result->owned) {
            bool valid = true;
            return_names(&f->body, &in->result_local, &valid, in->result->kind == T_UNION);
            if (!valid) {
                in->result_local = NULL;
            }
        }
    }
    for (int j = 0; j < f->params.n; j++) {
        Param *a = f->params.v[j];
        for (int k = 0; k < j; k++) {
            if (strcmp(a->name, "_^") && !strcmp(a->name, ((Param *)f->params.v[k])->name)) {
                fail(a->loc, "duplicate parameter '%s'", a->name);
            }
        }
        Type *t = actual->v[j];
        layout(t, a->loc);
        if (t->kind == T_VOID) {
            fail(a->loc, "parameter cannot have void type");
        }
        if (t->kind == T_STRUCT && t->def->tuple) {
            fail(a->loc, "multiple results '%s' cannot be passed; bind them with let^ a, b = ...",
                 type_name(t));
        }
        if (t->owned) {
            fail(a->loc, "owned by-value parameters are not supported; use 'ref^'");
        }
        Var *v = mem(sizeof(*v));
        v->declaration = a->loc;
        tool_record(p, a->loc, a->loc, a->name, t, NULL, NULL, TOOL_PARAMETER, true);
        v->name = !strcmp(a->name, "_^") ? fmt("\001discard_param%d", j) : a->name;
        v->type = t;
        v->slot = slot(&c, t, false);
        v->depth = 1;
        v->lexical = c.lexical;
        v->mut = true;
        v->parameter = true;
        v->root = root(&c, 1, false, a->loc);
        if (t->refs) {
            v->deps = root(&c, 0, true, a->loc);
        }
        if (j < 64) {
            c.param_deps[j] = v->deps;
        }
        if (a->self) {
            c.self_roots = v->deps;
        }
        add(&c.vars, v);
    }
    statements(&c, &f->body, false);
    if (!c.terminated) {
        if (nullable(in->result) && has_error(in->result)) {
            construct(&c, &(Expr){.kind = E_NAME, .text = "nil^", .loc = f->loc}, in->result,
                      in->result_slot);
        }
        else if (in->result->kind != T_VOID) {
            fail(f->loc, "not all paths return a value in '%s'", f->name);
        }
        cleanup(&c, 0, f->loc);
        Instr *i = emit(&c, I_RETURN, f->loc);
        i->type = in->result;
    }
    in->state = 2;
    return in;
}

void analyze(Program *p)
{
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        for (int k = 0; k < j; k++) {
            if (!strcmp(d->name, ((Def *)p->defs.v[k])->name)) {
                fail(d->loc, "duplicate def^ '%s'", d->name);
            }
        }
        Type *t = mem(sizeof(*t));
        t->kind = d->union_specs.n ? T_UNION : T_STRUCT;
        t->def = d;
        if (p->modules.n > 1 || (d->module && d->module->name)) {
            t->symbol = fmt(
                "%%rh_type_%s",
                link_identity(fmt(
                    "%s:%s", d->module && d->module->name ? d->module->name : "<entry>", d->name)));
        }
        t->id = p->types.n;
        d->type = t;
        add(&p->types, t);
    }
    for (int j = 0; j < p->defs.n; j++) {
        compose(p, p->defs.v[j]);
    }
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        if (!d->opaque) {
            layout(d->type, d->loc);
        }
        if (d->lhat_value || d->lhat_data) {
            lhat_def_validate(p, d);
        }
    }
    for (int j = 0; j < p->functions.n; j++) {
        Function *f = p->functions.v[j];
        if (lookup_def(p, f->name)) {
            fail(f->loc, "function conflicts with def^ '%s'", f->name);
        }
        if (f->external) {
            external_validate(p, f);
        }
        if (f->exported || f->lhat_export) {
            export_validate(p, f);
        }
    }
    infer_parameters(p);
    overload_validate(p, &p->functions);
    for (int j = 0; j < p->defs.n; j++) {
        overload_validate(p, &((Def *)p->defs.v[j])->methods);
    }
    for (int j = 0; j < p->functions.n; j++) {
        Function *f = p->functions.v[j];
        if (f->public) {
            inferred_export(f);
        }
    }
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        if (d->public) {
            for (int k = 0; k < d->methods.n; k++) {
                Function *f = d->methods.v[k];
                if (!f->priv) {
                    inferred_export(f);
                }
            }
        }
    }
    List arguments = {0};
    for (int j = 0; j < p->entry->params.n; j++) {
        add(&arguments, parameter_type(p, p->entry->params.v[j]));
    }
    p->entry->required_result = type_builtin(p, T_VOID);
    instantiate(p, p->entry, NULL, &arguments, p->entry->loc);
    /* Abstract nominal parameters require a concrete call-site specialization. */
    for (int j = 0; j < p->functions.n; j++) {
        Function *f = p->functions.v[j];
        if (!f->templ && !inferred_parameter(f) && declaration_instance_needed(p, f)) {
            List types = {0};
            bool concrete = true;
            for (int k = 0; k < f->params.n; k++) {
                Param *a = f->params.v[k];
                Type *t = parameter_type(p, a);
                Type *base = t->kind == T_REF ? t->elem : t;
                if ((base->kind == T_STRUCT && base->def->abstract) || callable_type(base)) {
                    concrete = false;
                }
                add(&types, t);
            }
            if (concrete) {
                instantiate(p, f, NULL, &types, f->loc);
            }
        }
    }
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        if (d->abstract) {
            continue;
        }
        for (int k = 0; k < d->methods.n; k++) {
            Function *f = d->methods.v[k];
            if (f->templ || inferred_parameter(f) || !declaration_instance_needed(p, f)) {
                continue;
            }
            List types = {0};
            bool concrete = true;
            for (int n = 0; n < f->params.n; n++) {
                Param *a = f->params.v[n];
                Type *type = a->self ? type_wrap(p, T_REF, d->type, 0) : parameter_type(p, a);
                concrete &= !callable_type(type);
                add(&types, type);
            }
            if (concrete) {
                instantiate(p, f, d, &types, f->loc);
            }
        }
    }
    for (int j = 0; j < p->functions.n; j++) {
        inferred_unused(p, p->functions.v[j]);
    }
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        for (int k = 0; k < d->own_methods.n; k++) {
            inferred_unused(p, d->own_methods.v[k]);
        }
    }
    for (int j = 0; j < p->types.n; j++) {
        Type *t = p->types.v[j];
        if (t->kind == T_UNION) {
            layout(t, t->def->loc);
        }
    }
    /* fits^ dyn^ D needs D's table for each concrete type it may find; building
     * tables lowers methods, which may convert or cast again. */
    for (int built = -1; built != p->dyn_tables.n;) {
        built = p->dyn_tables.n;
        for (int j = 0; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            for (int k = 0; k < in->code.n; k++) {
                Instr *i = in->code.v[k];
                if (i->op != I_DYNCAST) {
                    continue;
                }
                Def *from = ((Slot *)in->slots.v[i->a])->type->elem->def;
                for (int n = 0; n < built; n++) {
                    DynTable *table = p->dyn_tables.v[n];
                    if (table->iface == from && derives(table->concrete, i->type->elem->def)) {
                        dyn_table(p, table->concrete, i->type->elem->def, i->loc);
                    }
                }
            }
        }
    }
    /* Inference may inspect a specialization whose arguments never return. */
    List materialized = {0};
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->state == 2) {
            in->id = materialized.n;
            add(&materialized, in);
        }
    }
    p->instances = materialized;
    /* A dyn^ call may reach the entry of every table built for its def. */
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        for (int k = 0; k < in->code.n; k++) {
            Instr *i = in->code.v[k];
            if (i->op != I_DYNCALL) {
                continue;
            }
            Slot *receiver = in->slots.v[(int)(intptr_t)i->args.v[0] - 1];
            for (int n = 0; n < p->dyn_tables.n; n++) {
                DynTable *table = p->dyn_tables.v[n];
                if (table->iface == receiver->type->elem->def && table->entries.v[i->label]) {
                    add(&i->targets, table->entries.v[i->label]);
                }
            }
        }
    }
}
