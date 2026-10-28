#include "rhat.h"
#include <limits.h>

/* CFG states are lexical owner stacks, not reference roots. An initializer
 * acquires its destination before evaluating any field or callee. */
typedef struct {
  int n;
  int *owners;
  bool set;
} State;
static void join(State *states, int target, State *from, List *queue, Loc at) {
  State *to = &states[target];
  if (to->set) {
    if (to->n != from->n || (to->n && memcmp(to->owners, from->owners,
                                             (size_t)to->n * sizeof(int))))
      fail(at, "inconsistent data stack ownership at control-flow join");
    return;
  }
  to->set = true;
  to->n = from->n;
  to->owners = mem((size_t)(from->n + 1) * sizeof(int));
  if (from->n)
    memcpy(to->owners, from->owners, (size_t)from->n * sizeof(int));
  add(queue, (void *)(intptr_t)(target + 1));
}
static void local_plan(Instance *in) {
  int n = in->code.n;
  State *states = mem((size_t)(n + 1) * sizeof(State));
  int *labels = mem((size_t)(in->labels + 1) * sizeof(int));
  for (int k = 0; k < n; k++) {
    Instr *i = in->code.v[k];
    if (i->op == I_LABEL)
      labels[i->label] = k;
    if (i->op == I_FIELD || i->op == I_CASE_AT)
      ((Slot *)in->slots.v[i->dst])->owner = ((Slot *)in->slots.v[i->a])->owner;
  }
  /* A value built on another owner's data stack names that owner. */
  for (int k = 0; k < in->slots.n; k++) {
    Slot *s = in->slots.v[k];
    while (((Slot *)in->slots.v[s->owner])->owner != s->owner)
      s->owner = ((Slot *)in->slots.v[s->owner])->owner;
  }
  List queue = {0};
  State empty = {0};
  join(states, 0, &empty, &queue, in->fn->loc);
  for (int q = 0; q < queue.n; q++) {
    int k = (int)(intptr_t)queue.v[q] - 1;
    if (k == n)
      fail(in->fn->loc, "data stack analysis reached missing return");
    Instr *i = in->code.v[k];
    State state = states[k];
    int *owners = mem((size_t)(in->slots.n + 1) * sizeof(int));
    if (state.n)
      memcpy(owners, state.owners, (size_t)state.n * sizeof(int));
    state.owners = owners;
    if (i->op == I_STACK_BEGIN) {
      i->stack_index = state.n;
      owners[state.n++] = i->dst;
      if (state.n > in->stack_own)
        in->stack_own = state.n;
    } else if (i->op == I_RELEASE) {
      if (!state.n || owners[state.n - 1] != i->a)
        fail(i->loc, "data stack release does not match its owner");
      state.n--;
    } else if (i->op == I_CALL || i->op == I_DYNCALL) {
      i->stack_index = state.n;
      if (state.n) {
        for (int j = 0; j < n; j++) {
          Instr *begin = in->code.v[j];
          if (begin->op == I_STACK_BEGIN && begin->dst == owners[state.n - 1]) {
            i->text = begin->text;
            break;
          }
        }
      }
    }
    if (i->op == I_REGION || (i->op == I_CALL && i->type->owned)) {
      int owner = ((Slot *)in->slots.v[i->dst])->owner;
      bool found = owner == in->result_slot && in->result->owned;
      for (int j = 0; j < state.n; j++)
        found |= owners[j] == owner;
      if (!found)
        fail(i->loc,
             "owned construction has no reserved data stack destination");
    }
    if (i->op == I_RETURN) {
      if (state.n)
        fail(i->loc, "return leaves a local data stack owner live");
      continue;
    }
    if (i->op == I_TERMINATE) {
      continue; /* The process exits; owners are not released. */
    }
    if (i->op == I_BR || i->op == I_CBR) {
      join(states, labels[i->label], &state, &queue, i->loc);
      if (i->op == I_CBR)
        join(states, labels[i->other], &state, &queue, i->loc);
    } else
      join(states, k + 1, &state, &queue, i->loc);
  }
}
/* The instances a call may enter: its callee, or every dyn^ table entry. */
static List callees(Instr *i)
{
  List out = {0};
  if (i->op == I_CALL) {
    add(&out, i->callee);
  }
  else if (i->op == I_DYNCALL) {
    out = i->targets;
  }
  return out;
}
/* Raise each instance's stack need to cover its calls, to a fixed point. */
static void propagate(Program *p)
{
  int n = p->instances.n;
  bool changed;
  do {
    changed = false;
    for (int a = 0; a < n; a++) {
      Instance *in = p->instances.v[a];
      for (int k = 0; k < in->code.n; k++) {
        Instr *i = in->code.v[k];
        List targets = callees(i);
        for (int t = 0; t < targets.n; t++) {
          Instance *callee = targets.v[t];
          if (callee->stack_need > INT_MAX - i->stack_index)
            fail(i->loc, "data stack count overflows");
          int need = i->stack_index + callee->stack_need;
          if (need > in->stack_need) {
            in->stack_need = need;
            changed = true;
          }
        }
      }
    }
  } while (changed);
}
void plan_stacks(Program *p) {
  int n = p->instances.n;
  /* Reachability identifies recursive SCC membership. Specializations have
   * already been materialized, including methods and template instances. */
  unsigned char *reach = mem((size_t)n * (size_t)n);
  for (int a = 0; a < n; a++) {
    Instance *in = p->instances.v[a];
    if (!in->fn->external) {
      local_plan(in);
    }
    in->stack_need = in->stack_own;
    for (int k = 0; k < in->code.n; k++) {
      List targets = callees(in->code.v[k]);
      for (int t = 0; t < targets.n; t++) {
        reach[a * n + ((Instance *)targets.v[t])->id] = 1;
      }
    }
  }
  for (int k = 0; k < n; k++)
    for (int a = 0; a < n; a++)
      if (reach[a * n + k])
        for (int b = 0; b < n; b++)
          reach[a * n + b] |= reach[k * n + b];
  for (int a = 0; a < n; a++) {
    Instance *in = p->instances.v[a];
    for (int k = 0; k < in->code.n; k++) {
      Instr *i = in->code.v[k];
      List targets = callees(i);
      for (int t = 0; t < targets.n && i->stack_index; t++) {
        Instance *callee = targets.v[t];
        if (reach[callee->id * n + a]) {
          const char *name = i->text ? i->text : "local owned value";
          fail(i->loc,
               "recursive call to '%s' holds local owned value '%s' on a data "
               "stack; end its scope first or borrow storage owned outside the "
               "cycle",
               callee->fn->name, name);
        }
      }
    }
  }
  propagate(p);
  /* An exportC^ entered from C starts above the stacks the interrupted R^
   * call keeps, so every C call is planned to admit one level of callback;
   * deeper nesting is checked when an entry starts. */
  int callback = 0;
  for (int a = 0; a < n; a++) {
    Instance *in = p->instances.v[a];
    if (in->fn->exported || in->fn->lhat_export) {
      p->callbacks = true;
      in->callback_need = in->stack_need;
      callback = in->stack_need > callback ? in->stack_need : callback;
    }
  }
  for (int a = 0; callback && a < n; a++) {
    Instance *in = p->instances.v[a];
    if (in->fn->external) {
      in->stack_need = callback;
    }
  }
  if (callback) {
    propagate(p);
  }
  p->stack_count = ((Instance *)p->instances.v[0])->stack_need;
  /* Stacks are reserved address space, so admit at least eight levels of
   * callbacks that each hold owned values across their C call. */
  if (callback > (INT_MAX - p->stack_count) / 7) {
    fail(((Instance *)p->instances.v[0])->fn->loc, "data stack count overflows");
  }
  p->stack_count += 7 * callback;
}
void report_stacks(Program *p) {
  printf("data stacks: %d; 1 GiB usable + 1 MiB guard per stack\n",
         p->stack_count);
  for (int j = 0; j < p->instances.n; j++) {
    Instance *in = p->instances.v[j];
    printf("%s (%s): %d own, %d with callees\n", in->fn->name, in->symbol,
           in->stack_own, in->stack_need);
  }
}
