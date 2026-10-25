#include "rhat.h"
#include <errno.h>
#include <limits.h>

static Def *lookup_def(Program *p, const char *n) {
  for (int i = 0; i < p->defs.n; i++) {
    Def *d = p->defs.v[i];
    if (!strcmp(d->name, n))
      return d;
  }
  return NULL;
}
Type *type_builtin(Program *p, int k) {
  for (int i = 0; i < p->types.n; i++) {
    Type *t = p->types.v[i];
    if (t->kind == k && k < T_STRUCT)
      return t;
  }
  Type *t = mem(sizeof(*t));
  t->kind = k;
  t->id = p->types.n;
  t->align = 1;
  switch (k) {
  case T_BOOL:
  case T_NIL:
    t->size = 1;
    break;
  case T_STRING:
    t->size = 16;
    t->align = 8;
    t->refs = true;
    break;
  default:
    if (kind_integer(k) || kind_float(k))
      t->size = t->align = (size_t)numeric_bits(k) / 8;
    break;
  }
  add(&p->types, t);
  return t;
}
Type *type_wrap(Program *p, int k, Type *e, int n) {
  for (int i = 0; i < p->types.n; i++) {
    Type *t = p->types.v[i];
    if (t->kind == k && t->elem == e && t->count == n)
      return t;
  }
  Type *t = mem(sizeof(*t));
  t->kind = k;
  t->elem = e;
  t->count = n;
  t->id = p->types.n;
  if (k == T_REF) {
    t->size = t->align = 8;
    t->refs = true;
  } else if (k == T_SLICE) {
    t->size = 16;
    t->align = 8;
    t->refs = true;
  } else if (k == T_GROW) {
    t->size = 24;
    t->align = 8;
    t->owned = true;
    t->refs = e->refs;
  }
  add(&p->types, t);
  return t;
}
static void union_arms(List *arms, Type *t)
{
  if (t->kind == T_UNION && t->structural) {
    for (int j = 0; j < t->def->alternatives.n; j++) {
      union_arms(arms, t->def->alternatives.v[j]);
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
static int union_order(Type *a, Type *b)
{
  int ak = a->kind == T_NIL ? INT_MAX : a->kind;
  int bk = b->kind == T_NIL ? INT_MAX : b->kind;
  if (ak != bk) {
    return ak < bk ? -1 : 1;
  }
  if (a->structural != b->structural) {
    return a->structural ? 1 : -1;
  }
  return strcmp(type_name(a), type_name(b));
}
static bool nullable(Type *t);
static Type *type_union(Program *p, Type *a, Type *b, Loc loc)
{
  if (a->kind == T_NIL && nullable(b)) {
    return b;
  }
  if (b->kind == T_NIL && nullable(a)) {
    return a;
  }
  List arms = {0};
  union_arms(&arms, a);
  union_arms(&arms, b);
  if (arms.n == 1) {
    return arms.v[0];
  }
  for (int j = 1; j < arms.n; j++) {
    Type *t = arms.v[j];
    int k = j;
    while (k && union_order(t, arms.v[k - 1]) < 0) {
      arms.v[k] = arms.v[k - 1];
      k--;
    }
    arms.v[k] = t;
  }
  for (int j = 0; j < p->types.n; j++) {
    Type *t = p->types.v[j];
    if (!t->structural || t->def->alternatives.n != arms.n) {
      continue;
    }
    bool same = true;
    for (int k = 0; k < arms.n; k++) {
      same &= arms.v[k] == t->def->alternatives.v[k];
    }
    if (same) {
      return t;
    }
  }
  Type *t = mem(sizeof(*t));
  Def *d = mem(sizeof(*d));
  t->kind = T_UNION;
  t->structural = true;
  t->id = p->types.n;
  t->def = d;
  d->type = t;
  d->loc = loc;
  d->state = 2;
  d->alternatives = arms;
  char *identity = str("union");
  d->name = str("");
  for (int j = 0; j < arms.n; j++) {
    Type *arm = arms.v[j];
    if (arm->kind == T_VOID) {
      fail(loc, "void cannot be a union alternative");
    }
    char *name = type_name(arm);
    identity = fmt("%s:%d:%d:%zu:%s", identity, arm->kind, arm->structural,
                   strlen(name), name);
    d->name = fmt("%s%s%s", d->name, j ? " | " : "", name);
  }
  t->symbol = fmt("%%rh_sum_%s", link_identity(identity));
  add(&p->types, t);
  return t;
}
Type *type_spec(Program *p, Spec *s) {
  if (!s)
    return type_builtin(p, T_VOID);
  if (s->kind == SPEC_UNION) {
    return type_union(p, type_spec(p, s->elem), type_spec(p, s->other), s->loc);
  }
  if (s->kind != SPEC_NAME)
    return type_wrap(p,
                     s->kind == SPEC_REF     ? T_REF
                     : s->kind == SPEC_GROW  ? T_GROW
                     : s->kind == SPEC_SLICE ? T_SLICE
                                             : T_ARRAY,
                     type_spec(p, s->elem), s->count);
  for (int k = 0; k < T_STRUCT; k++)
    if (!strcmp(s->name, builtin_name(k)))
      return type_builtin(p, k);
  if (!strcmp(s->name, "f128^"))
    fail(s->loc, "f128^ is pending; binary128 is not implemented");
  Def *d = lookup_def(p, s->name);
  if (!d)
    fail(s->loc, "unknown type '%s'", s->name);
  return d->type;
}
char *type_name(Type *t) {
  if (t->kind < T_STRUCT)
    return (char *)builtin_name(t->kind);
  if (t->kind == T_STRUCT || t->kind == T_UNION)
    return t->def->name;
  if (t->kind == T_REF)
    return fmt(t->elem->kind == T_UNION ? "&(%s)" : "&%s", type_name(t->elem));
  if (t->kind == T_SLICE)
    return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION) ? "(%s)[:]"
                                                                    : "%s[:]",
               type_name(t->elem));
  if (t->kind == T_GROW)
    return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION)
                   ? "(%s)[>..]"
                   : "%s[>..]",
               type_name(t->elem));
  return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION) ? "(%s)[%d]"
                                                                  : "%s[%d]",
             type_name(t->elem), t->count);
}
bool derives(Def *d, Def *b) {
  if (d == b)
    return true;
  for (int i = 0; i < d->bases.n; i++)
    if (derives(d->bases.v[i], b))
      return true;
  return false;
}
static int union_alternative(Type *sum, Type *arm) {
  if (sum->kind == T_UNION)
    for (int i = 0; i < sum->def->alternatives.n; i++)
      if (sum->def->alternatives.v[i] == arm)
        return i;
  return -1;
}
static bool sum_accepts(Type *to, Type *from)
{
  if (to == from || union_alternative(to, from) >= 0) {
    return true;
  }
  if (to->kind != T_UNION || from->kind != T_UNION ||
      (!to->structural && !from->structural)) {
    return false;
  }
  for (int j = 0; j < from->def->alternatives.n; j++) {
    if (union_alternative(to, from->def->alternatives.v[j]) < 0) {
      return false;
    }
  }
  return true;
}
static Type *nil_subject(Type *t)
{
  return t->kind == T_REF && t->elem->kind == T_UNION ? t->elem : t;
}
static bool nullable(Type *t)
{
  if (t->kind == T_NIL) {
    return true;
  }
  if (t->kind == T_UNION) {
    for (int j = 0; j < t->def->alternatives.n; j++) {
      if (((Type *)t->def->alternatives.v[j])->kind == T_NIL) {
        return true;
      }
    }
  }
  return false;
}
static Type *without_nil(Program *p, Type *t, Loc loc)
{
  if (t->kind == T_NIL) {
    return NULL;
  }
  if (t->kind != T_UNION || union_alternative(t, type_builtin(p, T_NIL)) < 0) {
    return t;
  }
  Type *out = NULL;
  for (int j = 0; j < t->def->alternatives.n; j++) {
    Type *arm = t->def->alternatives.v[j];
    if (arm->kind != T_NIL) {
      out = out ? type_union(p, out, arm, loc) : arm;
    }
  }
  return out;
}
static Field *field(Def *d, const char *n) {
  for (int i = 0; i < d->fields.n; i++) {
    Field *f = d->fields.v[i];
    if (!strcmp(f->name, n))
      return f;
  }
  return NULL;
}
static Function *method(Def *d, const char *n) {
  for (int i = 0; i < d->methods.n; i++) {
    Function *f = d->methods.v[i];
    if (!strcmp(f->name, n))
      return f;
  }
  return NULL;
}
static void compatible(Program *p, Function *a, Function *b) {
  Type *ar = a->result ? type_spec(p, a->result) : a->required_result;
  Type *br = b->result ? type_spec(p, b->result) : b->required_result;
  if (a->abstract && !ar)
    ar = type_builtin(p, T_VOID);
  if (b->abstract && !br)
    br = type_builtin(p, T_VOID);
  if (a->params.n != b->params.n || a->procedure != b->procedure ||
      a->mutable_self != b->mutable_self || a->templ != b->templ ||
      (ar && br && ar != br))
    fail(b->loc, "override of '%s' changes its contract", a->name);
  if (ar && !br)
    b->required_result = ar;
  if (br && !ar)
    a->required_result = br;
  for (int i = 0; i < a->params.n; i++) {
    Param *x = a->params.v[i], *y = b->params.v[i];
    if (x->self != y->self || ((x->spec || y->spec) &&
                               type_spec(p, x->spec) != type_spec(p, y->spec)))
      fail(b->loc, "override of '%s' changes parameter types", a->name);
  }
  if (!a->priv && b->priv)
    fail(b->loc, "override cannot make a public method private");
}
static void compose(Program *p, Def *d) {
  if (d->state == 2)
    return;
  if (d->state == 1)
    fail(d->loc, "cyclic def^ composition at '%s'", d->name);
  d->state = 1;
  if (d->union_specs.n) {
    for (int i = 0; i < d->union_specs.n; i++) {
      Spec *alternative = d->union_specs.v[i];
      Def *arm = alternative->kind == SPEC_NAME
                     ? lookup_def(p, alternative->name)
                     : NULL;
      if (arm) {
        compose(p, arm);
        if (arm->abstract) {
          fail(d->loc, "union alternative '%s' must be concrete", arm->name);
        }
      }
      if (alternative->kind == SPEC_NAME && !arm) {
        bool builtin = false;
        for (int k = 0; k < T_STRUCT; k++) {
          builtin |= !strcmp(alternative->name, builtin_name(k));
        }
        if (!builtin) {
          fail(d->loc, "unknown union alternative '%s'", alternative->name);
        }
      }
      Type *at = type_spec(p, alternative);
      char *name = type_name(at);
      if (at->kind == T_VOID) {
        fail(d->loc, "void cannot be a union alternative");
      }
      for (int j = 0; j < d->alternatives.n; j++)
        if (d->alternatives.v[j] == at)
          fail(d->loc, "duplicate union alternative '%s'", name);
      add(&d->alternatives, at);
    }
    d->state = 2;
    return;
  }
  for (int i = 0; i < d->base_names.n; i++) {
    char *n = d->base_names.v[i];
    Def *b = lookup_def(p, n);
    if (!b)
      fail(d->loc, "unknown composition base '%s'", n);
    compose(p, b);
    if (b->type->kind != T_STRUCT)
      fail(d->loc, "composition base must be a structure, found union '%s'",
           b->name);
    add(&d->bases, b);
    for (int j = 0; j < b->fields.n; j++) {
      Field *f = b->fields.v[j], *old = field(d, f->name);
      if (old) {
        if (old->type == f->type && !old->priv && !f->priv &&
            (old->abstract || f->abstract)) {
          if (old->abstract && !f->abstract) {
            old->abstract = false;
            old->init = f->init;
            old->initializer_owner = f->initializer_owner;
          }
        } else if (old->owner != f->owner || old->init != f->init ||
                   old->abstract != f->abstract)
          fail(d->loc,
               "conflicting inherited field '%s'; use explicit named component "
               "fields",
               f->name);
      } else {
        Field *c = mem(sizeof(*c));
        *c = *f;
        c->index = d->fields.n;
        add(&d->fields, c);
      }
    }
    for (int j = 0; j < b->methods.n; j++) {
      Function *m = b->methods.v[j], *old = method(d, m->name);
      if (old && old != m) {
        if (old->abstract || m->abstract) {
          compatible(p, old->abstract ? old : m, old->abstract ? m : old);
          if (old->abstract && !m->abstract)
            for (int k = 0; k < d->methods.n; k++)
              if (d->methods.v[k] == old)
                d->methods.v[k] = m;
          continue;
        }
        bool resolved = false;
        for (int k = 0; k < d->own_methods.n; k++) {
          Function *own = d->own_methods.v[k];
          if (!strcmp(own->name, m->name)) {
            compatible(p, old, own);
            compatible(p, m, own);
            resolved = true;
            break;
          }
        }
        if (!resolved)
          fail(d->loc,
               "inherited method '%s' is ambiguous; provide an explicit "
               "override",
               m->name);
      } else if (!old)
        add(&d->methods, m);
    }
  }
  for (int i = 0; i < d->own_fields.n; i++) {
    Field *f = d->own_fields.v[i];
    for (int j = 0; j < i; j++)
      if (!strcmp(f->name, ((Field *)d->own_fields.v[j])->name))
        fail(f->loc, "duplicate field '%s'", f->name);
    f->type = type_spec(p, f->spec);
    if (f->abstract && f->priv)
      fail(f->loc,
           "private abstract fields cannot be implemented by another def^");
    if (f->type->kind == T_VOID)
      fail(f->loc, "field cannot have void type");
    Field *old = field(d, f->name);
    if (old) {
      if (!old->abstract || f->abstract || old->priv || old->type != f->type ||
          old->priv != f->priv)
        fail(f->loc, "field '%s' cannot replace its inherited slot", f->name);
      old->abstract = false;
      old->init = f->init;
      old->initializer_owner = f->initializer_owner;
    } else {
      f->index = d->fields.n;
      add(&d->fields, f);
    }
  }
  for (int i = 0; i < d->own_methods.n; i++) {
    Function *f = d->own_methods.v[i];
    for (int j = 0; j < i; j++)
      if (!strcmp(f->name, ((Function *)d->own_methods.v[j])->name))
        fail(f->loc, "duplicate method '%s'", f->name);
    if (field(d, f->name))
      fail(f->loc, "field/method name collision '%s'", f->name);
    if (!f->params.n || !((Param *)f->params.v[0])->self)
      fail(f->loc, "methods must start with self^ in this compiler");
    if (f->abstract && f->priv)
      fail(f->loc,
           "private abstract methods cannot be implemented by another def^");
    Function *old = method(d, f->name);
    if (old) {
      compatible(p, old, f);
      if (!old->abstract && !f->override)
        fail(f->loc, "replacing concrete method '%s' requires override^",
             f->name);
      for (int j = 0; j < d->methods.n; j++)
        if (d->methods.v[j] == old)
          d->methods.v[j] = f;
    } else {
      if (f->override)
        fail(f->loc, "override^ has no inherited method '%s'", f->name);
      add(&d->methods, f);
    }
  }
  for (int i = 0; i < d->fields.n; i++)
    if (((Field *)d->fields.v[i])->abstract)
      d->abstract = true;
  for (int i = 0; i < d->methods.n; i++)
    if (field(d, ((Function *)d->methods.v[i])->name))
      fail(d->loc, "inherited field/method name collision '%s'",
           ((Function *)d->methods.v[i])->name);
    else if (((Function *)d->methods.v[i])->abstract)
      d->abstract = true;
  d->state = 2;
}
static void layout(Type *t, Loc l) {
  if (t->kind == T_GROW) {
    layout(t->elem, l);
    if (t->elem->owned || !t->elem->size)
      fail(l, "growable elements must be nonempty fixed-size values");
    t->refs = t->elem->refs;
    return;
  }
  if (t->kind < T_STRUCT || t->kind == T_REF)
    return;
  if (t->size == SIZE_MAX)
    fail(l, "recursive by-value layout in '%s'", type_name(t));
  if (t->align)
    return;
  t->size = SIZE_MAX;
  size_t size = 0, align = 1;
  if (t->kind == T_UNION) {
    for (int j = 0; j < t->def->alternatives.n; j++) {
      Type *arm = t->def->alternatives.v[j];
      layout(arm, l);
      if (arm->kind == T_STRUCT && arm->def->abstract) {
        fail(l, "union alternative '%s' must be concrete", type_name(arm));
      }
      if (arm->owned)
        fail(l, "union alternatives must be non-owning fixed-size values");
      if (arm->size > size)
        size = arm->size;
      if (arm->align > align)
        align = arm->align;
      t->refs |= arm->refs;
    }
    t->payload_align = align;
    t->payload_bytes = size;
    t->payload_offset = (4 + align - 1) / align * align;
    if (size > SIZE_MAX - t->payload_offset)
      fail(l, "union layout overflows");
    size += t->payload_offset;
    if (align < 4)
      align = 4;
  } else if (t->kind == T_ARRAY) {
    layout(t->elem, l);
    if (t->elem->owned || !t->elem->size)
      fail(l, "array elements must be nonempty fixed-size values");
    if (t->elem->size > SIZE_MAX / (size_t)t->count)
      fail(l, "array layout overflows");
    size = t->elem->size * (size_t)t->count;
    align = t->elem->align;
    t->refs = t->elem->refs;
  } else {
    for (int i = 0; i < t->def->fields.n; i++) {
      Field *f = t->def->fields.v[i];
      layout(f->type, f->loc);
      if (f->type->owned && i != t->def->fields.n - 1)
        fail(f->loc, "a growable field must be the final field");
      if (size > SIZE_MAX - (f->type->align - 1))
        fail(f->loc, "structure layout overflows");
      size = (size + f->type->align - 1) / f->type->align * f->type->align;
      if (f->type->size > SIZE_MAX - size)
        fail(f->loc, "structure layout overflows");
      size += f->type->size;
      if (f->type->align > align)
        align = f->type->align;
      t->refs |= f->type->refs;
      t->owned |= f->type->owned;
    }
  }
  t->align = align;
  if (size > SIZE_MAX - (align - 1))
    fail(l, "type layout overflows");
  t->size = (size + align - 1) / align * align;
  if (!t->size)
    t->size = 1;
}

