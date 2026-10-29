#include "rhat.h"

// Version straight-line i64 loops with a unit positive step. A runtime guard
// proves every index expression over the whole induction interval, including
// absence of signed wrap. Failure falls back to the unmodified checked loop.
// Calls, address escapes, container mutations and irregular control flow are
// deliberately excluded. Stack/borrow analysis has already finished.
typedef struct {
    Instance *in;
    int first, last, induction, budget;
    int *written;
    unsigned char *addressed;
} Proof;

static Type *type(Proof *p, int slot)
{
    return ((Slot *)p->in->slots.v[slot])->type;
}

static bool alias(Proof *p, int slot)
{
    return ((Slot *)p->in->slots.v[slot])->alias;
}

static bool defines(Instr *i)
{
    switch (i->op) {
    case I_CONST:
    case I_COPY:
    case I_ZERO:
    case I_BIN:
    case I_UNARY:
    case I_ADDR:
    case I_DEREF:
    case I_FIELD:
    case I_INDEX:
    case I_LENGTH:
    case I_CONVERT:
        return i->dst >= 0;
    default:
        return false;
    }
}

static Instr *definition(Proof *p, int slot, int before, int *at)
{
    for (int j = before - 1; j >= p->first; j--) {
        Instr *i = p->in->code.v[j];
        if (defines(i) && i->dst == slot) {
            *at = j;
            return i;
        }
    }
    return NULL;
}

static RangeExpr *node(int kind, int slot)
{
    RangeExpr *e = mem(sizeof(RangeExpr));
    e->kind = kind;
    e->slot = slot;
    return e;
}

static bool container(Type *t)
{
    return t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_SLICE || t->kind == T_STRING;
}

static RangeExpr *array(Proof *p, int slot, int before)
{
    Type *t = type(p, slot);
    if (!container(t)) {
        return NULL;
    }
    RangeExpr *e = node(RANGE_LENGTH, slot);
    e->type = t;
    if (!alias(p, slot)) {
        return p->written[slot] ? NULL : e;
    }
    // Container dereferences often appear in the body. Read their stable
    // reference binding in the guard, before the place itself is defined.
    for (int j = before - 1; j >= 0; j--) {
        Instr *i = p->in->code.v[j];
        if (i->dst != slot || !defines(i)) {
            continue;
        }
        if (i->op != I_DEREF || alias(p, i->a) || p->written[i->a] ||
            type(p, i->a)->kind != T_REF) {
            return NULL;
        }
        e->slot = i->a;
        e->indirect = true;
        return e;
    }
    return NULL;
}

static RangeExpr *expression(Proof *p, int slot, int before, int depth)
{
    if (depth > 48 || --p->budget < 0 || alias(p, slot) || type(p, slot)->kind != T_I64) {
        return NULL;
    }
    if (slot == p->induction) {
        return node(RANGE_IV, slot);
    }
    int at = 0;
    Instr *i = definition(p, slot, before, &at);
    if (!i) {
        if (p->written[slot] || p->addressed[slot]) {
            return NULL;
        }
        return node(RANGE_SLOT, slot);
    }
    if (i->op == I_COPY) {
        return expression(p, i->a, at, depth + 1);
    }
    if (i->op == I_CONST) {
        RangeExpr *e = node(RANGE_CONST, -1);
        e->text = i->text;
        return e;
    }
    if (i->op == I_LENGTH) {
        return array(p, i->a, at);
    }
    if (i->op != I_BIN || (strcmp(i->text, "+") && strcmp(i->text, "-") && strcmp(i->text, "*"))) {
        return NULL;
    }
    RangeExpr *e = node(!strcmp(i->text, "+")   ? RANGE_ADD
                        : !strcmp(i->text, "-") ? RANGE_SUB
                                                : RANGE_MUL,
                        -1);
    e->a = expression(p, i->a, at, depth + 1);
    e->b = expression(p, i->b, at, depth + 1);
    return e->a && e->b ? e : NULL;
}

static bool uses_iv(RangeExpr *e)
{
    return e->kind == RANGE_IV || (e->a && uses_iv(e->a)) || (e->b && uses_iv(e->b));
}

static int copies(Proof *p, int slot, int before)
{
    int at = 0;
    Instr *i = definition(p, slot, before, &at);
    return i && i->op == I_COPY ? copies(p, i->a, at) : slot;
}

static bool element_place(Proof *p, int slot, int before)
{
    int at = 0;
    Instr *i = definition(p, slot, before, &at);
    if (!i) {
        return false;
    }
    if (i->op == I_FIELD) {
        return element_place(p, i->a, at);
    }
    return i->op == I_INDEX && !type(p, slot)->refs && !type(p, slot)->owned;
}

