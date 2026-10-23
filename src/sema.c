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
  } else if (k == T_GROW) {
    t->size = 24;
    t->align = 8;
    t->owned = true;
  }
  add(&p->types, t);
  return t;
}
Type *type_spec(Program *p, Spec *s) {
  if (!s)
    return type_builtin(p, T_VOID);
  if (s->kind != SPEC_NAME)
    return type_wrap(p,
                     s->kind == SPEC_REF    ? T_REF
                     : s->kind == SPEC_GROW ? T_GROW
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
  if (t->kind == T_STRUCT)
    return t->def->name;
  if (t->kind == T_REF)
    return fmt("&%s", type_name(t->elem));
  if (t->kind == T_GROW)
    return fmt("%s[>..]", type_name(t->elem));
  return fmt("%s[%d]", type_name(t->elem), t->count);
}
bool derives(Def *d, Def *b) {
  if (d == b)
    return true;
  for (int i = 0; i < d->bases.n; i++)
    if (derives(d->bases.v[i], b))
      return true;
  return false;
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
  for (int i = 0; i < d->base_names.n; i++) {
    char *n = d->base_names.v[i];
    Def *b = lookup_def(p, n);
    if (!b)
      fail(d->loc, "unknown composition base '%s'", n);
    compose(p, b);
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
  if (t->kind < T_STRUCT || t->kind == T_REF || t->kind == T_GROW)
    return;
  if (t->size == SIZE_MAX)
    fail(l, "recursive by-value layout in '%s'", type_name(t));
  if (t->align)
    return;
  t->size = SIZE_MAX;
  size_t size = 0, align = 1;
  if (t->kind == T_ARRAY) {
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
      size = (size + f->type->align - 1) / f->type->align * f->type->align;
      size += f->type->size;
      if (f->type->align > align)
        align = f->type->align;
      t->refs |= f->type->refs;
      t->owned |= f->type->owned;
    }
  }
  t->align = align;
  t->size = (size + align - 1) / align * align;
  if (!t->size)
    t->size = 1;
}

typedef struct Var Var;
struct Var {
  char *name;
  Type *type;
  int slot, depth;
  uint64_t root, deps;
  bool mut, parameter;
};
typedef struct {
  int id;
  Type *type;
  uint64_t deps, storage;
  Var *var;
  bool addressable;
} Value;
typedef struct {
  Program *p;
  Instance *in;
  Def *access;
  List vars;
  int depth, root_count;
  int root_depth[64];
  bool root_external[64];
  bool terminated;
  uint64_t self_roots;
  List default_stack;
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
static Instr *emit(Context *c, int op, Loc l) {
  Instr *i = mem(sizeof(*i));
  i->op = op;
  i->loc = l;
  i->dst = i->a = i->b = -1;
  add(&c->in->code, i);
  return i;
}
static int slot(Context *c, Type *t, bool alias) {
  Slot *s = mem(sizeof(*s));
  s->type = t;
  s->alias = alias;
  int id = c->in->slots.n;
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
static Value variable(Var *v) {
  return (Value){v->slot, v->type, v->deps, v->root, v, true};
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
    if (v->root & dst.storage)
      v->deps |= src.deps;
  }
  if (dst.var)
    dst.var->deps |= src.deps;
}
static Value copy_to(Context *c, Value dst, Value src, Loc l, bool init) {
  if (dst.type != src.type)
    fail(l, "expected '%s', found '%s'", type_name(dst.type),
         type_name(src.type));
  if (src.type->owned)
    fail(l, "owned region values cannot be copied; construct at the "
            "destination or borrow with '&'");
  if (!init)
    store_check(c, dst, src, l);
  Instr *i = emit(c, I_COPY, l);
  i->dst = dst.id;
  i->a = src.id;
  i->type = dst.type;
  dst.deps = src.deps;
  if (dst.var)
    dst.var->deps |= src.deps;
  return dst;
}
static Value snapshot(Context *c, Value src, Loc l) {
  if (src.type->kind == T_VOID || src.type->owned)
    return src;
  Value dst = value(c, src.type);
  return copy_to(c, dst, src, l, true);
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
  Instr *i = emit(c, I_FIELD, l);
  i->dst = r.id;
  i->a = base.id;
  i->type = base.type;
  i->label = f->index;
  return r;
}
static void release(Context *c, Type *t, int id, Loc l) {
  if (t->kind == T_GROW) {
    Instr *i = emit(c, I_RELEASE, l);
    i->a = id;
    i->type = t;
  } else if (t->kind == T_STRUCT && t->owned) {
    for (int j = 0; j < t->def->fields.n; j++) {
      Field *f = t->def->fields.v[j];
      if (f->type->owned) {
        Value b = {0};
        b.id = id;
        b.type = t;
        b.addressable = true;
        Def *access = c->access;
        c->access = f->owner;
        Value v = field_value(c, b, f, l);
        c->access = access;
        release(c, f->type, v.id, l);
      }
    }
  }
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
static Value call(Context *c, Expr *e, Type *expected, int destination) {
  Program *p = c->p;
  Expr *target = e->a;
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
    if (base.type->kind == T_GROW && !strcmp(target->text, "push")) {
      if (e->args.n != 1)
        fail(e->loc, "push expects one element");
      Value v = expression(c, e->args.v[0], base.type->elem);
      if (v.type != base.type->elem)
        fail(e->loc, "push element type mismatch");
      store_check(c, base, v, e->loc);
      Instr *i = emit(c, I_PUSH, e->loc);
      i->a = base.id;
      i->b = v.id;
      i->type = base.type;
      return (Value){.id = -1, .type = type_builtin(p, T_VOID)};
    }
    if (base.type->kind != T_STRUCT)
      fail(e->loc, "method call requires a structure");
    self = base.type->def;
    f = method(self, target->text);
    if (!f)
      fail(e->loc, "'%s' has no method '%s'", self->name, target->text);
    access_check(c, f->priv, f->owner, e->loc, f->name);
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
  } else if (target->kind == E_NAME && !strcmp(target->text, "this^")) {
    if (c->in->fn == p->entry)
      fail(e->loc, "this^ requires an enclosing function or procedure");
    f = c->in->fn;
    self = c->in->self;
    if (self) {
      Var *receiver = lookup(c, "self^");
      Value ref = snapshot(c, variable(receiver), e->loc);
      Value *saved = mem(sizeof(*saved));
      *saved = ref;
      add(&values, saved);
      add(&actual, ref.type);
    }
  } else if (target->kind == E_NAME) {
    for (int j = 0; j < p->functions.n; j++) {
      Function *candidate = p->functions.v[j];
      if (!strcmp(candidate->name, target->text)) {
        f = candidate;
        break;
      }
    }
    if (!f)
      fail(e->loc, "unknown function '%s'", target->text);
  } else
    fail(e->loc, "function values and escaping closures are not supported");
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
  return out;
}
static Value construct(Context *c, Expr *e, Type *t, int dst) {
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
    Value a = construct(c, e->b, t, out.id);
    i = emit(c, I_BR, e->loc);
    i->label = end;
    i = emit(c, I_LABEL, e->loc);
    i->label = no;
    Value b = construct(c, e->c, t, out.id);
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
      if (t->elem->owned || t->elem->refs || !t->elem->size)
        fail(e->loc, "growable elements must be fixed-size and contain no "
                     "references in this version");
      Instr *a = emit(c, I_REGION, e->loc);
      a->dst = out.id;
      a->type = t;
    }
    for (int j = 0; j < e->args.n; j++) {
      if (t->kind == T_GROW) {
        Value v = expression(c, e->args.v[j], t->elem);
        if (v.type != t->elem)
          fail(e->loc, "array element type mismatch");
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
  Program *p = c->p;
  Value out = {0};
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
    i->text = e->kind == E_BOOL    ? (!strcmp(e->text, "true^") ? "1" : "0")
              : e->kind == E_FLOAT ? float_constant(e->text, t->kind, e->loc)
                                   : e->text;
    return out;
  }
  if (e->kind == E_NAME || e->kind == E_SELF) {
    if (c->in_default)
      fail(e->loc, "default initializer cannot capture caller locals or self^");
    Var *v = lookup(c, e->text);
    if (!v)
      fail(e->loc,
           "unknown value '%s' (closures and global values are not supported)",
           e->text);
    return variable(v);
  }
  if (e->kind == E_CALL)
    return call(c, e, expected, -1);
  if (e->kind == E_RECORD || e->kind == E_ARRAY || e->kind == E_IF)
    return construct(c, e, expected, -1);
  if (e->kind == E_MEMBER) {
    Value base = deref(c, expression(c, e->a, NULL), e->loc);
    if ((base.type->kind == T_ARRAY || base.type->kind == T_GROW ||
         base.type->kind == T_STRING) &&
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
    if (base.type->kind != T_ARRAY && base.type->kind != T_GROW)
      fail(e->loc, "indexing requires an array");
    Value index = expression(c, e->b, type_builtin(p, T_I64));
    if (!kind_integer(index.type->kind))
      fail(e->loc, "index must be an integer");
    out.type = base.type->elem;
    out.id = slot(c, out.type, true);
    out.storage = base.storage;
    out.deps = out.type->refs ? base.deps : 0;
    out.var = base.var;
    out.addressable = base.addressable;
    Instr *i = emit(c, I_INDEX, e->loc);
    i->dst = out.id;
    i->a = base.id;
    i->b = index.id;
    i->type = base.type;
    return out;
  }
  if (e->kind == E_UNARY) {
    if (!strcmp(e->text, "-") && e->a->kind == E_INT) {
      Expr neg = *e->a;
      neg.text = fmt("-%s", neg.text);
      return literal_int(c, &neg, expected);
    }
    Value a = expression(c, e->a, !strcmp(e->text, "-") ? expected : NULL);
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
    if ((!strcmp(e->text, "!") && a.type->kind != T_BOOL) ||
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
    Value a = expression(c, e->a,
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
      Value b = expression(c, e->b, a.type);
      copy_to(c, out, b, e->loc, true);
      br = emit(c, I_BR, e->loc);
      br->label = end;
      lab = emit(c, I_LABEL, e->loc);
      lab->label = end;
      return out;
    }
    a = snapshot(c, a, e->loc);
    Value b = expression(c, e->b, a.type);
    if (a.type != b.type)
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
static void statements(Context *c, List *body, bool scope) {
  int start = c->vars.n;
  if (scope)
    c->depth++;
  for (int n = 0; n < body->n; n++) {
    Stmt *s = body->v[n];
    if (c->terminated)
      fail(s->loc, "unreachable statement");
    if (s->kind == S_BIND) {
      if (c->in->fn == c->p->entry && c->depth == 1) {
        if (lookup_def(c->p, s->name))
          fail(s->loc, "top-level binding conflicts with def^ '%s'", s->name);
        for (int j = 0; j < c->p->functions.n; j++) {
          Function *f = c->p->functions.v[j];
          if (!strcmp(s->name, f->name))
            fail(s->loc, "top-level binding conflicts with function '%s'",
                 s->name);
        }
      }
      for (int j = c->vars.n - 1; j >= 0; j--) {
        Var *v = c->vars.v[j];
        if (v->depth < c->depth)
          break;
        if (!strcmp(v->name, s->name))
          fail(s->loc, "duplicate binding '%s'", s->name);
      }
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
      Var *var = mem(sizeof(*var));
      var->name = s->name;
      var->slot = v.id;
      var->type = t;
      var->depth = c->depth;
      var->mut = s->mut;
      var->root = root(c, c->depth, false, s->loc);
      var->deps = v.deps;
      Value dst = variable(var);
      dst.var = NULL;
      store_check(c, dst, v, s->loc);
      add(&c->vars, var);
    } else if (s->kind == S_ASSIGN) {
      Value dst = expression(c, s->a, NULL);
      if (!dst.addressable)
        fail(s->loc, "assignment destination is not addressable");
      if (dst.type->owned)
        fail(s->loc,
             "owned values cannot be reassigned while references may exist");
      Value src = expression(c, s->b, dst.type);
      copy_to(c, dst, src, s->loc, false);
    } else if (s->kind == S_EXPR) {
      Value v = expression(c, s->a, NULL);
      if (v.type->owned)
        fail(s->loc, "owned temporary requires a binding");
      if (s->a->kind != E_CALL)
        fail(s->loc, "only calls can be expression statements");
    } else if (s->kind == S_RETURN) {
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
    } else if (s->kind == S_BLOCK)
      statements(c, &s->body, true);
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
      statements(c, &s->body, true);
      bool a = c->terminated;
      if (!a) {
        i = emit(c, I_BR, s->loc);
        i->label = end;
      }
      c->terminated = false;
      i = emit(c, I_LABEL, s->loc);
      i->label = no;
      statements(c, &s->other, true);
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
    } else if (s->kind == S_WHILE) { /* Conservative root history prevents
                                        loop-carried reference escape. */
      int test = ++c->in->labels, loop = ++c->in->labels, end = ++c->in->labels;
      Instr *i = emit(c, I_BR, s->loc);
      i->label = test;
      i = emit(c, I_LABEL, s->loc);
      i->label = test;
      Value cond = expression(c, s->a, type_builtin(c->p, T_BOOL));
      if (cond.type->kind != T_BOOL)
        fail(s->loc, "condition requires bool");
      i = emit(c, I_CBR, s->loc);
      i->a = cond.id;
      i->label = loop;
      i->other = end;
      i = emit(c, I_LABEL, s->loc);
      i->label = loop;
      statements(c, &s->body, true);
      if (!c->terminated) {
        i = emit(c, I_BR, s->loc);
        i->label = test;
      }
      c->terminated = false;
      i = emit(c, I_LABEL, s->loc);
      i->label = end;
    }
  }
  if (scope) {
    if (!c->terminated)
      cleanup(c, start,
              body->n ? ((Stmt *)body->v[body->n - 1])->loc : c->in->fn->loc);
    c->vars.n = start;
    c->depth--;
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
} Infer;

static Function *lookup_function(Program *p, const char *name) {
  for (int j = 0; j < p->functions.n; j++) {
    Function *f = p->functions.v[j];
    if (!strcmp(f->name, name))
      return f;
  }
  return NULL;
}
static Type *infer_expr(Infer *c, Expr *e, Type *expected);
static Type *infer_lookup(Infer *c, const char *name) {
  for (int j = c->vars.n - 1; j >= 0; j--) {
    Var *v = c->vars.v[j];
    if (!strcmp(v->name, name))
      return v->type;
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
static Type *join_types(Type *a, Expr *ae, Type *b, Expr *be, Loc loc) {
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
  fail(loc, "inferred types disagree: '%s' and '%s'", type_name(a),
       type_name(b));
}
static Type *infer_call(Infer *c, Expr *e) {
  Expr *target = e->a;
  Function *f = NULL;
  Def *self = NULL;
  List actual = {0};
  if (target->kind == E_FUNCTION) {
    f = target->function;
  } else if (target->kind == E_NAME) {
    if (!strcmp(target->text, "print") || !strcmp(target->text, "assert")) {
      for (int j = 0; j < e->args.n; j++)
        infer_expr(c, e->args.v[j], NULL);
      return type_builtin(c->p, T_VOID);
    }
    if (!strcmp(target->text, "this^")) {
      if (c->in->fn == c->p->entry)
        fail(e->loc, "this^ requires an enclosing function or procedure");
      f = c->in->fn;
      self = c->in->self;
      if (self)
        add(&actual, infer_lookup(c, "self^"));
    } else
      f = lookup_function(c->p, target->text);
  } else if (target->kind == E_MEMBER) {
    if (target->a->kind == E_NAME && !strcmp(target->text, "new")) {
      Def *d = lookup_def(c->p, target->a->text);
      if (d)
        return d->type;
    }
    Type *base = infer_expr(c, target->a, NULL);
    if (base && base->kind == T_REF)
      base = base->elem;
    if (base && base->kind == T_GROW && !strcmp(target->text, "push")) {
      for (int j = 0; j < e->args.n; j++)
        infer_expr(c, e->args.v[j], base->elem);
      return type_builtin(c->p, T_VOID);
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
    if (!t)
      known = false;
    add(&actual, t);
  }
  if (!known || actual.n != f->params.n)
    return f->result ? type_spec(c->p, f->result) : f->required_result;
  return instance_for(c->p, f, self, &actual, e->loc)->result;
}
static Type *infer_expr(Infer *c, Expr *e, Type *expected) {
  if (!e)
    return type_builtin(c->p, T_VOID);
  switch (e->kind) {
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
         base->kind == T_STRING) &&
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
    return base && (base->kind == T_ARRAY || base->kind == T_GROW) ? base->elem
                                                                   : NULL;
  }
  case E_UNARY: {
    Type *a = infer_expr(c, e->a, !strcmp(e->text, "-") ? expected : NULL);
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
  case E_IF: {
    infer_expr(c, e->a, type_builtin(c->p, T_BOOL));
    Type *a = infer_expr(c, e->b, expected);
    Type *b = infer_expr(c, e->c, expected);
    return join_types(a, e->b, b, e->c, e->loc);
  }
  case E_BIN: {
    bool logic = !strcmp(e->text, "and^") || !strcmp(e->text, "or^") ||
                 !strcmp(e->text, "&&") || !strcmp(e->text, "||");
    bool comparison = strchr("=!<>", e->text[0]) != NULL;
    Type *a = infer_expr(c, e->a, comparison || logic ? NULL : expected);
    Type *b = infer_expr(c, e->b, a);
    if (comparison || logic)
      return type_builtin(c->p, T_BOOL);
    return join_types(a, e->a, b, e->b, e->loc);
  }
  default:
    return NULL;
  }
}
static void infer_statements(Infer *c, List *body, bool scoped) {
  int mark = c->vars.n;
  for (int j = 0; j < body->n; j++) {
    Stmt *s = body->v[j];
    if (s->kind == S_BIND) {
      Type *t = s->spec ? type_spec(c->p, s->spec) : NULL;
      Type *actual = infer_expr(c, s->a, t);
      Var *v = mem(sizeof(*v));
      v->name = s->name;
      v->type = t ? t : actual;
      add(&c->vars, v);
    } else if (s->kind == S_RETURN) {
      Type *hint = c->in->fn->result ? type_spec(c->p, c->in->fn->result)
                                     : c->in->fn->required_result;
      Type *t = infer_expr(c, s->a, hint);
      c->value_return |= s->a != NULL;
      c->returned = join_types(c->returned, c->return_expr, t, s->a, s->loc);
      if (!c->return_expr || !integer_literal(s->a))
        c->return_expr = s->a;
    } else if (s->kind == S_IF || s->kind == S_WHILE) {
      infer_expr(c, s->a, type_builtin(c->p, T_BOOL));
      infer_statements(c, &s->body, true);
      infer_statements(c, &s->other, true);
    } else if (s->kind == S_BLOCK)
      infer_statements(c, &s->body, true);
    else {
      infer_expr(c, s->a, NULL);
      if (s->b)
        infer_expr(c, s->b, NULL);
    }
  }
  if (scoped)
    c->vars.n = mark;
}
static void infer_results(Program *p) {
  for (int round = 0; round < 1024; round++) {
    bool changed = false;
    int count = p->instances.n;
    for (int j = 0; j < p->instances.n; j++) {
      Instance *in = p->instances.v[j];
      Infer c = {.p = p, .in = in};
      for (int k = 0; k < in->fn->params.n; k++) {
        Param *param = in->fn->params.v[k];
        Var *v = mem(sizeof(*v));
        v->name = param->name;
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
static Type *expression_type(Context *c, Expr *e, Type *expected) {
  Infer inference = {.p = c->p, .in = c->in, .vars = c->vars};
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
      if (!strcmp(a->name, ((Param *)f->params.v[k])->name))
        fail(a->loc, "duplicate parameter '%s'", a->name);
    Type *t = actual->v[j];
    layout(t, a->loc);
    if (t->kind == T_VOID)
      fail(a->loc, "parameter cannot have void type");
    if (t->owned)
      fail(a->loc, "owned by-value parameters are not supported; use '&'");
    Var *v = mem(sizeof(*v));
    v->name = a->name;
    v->type = t;
    v->slot = slot(&c, t, false);
    v->depth = 1;
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
    t->kind = T_STRUCT;
    t->def = d;
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
      if (!strcmp(f->name, ((Function *)p->functions.v[k])->name))
        fail(f->loc, "duplicate function '%s'", f->name);
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
}