static bool guarded_chain(Expr *e)
{
  return e &&
         (e->kind == E_MEMBER || e->kind == E_INDEX || e->kind == E_CALL) &&
         (e->optional || guarded_chain(e->a));
}
/* A method name is not a first-class value; keep it with its call. */
static Expr *chain_parts(Expr *e, List *steps)
{
  if (e->kind == E_MEMBER || e->kind == E_INDEX ||
      (e->kind == E_CALL && e->a->kind == E_MEMBER)) {
    Expr *base = chain_parts(e->kind == E_CALL ? e->a->a : e->a, steps);
    add(steps, e);
    return base;
  }
  return e;
}
static bool step_optional(Expr *e)
{
  return e->kind == E_CALL ? e->a->optional : e->optional;
}
static Expr step_expression(Expr *step, Expr *receiver, Expr *member)
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
typedef struct Var Var;
struct Var {
  char *name;
  Type *type;
  int slot, depth, lexical;
  uint64_t root, deps;
  bool mut, parameter, unavailable, pending;
  unsigned version;
  Var *alias;     /* Inference-only direct reference target. */
  Type *narrowed; /* A branch-local view; the storage type never changes. */
};
typedef struct {
  int id;
  Type *type;
  uint64_t deps, storage;
  Var *var;
  bool addressable, guarded_copy;
} Value;
typedef struct {
  int end, update, bindings, phase;
  List focuses, saved, first;
} Loop; /* Jump targets carry the ownership cleanup boundary. */
typedef struct {
  Expr *test;
  Var *var;
  unsigned version;
  bool positive;
} NilGuard;
typedef struct {
  List guards;
} NilFlow;
typedef struct {
  Type *type;
  unsigned version;
} NilFact;
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
  NilFlow flow;
  bool raw_name;
  bool in_default;
} Context;
static Instance *instantiate(Program *p, Function *f, Def *self, List *actual,
                             Loc at);
static Instance *instance_for(Program *p, Function *f, Def *self, List *actual,
                              Loc at);