static Instr *instruction(int op, Loc loc)
{
    Instr *i = mem(sizeof(Instr));
    i->op = op;
    i->loc = loc;
    i->dst = i->a = i->b = -1;
    return i;
}

static int newslot(Instance *in, Slot *original)
{
    Slot *s = mem(sizeof(Slot));
    *s = *original;
    add(&in->slots, s);
    return in->slots.n - 1;
}

static void clone_loop(Instance *in, List *out, int first, int last, int exit, int entry,
                       bool proven, int old_labels, RangePlan *plan)
{
    int *labels = mem((size_t)(old_labels + 1) * sizeof(int));
    int *slots = mem((size_t)in->slots.n * sizeof(int));
    for (int k = 0; k < in->slots.n; k++) {
        slots[k] = k;
    }
    int index = 0;
    for (int k = first; k <= last; k++) {
        Instr *i = in->code.v[k];
        if (i->op == I_LABEL) {
            labels[i->label] = i->label == entry ? entry : ++in->labels;
        }
        if (defines(i) && ((Slot *)in->slots.v[i->dst])->alias) {
            slots[i->dst] = newslot(in, in->slots.v[i->dst]);
        }
    }
    labels[exit] = exit;
    for (int k = first; k <= last; k++) {
        Instr *original = in->code.v[k];
        Instr *i = mem(sizeof(Instr));
        *i = *original;
        if (i->dst >= 0) {
            i->dst = slots[i->dst];
        }
        if (i->a >= 0) {
            i->a = slots[i->a];
        }
        if (i->b >= 0) {
            i->b = slots[i->b];
        }
        if (i->op == I_LABEL || i->op == I_BR || i->op == I_CBR) {
            i->label = labels[i->label];
        }
        if (i->op == I_CBR) {
            i->other = labels[i->other];
        }
        if (proven && i->op == I_BIN && i->dst == plan->comparison) {
            i->b = plan->limit;
        }
        if (i->op == I_INDEX && i->b >= 0) {
            i->index_proven = proven;
            if (proven) {
                i->index_data = (int)(intptr_t)plan->data.v[index++] - 1;
            }
        }
        add(out, i);
    }
}

