#include "rhat.h"

// A true bit proves logical end == retained stack top for this owner.
// Only construction establishes it. Shrinking through any alias invalidates
// every bit; joins intersect and loop back-edges must reach a fixed point.
// Borrowed pushes keep the maximum-update path unless the whole closed
// program is proven never to shrink any array.
typedef struct {
    bool seen;
    unsigned char *top;
} Growth;

static void merge(Growth *states, int target, unsigned char *top, int count, List *queue)
{
    Growth *s = &states[target];
    bool changed = !s->seen;
    if (!s->seen) {
        s->top = mem((size_t)count);
        memcpy(s->top, top, (size_t)count);
        s->seen = true;
    }
    else {
        for (int j = 0; j < count; j++) {
            unsigned char bit = s->top[j] & top[j];
            changed |= bit != s->top[j];
            s->top[j] = bit;
        }
    }
    if (changed) {
        add(queue, (void *)(intptr_t)(target + 1));
    }
}

static bool dyn_shrinks(Instr *i)
{
    bool any = false;
    for (int j = 0; i->op == I_DYNCALL && j < i->targets.n; j++) {
        any |= ((Instance *)i->targets.v[j])->may_shrink;
    }
    return any;
}

// A flex^ shrink lowers the stack top with the length, so its end and top
// stay equal; only grow^ shrinks retain a high-water mark above the end.
static bool shrinks(Instr *i)
{
    return ((i->op == I_POP || i->op == I_CLEAR || i->op == I_TRUNCATE) && !i->type->count) ||
           (i->op == I_CALL && i->callee->may_shrink) || dyn_shrinks(i);
}

void analyze_array_growth(Program *p)
{
    bool changed;
    // A conservative effect summary, also for mutual recursion. Local shrinks
    // count too: precision can improve later without changing correctness.
    do {
        changed = false;
        for (int j = 0; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            for (int k = 0; k < in->code.n; k++) {
                if (shrinks(in->code.v[k]) && !in->may_shrink) {
                    in->may_shrink = true;
                    changed = true;
                }
            }
        }
    } while (changed);
    // Every owner starts at its stack top. Without any shrink operation in
    // this closed program, construction/push/append and scope reuse preserve
    // logical end == top for borrowed owners as well. No external entry or
    // user-forged array descriptors exist. A single possible shrink disables
    // this global proof; the local CFG proof below still applies.
    bool never_shrinks = true;
    for (int j = 0; j < p->instances.n; j++) {
        if (((Instance *)p->instances.v[j])->may_shrink) {
            never_shrinks = false;
        }
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->fn->external) {
            continue;
        }
        int n = in->code.n, count = in->slots.n;
        Growth *states = mem((size_t)n * sizeof(Growth));
        int *labels = mem((size_t)(in->labels + 1) * sizeof(int));
        for (int k = 0; k < n; k++) {
            Instr *i = in->code.v[k];
            if (i->op == I_LABEL) {
                labels[i->label] = k;
            }
        }
        unsigned char *top = mem((size_t)count);
        List queue = {0};
        merge(states, 0, top, count, &queue);
        for (int q = 0; q < queue.n; q++) {
            int k = (int)(intptr_t)queue.v[q] - 1;
            Instr *i = in->code.v[k];
            memcpy(top, states[k].top, (size_t)count);
            if (shrinks(i)) {
                memset(top, 0, (size_t)count);
            }
            if (i->op == I_REGION) {
                top[((Slot *)in->slots.v[i->dst])->owner] = 1;
            }
            if (i->op == I_STACK_BEGIN || (i->op == I_CALL && i->type->owned)) {
                top[((Slot *)in->slots.v[i->dst])->owner] = 0;
            }
            if (i->op == I_RELEASE) {
                top[i->a] = 0;
            }
            if (i->op == I_RETURN || i->op == I_TERMINATE) {
                continue;
            }
            if (i->op == I_BR || i->op == I_CBR) {
                merge(states, labels[i->label], top, count, &queue);
                if (i->op == I_CBR) {
                    merge(states, labels[i->other], top, count, &queue);
                }
            }
            else {
                merge(states, k + 1, top, count, &queue);
            }
        }
        // Mark only after convergence: an initially clean loop can shrink an
        // owner on its back-edge, invalidating an earlier optimistic state.
        for (int k = 0; k < n; k++) {
            Instr *i = in->code.v[k];
            if (i->op == I_PUSH && states[k].seen) {
                i->push_at_top = never_shrinks || i->type->count ||
                                 states[k].top[((Slot *)in->slots.v[i->a])->owner] != 0;
            }
        }
    }
}