static void infer_results(Program *p);
static Type *expression_type(Context *c, Expr *e, Type *expected);
static void statements(Context *c, List *body, bool scope);
static Value expression(Context *c, Expr *e, Type *expected);
static Value construct(Context *c, Expr *e, Type *t, int dst);
static List first_types(Context *c, Stmt *s);
static Type *chain_type(Context *c, Expr *e);
static Type *chain_place_type(Context *c, Expr *e);
static Value convert_value(Context *c, Value src, Type *to, int dst, Loc loc);
static Instr *emit(Context *c, int op, Loc l) {
  Instr *i = mem(sizeof(*i));
  i->op = op;
  i->loc = l;
  i->dst = i->a = i->b = -1;
  add(&c->in->code, i);
  return i;
}
static int slot(Context *c, Type *t, bool alias) {
  layout(t, c->in->fn->loc);
  Slot *s = mem(sizeof(*s));
  s->type = t;
  s->alias = alias;
  int id = c->in->slots.n;
  s->owner = id;
  add(&c->in->slots, s);
  return id;
}
static Value value(Context *c, Type *t) {
  Value v = {0};
  v.id = slot(c, t, false);
  v.type = t;
  return v;
}
static uint64_t root(Context *c, int depth, bool external, Loc l) {
  if (c->root_count == 64)
    fail(l, "function exceeds 64 provenance roots; split the function");
  int n = c->root_count++;
  c->root_depth[n] = depth;
  c->root_external[n] = external;
  return UINT64_C(1) << n;
}
static Var *lookup(Context *c, const char *name) {
  for (int i = c->vars.n - 1; i >= 0; i--) {
    Var *v = c->vars.v[i];
    if (!strcmp(v->name, name))
      return v;
  }
  return NULL;
}
static Var *selected(Context *c, Expr *e) {
  if (!e->scope_kind) {
    Var *pending = NULL;
    for (int j = c->vars.n - 1; j >= 0; j--) {
      Var *v = c->vars.v[j];
      if (strcmp(v->name, e->text))
        continue;
      if (!v->pending)
        return v;
      pending = v;
    }
    return pending;
  }
  int target =
      e->scope_kind == 1 ? e->scope_count + 1 : c->lexical - e->scope_count;
  if (e->scope_kind != 3 && (target < 1 || target > c->lexical))
    fail(e->loc, "scope selector is outside the current function");
  int skip = e->scope_count;
  for (int j = c->vars.n - 1; j >= 0; j--) {
    Var *v = c->vars.v[j];
    if (strcmp(v->name, e->text))
      continue;
    if (e->scope_kind == 3) {
      if (skip-- == 0)
        return v;
    } else if (v->lexical <= target)
      return v;
  }
  return NULL;
}
static void forget_roots(List *vars, uint64_t roots, bool all)
{
  for (int j = 0; j < vars->n; j++) {
    Var *v = vars->v[j];
    if (all || (v->root & roots)) {
      v->version++;
      v->narrowed = NULL;
    }
  }
}
static List save_facts(List *vars)
{
  List facts = {0};
  for (int j = 0; j < vars->n; j++) {
    Var *v = vars->v[j];
    NilFact *f = mem(sizeof(*f));
    f->type = v->narrowed;
    f->version = v->version;
    add(&facts, f);
  }
  return facts;
}
static void restore_facts(List *vars, List facts)
{
  for (int j = 0; j < facts.n; j++) {
    Var *v = vars->v[j];
    NilFact *f = facts.v[j];
    v->narrowed = v->version == f->version ? f->type : NULL;
  }
}
static void remember_guard(NilFlow *flow, Context *context, Expr *test,
                           Expr *value, bool positive)
{
  if (value->kind != E_NAME) {
    return;
  }
  Var *v = selected(context, value);
  if (!v || v->type->kind != T_UNION || !nullable(v->type)) {
    return;
  }
  NilGuard *guard = NULL;
  for (int j = 0; j < flow->guards.n; j++) {
    NilGuard *g = flow->guards.v[j];
    if (g->test == test) {
      guard = g;
      break;
    }
  }
  if (!guard) {
    guard = mem(sizeof(*guard));
    add(&flow->guards, guard);
  }
  *guard = (NilGuard){
      .test = test, .var = v, .version = v->version, .positive = positive};
}
static bool nil_name(Expr *e)
{
  return e && e->kind == E_NAME && !strcmp(e->text, "nil^");
}
static void refine_condition(Program *p, List *vars, NilFlow *flow, Expr *e,
                             bool truth)
{
  if (e->kind == E_UNARY && !strcmp(e->text, "!")) {
    refine_condition(p, vars, flow, e->a, !truth);
    return;
  }
  if (e->kind == E_BIN && ((!strcmp(e->text, "and^") && truth) ||
                           (!strcmp(e->text, "or^") && !truth))) {
    refine_condition(p, vars, flow, e->a, truth);
    refine_condition(p, vars, flow, e->b, truth);
    return;
  }
  for (int j = 0; j < flow->guards.n; j++) {
    NilGuard *g = flow->guards.v[j];
    if (g->test != e || g->version != g->var->version) {
      continue;
    }
    for (int k = 0; k < vars->n; k++) {
      if (vars->v[k] == g->var) {
        g->var->narrowed = truth == g->positive
                               ? without_nil(p, g->var->type, e->loc)
                               : type_builtin(p, T_NIL);
        break;
      }
    }
  }
}
static Value place_expression(Context *c, Expr *e)
{
  bool raw = c->raw_name;
  c->raw_name = e->kind == E_NAME;
  Value v = expression(c, e, NULL);
  c->raw_name = raw;
  return v;
}
static Value variable(Var *v) {
  return (Value){v->slot, v->type, v->type->refs ? v->deps : 0,
                 v->root, v,       true};
}
static void access_check(Context *c, bool priv, Def *owner, Loc l,
                         const char *name) {
  if (priv && c->access != owner)
    fail(l, "private member '%s' belongs to '%s'", name, owner->name);
}
static Value deref(Context *c, Value v, Loc l) {
  if (v.type->kind != T_REF)
    return v;
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
static void store_check(Context *c, Value dst, Value src, Loc l) {
  if (dst.var && !dst.var->mut)
    fail(l, "cannot modify let^ binding '%s'", dst.var->name);
  for (int n = 0; n < c->root_count; n++)
    if (dst.storage & (UINT64_C(1) << n)) {
      if (c->root_external[n] && !c->in->fn->procedure &&
          !(c->in->fn->mutable_self && (c->self_roots & (UINT64_C(1) << n))))
        fail(l, "f^ cannot mutate borrowed storage; use p^ or mutable^self^");
      for (int j = 0; j < c->root_count; j++)
        if (src.deps & (UINT64_C(1) << j)) {
          if (c->root_depth[j] > c->root_depth[n])
            fail(l,
                 "reference would outlive its owner (scope %d into scope %d)",
                 c->root_depth[j], c->root_depth[n]);
          if (c->root_external[n])
            fail(l,
                 "cannot store nonstatic references through borrowed storage");
        }
    }
  for (int n = 0; n < c->vars.n; n++) {
    Var *v = c->vars.v[n];
    if (v->type->refs && (v->root & dst.storage))
      v->deps |= src.deps;
  }
  if (dst.var && dst.var->type->refs)
    dst.var->deps |= src.deps;
}
static Value copy_to(Context *c, Value dst, Value src, Loc l, bool init) {
  if (dst.type != src.type && sum_accepts(dst.type, src.type)) {
    src = convert_value(c, src, dst.type, -1, l);
  }
  if (dst.type != src.type)
    fail(l, "expected '%s', found '%s'", type_name(dst.type),
         type_name(src.type));
  if (src.type->owned)
    fail(l, "owned region values cannot be copied; construct at the "
            "destination or borrow with '&'");
  if (!init) {
    store_check(c, dst, src, l);
    forget_roots(&c->vars, dst.storage, false);
  }
  Instr *i = emit(c, I_COPY, l);
  i->dst = dst.id;
  i->a = src.id;
  i->type = dst.type;
  dst.deps = src.deps;
  if (dst.var && dst.var->type->refs)
    dst.var->deps |= src.deps;
  return dst;
}
static Value snapshot(Context *c, Value src, Loc l) {
  if (src.type->kind == T_VOID || src.type->owned)
    return src;
  Value dst = value(c, src.type);
  return copy_to(c, dst, src, l, true);
}
static Value case_value(Context *c, Value src, Type *arm, Loc loc)
{
  layout(arm, loc);
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
/* This also projects a proven subset for nil narrowing. Callers validate all
   ordinary implicit conversions with sum_accepts before entering here. */
static Value convert_value(Context *c, Value src, Type *to, int dst, Loc loc)
{
  layout(to, loc);
  if (src.type == to && dst < 0) {
    return src;
  }
  Value out = {.id = dst >= 0 ? dst : slot(c, to, false),
               .type = to,
               .deps = to->refs ? src.deps : 0};
  if (src.type == to) {
    return copy_to(c, out, src, loc, true);
  }
  int tag = union_alternative(to, src.type);
  if (tag >= 0) {
    Instr *i = emit(c, I_UNION, loc);
    i->dst = out.id;
    i->a = src.id;
    i->type = to;
    i->label = tag;
    return out;
  }
  if (src.type->kind != T_UNION) {
    fail(loc, "expected '%s', found '%s'", type_name(to), type_name(src.type));
  }
  List selected = {0};
  for (int j = 0; j < src.type->def->alternatives.n; j++) {
    Type *arm = src.type->def->alternatives.v[j];
    if (arm == to || union_alternative(to, arm) >= 0) {
      add(&selected, arm);
    }
  }
  if (!selected.n) {
    fail(loc, "cannot project '%s' to '%s'", type_name(src.type),
         type_name(to));
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
  return out;
}
static Value nil_base(Context *c, Value value, Loc loc)
{
  return value.type->kind == T_REF && value.type->elem->kind == T_UNION
             ? deref(c, value, loc)
             : value;
}
static Value present_value(Context *c, Value src, Loc loc)
{
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
static Expr *saved_value(Value value, Loc loc)
{
  Expr *e = mem(sizeof(*e));
  e->kind = E_SAVED;
  e->loc = loc;
  Value *v = mem(sizeof(*v));
  *v = value;
  e->compiler_value = v;
  return e;
}
static void nil_branch(Context *c, Value condition, int yes, int no, Loc loc)
{
  Instr *i = emit(c, I_CBR, loc);
  i->a = condition.id;
  i->label = yes;
  i->other = no;
}
static void nil_label(Context *c, int label, Loc loc)
{
  Instr *i = emit(c, I_LABEL, loc);
  i->label = label;
}
static void nil_jump(Context *c, int label, Loc loc)
{
  Instr *i = emit(c, I_BR, loc);
  i->label = label;
}
static Value nil_literal(Context *c, Loc loc)
{
  Expr e = {.kind = E_NAME, .loc = loc, .text = "nil^"};
  return expression(c, &e, NULL);
}
static Value chain_walk(Context *c, Expr *e, int absent, bool *never)
{
  List steps = {0};
  Expr *root = chain_parts(e, &steps);
  Value base = expression(c, root, NULL);
  *never = false;
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
        nil_branch(c, present_value(c, base, step->loc), yes, absent,
                   step->loc);
        nil_label(c, yes, step->loc);
        base = convert_value(c, base, plain, -1, step->loc);
      }
    }
    Expr member = {0},
         copy = step_expression(step, saved_value(base, step->loc), &member);
    base = expression(c, &copy, NULL);
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
  Value out = {.id = result->kind == T_VOID ? -1 : slot(c, result, false),
               .type = result};
  int absent = ++c->in->labels, end = ++c->in->labels;
  bool never = false;
  Value base = chain_walk(c, e, absent, &never);
  if (!never) {
    if (result->kind != T_VOID) {
      out.deps |= convert_value(c, base, result, out.id, e->loc).deps;
    }
    nil_jump(c, end, e->loc);
  }
  nil_label(c, absent, e->loc);
  if (result->kind != T_VOID) {
    convert_value(c, nil_literal(c, e->loc), result, out.id, e->loc);
  }
  nil_jump(c, end, e->loc);
  nil_label(c, end, e->loc);
  return out;
}
static Value coalesce_value(Context *c, Expr *e)
{
  Value base = expression(c, e->a, NULL);
  if (!nullable(base.type)) {
    fail(e->loc, "?? requires a nil alternative");
  }
  Type *result = expression_type(c, e, NULL);
  layout(result, e->loc);
  Value out = value(c, result);
  Type *plain = without_nil(c->p, base.type, e->loc);
  if (!plain) {
    return construct(c, e->b, result, out.id);
  }
  int yes = ++c->in->labels, no = ++c->in->labels, end = ++c->in->labels;
  nil_branch(c, present_value(c, base, e->loc), yes, no, e->loc);
  nil_label(c, yes, e->loc);
  out.deps |= convert_value(c, convert_value(c, base, plain, -1, e->loc),
                            result, out.id, e->loc)
                  .deps;
  nil_jump(c, end, e->loc);
  nil_label(c, no, e->loc);
  out.deps |= construct(c, e->b, result, out.id).deps;
  nil_jump(c, end, e->loc);
  nil_label(c, end, e->loc);
  return out;
}
static Value field_value(Context *c, Value base, Field *f, Loc l) {
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
static void release(Context *c, Type *t, int id, Loc l) {
  Instr *i = emit(c, I_RELEASE, l);
  i->a = id;
  i->type = t;
}
static void cleanup(Context *c, int from, Loc l) {
  for (int j = c->vars.n - 1; j >= from; j--) {
    Var *v = c->vars.v[j];
    if (v->type->owned && v->slot != c->in->result_slot)
      release(c, v->type, v->slot, l);
  }
}
static Value literal_int(Context *c, Expr *e, Type *expected) {
  Type *t = expected;
  if (t && kind_float(t->kind)) {
    Value v = value(c, t);
    Instr *i = emit(c, I_CONST, e->loc);
    i->dst = v.id;
    i->type = t;
    i->text = float_constant(e->text, t->kind, e->loc);
    return v;
  }
  if (!t || !kind_integer(t->kind))
    t = type_builtin(c->p, T_I32);
  if (!integer_fits(e->text, t->kind))
    fail(e->loc, "integer literal does not fit '%s'", type_name(t));
  Value v = value(c, t);
  Instr *i = emit(c, I_CONST, e->loc);
  i->dst = v.id;
  i->type = t;
  i->text = e->text;
  return v;
}
static int random_intrinsic(Instance *in, Expr *target) {
  if (target->kind != E_NAME)
    return -1;
  const char *names[] = {"__seed^", "__sqrt^",      "__log^",
                         "__cos^",  "__nextafter^", "__finite^"};
  for (int j = 0; j < 6; j++)
    if (!strcmp(target->text, names[j])) {
      if (!in->fn->module || !in->fn->module->name ||
          strcmp(in->fn->module->name, "std.random"))
        fail(target->loc, "private standard-library intrinsic");
      return j;
    }
  return -1;
}
/* this^ selects a declaration, independently of its runtime activation. */
static Function *this_function(Program *p, Instance *in, Expr *target)
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
    fail(target->loc,
         "%s requires an enclosing function or procedure at that nesting depth",
         target->text);
  }
  return f;
}
static Def *outer_receiver(Function *f, Type *type, Loc loc)
{
  if (type->kind != T_REF || type->elem->kind != T_STRUCT ||
      !derives(type->elem->def, f->owner)) {
    fail(loc, "outer method call requires an explicit '&%s' receiver",
         f->owner->name);
  }
  return type->elem->def;
}
static Value call(Context *c, Expr *e, Type *expected, int destination) {
  Program *p = c->p;
  Expr *target = e->a;
  int native = random_intrinsic(c->in, target);
  if (native >= 0) {
    int count = native == 4 ? 2 : 1;
    if (e->args.n != count)
      fail(e->loc, "invalid intrinsic arity");
    Type *input = native == 0 ? type_wrap(p, T_REF, type_builtin(p, T_U64), 0)
                              : type_builtin(p, T_F64);
    Type *output = type_builtin(p, native == 0 || native == 5 ? T_BOOL : T_F64);
    Value out = value(c, output);
    Instr *i = mem(sizeof(*i));
    i->op = I_NATIVE;
    i->loc = e->loc;
    i->dst = out.id;
    i->type = output;
    i->other = native;
    for (int j = 0; j < count; j++) {
      Value v = expression(c, e->args.v[j], input);
      if (v.type != input)
        fail(e->loc, "invalid intrinsic argument");
      v = snapshot(c, v, e->loc);
      add(&i->args, (void *)(intptr_t)(v.id + 1));
    }
    add(&c->in->code, i);
    return out;
  }
  if (target->kind == E_NAME) {
    for (int k = T_U8; k <= T_F64; k++) {
      if (strcmp(target->text, builtin_name(k)))
        continue;
      if (e->args.n != 1)
        fail(e->loc, "numeric conversion expects one argument");
      Type *t = type_builtin(p, k);
      Value v = expression(c, e->args.v[0], NULL);
      if (!kind_integer(v.type->kind) && !kind_float(v.type->kind))
        fail(e->loc, "numeric conversion requires a number");
      if (expected && expected != t)
        fail(e->loc, "conversion result is '%s', expected '%s'", type_name(t),
             type_name(expected));
      Value out = {.id = destination >= 0 ? destination : slot(c, t, false),
                   .type = t};
      Instr *i = emit(c, I_CONVERT, e->loc);
      i->dst = out.id;
      i->a = v.id;
      i->type = t;
      return out;
    }
  }
  if (target->kind == E_NAME &&
      (!strcmp(target->text, "print") || !strcmp(target->text, "assert"))) {
    bool print = !strcmp(target->text, "print");
    if (print && !c->in->fn->procedure)
      fail(e->loc, "print requires p^");
    if (!print && e->args.n != 1)
      fail(e->loc, "assert expects one bool");
    Instr *i = mem(sizeof(*i));
    i->op = print ? I_PRINT : I_ASSERT;
    i->loc = e->loc;
    i->dst = -1;
    for (int j = 0; j < e->args.n; j++) {
      Value v =
          expression(c, e->args.v[j], print ? NULL : type_builtin(p, T_BOOL));
      if (print && !kind_integer(v.type->kind) && !kind_float(v.type->kind) &&
          v.type->kind != T_STRING && v.type->kind != T_BOOL)
        fail(e->loc, "print does not support '%s'", type_name(v.type));
      if (!print && v.type->kind != T_BOOL)
        fail(e->loc, "assert requires bool");
      v = snapshot(c, v, e->loc);
      add(&i->args, (void *)(intptr_t)(v.id + 1));
    }
    add(&c->in->code, i);
    return (Value){.id = -1, .type = type_builtin(p, T_VOID)};
  }
  if (target->kind == E_MEMBER && target->a->kind == E_NAME &&
      !strcmp(target->text, "new")) {
    Def *d = lookup_def(p, target->a->text);
    if (d) {
      if (e->args.n)
        fail(e->loc, "new() takes no arguments; use Type{ field = value }");
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
    Value base = deref(c, receiver, e->loc);
    if (base.type->kind == T_UNION &&
        (!strcmp(target->text, "is") || !strcmp(target->text, "get"))) {
      if (e->args.n != 1 || ((Expr *)e->args.v[0])->kind != E_NAME)
        fail(e->loc, "%s expects one alternative type name", target->text);
      Expr *name = e->args.v[0];
      Spec arm_spec = {.kind = SPEC_NAME, .name = name->text, .loc = name->loc};
      Type *arm = type_spec(p, &arm_spec);
      int tag = union_alternative(base.type, arm);
      if (tag < 0)
        fail(e->loc, "'%s' is not an alternative of '%s'", name->text,
             type_name(base.type));
      bool get = !strcmp(target->text, "get");
      Type *t = get ? arm : type_builtin(p, T_BOOL);
      if (expected && t != expected)
        fail(e->loc, "%s returns '%s', expected '%s'", target->text,
             type_name(t), type_name(expected));
      Value out = {.id = destination >= 0 ? destination : slot(c, t, false),
                   .type = t,
                   .deps = t->refs ? base.deps : 0};
      Instr *i = emit(c, get ? I_CASE_GET : I_CASE_IS, e->loc);
      i->dst = out.id;
      i->a = base.id;
      i->type = base.type;
      i->label = tag;
      return out;
    }
    if ((base.type->kind == T_ARRAY || base.type->kind == T_GROW ||
         base.type->kind == T_SLICE || base.type->kind == T_STRING) &&
        !strcmp(target->text, "slice")) {
      if (e->args.n != 2)
        fail(e->loc, "slice expects start and count");
      if ((base.type->kind == T_ARRAY || base.type->kind == T_GROW) &&
          !base.addressable)
        fail(e->loc,
             "slice source needs a binding; cannot borrow a temporary array");
      Value start = snapshot(
          c, expression(c, e->args.v[0], type_builtin(p, T_I64)), e->loc);
      Value count = snapshot(
          c, expression(c, e->args.v[1], type_builtin(p, T_I64)), e->loc);
      if (start.type->kind != T_I64 || count.type->kind != T_I64)
        fail(e->loc, "slice start and count must be i64^");
      Type *elem =
          base.type->kind == T_STRING ? type_builtin(p, T_U8) : base.type->elem;
      Type *t = type_wrap(p, T_SLICE, elem, 0);
      if (expected && expected != t)
        fail(e->loc, "slice returns '%s', expected '%s'", type_name(t),
             type_name(expected));
      Value out = {.id = destination >= 0 ? destination : slot(c, t, false),
                   .type = t,
                   .deps = base.deps | ((base.type->kind == T_ARRAY ||
                                         base.type->kind == T_GROW)
                                            ? base.storage
                                            : 0)};
      Instr *i = emit(c, I_SLICE, e->loc);
      i->dst = out.id;
      i->a = base.id;
      i->b = start.id;
      i->other = count.id;
      i->type = base.type;
      return out;
    }
    if (base.type->kind == T_GROW && !strcmp(target->text, "truncate")) {
      if (e->args.n != 1)
        fail(e->loc, "truncate expects one new length");
      Value len = expression(c, e->args.v[0], type_builtin(p, T_I64));
      if (len.type->kind != T_I64)
        fail(e->loc, "truncate length must be i64^");
      store_check(c, base, (Value){0}, e->loc);
      Type *t = type_builtin(p, T_VOID);
      if (expected && expected != t)
        fail(e->loc, "truncate returns void^");
      Instr *i = emit(c, I_TRUNCATE, e->loc);
      i->a = base.id;
      i->b = len.id;
      i->type = base.type;
      return (Value){.id = -1, .type = t};
    }
    if (base.type->kind == T_GROW && !strcmp(target->text, "append")) {
      if (e->args.n != 1)
        fail(e->loc, "append expects one slice");
      Value src = expression(c, e->args.v[0], NULL);
      if (!((src.type->kind == T_SLICE && src.type->elem == base.type->elem) ||
            (src.type->kind == T_STRING && base.type->elem->kind == T_U8)))
        fail(e->loc, "append needs a slice with the same element type");
      Value dependencies = {.deps = base.type->elem->refs ? src.deps : 0};
      store_check(c, base, dependencies, e->loc);
      Type *t = type_builtin(p, T_VOID);
      if (expected && expected != t)
        fail(e->loc, "append returns void^");
      Instr *i = emit(c, I_APPEND, e->loc);
      i->a = base.id;
      i->b = src.id;
      i->type = base.type;
      return (Value){.id = -1, .type = t};
    }
    if (base.type->kind == T_GROW &&
        (!strcmp(target->text, "pop") || !strcmp(target->text, "clear"))) {
      bool pop = !strcmp(target->text, "pop");
      if (e->args.n)
        fail(e->loc, "%s expects no arguments", target->text);
      store_check(c, base, (Value){0}, e->loc);
      Type *t = pop ? base.type->elem : type_builtin(p, T_VOID);
      if (expected && expected != t)
        fail(e->loc, "%s returns '%s', expected '%s'", target->text,
             type_name(t), type_name(expected));
      Value out = {
          .id = pop ? (destination >= 0 ? destination : slot(c, t, false)) : -1,
          .type = t,
          .deps = t->refs ? base.storage | base.deps : 0};
      Instr *i = emit(c, pop ? I_POP : I_CLEAR, e->loc);
      i->dst = out.id;
      i->a = base.id;
      i->type = base.type;
      return out;
    }
    if (base.type->kind == T_GROW && !strcmp(target->text, "push")) {
      if (e->args.n != 1)
        fail(e->loc, "push expects one element");
      Value v = expression(c, e->args.v[0], base.type->elem);
      if (v.type != base.type->elem)
        fail(e->loc, "push element type mismatch");
      store_check(c, base, v, e->loc);
      Type *t = type_wrap(p, T_REF, base.type->elem, 0);
      if (expected && expected != t)
        fail(e->loc, "push returns '%s', expected '%s'", type_name(t),
             type_name(expected));
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
    if (base.type->kind != T_STRUCT)
      fail(e->loc, "method call requires a structure");
    self = base.type->def;
    f = method(self, target->text);
    if (!f)
      fail(e->loc, "'%s' has no method '%s'", self->name, target->text);
    access_check(c, f->priv, f->owner, e->loc, f->name);
    if (base.guarded_copy && (f->procedure || f->mutable_self)) {
      fail(e->loc,
           "mutable nil-propagating receiver requires an optional reference");
    }
    if (f->procedure || f->mutable_self)
      store_check(c, base, (Value){0}, e->loc);
    Value ref = value(c, type_wrap(p, T_REF, base.type, 0));
    Instr *i = emit(c, I_ADDR, e->loc);
    i->dst = ref.id;
    i->a = base.id;
    i->type = ref.type;
    ref.deps = base.storage | base.deps;
    if (!base.addressable)
      ref.deps |= root(c, c->depth + 1, false, e->loc);
    Value *saved = mem(sizeof(*saved));
    *saved = ref;
    add(&values, saved);
    add(&actual, ref.type);
  } else if (target->kind == E_FUNCTION) {
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
    for (int j = 0; j < p->functions.n; j++) {
      Function *candidate = p->functions.v[j];
      if (!strcmp(candidate->name, target->text) &&
          candidate->params.n == e->args.n) {
        f = candidate;
        break;
      }
    }
    if (!f)
      fail(e->loc, "unknown function '%s'", target->text);
  }
  else {
    fail(e->loc, "function values and escaping closures are not supported");
  }
  if (f->abstract)
    fail(e->loc, "cannot call abstract method '%s'", f->name);
  int offset = values.n;
  if (f->params.n != e->args.n + offset)
    fail(e->loc, "'%s' expects %d arguments", f->name, f->params.n - offset);
  if (f->procedure && !c->in->fn->procedure)
    fail(e->loc, "f^ cannot call p^ '%s'", f->name);
  for (int j = 0; j < e->args.n; j++) {
    Param *param = f->params.v[j + offset];
    Type *want = param->spec ? type_spec(p, param->spec) : NULL;
    Value v = expression(c, e->args.v[j], want);
    if (v.type->kind == T_VOID)
      fail(e->loc, "void argument");
    if (want) {
      Type *a = v.type, *b = want;
      if (a != b) {
        bool okay = (a->kind == T_STRUCT && b->kind == T_STRUCT &&
                     derives(a->def, b->def)) ||
                    (a->kind == T_REF && b->kind == T_REF &&
                     a->elem->kind == T_STRUCT && b->elem->kind == T_STRUCT &&
                     derives(a->elem->def, b->elem->def));
        if (!okay)
          fail(e->loc, "argument to '%s' requires nominal '%s', found '%s'",
               f->name, type_name(want), type_name(v.type));
      }
    }
    if (!self && f->owner && param->self) {
      self = outer_receiver(f, v.type, e->loc);
      if (f->procedure || f->mutable_self) {
        store_check(c, deref(c, v, e->loc), (Value){0}, e->loc);
      }
    }
    if (v.type->owned)
      fail(e->loc, "pass owned values by reference with '&'");
    v = snapshot(c, v, e->loc);
    Value *saved = mem(sizeof(*saved));
    *saved = v;
    add(&values, saved);
    add(&actual, v.type);
  }
  Instance *in = instantiate(p, f, self, &actual, e->loc);
  Type *t = in->result;
  if (expected && expected != t)
    fail(e->loc, "call returns '%s', expected '%s'", type_name(t),
         type_name(expected));
  Value out = {0};
  out.type = t;
  out.id = t->kind == T_VOID  ? -1
           : destination >= 0 ? destination
                              : slot(c, t, false);
  uint64_t deps = 0;
  for (int j = 0; j < values.n; j++) {
    Value *v = values.v[j];
    deps |= v->deps;
  }
  if (t->refs)
    out.deps = deps;
  if (t->owned && destination < 0)
    fail(e->loc, "owned return requires a binding or return destination");
  Instr *i = emit(c, I_CALL, e->loc);
  i->dst = out.id;
  i->callee = in;
  i->type = t;
  for (int j = 0; j < values.n; j++) {
    Value *v = values.v[j];
    add(&i->args, (void *)(intptr_t)(v->id + 1));
  }
  if (f->procedure || f->mutable_self) {
    forget_roots(&c->vars, deps, false);
  }
  return out;
}
static Type *union_literal_hint(Type *t, Expr *e) {
  if (!t || t->kind != T_UNION || e->constant_spec)
    return NULL;
  Expr *literal = e;
  if (literal->kind == E_UNARY && !strcmp(literal->text, "-"))
    literal = literal->a;
  if (literal->kind != E_INT && literal->kind != E_FLOAT &&
      literal->kind != E_ARRAY) {
    return NULL;
  }
  Type *hint = NULL;
  for (int j = 0; j < t->def->alternatives.n; j++) {
    Type *a = t->def->alternatives.v[j];
    if ((literal->kind == E_INT && kind_integer(a->kind)) ||
        (literal->kind == E_FLOAT && kind_float(a->kind)) ||
        (literal->kind == E_ARRAY &&
         (a->kind == T_ARRAY || a->kind == T_GROW))) {
      if (hint)
        return NULL;
      hint = a;
    }
  }
  return hint;
}
static Value construct(Context *c, Expr *e, Type *t, int dst) {
  if (e->kind == E_SCOPE) {
    int begin = c->vars.n, depth = c->depth++;
    c->lexical++;
    statements(c, &e->bindings, false);
    Value out = construct(c, e->a, t, dst);
    for (int j = 0; j < c->root_count; j++)
      if ((out.deps & (UINT64_C(1) << j)) && c->root_depth[j] > depth)
        fail(e->loc, "scoped expression cannot escape a local owner");
    if (dst < 0)
      out = snapshot(c, out, e->loc);
    cleanup(c, begin, e->loc);
    c->vars.n = begin;
    c->depth = depth;
    c->lexical--;
    return out;
  }
  if (t && t->kind == T_UNION && e->kind != E_IF) {
    if (e->kind == E_RECORD && (!e->text || !strcmp(e->text, t->def->name)))
      fail(e->loc, "construct union '%s' from a named alternative",
           type_name(t));
    Value arm = expression(c, e, union_literal_hint(t, e));
    if (!sum_accepts(t, arm.type)) {
      fail(e->loc, "'%s' is not an alternative of nominal union '%s'",
           type_name(arm.type), type_name(t));
    }
    return convert_value(c, arm, t, dst, e->loc);
  }
  if (e->kind == E_CALL)
    return call(c, e, t, dst);
  if (e->kind == E_IF) {
    if (!t)
      t = expression_type(c, e, NULL);
    if (!t || t->kind == T_VOID)
      fail(e->loc, "if^ expression must produce a value on every branch");
    Value out = {.id = dst >= 0 ? dst : slot(c, t, false), .type = t};
    if (t->owned && dst < 0)
      fail(e->loc, "owned conditional requires a construction destination");
    Value condition = expression(c, e->a, type_builtin(c->p, T_BOOL));
    if (condition.type->kind != T_BOOL)
      fail(e->loc, "condition requires bool");
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
    restore_facts(&c->vars, facts);
    i = emit(c, I_BR, e->loc);
    i->label = end;
    i = emit(c, I_LABEL, e->loc);
    i->label = no;
    refine_condition(c->p, &c->vars, &c->flow, e->a, false);
    Value b = construct(c, e->c, t, out.id);
    restore_facts(&c->vars, facts);
    i = emit(c, I_BR, e->loc);
    i->label = end;
    i = emit(c, I_LABEL, e->loc);
    i->label = end;
    out.deps = a.deps | b.deps;
    return out;
  }
  if (e->kind == E_RECORD) {
    if (e->text) {
      Def *d = lookup_def(c->p, e->text);
      if (!d)
        fail(e->loc, "unknown constructor '%s'", e->text);
      if (t && t != d->type)
        fail(e->loc, "constructor has nominal type '%s', expected '%s'",
             d->name, type_name(t));
      t = d->type;
    }
    if (!t || t->kind != T_STRUCT)
      fail(e->loc, "record construction needs a nominal type");
    if (t->kind == T_STRUCT && (e->error_constructor != t->def->error_variant))
      fail(e->loc, t->def->error_variant
                       ? "error variant requires error^ constructor"
                       : "error^ requires an errordef^ variant");
    if (t->def->abstract)
      fail(e->loc, "cannot construct abstract def^ '%s'", t->def->name);
    if (dst < 0 && t->owned)
      fail(e->loc, "owned construction requires a destination");
    Value out = {0};
    out.type = t;
    out.id = dst >= 0 ? dst : slot(c, t, false);
    if (!t->def->fields.n) {
      Instr *z = emit(c, I_ZERO, e->loc);
      z->dst = out.id;
      z->type = t;
    }
    for (int j = 0; j < e->names.n; j++) {
      char *name = e->names.v[j];
      Field *f = field(t->def, name);
      if (!f)
        fail(e->loc, "unknown field '%s' in '%s'", name, t->def->name);
      access_check(c, f->priv, f->owner, e->loc, name);
      for (int k = 0; k < j; k++)
        if (!strcmp(name, e->names.v[k]))
          fail(e->loc, "duplicate field initializer '%s'", name);
    }
    for (int j = 0; j < t->def->fields.n; j++) {
      Field *f = t->def->fields.v[j];
      Expr *init = f->init;
      for (int k = 0; k < e->names.n; k++)
        if (!strcmp(f->name, e->names.v[k]))
          init = e->args.v[k];
      Def *old = c->access; /* Default initializers retain their declaration's
                               access context. */
      bool old_default = c->in_default;
      bool default_value = init && init == f->init;
      if (default_value) {
        for (int k = 0; k < c->default_stack.n; k++)
          if (c->default_stack.v[k] == f->init)
            fail(e->loc, "recursive default construction for '%s'", f->name);
        add(&c->default_stack, f->init);
        c->in_default = true;
      }
      if (init == f->init)
        c->access = f->initializer_owner;
      Value dest = {0};
      dest.type = f->type;
      dest.id = slot(c, f->type, true);
      Instr *a = emit(c, I_FIELD, e->loc);
      a->dst = dest.id;
      a->a = out.id;
      a->type = t;
      a->label = f->index;
      Value v = {0};
      if (init)
        v = construct(c, init, f->type, dest.id);
      else if (f->type->kind == T_REF || f->type->kind == T_STRING ||
               f->type->kind == T_SLICE || f->type->kind == T_UNION ||
               (f->type->kind == T_ARRAY && f->type->refs))
        fail(e->loc, "field '%s' needs an initializer", f->name);
      else if (f->type->kind == T_STRUCT) {
        Expr empty = {.kind = E_RECORD, .loc = e->loc};
        v = construct(c, &empty, f->type, dest.id);
      } else if (f->type->kind == T_GROW) {
        Expr empty = {.kind = E_ARRAY, .loc = e->loc};
        v = construct(c, &empty, f->type, dest.id);
      } else {
        Instr *z = emit(c, I_ZERO, e->loc);
        z->dst = dest.id;
        z->type = dest.type;
      }
      c->access = old;
      c->in_default = old_default;
      if (default_value)
        c->default_stack.n--;
      out.deps |= v.deps;
    }
    return out;
  }
  if (e->kind == E_ARRAY) {
    if (!t) {
      if (!e->args.n)
        fail(e->loc, "empty array needs a type annotation");
      Value first = expression(c, e->args.v[0], NULL);
      t = type_wrap(c->p, T_ARRAY, first.type, e->args.n);
      layout(t, e->loc); /* Re-evaluation of inference expressions is avoided
                            below. */
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
    if (t->kind != T_ARRAY && t->kind != T_GROW)
      fail(e->loc, "array literal requires array type");
    if (t->kind == T_ARRAY && e->args.n != t->count)
      fail(e->loc, "fixed array requires exactly %d elements", t->count);
    Value out = {0};
    out.type = t;
    out.id = dst >= 0 ? dst : slot(c, t, false);
    if (t->kind == T_GROW) {
      layout(t->elem, e->loc);
      if (t->elem->owned || !t->elem->size)
        fail(e->loc, "growable elements must be nonempty fixed-size values");
      Instr *a = emit(c, I_REGION, e->loc);
      a->dst = out.id;
      a->type = t;
    }
    for (int j = 0; j < e->args.n; j++) {
      if (t->kind == T_GROW) {
        Value v = expression(c, e->args.v[j], t->elem);
        if (v.type != t->elem)
          fail(e->loc, "array element type mismatch");
        out.deps |= v.deps;
        Instr *a = emit(c, I_PUSH, e->loc);
        a->a = out.id;
        a->b = v.id;
        a->type = t;
      } else {
        int d = slot(c, t->elem, true);
        Instr *ix = emit(c, I_INDEX, e->loc);
        ix->dst = d;
        ix->a = out.id;
        ix->type = t;
        ix->b = -1;
        ix->label = j;
        Value v = construct(c, e->args.v[j], t->elem, d);
        out.deps |= v.deps;
      }
    }
    return out;
  }
  Value v = expression(c, e, t);
  if (dst >= 0) {
    Value d = {0};
    d.id = dst;
    d.type = t;
    return copy_to(c, d, v, e->loc, true);
  }
  return v;
}
static Value expression(Context *c, Expr *e, Type *expected) {
  if (expected && expected->kind == T_UNION)
    return construct(c, e, expected, -1);
  Program *p = c->p;
  if (e->constant_spec) {
    Expr copy = *e;
    copy.constant_spec = NULL;
    Type *t = type_spec(p, e->constant_spec);
    Value v = expression(c, &copy, t);
    if (v.type != t || (expected && expected != t))
      fail(e->loc, "constant type mismatch");
    return v;
  }
  Value out = {0};
  if (e->kind == E_EXPECT)
    return expression(c, e->a, expression_type(c, e->b, NULL));
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
    out = value(c, type_builtin(p, T_BOOL));
    int end = ++c->in->labels;
    for (int j = 0; j < e->names.n; j++) {
      Value current =
          snapshot(c, expression(c, e->args.v[j + 1], previous.type), e->loc);
      Expr left = {.kind = E_SAVED, .loc = e->loc, .saved_slot = previous.id};
      Expr right = {.kind = E_SAVED, .loc = e->loc, .saved_slot = current.id};
      Expr pair = {.kind = E_BIN,
                   .loc = e->loc,
                   .text = e->names.v[j],
                   .a = &left,
                   .b = &right};
      Value test = expression(c, &pair, NULL);
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
    Value src = e->a->kind == E_NAME ? place_expression(c, e->a)
                                     : expression(c, e->a, NULL);
    Value result = present_value(c, src, e->loc);
    remember_guard(&c->flow, c, e, e->a, true);
    return result;
  }
  if (e->kind == E_COALESCE) {
    return coalesce_value(c, e);
  }
  if (guarded_chain(e)) {
    return chain_value(c, e);
  }
  if (e->kind == E_FUNCTION)
    fail(e->loc, "function values cannot escape; invoke the literal directly");
  if (e->kind == E_INT)
    return literal_int(c, e, expected);
  if (e->kind == E_BOOL || e->kind == E_FLOAT || e->kind == E_STRING) {
    Type *t = type_builtin(
        p,
        e->kind == E_BOOL ? T_BOOL
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
    if (!strcmp(e->text, "_^"))
      fail(e->loc, "_^ discards a value and cannot be read");
    if (c->in_default)
      fail(e->loc, "default initializer cannot capture caller locals or self^");
    Var *v = selected(c, e);
    if (!v)
      fail(e->loc,
           "unknown value '%s' (closures and global values are not supported)",
           e->text);
    if (v->unavailable)
      fail(e->loc, "first^ binding '%s' is not definitely initialized here",
           v->name);
    Value result = variable(v);
    if (v->narrowed && !c->raw_name) {
      return convert_value(c, result, v->narrowed, -1, e->loc);
    }
    return result;
  }
  if (e->kind == E_CALL)
    return call(c, e, expected, -1);
  if (e->kind == E_RECORD || e->kind == E_ARRAY || e->kind == E_IF ||
      e->kind == E_SCOPE)
    return construct(c, e, expected, -1);
  if (e->kind == E_MEMBER) {
    Value base = deref(c, expression(c, e->a, NULL), e->loc);
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
    if (base.type->kind != T_STRUCT)
      fail(e->loc, "field access requires a structure");
    Field *f = field(base.type->def, e->text);
    if (!f)
      fail(e->loc, "'%s' has no field '%s'", base.type->def->name, e->text);
    return field_value(c, base, f, e->loc);
  }
  if (e->kind == E_INDEX) {
    Value base = deref(c, expression(c, e->a, NULL), e->loc);
    if (base.type->kind != T_ARRAY && base.type->kind != T_GROW &&
        base.type->kind != T_SLICE && base.type->kind != T_STRING)
      fail(e->loc, "indexing requires an array");
    Value index = expression(c, e->b, type_builtin(p, T_I64));
    if (!kind_integer(index.type->kind))
      fail(e->loc, "index must be an integer");
    bool view = base.type->kind == T_SLICE || base.type->kind == T_STRING;
    out.type =
        base.type->kind == T_STRING ? type_builtin(p, T_U8) : base.type->elem;
    out.id = slot(c, out.type, true);
    out.storage = view ? base.deps : base.storage;
    /* The container must outlive references loaded from it. This conservative
       bound also covers aliases and loop-carried updates to element roots. */
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
    Value a =
        !strcmp(e->text, "&") && e->a->kind == E_NAME
            ? place_expression(c, e->a)
            : expression(c, e->a,
                         (!strcmp(e->text, "-") || !strcmp(e->text, "bitnot^"))
                             ? expected
                             : NULL);
    if (!strcmp(e->text, "&")) {
      if (!a.addressable)
        fail(e->loc, "cannot borrow a temporary; bind it first");
      if (a.var && !a.var->mut)
        fail(e->loc, "borrowing storage requires var^, found let^ '%s'",
             a.var->name);
      out = value(c, type_wrap(p, T_REF, a.type, 0));
      out.deps = a.storage | a.deps;
      Instr *i = emit(c, I_ADDR, e->loc);
      i->dst = out.id;
      i->a = a.id;
      i->type = out.type;
      return out;
    }
    if (!strcmp(e->text, "*")) {
      if (a.type->kind != T_REF)
        fail(e->loc, "'*' requires a reference");
      return deref(c, a, e->loc);
    }
    if ((!strcmp(e->text, "bitnot^") && !kind_integer(a.type->kind)) ||
        (!strcmp(e->text, "!") && a.type->kind != T_BOOL) ||
        (!strcmp(e->text, "-") &&
         !(kind_float(a.type->kind) ||
           (kind_integer(a.type->kind) && !kind_unsigned(a.type->kind)))))
      fail(e->loc, "invalid unary operand");
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
    Type *rhs_kind = comparison && e->b->kind != E_INT &&
                             e->b->kind != E_FLOAT && e->b->kind != E_BOOL &&
                             e->b->kind != E_STRING
                         ? expression_type(c, e->b, NULL)
                         : NULL;
    bool nil_rhs = nil_name(e->b) || (rhs_kind && rhs_kind->kind == T_NIL);
    Value a = comparison && nil_rhs && e->a->kind == E_NAME
                  ? place_expression(c, e->a)
                  : expression(c, e->a,
                               logic        ? type_builtin(p, T_BOOL)
                               : comparison ? NULL
                                            : expected);
    if (logic) {
      if (a.type->kind != T_BOOL)
        fail(e->loc, "logical operands must be bool");
      out = value(c, a.type);
      copy_to(c, out, a, e->loc, true);
      int rhs = ++c->in->labels, end = ++c->in->labels;
      Instr *br = emit(c, I_CBR, e->loc);
      br->a = a.id;
      bool andop = !strcmp(e->text, "and^") || !strcmp(e->text, "&&");
      br->label = andop ? rhs : end;
      br->other = andop ? end : rhs;
      Instr *lab = emit(c, I_LABEL, e->loc);
      lab->label = rhs;
      List facts = save_facts(&c->vars);
      refine_condition(p, &c->vars, &c->flow, e->a, andop);
      Value b = expression(c, e->b, a.type);
      restore_facts(&c->vars, facts);
      copy_to(c, out, b, e->loc, true);
      br = emit(c, I_BR, e->loc);
      br->label = end;
      lab = emit(c, I_LABEL, e->loc);
      lab->label = end;
      return out;
    }
    a = snapshot(c, a, e->loc);
    Value b = comparison && nil_name(e->a) && e->b->kind == E_NAME
                  ? place_expression(c, e->b)
                  : expression(c, e->b, shift || nil_rhs ? NULL : a.type);
    if (bitwise && (!kind_integer(a.type->kind) || !kind_integer(b.type->kind)))
      fail(e->loc, "bitwise operands must be integers");
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
    if (!shift && a.type != b.type)
      fail(e->loc, "binary operands have different types");
    bool cmp = strchr("=!<>", e->text[0]) != NULL;
    if (!kind_integer(a.type->kind) && !kind_float(a.type->kind) &&
        !(cmp && a.type->kind == T_BOOL && strchr("=!", e->text[0])))
      fail(e->loc, "operator '%s' is not defined for '%s'", e->text,
           type_name(a.type));
    if (kind_float(a.type->kind) && !strcmp(e->text, "%"))
      fail(e->loc, "floating remainder is not supported");
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
static void branch_to(Context *c, int target, Loc loc) {
  Instr *i = emit(c, I_BR, loc);
  i->label = target;
}
static void label_at(Context *c, int label, Loc loc) {
  Instr *i = emit(c, I_LABEL, loc);
  i->label = label;
}
static void conditional(Context *c, Value value, int yes, int no, Loc loc) {
  Instr *i = emit(c, I_CBR, loc);
  i->a = value.id;
  i->label = yes;
  i->other = no;
}
static void remember_focus(Context *c, Loop *loop, Loc loc, bool restore) {
  for (int j = 0; j < loop->focuses.n; j++) {
    Var *v = loop->focuses.v[j];
    Value *save = loop->saved.v[j];
    bool mut = v->mut;
    v->mut = true;
    if (restore)
      copy_to(c, variable(v), *save, loc, false);
    else
      *save = copy_to(c, *save, variable(v), loc, true);
    v->mut = mut;
  }
}
static void plain_loop(Context *c, Stmt *s) {
  int test = ++c->in->labels, body = ++c->in->labels, end = ++c->in->labels;
  int update = s->update.n ? ++c->in->labels : test;
  Loop target = {
      .end = end, .update = update, .bindings = c->vars.n, .phase = 1};
  add(&c->loops, &target);
  branch_to(c, test, s->loc);
  int code = c->in->code.n, slots = c->in->slots.n, labels = c->in->labels,
      roots = c->root_count;
  int bindings = c->vars.n;
  List entry_facts = save_facts(&c->vars);
  for (;;) {
    Type *before_fact[64];
    for (int j = 0; j < bindings; j++) {
      before_fact[j] = ((Var *)c->vars.v[j])->narrowed;
    }
    uint64_t before[64];
    for (int j = 0; j < bindings; j++)
      before[j] = ((Var *)c->vars.v[j])->deps;
    label_at(c, test, s->loc);
    statements(c, &s->pre, false);
    Value cond = expression(c, s->a, type_builtin(c->p, T_BOOL));
    if (cond.type->kind != T_BOOL)
      fail(s->loc, "condition requires bool");
    conditional(c, cond, body, end, s->loc);
    label_at(c, body, s->loc);
    refine_condition(c->p, &c->vars, &c->flow, s->a, true);
    c->lexical--;
    statements(c, &s->body, true);
    c->lexical++;
    if (s->update.n) {
      bool targeted = false;
      for (int j = code; j < c->in->code.n; j++) {
        Instr *edge = c->in->code.v[j];
        targeted |= (edge->op == I_BR && edge->label == update) ||
                    (edge->op == I_CBR &&
                     (edge->label == update || edge->other == update));
      }
      /* Keep an ordinary header update in the canonical straight-line loop. */
      if (targeted || c->terminated) {
        if (!c->terminated)
          branch_to(c, update, s->loc);
        c->terminated = false;
        label_at(c, update, s->loc);
      }
      c->lexical--;
      statements(c, &s->update, true);
      c->lexical++;
    }
    if (!c->terminated)
      branch_to(c, test, s->loc);
    c->terminated = false;
    bool changed = false;
    for (int j = 0; j < bindings; j++)
      changed |= before[j] != ((Var *)c->vars.v[j])->deps ||
                 (before_fact[j] && !((Var *)c->vars.v[j])->narrowed);
    if (!changed)
      break;
    c->in->code.n = code;
    c->in->slots.n = slots;
    c->in->labels = labels;
    c->root_count = roots;
  }
  c->loops.n--;
  label_at(c, end, s->loc);
  restore_facts(&c->vars, entry_facts);
}
static void loop_code(Context *c, Stmt *s) {
  bool simple = !s->prolog.n && !s->first.n && !s->last.n && !s->epilog.n;
  for (int j = 0; j < s->pre.n; j++)
    simple &= ((Stmt *)s->pre.v[j])->internal;
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
    if (!v->type)
      fail(s->loc, "cannot infer first^ binding '%s'", v->name);
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
  int test = ++c->in->labels, yes = ++c->in->labels, no = ++c->in->labels,
      once = ++c->in->labels;
  int main = ++c->in->labels, update = ++c->in->labels,
      finish = ++c->in->labels;
  int last = ++c->in->labels, epilog = ++c->in->labels;
  Loop target = {
      .end = finish, .update = update, .bindings = c->vars.n, .first = first};
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
  int code = c->in->code.n, slots = c->in->slots.n, labels = c->in->labels,
      roots = c->root_count;
  int persistent = c->vars.n;
  for (;;) {
    Type *before_fact[64];
    for (int j = 0; j < persistent; j++) {
      before_fact[j] = ((Var *)c->vars.v[j])->narrowed;
    }
    uint64_t before[64];
    for (int j = 0; j < persistent; j++)
      before[j] = ((Var *)c->vars.v[j])->deps;
    for (int j = 0; j < first.n; j++) {
      Var *v = first.v[j];
      v->unavailable = v->pending = true;
    }
    target.phase = 0;
    label_at(c, test, s->loc);
    c->depth++;
    statements(c, &s->pre, false);
    if (c->terminated) {
      c->terminated = false;
      label_at(c, ++c->in->labels, s->loc);
    }
    Value cond = expression(c, s->a, type_builtin(c->p, T_BOOL));
    if (cond.type->kind != T_BOOL)
      fail(s->loc, "condition requires bool");
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
    for (int j = persistent; j < turn; j++)
      add(&pre_vars, c->vars.v[j]);
    List init = {0};
    for (int j = 0; j < s->first.n; j++) {
      Stmt *item = s->first.v[j], *copy = mem(sizeof(*copy));
      *copy = *item;
      if (copy->kind == S_BIND)
        copy->kind = S_INIT;
      if (copy->kind == S_BIND_MANY)
        copy->kind = S_INIT_MANY;
      add(&init, copy);
    }
    /* First-section declarations outlive the per-turn pre/main storage. */
    int depth = c->depth;
    c->depth--;
    statements(c, &init, false);
    c->depth = depth;
    if (c->vars.n != turn)
      fail(s->loc, "first^ must declare persistent values at its top level");
    if (!c->terminated) {
      i = emit(c, I_CONST, s->loc);
      i->dst = initial.id;
      i->type = initial.type;
      i->text = "0";
      branch_to(c, main, s->loc);
    }
    c->terminated = false;
    label_at(c, main, s->loc);
    for (int j = 0; j < first.n; j++)
      ((Var *)first.v[j])->unavailable = false;
    statements(c, &s->body, false);
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
    statements(c, &s->update, true);
    c->lexical++;
    if (!c->terminated)
      branch_to(c, test, s->loc);
    c->terminated = false;
    label_at(c, no, s->loc);
    /* The failed test also owns the final pre-section activation. */
    c->depth++;
    int saved = c->vars.n;
    /* Reuse the exact pre variables/slots from the emitted test. */
    for (int j = 0; j < pre_vars.n; j++)
      add(&c->vars, pre_vars.v[j]);
    cleanup(c, persistent, s->loc);
    c->vars.n = saved;
    c->depth--;
    branch_to(c, finish, s->loc);
    bool changed = false;
    for (int j = 0; j < persistent; j++)
      changed |= before[j] != ((Var *)c->vars.v[j])->deps ||
                 (before_fact[j] && !((Var *)c->vars.v[j])->narrowed);
    if (!changed)
      break;
    c->in->code.n = code;
    c->in->slots.n = slots;
    c->in->labels = labels;
    c->root_count = roots;
  }
  label_at(c, finish, s->loc);
  conditional(c, ran, last, epilog, s->loc);
  label_at(c, last, s->loc);
  target.phase = 2;
  remember_focus(c, &target, s->loc, true);
  c->lexical--;
  statements(c, &s->last, true);
  c->lexical++;
  if (!c->terminated)
    branch_to(c, epilog, s->loc);
  c->terminated = false;
  label_at(c, epilog, s->loc);
  target.phase = 3;
  for (int j = 0; j < first.n; j++)
    ((Var *)first.v[j])->unavailable = true;
  c->lexical--;
  statements(c, &s->epilog, true);
  c->lexical++;
  c->loops.n--;
  if (!c->terminated)
    cleanup(c, scope, s->loc);
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
      if (!pair->location.addressable) {
        fail(target->loc, "assignment destination is not addressable");
      }
    }
    if (pair->location.var && pair->location.type->kind == T_UNION) {
      pair->prior_fact = pair->location.var->narrowed;
    }
    if (pair->location.type->owned) {
      fail(target->loc,
           "owned values cannot be reassigned while references may exist");
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
      pair->old = snapshot(
          c, convert_value(c, pair->location, plain, -1, s->loc), s->loc);
    }
    nil_jump(c, end, s->loc);
    nil_label(c, absent, s->loc);
    nil_jump(c, end, s->loc);
    nil_label(c, end, s->loc);
  }
  /* Preserve parallel assignment: snapshot every enabled RHS before any write.
   */
  for (int j = 0; j < count; j++) {
    NilAssignment *pair = &pairs[j];
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
      pair->pending = convert_value(c, expression(c, &binary, pair->old.type),
                                    pair->location.type, -1, s->loc);
    }
    else {
      pair->pending = expression(c, rhs, pair->location.type);
    }
    pair->pending = snapshot(c, pair->pending, s->loc);
    nil_jump(c, end, s->loc);
    nil_label(c, end, s->loc);
  }
  for (int j = 0; j < count; j++) {
    NilAssignment *pair = &pairs[j];
    int yes = ++c->in->labels, end = ++c->in->labels;
    nil_branch(c, pair->active, yes, end, s->loc);
    nil_label(c, yes, s->loc);
    copy_to(c, assignment_location(c, pair, s->loc), pair->pending, s->loc,
            false);
    if (s->op && pair->prior_fact && pair->prior_fact->kind != T_NIL &&
        pair->location.var) {
      pair->location.var->narrowed = pair->prior_fact;
    }
    nil_jump(c, end, s->loc);
    nil_label(c, end, s->loc);
  }
}
static void binding_name(Context *c, Stmt *s) {
  if (c->in->fn == c->p->entry && c->depth == 1) {
    if (lookup_def(c->p, s->name))
      fail(s->loc, "top-level binding conflicts with def^ '%s'", s->name);
    for (int j = 0; j < c->p->functions.n; j++) {
      Function *f = c->p->functions.v[j];
      if (!strcmp(s->name, f->name))
        fail(s->loc, "top-level binding conflicts with function '%s'", s->name);
    }
  }
  for (int j = c->vars.n - 1; j >= 0; j--) {
    Var *v = c->vars.v[j];
    if (v->depth < c->depth)
      break;
    if (!strcmp(v->name, s->name))
      fail(s->loc, "duplicate binding '%s'", s->name);
  }
}
static void statements(Context *c, List *body, bool scope) {
  int start = c->vars.n;
  if (scope) {
    c->depth++;
    c->lexical++;
  }
  for (int n = 0; n < body->n; n++) {
    Stmt *s = body->v[n];
    if (c->terminated)
      fail(s->loc, "unreachable statement");
    if (s->kind == S_INIT_MANY) {
      for (int j = 0; j < s->body.n; j++) {
        Stmt *b = s->body.v[j];
        Var *v = lookup(c, b->name);
        if (!v || !v->unavailable || !v->pending)
          fail(b->loc, "first^ binding conflicts with a pre^ binding");
        Value out = construct(c, b->a, v->type, v->slot);
        Value dst = variable(v);
        dst.var = NULL;
        store_check(c, dst, out, b->loc);
        v->deps |= out.deps;
      }
      for (int j = 0; j < s->body.n; j++) {
        Var *v = lookup(c, ((Stmt *)s->body.v[j])->name);
        v->unavailable = v->pending = false;
      }
    } else if (s->kind == S_INIT) {
      Var *var = lookup(c, s->name);
      if (!var || !var->unavailable)
        fail(s->loc, "invalid first^ initialization");
      Value v = construct(c, s->a, var->type, var->slot);
      Value dst = variable(var);
      dst.var = NULL;
      store_check(c, dst, v, s->loc);
      var->deps |= v.deps;
      var->unavailable = var->pending = false;
    } else if (s->kind == S_BIND) {
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
      if (!t)
        t = expression_type(c, s->a, NULL);
      Value v = {0};
      bool alias_result = c->in->result_local &&
                          !strcmp(c->in->result_local, s->name) &&
                          c->depth == 1;
      int dest = alias_result ? c->in->result_slot : -1;
      if (!t && alias_result)
        t = c->in->result;
      if (t) {
        layout(t, s->loc);
        if (t->kind == T_VOID)
          fail(s->loc, "binding cannot have void type");
        if (dest < 0)
          dest = slot(c, t, false);
        if (t->owned && !alias_result) {
          Instr *begin = emit(c, I_STACK_BEGIN, s->loc);
          begin->dst = dest;
          begin->type = t;
          begin->text = s->name;
        }
        v = construct(c, s->a, t, dest);
      } else {
        v = expression(c, s->a, NULL);
        t = v.type;
        if (t->kind == T_VOID)
          fail(s->loc, "binding cannot have void type");
        int d = slot(c, t, false);
        Value dst = {.id = d, .type = t};
        v = copy_to(c, dst, v, s->loc, true);
      }
      if (s->integer && !kind_integer(t->kind))
        fail(s->loc, "counted loops require an integer type");
      Var *var = mem(sizeof(*var));
      var->name = s->name;
      var->slot = v.id;
      var->type = t;
      var->depth = c->depth;
      var->lexical = c->lexical;
      var->mut = s->mut;
      var->root = root(c, c->depth, false, s->loc);
      var->deps = v.deps;
      Value dst = variable(var);
      dst.var = NULL;
      store_check(c, dst, v, s->loc);
      add(&c->vars, var);
    } else if (s->kind == S_BIND_MANY) {
      int begin = c->vars.n;
      for (int j = 0; j < s->body.n; j++) {
        Stmt *binding = s->body.v[j];
        for (int k = 0; k < j; k++)
          if (strcmp(binding->name, "_^") &&
              !strcmp(binding->name, ((Stmt *)s->body.v[k])->name))
            fail(binding->loc, "duplicate binding '%s'", binding->name);
        binding_name(c, binding);
        Stmt hidden = *binding;
        hidden.name = fmt("\001parallel%d", c->in->slots.n);
        List one = {0};
        add(&one, &hidden);
        /* Keep a parallel declaration's owned result at the caller destination.
         */
        char *result = c->in->result_local;
        if (result && !strcmp(result, binding->name))
          c->in->result_local = hidden.name;
        statements(c, &one, false);
        c->in->result_local = result;
      }
      for (int j = 0; j < s->body.n; j++)
        if (strcmp(((Stmt *)s->body.v[j])->name, "_^"))
          ((Var *)c->vars.v[begin + j])->name = ((Stmt *)s->body.v[j])->name;
    }
    else if (s->kind == S_ASSIGN && s->optional) {
      nil_assignment(c, s);
    }
    else if (s->kind == S_ASSIGN) {
      int count = s->targets.n ? s->targets.n : 1;
      Value *dest = mem(sizeof(*dest) * count),
            *old = mem(sizeof(*old) * count), *src = mem(sizeof(*src) * count);
      for (int j = 0; j < count; j++) {
        Expr *target = s->targets.n ? s->targets.v[j] : s->a;
        if (target->kind == E_NAME && !strcmp(target->text, "_^")) {
          if (s->op)
            fail(s->loc, "discard cannot be a compound assignment target");
          continue;
        }
        dest[j] = place_expression(c, s->targets.n ? s->targets.v[j] : s->a);
        if (!dest[j].addressable)
          fail(s->loc, "assignment destination is not addressable");
        if (dest[j].type->owned)
          fail(s->loc,
               "owned values cannot be reassigned while references may exist");
        if (s->op) {
          Value previous = dest[j];
          if (previous.var && previous.var->narrowed &&
              previous.type->kind == T_UNION) {
            previous =
                convert_value(c, previous, previous.var->narrowed, -1, s->loc);
          }
          old[j] = snapshot(c, previous, s->loc);
        }
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
        src[j] = snapshot(c,
                          expression(c, s->values.n ? s->values.v[j] : s->b,
                                     s->op ? old[j].type : dest[j].type),
                          s->loc);
        if (s->op) {
          Expr left = {.kind = E_SAVED, .loc = s->loc, .saved_slot = old[j].id};
          Expr right = {
              .kind = E_SAVED, .loc = s->loc, .saved_slot = src[j].id};
          Expr binary = {.kind = E_BIN,
                         .loc = s->loc,
                         .text = s->op,
                         .a = &left,
                         .b = &right};
          src[j] = expression(c, &binary, old[j].type);
        }
      }
      for (int j = 0; j < count; j++) {
        if (!dest[j].type)
          continue;
        bool mut = dest[j].var && dest[j].var->mut;
        if (s->internal && dest[j].var)
          dest[j].var->mut = true;
        copy_to(c, dest[j], src[j], s->loc, false);
        if (s->op && dest[j].var && dest[j].type->kind == T_UNION &&
            union_alternative(dest[j].type, src[j].type) >= 0) {
          dest[j].var->narrowed = src[j].type;
        }
        if (s->internal && dest[j].var)
          dest[j].var->mut = mut;
      }
    }
    else if (s->kind == S_BREAK || s->kind == S_NEXT) {
      if (s->depth > c->loops.n)
        fail(s->loc, "jump depth exceeds enclosing loops");
      Loop *loop = c->loops.v[c->loops.n - s->depth];
      if (loop->phase < 0 || loop->phase >= 2)
        fail(s->loc, "prolog^/last^/epilog^ cannot jump to their own loop");
      if (loop->phase == 1)
        for (int j = 0; j < loop->first.n; j++)
          if (((Var *)loop->first.v[j])->unavailable)
            fail(s->loc,
                 "cannot jump before all first^ bindings are initialized");
      if (loop->phase == 1)
        remember_focus(c, loop, s->loc, false);
      cleanup(c, loop->bindings, s->loc);
      Instr *br = emit(c, I_BR, s->loc);
      br->label = s->kind == S_BREAK ? loop->end : loop->update;
      c->terminated = true;
    }
    else if (s->kind == S_EXPR) {
      Value v = expression(c, s->a, NULL);
      if (v.type->owned)
        fail(s->loc, "owned temporary requires a binding");
      if (s->a->kind != E_CALL)
        fail(s->loc, "only calls can be expression statements");
    }
    else if (s->kind == S_RETURN) {
      Type *t = c->in->result;
      Value v = {.id = -1, .type = type_builtin(c->p, T_VOID)};
      if (s->a) {
        if (t->kind == T_VOID) {
          expression(c, s->a, NULL);
          fail(s->loc, "procedure or void function cannot return a value");
        }
        if (t->owned && c->in->result_local) {
          if (s->a->kind != E_NAME || strcmp(s->a->text, c->in->result_local))
            fail(
                s->loc,
                "owned result must return the designated construction binding");
          Var *r = lookup(c, s->a->text);
          if (!r || r->slot != c->in->result_slot)
            fail(s->loc, "owned result binding was shadowed");
          v = variable(r);
        } else
          v = construct(c, s->a, t, c->in->result_slot);
      } else if (t->kind != T_VOID)
        fail(s->loc, "missing return value");
      for (int j = 0; j < c->root_count; j++)
        if ((v.deps & (UINT64_C(1) << j)) && !c->root_external[j])
          fail(s->loc, "cannot return a reference rooted in a local owner");
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
      if (cond.type->kind != T_BOOL)
        fail(s->loc, "condition requires bool");
      int yes = ++c->in->labels, no = ++c->in->labels, end = ++c->in->labels;
      Instr *i = emit(c, I_CBR, s->loc);
      i->a = cond.id;
      i->label = yes;
      i->other = no;
      i = emit(c, I_LABEL, s->loc);
      i->label = yes;
      if (s->internal)
        c->lexical--;
      List facts = save_facts(&c->vars);
      refine_condition(c->p, &c->vars, &c->flow, s->a, true);
      statements(c, &s->body, true);
      restore_facts(&c->vars, facts);
      if (s->internal)
        c->lexical++;
      bool a = c->terminated;
      if (!a) {
        i = emit(c, I_BR, s->loc);
        i->label = end;
      }
      c->terminated = false;
      i = emit(c, I_LABEL, s->loc);
      i->label = no;
      if (s->internal)
        c->lexical--;
      refine_condition(c->p, &c->vars, &c->flow, s->a, false);
      statements(c, &s->other, true);
      restore_facts(&c->vars, facts);
      if (s->internal)
        c->lexical++;
      bool b = c->terminated;
      if (!b) {
        i = emit(c, I_BR, s->loc);
        i->label = end;
      }
      c->terminated = a && b;
      if (!c->terminated) {
        i = emit(c, I_LABEL, s->loc);
        i->label = end;
      }
    } else if (s->kind == S_WHILE)
      loop_code(c, s);
  }
  if (scope) {
    if (!c->terminated)
      cleanup(c, start,
              body->n ? ((Stmt *)body->v[body->n - 1])->loc : c->in->fn->loc);
    c->vars.n = start;
    c->depth--;
    c->lexical--;
  }
}
static void return_names(List *body, char **name, bool *valid) {
  for (int j = 0; j < body->n; j++) {
    Stmt *s = body->v[j];
    if (s->kind == S_RETURN) {
      if (!s->a || s->a->kind != E_NAME) {
        *valid = false;
        continue;
      }
      if (*name && strcmp(*name, s->a->text))
        *valid = false;
      else
        *name = s->a->text;
    }
    return_names(&s->body, name, valid);
    return_names(&s->other, name, valid);
    return_names(&s->prolog, name, valid);
    return_names(&s->pre, name, valid);
    return_names(&s->first, name, valid);
    return_names(&s->last, name, valid);
    return_names(&s->epilog, name, valid);
    return_names(&s->update, name, valid);
  }
}
/* Return inference is a fixed point over concrete call instances. Unknown
   results stay unknown across back edges until a return or operation seeds
   them. */
typedef struct {
  Program *p;
  Instance *in;
  List vars;
  Type *returned;
  Expr *return_expr;
  bool value_return;
  int lexical;
  NilFlow flow;
  bool raw_name;
} Infer;

static Infer infer_context(Context *c)
{
  Infer out = {.p = c->p, .in = c->in, .lexical = c->lexical};
  for (int j = 0; j < c->vars.n; j++) {
    Var *v = mem(sizeof(*v));
    *v = *(Var *)c->vars.v[j];
    v->alias = NULL;
    add(&out.vars, v);
  }
  for (int j = 0; j < c->vars.n; j++) {
    Var *old = c->vars.v[j];
    for (int k = 0; k < c->vars.n; k++) {
      if (old->alias && old->alias == c->vars.v[k]) {
        ((Var *)out.vars.v[j])->alias = out.vars.v[k];
      }
    }
  }
  return out;
}
static Var *infer_variable(Infer *c, Expr *e)
{
  Context scope = {.vars = c->vars, .lexical = c->lexical};
  return e->kind == E_NAME ? selected(&scope, e) : NULL;
}
static void infer_guard(Infer *c, Expr *test, Expr *name, bool positive)
{
  Context scope = {.vars = c->vars, .lexical = c->lexical};
  remember_guard(&c->flow, &scope, test, name, positive);
}
static Var *infer_alias(Infer *c, Expr *e)
{
  if (e->kind == E_UNARY && !strcmp(e->text, "&") && e->a->kind == E_NAME) {
    return infer_variable(c, e->a);
  }
  Var *v = infer_variable(c, e);
  return v ? v->alias : NULL;
}
static void infer_mutation(Infer *c, Expr *call, List *types, int offset)
{
  if (offset && types->n) {
    Type *receiver = types->v[0];
    if (receiver && receiver->kind == T_REF) {
      layout(receiver->elem, call->loc);
      if (receiver->elem->refs) {
        forget_roots(&c->vars, 0, true);
        return;
      }
    }
  }
  for (int j = 0; j < call->args.n; j++) {
    Type *type = types->v[j + offset];
    if (type) {
      layout(type, call->loc);
      if (!type->refs) {
        continue;
      }
    }
    Expr *arg = call->args.v[j];
    Var *target = infer_alias(c, arg);
    if (target) {
      layout(target->type, arg->loc);
      if (!target->type->refs) {
        target->version++;
        target->narrowed = NULL;
        continue;
      }
    }
    /* Unknown references may alias a local whose address was passed earlier. */
    Var *v = infer_variable(c, arg);
    if (target || !v || v->type->refs) {
      forget_roots(&c->vars, 0, true);
      return;
    }
  }
}
static Function *lookup_function(Program *p, const char *name, int arity) {
  for (int j = 0; j < p->functions.n; j++) {
    Function *f = p->functions.v[j];
    if (!strcmp(f->name, name) && f->params.n == arity)
      return f;
  }
  return NULL;
}
static Type *infer_expr(Infer *c, Expr *e, Type *expected);
static Type *infer_lookup(Infer *c, const char *name) {
  for (int j = c->vars.n - 1; j >= 0; j--) {
    Var *v = c->vars.v[j];
    if (!strcmp(v->name, name))
      return !c->raw_name && v->narrowed ? v->narrowed : v->type;
  }
  return NULL;
}
static bool integer_literal(Expr *e) {
  return e &&
         (e->kind == E_INT ||
          (e->kind == E_UNARY && !strcmp(e->text, "-") && e->a->kind == E_INT));
}
static bool numeric(Type *t) {
  return t && (kind_integer(t->kind) || kind_float(t->kind));
}
static Type *join_types(Program *p, Type *a, Expr *ae, Type *b, Expr *be,
                        Loc loc)
{
  if (!a)
    return b;
  if (!b || a == b)
    return a;
  if (integer_literal(ae) && numeric(b))
    return b;
  if (integer_literal(be) && numeric(a))
    return a;
  if (ae && ae->kind == E_FLOAT && b && kind_float(b->kind))
    return b;
  if (be && be->kind == E_FLOAT && a && kind_float(a->kind))
    return a;
  if (sum_accepts(a, b)) {
    return a;
  }
  if (sum_accepts(b, a)) {
    return b;
  }
  if (a->kind == T_NIL || b->kind == T_NIL || a->structural || b->structural) {
    return type_union(p, a, b, loc);
  }
  fail(loc, "inferred types disagree: '%s' and '%s'", type_name(a),
       type_name(b));
}
static Type *infer_call(Infer *c, Expr *e) {
  Expr *target = e->a;
  int native = random_intrinsic(c->in, target);
  if (native >= 0)
    return type_builtin(c->p, native == 0 || native == 5 ? T_BOOL : T_F64);
  Function *f = NULL;
  Def *self = NULL;
  List actual = {0};
  if (target->kind == E_FUNCTION) {
    f = target->function;
  } else if (target->kind == E_NAME) {
    for (int k = T_U8; k <= T_F64; k++)
      if (!strcmp(target->text, builtin_name(k))) {
        for (int j = 0; j < e->args.n; j++)
          infer_expr(c, e->args.v[j], NULL);
        return type_builtin(c->p, k);
      }
    if (!strcmp(target->text, "print") || !strcmp(target->text, "assert")) {
      for (int j = 0; j < e->args.n; j++)
        infer_expr(c, e->args.v[j], NULL);
      return type_builtin(c->p, T_VOID);
    }
    if ((f = this_function(c->p, c->in, target))) {
      self = f == c->in->fn ? c->in->self : NULL;
      if (self)
        add(&actual, infer_lookup(c, "self^"));
    }
    else {
      f = lookup_function(c->p, target->text, e->args.n);
    }
  } else if (target->kind == E_MEMBER) {
    if (target->a->kind == E_NAME && !strcmp(target->text, "new")) {
      Def *d = lookup_def(c->p, target->a->text);
      if (d)
        return d->type;
    }
    Type *base = infer_expr(c, target->a, NULL);
    if (base && base->kind == T_REF)
      base = base->elem;
    if (base && base->kind == T_UNION &&
        (!strcmp(target->text, "is") || !strcmp(target->text, "get"))) {
      if (!strcmp(target->text, "is"))
        return type_builtin(c->p, T_BOOL);
      if (e->args.n == 1 && ((Expr *)e->args.v[0])->kind == E_NAME) {
        Spec spec = {.kind = SPEC_NAME,
                     .name = ((Expr *)e->args.v[0])->text,
                     .loc = e->loc};
        return type_spec(c->p, &spec);
      }
      return NULL;
    }
    if (base &&
        (base->kind == T_ARRAY || base->kind == T_GROW ||
         base->kind == T_SLICE || base->kind == T_STRING) &&
        !strcmp(target->text, "slice"))
      return type_wrap(
          c->p, T_SLICE,
          base->kind == T_STRING ? type_builtin(c->p, T_U8) : base->elem, 0);
    if (base && base->kind == T_GROW && !strcmp(target->text, "pop"))
      return base->elem;
    if (base && base->kind == T_GROW && !strcmp(target->text, "truncate"))
      return type_builtin(c->p, T_VOID);
    if (base && base->kind == T_GROW && !strcmp(target->text, "append"))
      return type_builtin(c->p, T_VOID);
    if (base && base->kind == T_GROW && !strcmp(target->text, "clear"))
      return type_builtin(c->p, T_VOID);
    if (base && base->kind == T_GROW && !strcmp(target->text, "push")) {
      for (int j = 0; j < e->args.n; j++)
        infer_expr(c, e->args.v[j], base->elem);
      return type_wrap(c->p, T_REF, base->elem, 0);
    }
    if (base && base->kind == T_STRUCT) {
      self = base->def;
      f = method(self, target->text);
      add(&actual, type_wrap(c->p, T_REF, base, 0));
    }
  }
  if (!f)
    return NULL;
  int offset = actual.n;
  bool known = true;
  for (int j = 0; j < e->args.n; j++) {
    Param *param = j + offset < f->params.n ? f->params.v[j + offset] : NULL;
    Type *t =
        infer_expr(c, e->args.v[j],
                   param && param->spec ? type_spec(c->p, param->spec) : NULL);
    Type *want = param && param->spec ? type_spec(c->p, param->spec) : NULL;
    if (t && want && sum_accepts(want, t)) {
      t = want;
    }
    if (!t)
      known = false;
    add(&actual, t);
  }
  if (f->procedure || f->mutable_self) {
    infer_mutation(c, e, &actual, offset);
  }
  if (!known || actual.n != f->params.n)
    return f->result ? type_spec(c->p, f->result) : f->required_result;
  if (!self && f->owner && f->params.n && ((Param *)f->params.v[0])->self) {
    self = outer_receiver(f, actual.v[0], e->loc);
  }
  return instance_for(c->p, f, self, &actual, e->loc)->result;
}
static Type *infer_chain(Infer *c, Expr *e, bool lift)
{
  List steps = {0};
  Expr *root = chain_parts(e, &steps);
  Type *base = infer_expr(c, root, NULL);
  if (!base) {
    return NULL;
  }
  for (int j = 0; j < steps.n; j++) {
    Expr *step = steps.v[j];
    if (step_optional(step)) {
      base = without_nil(c->p, nil_subject(base), step->loc);
      if (!base) {
        return type_builtin(c->p, T_NIL);
      }
    }
    Value value = {.id = -1, .type = base};
    Expr receiver = {.kind = E_SAVED,
                     .loc = step->loc,
                     .compiler_value = &value},
         member = {0};
    Expr copy = step_expression(step, &receiver, &member);
    base = infer_expr(c, &copy, NULL);
    if (!base) {
      return NULL;
    }
  }
  return !lift || base->kind == T_VOID
             ? base
             : type_union(c->p, base, type_builtin(c->p, T_NIL), e->loc);
}
static void infer_statements(Infer *c, List *body, bool scoped);
static Type *infer_expr(Infer *c, Expr *e, Type *expected) {
  if (!e)
    return type_builtin(c->p, T_VOID);
  if (e->constant_spec)
    return type_spec(c->p, e->constant_spec);
  if (expected && expected->kind == T_UNION && e->kind != E_IF) {
    Type *actual = infer_expr(c, e, union_literal_hint(expected, e));
    return actual && sum_accepts(expected, actual) ? expected : actual;
  }
  if (guarded_chain(e)) {
    return infer_chain(c, e, true);
  }
  switch (e->kind) {
  case E_PRESENT: {
    bool raw = c->raw_name;
    c->raw_name = e->a->kind == E_NAME;
    Type *type = infer_expr(c, e->a, NULL);
    c->raw_name = raw;
    if (type && type->kind == T_VOID) {
      fail(e->loc, "presence test requires a value");
    }
    infer_guard(c, e, e->a, true);
    return type_builtin(c->p, T_BOOL);
  }
  case E_COALESCE: {
    Type *base = infer_expr(c, e->a, NULL);
    if (!base) {
      return NULL;
    }
    if (!nullable(base)) {
      fail(e->loc, "?? requires a nil alternative");
    }
    Type *plain = without_nil(c->p, base, e->loc);
    Type *fallback = infer_expr(c, e->b, plain);
    if (fallback && fallback->kind == T_VOID) {
      fail(e->loc, "?? requires a value-producing fallback");
    }
    if (!plain) {
      return fallback;
    }
    if (!fallback) {
      return plain;
    }
    if (sum_accepts(plain, fallback)) {
      return plain;
    }
    if (sum_accepts(fallback, plain)) {
      return fallback;
    }
    return type_union(c->p, plain, fallback, e->loc);
  }
  case E_INT:
    return numeric(expected) ? expected : type_builtin(c->p, T_I32);
  case E_FLOAT:
    return expected && kind_float(expected->kind) ? expected
                                                  : type_builtin(c->p, T_F64);
  case E_BOOL:
    return type_builtin(c->p, T_BOOL);
  case E_STRING:
    return type_builtin(c->p, T_STRING);
  case E_NAME:
    if (e->scope_kind) {
      Context scope = {.vars = c->vars, .lexical = c->lexical};
      Var *v = selected(&scope, e);
      return v ? (!c->raw_name && v->narrowed ? v->narrowed : v->type) : NULL;
    }

    if (!strcmp(e->text, "nil^"))
      return type_builtin(c->p, T_NIL);
    return infer_lookup(c, e->text);
  case E_SELF:
    return infer_lookup(c, e->text);
  case E_FUNCTION:
    fail(e->loc, "function values cannot escape; invoke the literal directly");
  case E_CALL:
    return infer_call(c, e);
  case E_RECORD: {
    if (e->text) {
      Def *d = lookup_def(c->p, e->text);
      return d ? d->type : NULL;
    }
    return expected;
  }
  case E_ARRAY: {
    if (expected && (expected->kind == T_ARRAY || expected->kind == T_GROW)) {
      for (int j = 0; j < e->args.n; j++)
        infer_expr(c, e->args.v[j], expected->elem);
      return expected;
    }
    Type *elem = e->args.n ? infer_expr(c, e->args.v[0], NULL) : NULL;
    for (int j = 1; j < e->args.n; j++)
      infer_expr(c, e->args.v[j], elem);
    return elem ? type_wrap(c->p, T_ARRAY, elem, e->args.n) : NULL;
  }
  case E_MEMBER: {
    Type *base = infer_expr(c, e->a, NULL);
    if (base && base->kind == T_REF)
      base = base->elem;
    if (!base)
      return NULL;
    if ((base->kind == T_ARRAY || base->kind == T_GROW ||
         base->kind == T_STRING || base->kind == T_SLICE) &&
        !strcmp(e->text, "len"))
      return type_builtin(c->p, T_I64);
    Field *f = base->kind == T_STRUCT ? field(base->def, e->text) : NULL;
    return f ? f->type : NULL;
  }
  case E_INDEX: {
    Type *base = infer_expr(c, e->a, NULL);
    if (base && base->kind == T_REF)
      base = base->elem;
    infer_expr(c, e->b, type_builtin(c->p, T_I64));
    return base && base->kind == T_STRING ? type_builtin(c->p, T_U8)
           : base && (base->kind == T_ARRAY || base->kind == T_GROW ||
                      base->kind == T_SLICE)
               ? base->elem
               : NULL;
  }
  case E_UNARY: {
    bool raw = c->raw_name;
    if (!strcmp(e->text, "&") && e->a->kind == E_NAME) {
      c->raw_name = true;
    }
    Type *a = infer_expr(c, e->a,
                         (!strcmp(e->text, "-") || !strcmp(e->text, "bitnot^"))
                             ? expected
                             : NULL);
    c->raw_name = raw;
    if (!strcmp(e->text, "!"))
      return type_builtin(c->p, T_BOOL);
    if (!a)
      return NULL;
    if (!strcmp(e->text, "&"))
      return type_wrap(c->p, T_REF, a, 0);
    if (!strcmp(e->text, "*"))
      return a->kind == T_REF ? a->elem : NULL;
    return a;
  }
  case E_SCOPE: {
    int begin = c->vars.n;
    c->lexical++;
    infer_statements(c, &e->bindings, false);
    Type *t = infer_expr(c, e->a, expected);
    c->vars.n = begin;
    c->lexical--;
    return t;
  }
  case E_EXPECT:
    return infer_expr(c, e->a, infer_expr(c, e->b, NULL));
  case E_SAVED:
    if (e->compiler_value) {
      return ((Value *)e->compiler_value)->type;
    }
    return ((Slot *)c->in->slots.v[e->saved_slot])->type;
  case E_CHAIN: {
    Type *t = infer_expr(c, e->args.v[0], NULL);
    for (int j = 1; j < e->args.n; j++)
      t = infer_expr(c, e->args.v[j], t);
    return type_builtin(c->p, T_BOOL);
  }
  case E_IF: {
    infer_expr(c, e->a, type_builtin(c->p, T_BOOL));
    List facts = save_facts(&c->vars);
    refine_condition(c->p, &c->vars, &c->flow, e->a, true);
    Type *a = infer_expr(c, e->b, expected);
    restore_facts(&c->vars, facts);
    refine_condition(c->p, &c->vars, &c->flow, e->a, false);
    Type *b = infer_expr(c, e->c, expected);
    restore_facts(&c->vars, facts);
    return join_types(c->p, a, e->b, b, e->c, e->loc);
  }
  case E_BIN: {
    bool logic = !strcmp(e->text, "and^") || !strcmp(e->text, "or^") ||
                 !strcmp(e->text, "&&") || !strcmp(e->text, "||");
    bool comparison = strchr("=!<>", e->text[0]) != NULL;
    Type *a = infer_expr(c, e->a, comparison || logic ? NULL : expected);
    List facts = {0};
    if (logic) {
      facts = save_facts(&c->vars);
      refine_condition(c->p, &c->vars, &c->flow, e->a,
                       !strcmp(e->text, "and^"));
    }
    Type *b = infer_expr(c, e->b, !strcmp(e->text, "bitshift^") ? NULL : a);
    if (logic) {
      restore_facts(&c->vars, facts);
    }
    if (comparison && (nil_name(e->a) || nil_name(e->b)) &&
        (!strcmp(e->text, "=") || !strcmp(e->text, "!="))) {
      infer_guard(c, e, nil_name(e->a) ? e->b : e->a, !strcmp(e->text, "!="));
    }
    if (comparison || logic)
      return type_builtin(c->p, T_BOOL);
    if (!strcmp(e->text, "bitshift^"))
      return a;
    return join_types(c->p, a, e->a, b, e->b, e->loc);
  }
  default:
    return NULL;
  }
}
static void infer_statements(Infer *c, List *body, bool scoped) {
  int mark = c->vars.n;
  if (scoped)
    c->lexical++;
  for (int j = 0; j < body->n; j++) {
    Stmt *s = body->v[j];
    if (s->kind == S_BIND) {
      Type *t = s->spec ? type_spec(c->p, s->spec) : NULL;
      Type *actual = infer_expr(c, s->a, t);
      Var *v = mem(sizeof(*v));
      v->name = s->name;
      v->lexical = c->lexical;
      v->type = t ? t : actual;
      v->alias = infer_alias(c, s->a);
      add(&c->vars, v);
    } else if (s->kind == S_BIND_MANY) {
      List pending = {0};
      for (int k = 0; k < s->body.n; k++) {
        Stmt *b = s->body.v[k];
        Type *t = b->spec ? type_spec(c->p, b->spec) : NULL;
        Type *actual = infer_expr(c, b->a, t);
        Var *v = mem(sizeof(*v));
        v->name = b->name;
        v->lexical = c->lexical;
        v->type = t ? t : actual;
        v->alias = infer_alias(c, b->a);
        add(&pending, v);
      }
      for (int k = 0; k < pending.n; k++)
        add(&c->vars, pending.v[k]);
    }
    else if (s->kind == S_ASSIGN) {
      int count = s->targets.n ? s->targets.n : 1;
      for (int k = 0; k < count; k++) {
        Expr *dst = s->targets.n ? s->targets.v[k] : s->a;
        Var *v = infer_variable(c, dst);
        Type *hint = v ? v->type : NULL;
        if (s->op && v && v->narrowed) {
          hint = v->narrowed;
        }
        if (s->op && s->optional && hint) {
          hint = without_nil(c->p, hint, s->loc);
        }
        infer_expr(c, s->values.n ? s->values.v[k] : s->b, hint);
      }
      for (int k = 0; k < count; k++) {
        Expr *dst = s->targets.n ? s->targets.v[k] : s->a;
        Var *v = infer_variable(c, dst);
        if (v) {
          Type *narrow = v->narrowed;
          v->version++;
          v->narrowed = s->op ? narrow : NULL;
          if (!s->op) {
            v->alias = infer_alias(c, s->values.n ? s->values.v[k] : s->b);
          }
        }
        else {
          forget_roots(&c->vars, 0, true);
        }
      }
    }
    else if (s->kind == S_RETURN) {
      Type *hint = c->in->fn->result ? type_spec(c->p, c->in->fn->result)
                                     : c->in->fn->required_result;
      Type *t = infer_expr(c, s->a, hint);
      c->value_return |= s->a != NULL;
      c->returned =
          join_types(c->p, c->returned, c->return_expr, t, s->a, s->loc);
      if (!c->return_expr || !integer_literal(s->a))
        c->return_expr = s->a;
    }
    else if (s->kind == S_WHILE) {
      int begin = c->vars.n;
      List loop_facts = save_facts(&c->vars);
      infer_statements(c, &s->prolog, false);
      infer_statements(c, &s->pre, false);
      infer_expr(c, s->a, type_builtin(c->p, T_BOOL));
      refine_condition(c->p, &c->vars, &c->flow, s->a, true);
      infer_statements(c, &s->first, false);
      c->lexical--;
      infer_statements(c, &s->body, true);
      c->lexical++;
      c->lexical--;
      infer_statements(c, &s->update, true);
      infer_statements(c, &s->last, true);
      infer_statements(c, &s->epilog, true);
      c->lexical++;
      c->vars.n = begin;
      restore_facts(&c->vars, loop_facts);
    }
    else if (s->kind == S_IF) {
      infer_expr(c, s->a, type_builtin(c->p, T_BOOL));
      if (s->internal)
        c->lexical--;
      List facts = save_facts(&c->vars);
      refine_condition(c->p, &c->vars, &c->flow, s->a, true);
      infer_statements(c, &s->body, true);
      restore_facts(&c->vars, facts);
      refine_condition(c->p, &c->vars, &c->flow, s->a, false);
      infer_statements(c, &s->other, true);
      restore_facts(&c->vars, facts);
      if (s->internal)
        c->lexical++;
      infer_statements(c, &s->update, true);
    }
    else if (s->kind == S_BLOCK) {
      infer_statements(c, &s->body, true);
    }
    else {
      infer_expr(c, s->a, NULL);
      if (s->b)
        infer_expr(c, s->b, NULL);
    }
  }
  if (scoped) {
    c->vars.n = mark;
    c->lexical--;
  }
}
static List first_types(Context *c, Stmt *s) {
  Infer infer = infer_context(c);
  infer_statements(&infer, &s->pre, false);
  int begin = infer.vars.n;
  infer_statements(&infer, &s->first, false);
  List out = {0};
  for (int j = begin; j < infer.vars.n; j++) {
    Var *v = infer.vars.v[j];
    for (int k = c->vars.n - 1; k >= 0; k--) {
      Var *old = c->vars.v[k];
      if (old->depth < c->depth)
        break;
      if (!strcmp(old->name, v->name))
        fail(s->loc, "duplicate binding '%s'", v->name);
    }
    for (int k = 0; k < out.n; k++)
      if (!strcmp(((Var *)out.v[k])->name, v->name))
        fail(s->loc, "duplicate binding '%s'", v->name);
    for (int k = 0; k < s->first.n; k++) {
      Stmt *b = s->first.v[k];
      if (b->kind == S_BIND && !strcmp(b->name, v->name))
        v->mut = b->mut;
      if (b->kind == S_BIND_MANY)
        for (int q = 0; q < b->body.n; q++) {
          Stmt *part = b->body.v[q];
          if (!strcmp(part->name, v->name))
            v->mut = part->mut;
        }
    }
    add(&out, v);
  }
  return out;
}
static void infer_results(Program *p) {
  for (int round = 0; round < 1024; round++) {
    bool changed = false;
    int count = p->instances.n;
    for (int j = 0; j < p->instances.n; j++) {
      Instance *in = p->instances.v[j];
      Infer c = {
          .p = p, .in = in, .lexical = in->fn->lexical ? in->fn->lexical : 1};
      for (int k = 0; k < in->fn->params.n; k++) {
        Param *param = in->fn->params.v[k];
        Var *v = mem(sizeof(*v));
        v->name = param->name;
        v->lexical = c.lexical;
        v->type = in->params.v[k];
        add(&c.vars, v);
      }
      infer_statements(&c, &in->fn->body, false);
      Type *result = c.returned;
      if (!result && !c.value_return)
        result = type_builtin(p, T_VOID);
      if (!in->fn->result && !in->fn->required_result && result &&
          result != in->result) {
        if (in->state)
          fail(in->fn->loc, "return inference changed after code generation");
        in->result = result;
        changed = true;
      }
    }
    if (count != p->instances.n)
      changed = true;
    if (!changed)
      return;
  }
  fail((Loc){p->path, 1, 1}, "return type inference did not converge");
}
static Type *chain_type(Context *c, Expr *e)
{
  Infer in = infer_context(c);
  return infer_chain(&in, e, true);
}
static Type *chain_place_type(Context *c, Expr *e)
{
  Infer in = infer_context(c);
  return infer_chain(&in, e, false);
}
static Type *expression_type(Context *c, Expr *e, Type *expected) {
  Infer inference = infer_context(c);
  return infer_expr(&inference, e, expected);
}
static Instance *instance_for(Program *p, Function *f, Def *self, List *actual,
                              Loc at) {
  for (int j = 0; j < p->instances.n; j++) {
    Instance *in = p->instances.v[j];
    if (in->fn != f || in->self != self || in->params.n != actual->n)
      continue;
    bool same = true;
    for (int k = 0; k < actual->n; k++)
      if (in->params.v[k] != actual->v[k])
        same = false;
    if (same)
      return in;
  }
  if (p->instances.n >= 512)
    fail(at, "template specialization limit exceeded");
  Instance *in = mem(sizeof(*in));
  in->fn = f;
  in->self = self;
  in->params = *actual;
  in->result = f->result ? type_spec(p, f->result) : f->required_result;
  in->id = p->instances.n;
  in->symbol = fmt("rh_fn_%d", in->id);
  if (p->modules.n > 1 && f != p->entry) {
    char *identity = fmt(
        "%s:%s:%s", f->module && f->module->name ? f->module->name : "<entry>",
        f->name, self ? self->name : "");
    for (int j = 0; j < actual->n; j++)
      identity = fmt("%s:%s", identity, type_name(actual->v[j]));
    in->symbol = fmt("rh_fn_%s", link_identity(identity));
  }
  in->result_slot = -1;
  add(&p->instances, in);
  return in;
}
static Instance *instantiate(Program *p, Function *f, Def *self, List *actual,
                             Loc at) {
  Instance *in = instance_for(p, f, self, actual, at);
  if (in->state)
    return in;
  infer_results(p);
  if (!in->result)
    fail(f->loc,
         "cannot infer return type of '%s'; add a grounded return or an "
         "explicit result type",
         f->name);
  layout(in->result, f->loc);
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
      return_names(&f->body, &in->result_local, &valid);
      if (!valid)
        in->result_local = NULL;
    }
  }
  for (int j = 0; j < f->params.n; j++) {
    Param *a = f->params.v[j];
    for (int k = 0; k < j; k++)
      if (strcmp(a->name, "_^") &&
          !strcmp(a->name, ((Param *)f->params.v[k])->name))
        fail(a->loc, "duplicate parameter '%s'", a->name);
    Type *t = actual->v[j];
    layout(t, a->loc);
    if (t->kind == T_VOID)
      fail(a->loc, "parameter cannot have void type");
    if (t->owned)
      fail(a->loc, "owned by-value parameters are not supported; use '&'");
    Var *v = mem(sizeof(*v));
    v->name = !strcmp(a->name, "_^") ? fmt("\001discard_param%d", j) : a->name;
    v->type = t;
    v->slot = slot(&c, t, false);
    v->depth = 1;
    v->lexical = c.lexical;
    v->mut = true;
    v->parameter = true;
    v->root = root(&c, 1, false, a->loc);
    if (t->refs)
      v->deps = root(&c, 0, true, a->loc);
    if (a->self)
      c.self_roots = v->deps;
    add(&c.vars, v);
  }
  statements(&c, &f->body, false);
  if (!c.terminated) {
    if (in->result->kind != T_VOID)
      fail(f->loc, "not all paths return a value in '%s'", f->name);
    cleanup(&c, 0, f->loc);
    Instr *i = emit(&c, I_RETURN, f->loc);
    i->type = in->result;
  }
  in->state = 2;
  return in;
}
void analyze(Program *p) {
  for (int j = 0; j < p->defs.n; j++) {
    Def *d = p->defs.v[j];
    for (int k = 0; k < j; k++)
      if (!strcmp(d->name, ((Def *)p->defs.v[k])->name))
        fail(d->loc, "duplicate def^ '%s'", d->name);
    Type *t = mem(sizeof(*t));
    t->kind = d->union_specs.n ? T_UNION : T_STRUCT;
    t->def = d;
    if (p->modules.n > 1 || (d->module && d->module->name))
      t->symbol =
          fmt("%%rh_type_%s",
              link_identity(fmt("%s:%s",
                                d->module && d->module->name ? d->module->name
                                                             : "<entry>",
                                d->name)));
    t->id = p->types.n;
    d->type = t;
    add(&p->types, t);
  }
  for (int j = 0; j < p->defs.n; j++)
    compose(p, p->defs.v[j]);
  for (int j = 0; j < p->defs.n; j++) {
    Def *d = p->defs.v[j];
    layout(d->type, d->loc);
  }
  for (int j = 0; j < p->functions.n; j++) {
    Function *f = p->functions.v[j];
    if (lookup_def(p, f->name))
      fail(f->loc, "function conflicts with def^ '%s'", f->name);
    for (int k = 0; k < j; k++)
      if (!strcmp(f->name, ((Function *)p->functions.v[k])->name) &&
          f->params.n == ((Function *)p->functions.v[k])->params.n)
        fail(f->loc, "duplicate function '%s' with %d arguments", f->name,
             f->params.n);
  }
  List none = {0};
  p->entry->required_result = type_builtin(p, T_VOID);
  instantiate(p, p->entry, NULL, &none, p->entry->loc);
  /* Abstract nominal parameters require a concrete call-site specialization. */
  for (int j = 0; j < p->functions.n; j++) {
    Function *f = p->functions.v[j];
    if (!f->templ) {
      List types = {0};
      bool concrete = true;
      for (int k = 0; k < f->params.n; k++) {
        Param *a = f->params.v[k];
        Type *t = type_spec(p, a->spec);
        Type *base = t->kind == T_REF ? t->elem : t;
        if (base->kind == T_STRUCT && base->def->abstract)
          concrete = false;
        add(&types, t);
      }
      if (concrete)
        instantiate(p, f, NULL, &types, f->loc);
    }
  }
  for (int j = 0; j < p->defs.n; j++) {
    Def *d = p->defs.v[j];
    if (d->abstract)
      continue;
    for (int k = 0; k < d->methods.n; k++) {
      Function *f = d->methods.v[k];
      if (f->templ)
        continue;
      List types = {0};
      for (int n = 0; n < f->params.n; n++) {
        Param *a = f->params.v[n];
        add(&types,
            a->self ? type_wrap(p, T_REF, d->type, 0) : type_spec(p, a->spec));
      }
      instantiate(p, f, d, &types, f->loc);
    }
  }
  for (int j = 0; j < p->types.n; j++) {
    Type *t = p->types.v[j];
    if (t->kind == T_UNION) {
      layout(t, t->def->loc);
    }
  }
}