static bool version(Program *program, Instance *in, int first, int branch, int last)
{
    Instr *br = in->code.v[branch];
    Proof p = {in, first, last, -1, 4096, NULL, NULL};
    p.written = mem((size_t)in->slots.n * sizeof(int));
    p.addressed = mem((size_t)in->slots.n);
    for (int k = 0; k < in->code.n; k++) {
        Instr *i = in->code.v[k];
        if (i->op == I_ADDR) {
            p.addressed[i->a] = 1;
        }
    }
    for (int k = branch + 2; k < last; k++) {
        Instr *i = in->code.v[k];
        if (!defines(i)) {
            return false;
        }
        if (i->op == I_ADDR || i->op == I_ZERO ||
            (i->op == I_DEREF && !container(type(&p, i->dst)))) {
            return false;
        }
        if (i->op == I_COPY && alias(&p, i->dst) && !element_place(&p, i->dst, k)) {
            return false;
        }
        if (i->op == I_COPY && (type(&p, i->dst)->refs || type(&p, i->dst)->owned)) {
            return false;
        }
        if (!alias(&p, i->dst)) {
            p.written[i->dst]++;
        }
    }
    int at = 0;
    Instr *comparison = definition(&p, br->a, branch, &at);
    if (!comparison || comparison->op != I_BIN || strcmp(comparison->text, "<")) {
        return false;
    }
    p.induction = copies(&p, comparison->a, at);
    if (type(&p, p.induction)->kind != T_I64 || alias(&p, p.induction) ||
        p.addressed[p.induction] || p.written[p.induction] != 1) {
        return false;
    }
    int step_at = 0;
    Instr *step = definition(&p, p.induction, last, &step_at);
    if (!step || step->op != I_COPY) {
        return false;
    }
    RangeExpr *increment = expression(&p, step->a, step_at, 0);
    if (!increment || increment->kind != RANGE_ADD || increment->a->kind != RANGE_IV ||
        increment->b->kind != RANGE_CONST || strcmp(increment->b->text, "1")) {
        return false;
    }
    RangePlan *plan = mem(sizeof(RangePlan));
    plan->induction = p.induction;
    plan->comparison = comparison->dst;
    plan->upper = expression(&p, comparison->b, at, 0);
    if (!plan->upper || uses_iv(plan->upper)) {
        return false;
    }
    for (int k = first + 1; k < branch; k++) {
        Instr *i = in->code.v[k];
        // Header evaluation is duplicated. Admit only pure index arithmetic.
        if (i->op != I_COPY && i->op != I_CONST && i->op != I_BIN && i->op != I_LENGTH &&
            !(i->op == I_DEREF && container(type(&p, i->dst)))) {
            return false;
        }
        if (i->op == I_BIN && strcmp(i->text, "<") && strcmp(i->text, "+") &&
            strcmp(i->text, "-") && strcmp(i->text, "*")) {
            return false;
        }
        if ((alias(&p, i->dst) && i->op != I_DEREF) || p.written[i->dst]) {
            return false;
        }
    }
    for (int k = branch + 2; k < last; k++) {
        Instr *i = in->code.v[k];
        if (i->op != I_INDEX || i->b < 0) {
            continue;
        }
        if (k >= step_at) {
            return false;
        }
        RangeExpr *index = expression(&p, i->b, k, 0);
        RangeExpr *arr = array(&p, i->a, k);
        if (!index || !arr) {
            return false;
        }
        add(&plan->indices, index);
        add(&plan->arrays, arr);
    }
    if (!plan->indices.n) {
        return false;
    }
    Slot limit = {type_builtin(program, T_I64), false, -1};
    plan->limit = newslot(in, &limit);
    for (int k = 0; k < plan->arrays.n; k++) {
        RangeExpr *arr = plan->arrays.v[k];
        Type *elem = arr->type->kind == T_STRING ? type_builtin(program, T_U8) : arr->type->elem;
        Slot data = {type_wrap(program, T_REF, elem, 0), false, -1};
        add(&plan->data, (void *)(intptr_t)(newslot(in, &data) + 1));
    }
    int slow = ++in->labels, fast = ++in->labels, dispatch = ++in->labels;
    Slot flag = {type_builtin(program, T_BOOL), false, -1};
    int condition = newslot(in, &flag);
    List out = {0};
    for (int k = 0; k <= branch; k++) {
        Instr *i = in->code.v[k];
        if (k == branch) {
            Instr *copy = mem(sizeof(Instr));
            *copy = *i;
            copy->label = dispatch;
            i = copy;
        }
        add(&out, i);
    }
    Instr *i = instruction(I_LABEL, br->loc);
    i->label = dispatch;
    add(&out, i);
    i = instruction(I_RANGE_GUARD, br->loc);
    i->dst = condition;
    i->type = flag.type;
    i->range = plan;
    add(&out, i);
    i = instruction(I_CBR, br->loc);
    i->a = condition;
    i->label = fast;
    i->other = slow;
    add(&out, i);
    // Remap the old header label to each clone's new entry label.
    int original_entry = ((Instr *)in->code.v[first])->label;
    Instr *header = in->code.v[first];
    Instr *back = in->code.v[last];
    header->label = slow;
    back->label = slow;
    clone_loop(in, &out, first, last, br->other, slow, false, in->labels, plan);
    header->label = fast;
    back->label = fast;
    clone_loop(in, &out, first, last, br->other, fast, true, in->labels, plan);
    header->label = original_entry;
    back->label = original_entry;
    // The entry header was copied by pointer above; preserve its old label.
    for (int k = last + 1; k < in->code.n; k++) {
        add(&out, in->code.v[k]);
    }
    in->code = out;
    return true;
}

void version_range_loops(Program *p)
{
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        // Work backwards, so inner canonical loops are considered first. A
        // versioned region is no longer straight-line and cannot be reversioned.
        for (int last = in->code.n - 1; last >= 0; last--) {
            Instr *back = in->code.v[last];
            if (back->op != I_BR) {
                continue;
            }
            int first = last - 1;
            while (first >= 0) {
                Instr *i = in->code.v[first];
                if (i->op == I_LABEL && i->label == back->label) {
                    break;
                }
                first--;
            }
            if (first < 0) {
                continue;
            }
            int branch = first + 1;
            while (branch < last && ((Instr *)in->code.v[branch])->op != I_CBR &&
                   ((Instr *)in->code.v[branch])->op != I_LABEL) {
                branch++;
            }
            if (branch + 2 >= last) {
                continue;
            }
            Instr *br = in->code.v[branch], *body = in->code.v[branch + 1];
            if (br->op != I_CBR || body->op != I_LABEL || body->label != br->label) {
                continue;
            }
            if (version(p, in, first, branch, last)) {
                last = first;
            }
        }
    }
}
