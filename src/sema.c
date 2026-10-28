#include "sema_internal.h"
#include "tooling.h"
#include <errno.h>
#include <limits.h>

Def *lookup_def(Program *p, const char *n)
{
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
    if (t->kind == k && (k < T_STRUCT || k == T_NEVER)) {
      return t;
    }
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
  case T_CHAR:
    t->size = t->align = 4;
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
  if (k == T_REF || k == T_CFUNC) {
    t->size = t->align = 8;
    t->refs = k == T_REF;
  } else if (k == T_SLICE || k == T_DYN) {
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
bool nullable(Type *t);
Type *type_union(Program *p, Type *a, Type *b, Loc loc)
{
  if (a->kind == T_NEVER) {
    return b;
  }
  if (b->kind == T_NEVER) {
    return a;
  }
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
Type *type_tuple(Program *p, List *elems, Loc loc)
{
  for (int j = 0; j < p->types.n; j++) {
    Type *t = p->types.v[j];
    if (t->kind != T_STRUCT || !t->def->tuple || t->def->fields.n != elems->n) {
      continue;
    }
    bool same = true;
    for (int k = 0; k < elems->n; k++) {
      same &= ((Field *)t->def->fields.v[k])->type == elems->v[k];
    }
    if (same) {
      return t;
    }
  }
  Type *t = mem(sizeof(*t));
  Def *d = mem(sizeof(*d));
  t->kind = T_STRUCT;
  t->id = p->types.n;
  t->def = d;
  d->type = t;
  d->loc = loc;
  d->state = 2;
  d->public = true;
  d->tuple = true;
  char *identity = str("tuple");
  d->name = str("(");
  for (int j = 0; j < elems->n; j++) {
    Type *elem = elems->v[j];
    if (elem->kind == T_VOID || elem->kind == T_NEVER) {
      fail(loc, "a result cannot have void type");
    }
    char *name = type_name(elem);
    identity = fmt("%s:%d:%zu:%s", identity, elem->kind, strlen(name), name);
    d->name = fmt("%s%s%s", d->name, j ? ", " : "", name);
    Field *f = mem(sizeof(*f));
    f->loc = loc;
    f->name = fmt("%d", j);
    f->type = elem;
    f->owner = d;
    f->index = j;
    add(&d->own_fields, f);
    add(&d->fields, f);
  }
  d->name = fmt("%s)", d->name);
  t->symbol = fmt("%%rh_tuple_%s", link_identity(identity));
  add(&p->types, t);
  layout(t, loc);
  return t;
}
Type *type_range(Program *p)
{
  for (int j = 0; j < p->types.n; j++) {
    if (range_type(p->types.v[j])) {
      return p->types.v[j];
    }
  }
  Type *t = mem(sizeof(*t));
  Def *d = mem(sizeof(*d));
  t->kind = T_STRUCT;
  t->id = p->types.n;
  t->def = d;
  t->symbol = "%rh_range";
  d->type = t;
  d->name = "range^";
  d->state = 2;
  d->public = true;
  for (int j = 0; j < 2; j++) {
    Field *f = mem(sizeof(*f));
    f->name = j ? "\001end" : "start";
    f->type = type_builtin(p, T_I64);
    f->owner = d;
    f->index = j;
    add(&d->own_fields, f);
    add(&d->fields, f);
  }
  add(&p->types, t);
  layout(t, (Loc){0});
  return t;
}
bool range_type(Type *t)
{
  return t && t->kind == T_STRUCT && !strcmp(t->def->name, "range^");
}
Type *type_spec(Program *p, Spec *s) {
  if (!s)
    return type_builtin(p, T_VOID);
  if (s->kind == SPEC_NAME && !strcmp(s->name, "range^")) {
    return type_range(p);
  }
  if (s->kind == SPEC_TUPLE) {
    List elems = {0};
    for (int j = 0; j < s->params.n; j++) {
      add(&elems, type_spec(p, s->params.v[j]));
    }
    return type_tuple(p, &elems, s->loc);
  }
  if (s->kind == SPEC_UNION) {
    return type_union(p, type_spec(p, s->elem), type_spec(p, s->other), s->loc);
  }
  if (s->kind == SPEC_INTERSECTION) {
    return callable_intersection(p, type_spec(p, s->elem),
                                  type_spec(p, s->other), s->loc);
  }
  if (s->kind == SPEC_FUNCTION) {
    List params = {0};
    for (int j = 0; j < s->params.n; j++) {
      Type *param = type_spec(p, s->params.v[j]);
      if (param->kind == T_VOID) {
        fail(s->loc, "callable parameter cannot have void type");
      }
      add(&params, param);
    }
    Type *t = callable_signature(p, &params, type_spec(p, s->elem), s->procedure, NULL);
    return s->foreign ? type_wrap(p, T_CFUNC, t, 0) : t;
  }
  if (s->kind == SPEC_ARRAY && !s->count)
    fail(s->loc, "an array type without a length needs grow^, as in grow^ T[]");
  if (s->kind == SPEC_DYN) {
    fail(s->loc, "dyn^ is a reference; write ref^ dyn^ T");
  }
  if (s->kind == SPEC_REF && s->elem->kind == SPEC_DYN) {
    Type *t = type_spec(p, s->elem->elem);
    if (t->kind != T_STRUCT) {
      fail(s->loc, "dyn^ needs a def^ type, found '%s'", type_name(t));
    }
    return type_wrap(p, T_DYN, t, 0);
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
  tool_record(p, s->loc, d->loc, d->name, d->type, NULL, d, 23, false);
  return d->type;
}
char *type_name(Type *t) {
  if (!t) {
    return "?";
  }
  if (t->kind == T_NEVER) {
    return "<never>";
  }
  if (t->kind == T_INTERSECTION) {
    char *name = str("");
    for (int j = 0; j < t->arms.n; j++) {
      name = fmt("%s%s%s", name, j ? " & " : "", type_name(t->arms.v[j]));
    }
    return name;
  }
  if (t->kind == T_CFUNC) {
    return fmt("externC^ %s", type_name(t->elem));
  }
  if (t->kind == T_FUNCTION) {
    char *name = str(t->procedure ? "p^" : "f^");
    for (int j = 0; j < t->params.n; j++) {
      name = fmt("%s%s%s", name, j ? ", " : " ", type_name(t->params.v[j]));
    }
    return fmt("%s -> %s;", name, type_name(t->result));
  }
  if (t->kind < T_STRUCT)
    return (char *)builtin_name(t->kind);
  if (t->kind == T_STRUCT || t->kind == T_UNION)
    return t->def->name;
  if (t->kind == T_REF)
    return fmt(t->elem->kind == T_UNION ? "ref^ (%s)" : "ref^ %s", type_name(t->elem));
  if (t->kind == T_DYN)
    return fmt("ref^ dyn^ %s", type_name(t->elem));
  if (t->kind == T_SLICE)
    return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION) ? "(%s)[:]"
                                                                    : "%s[:]",
               type_name(t->elem));
  if (t->kind == T_GROW)
    return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION)
                   ? "%s (%s)[]"
                   : "%s %s[]",
               t->count ? "flex^" : "grow^", type_name(t->elem));
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
bool sum_accepts(Type *to, Type *from)
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
Type *nil_subject(Type *t)
{
  return t->kind == T_REF && t->elem->kind == T_UNION ? t->elem : t;
}
bool nullable(Type *t)
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
Type *without_nil(Program *p, Type *t, Loc loc)
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
static bool fits_type(Type *from, Type *to)
{
  if (sum_accepts(to, from) || callable_accepts(to, from)) {
    return true;
  }
  if (from->kind == T_STRUCT && to->kind == T_STRUCT) {
    return derives(from->def, to->def);
  }
  return from->kind == T_REF && to->kind == T_REF &&
         from->elem->kind == T_STRUCT && to->elem->kind == T_STRUCT &&
         derives(from->elem->def, to->elem->def);
}
/* Preserve concrete alternatives: a base-type test does not slice a value. */
Type *fits_subset(Program *p, Type *from, Type *to, bool yes, Loc loc)
{
  if (fits_type(from, to)) {
    return yes ? from : NULL;
  }
  if (from->kind != T_UNION) {
    return yes ? NULL : from;
  }
  Type *subset = NULL;
  int count = 0;
  for (int j = 0; j < from->def->alternatives.n; j++) {
    Type *arm = from->def->alternatives.v[j];
    if (fits_type(arm, to) == yes) {
      subset = subset ? type_union(p, subset, arm, loc) : arm;
      count++;
    }
  }
  return count == from->def->alternatives.n ? from : subset;
}
bool has_error(Type *t)
{
  if (t->kind == T_STRUCT) {
    return t->def->error_variant;
  }
  if (t->kind == T_UNION) {
    for (int j = 0; j < t->def->alternatives.n; j++) {
      if (has_error(t->def->alternatives.v[j])) {
        return true;
      }
    }
  }
  return false;
}
bool only_errors(Type *t)
{
  if (t->kind != T_UNION) {
    return t->kind == T_STRUCT && t->def->error_variant;
  }
  for (int j = 0; j < t->def->alternatives.n; j++) {
    if (!only_errors(t->def->alternatives.v[j])) {
      return false;
    }
  }
  return true;
}
Type *error_join(Program *p, Type *a, Type *b, Loc loc)
{
  return !a ? b : !b ? a : type_union(p, a, b, loc);
}
Type *error_leaves(Program *p, Type *t, Loc loc)
{
  if (t->kind != T_UNION) {
    return only_errors(t) ? t : NULL;
  }
  Type *out = NULL;
  for (int j = 0; j < t->def->alternatives.n; j++) {
    out = error_join(p, out, error_leaves(p, t->def->alternatives.v[j], loc),
                     loc);
  }
  return out;
}
Type *success_type(Program *p, Type *t, Loc loc)
{
  if (only_errors(t)) {
    return type_builtin(p, T_NEVER);
  }
  if (t->kind != T_UNION) {
    return t;
  }
  Type *out = NULL;
  for (int j = 0; j < t->def->alternatives.n; j++) {
    Type *arm = t->def->alternatives.v[j];
    if (has_error(arm) && !only_errors(arm)) {
      fail(loc, "try^/catch^ require errors at the outer union level; nested "
                "mixed nominal unions must be unpacked explicitly");
    }
    if (!only_errors(arm)) {
      out = error_join(p, out, arm, loc);
    }
  }
  return out == NULL ? type_builtin(p, T_NEVER) : !has_error(t) ? t : out;
}
static bool error_accepts(Type *to, Type *from)
{
  if (sum_accepts(to, from)) {
    return true;
  }
  if (from->kind == T_UNION) {
    for (int j = 0; j < from->def->alternatives.n; j++) {
      if (!error_accepts(to, from->def->alternatives.v[j])) {
        return false;
      }
    }
    return true;
  }
  if (to->kind == T_UNION) {
    for (int j = 0; j < to->def->alternatives.n; j++) {
      if (error_accepts(to->def->alternatives.v[j], from)) {
        return true;
      }
    }
  }
  return false;
}
static bool error_overlap(Type *to, Type *from)
{
  if (from->kind == T_UNION) {
    for (int j = 0; j < from->def->alternatives.n; j++) {
      if (error_overlap(to, from->def->alternatives.v[j])) {
        return true;
      }
    }
    return false;
  }
  return only_errors(from) && error_accepts(to, from);
}
Field *field(Def *d, const char *n)
{
  for (int i = 0; i < d->fields.n; i++) {
    Field *f = d->fields.v[i];
    if (!strcmp(f->name, n))
      return f;
  }
  return NULL;
}
Function *method(Def *d, const char *n)
{
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
      Function *m = b->methods.v[j], *old = NULL;
      for (int k = 0; k < d->methods.n; k++) {
        Function *candidate = d->methods.v[k];
        if (!strcmp(candidate->name, m->name) &&
            overload_overlap(p, candidate, m)) {
          old = candidate;
          break;
        }
      }
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
          if (!strcmp(own->name, m->name) &&
              overload_overlap(p, old, own) && overload_overlap(p, m, own)) {
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
    if (field(d, f->name)) {
      fail(f->loc, "field/method name collision '%s'", f->name);
    }
    if (!f->operator_member &&
        (!f->params.n || !((Param *)f->params.v[0])->self)) {
      fail(f->loc, "methods must start with self^ in this compiler");
    }
    if (f->abstract && f->priv) {
      fail(f->loc, "private abstract methods cannot be implemented by another def^");
    }
    Function *old = NULL;
    bool named = false;
    int matches = 0, position = -1;
    for (int j = 0; j < d->methods.n; j++) {
      Function *candidate = d->methods.v[j];
      if (strcmp(candidate->name, f->name)) {
        continue;
      }
      named = true;
      if (overload_overlap(p, candidate, f)) {
        old = candidate;
        position = j;
        matches++;
      }
    }
    if (f->overload) {
      if (!named) {
        fail(f->loc, "overload^ has no existing method '%s'", f->name);
      }
      if (matches) {
        fail(f->loc, "overlapping overload '%s'", f->name);
      }
      add(&d->methods, f);
    }
    else if (old) {
      if (matches != 1) {
        fail(f->loc, "override^ overlaps multiple arms of '%s'", f->name);
      }
      if (old->owner == d) {
        fail(f->loc, "duplicate method '%s'; use overload^ for disjoint signatures", f->name);
      }
      compatible(p, old, f);
      if (!old->abstract && !f->override) {
        fail(f->loc, "replacing concrete method '%s' requires override^", f->name);
      }
      d->methods.v[position] = f;
    }
    else {
      if (f->override) {
        fail(f->loc, "override^ has no inherited method '%s'", f->name);
      }
      if (named) {
        fail(f->loc, "adding a signature to '%s' requires overload^", f->name);
      }
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
void layout(Type *t, Loc l)
{
  if (callable_type(t)) {
    if (t->kind == T_FUNCTION && !t->function) {
      fail(l, "callable storage requires a statically selected implementation");
    }
    for (int j = 0; j < t->arms.n; j++) {
      layout(t->arms.v[j], l);
    }
    return;
  }
  if ((t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_SLICE) &&
      callable_type(t->elem)) {
    fail(l, "callable array storage is not supported; use statically selected let^ bindings");
  }
  if (t->kind == T_GROW) {
    layout(t->elem, l);
    if (t->elem->owned || !t->elem->size)
      fail(l, "growable elements must be nonempty fixed-size values");
    t->refs = t->elem->refs;
    return;
  }
  if (t->kind < T_STRUCT || t->kind == T_REF)
    return;
  if (t->kind == T_STRUCT && t->def->opaque)
    fail(l, "C type '%s' is opaque; use it only through ref^", t->def->name);
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
      t->owned |= arm->owned;
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

bool guarded_chain(Expr *e)
{
  return e &&
         (e->kind == E_MEMBER || e->kind == E_INDEX || e->kind == E_SLICE ||
          e->kind == E_CALL) &&
         (e->optional || guarded_chain(e->a));
}
/* A method name is not a first-class value; keep it with its call. */
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
static Instance *instantiate(Program *p, Function *f, Def *self, List *actual,
                             Loc at);
static void statements(Context *c, List *body, bool scope);
static Value expression(Context *c, Expr *e, Type *expected);
static Value construct(Context *c, Expr *e, Type *t, int dst);
static Value convert_value(Context *c, Value src, Type *to, int dst, Loc loc);
static void handle_statements(Context *c, Stmt *s);
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
static Value abrupt(Context *c)
{
  return (Value){.id = -1, .type = type_builtin(c->p, T_NEVER)};
}
static bool dead(Value v)
{
  return v.type->kind == T_NEVER;
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
    if (v->hidden) {
      continue;
    }
    if (!strcmp(v->name, name))
      return v;
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
    if (v->hidden) {
      continue;
    }
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
/* Compile alternate branches from the same input, then merge mutations. */
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
    bool paths_changed = v->path_version != old->path_version ||
                         a->path_version != old->path_version;
    if (changed) {
      v->version = (v->version > a->version ? v->version : a->version) + 1;
    }
    v->narrowed = changed ? NULL : old->type;
    if (paths_changed) {
      v->path_version = (v->path_version > a->path_version ? v->path_version
                                                           : a->path_version) +
                        1;
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
void remember_test(NilFlow *flow, Context *context, Expr *test, Expr *value,
                   Type *yes, Type *no)
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
    *guard =
        (NilGuard){.test = test,
                   .var = v,
                   .path = path,
                   .version = v ? (*path ? v->path_version : v->version) : 0,
                   .yes = yes,
                   .no = no,
                   .known = !yes  ? -1
                            : !no ? 1
                                  : 0};
    v = v && v->source && v->source->version == v->source_version ? v->source
                                                                  : NULL;
  } while (v);
}
void remember_guard(NilFlow *flow, Context *context, Expr *test, Expr *value,
                    bool positive)
{
  Var *v = NULL;
  char *path = NULL;
  if (!stable_path(context, value, &v, &path) || *path ||
      v->type->kind != T_UNION || !nullable(v->type)) {
    return;
  }
  Type *type = v->narrowed ? v->narrowed : v->type;
  Type *yes = without_nil(context->p, type, test->loc);
  Type *no = nullable(type) ? type_builtin(context->p, T_NIL) : NULL;
  remember_test(flow, context, test, value, positive ? yes : no,
                positive ? no : yes);
}
int condition_truth(NilFlow *flow, Expr *e)
{
  if (e->kind == E_SAVED && e->a) {
    return condition_truth(flow, e->a);
  }
  if (e->kind == E_UNARY && !strcmp(e->text, "!")) {
    return -condition_truth(flow, e->a);
  }
  if (e->kind == E_BIN &&
      (!strcmp(e->text, "and^") || !strcmp(e->text, "or^"))) {
    int a = condition_truth(flow, e->a), b = condition_truth(flow, e->b);
    if (!strcmp(e->text, "and^")) {
      return a < 0 || b < 0 ? -1 : a > 0 && b > 0 ? 1 : 0;
    }
    return a > 0 || b > 0 ? 1 : a < 0 && b < 0 ? -1 : 0;
  }
  /* Keep existing non-fits condition checking unchanged. */
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
void refine_condition(Program *p, List *vars, NilFlow *flow, Expr *e,
                      bool truth)
{
  if (e->kind == E_SAVED && e->a) {
    refine_condition(p, vars, flow, e->a, truth);
    return;
  }
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
static Value place_expression(Context *c, Expr *e)
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
  if (dead(v)) {
    return v;
  }
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
  if (dead(src)) {
    return src;
  }
  if (dst.type != src.type && sum_accepts(dst.type, src.type)) {
    src = convert_value(c, src, dst.type, -1, l);
  }
  if (dst.type != src.type)
    fail(l, "expected '%s', found '%s'", type_name(dst.type),
         type_name(src.type));
  if (src.type->owned && !src.held)
    fail(l, "owned region values cannot be copied; construct at the "
            "destination or borrow with 'ref^'");
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
  if (dead(src) || src.type->kind == T_VOID || src.type->owned) {
    return src;
  }
  Value dst = value(c, src.type);
  return copy_to(c, dst, src, l, true);
}
/* A constant of builtin kind k. */
static Value constant(Context *c, int k, const char *text, Loc loc)
{
  Value v = value(c, type_builtin(c->p, k));
  Instr *i = emit(c, I_CONST, loc);
  i->dst = v.id;
  i->type = v.type;
  i->text = (char *)text;
  return v;
}
/* a op b over a's type; a comparison answers bool^. */
static Value operate(Context *c, const char *op, Value a, Value b, Loc loc)
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
#define RANGE_OPEN "-9223372036854775808"
/* The place of union u's alternative arm, setting the tag when it writes. */
static int case_at(Context *c, Value u, Type *arm, bool write, Loc loc)
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
/* An owned operand of try^, catch^ or ?? is built on the data stack of dst,
 * which then takes the value it holds without a copy. */
static Value held_operand(Context *c, Expr *e, int dst)
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
static Value case_value(Context *c, Value src, Type *arm, Loc loc)
{
  layout(arm, loc);
  if (arm->owned) {
    /* An owned alternative is used where it lies. */
    return (Value){.id = case_at(c, src, arm, false, loc), .type = arm,
                   .deps = arm->refs ? src.deps : 0, .storage = src.storage,
                   .var = src.var, .addressable = src.addressable, .held = src.held};
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
  Type *pointer = c_function_arm(to, src.type);
  if (pointer) {
    /* A C-ABI declaration named as a value is the address of its symbol. */
    Value address = pointer == to ? out : value(c, pointer);
    Instr *i = emit(c, I_CONST, loc);
    i->dst = address.id;
    i->type = pointer;
    i->text = fmt("@%s", c_function_symbol(src.type->function));
    return pointer == to ? out : convert_value(c, address, to, out.id, loc);
  }
  if (src.type->kind == T_DYN) {
    /* A fits^ test on the dyn^ value proved its concrete type or composer. */
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
    /* A narrowed owned alternative is used where it lies. */
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
  out.held = src.held && to->owned;
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
    fail(loc, "no value has exactly the abstract type '%s'; write fits^ dyn^ %s",
         t->def->name, t->def->name);
  }
  return type_wrap(p, T_REF, t, 0);
}
static Value fits_value(Context *c, Expr *e)
{
  Value src = expression(c, e->a, NULL);
  if (dead(src)) {
    return src;
  }
  if (src.type->kind == T_DYN) {
    /* Only the yes branch narrows; the remaining types are not expressible. */
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
  if (src.type->kind == T_VOID || target->kind == T_VOID) {
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
      /* Both operands are pure tag predicates, so a boolean OR is sufficient.
       */
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
static Value chain_walk(Context *c, Expr *e, int absent, bool *never)
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
        nil_branch(c, present_value(c, base, step->loc), yes, absent,
                   step->loc);
        nil_label(c, yes, step->loc);
        base = convert_value(c, base, plain, -1, step->loc);
      }
    }
    Expr member = {0},
         copy = step_expression(step, saved_value(base, step->loc), &member);
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
  Value out = {.id = result->kind == T_VOID || result->kind == T_NEVER
                         ? -1
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
  out.deps |= convert_value(c, convert_value(c, base, plain, -1, e->loc),
                            result, out.id, e->loc)
                  .deps;
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
/* Where a binding of type want builds the owned result: the result, its
 * owned alternative, or that one's owned last part; -1 if it cannot. */
static int result_place(Context *c, Type *want, Loc loc)
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
static void release(Context *c, Type *t, int id, Loc l) {
  Instr *i = emit(c, I_RELEASE, l);
  i->a = id;
  i->type = t;
}
/* Whether v owns its storage, rather than being a result's part in place. */
static bool owner_var(Context *c, Var *v)
{
  return v->type->owned && v->slot != c->in->result_slot &&
         !((Slot *)c->in->slots.v[v->slot])->alias;
}
static void cleanup(Context *c, int from, Loc l) {
  for (int j = c->vars.n - 1; j >= from; j--) {
    Var *v = c->vars.v[j];
    if (owner_var(c, v))
      release(c, v->type, v->slot, l);
  }
}
static void escape_cleanup(Context *c, int bindings, int pending, Loc loc)
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
static Value error_test(Context *c, Value src, Loc loc)
{
  Value out = value(c, type_builtin(c->p, T_BOOL));
  if (only_errors(src.type) || !has_error(src.type)) {
    Instr *i = emit(c, I_CONST, loc);
    i->dst = out.id;
    i->type = out.type;
    i->text = only_errors(src.type) ? "1" : "0";
    return out;
  }
  bool first = true;
  for (int j = 0; j < src.type->def->alternatives.n; j++) {
    if (!only_errors(src.type->def->alternatives.v[j])) {
      continue;
    }
    Value test = value(c, out.type);
    Instr *i = emit(c, I_CASE_IS, loc);
    i->dst = test.id;
    i->a = src.id;
    i->type = src.type;
    i->label = j;
    if (first) {
      copy_to(c, out, test, loc, true);
    }
    else {
      i = emit(c, I_BIN, loc);
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
static Value pack_error(Context *c, Value src, Type *to, int dst, Loc loc)
{
  if (sum_accepts(to, src.type)) {
    return convert_value(c, src, to, dst, loc);
  }
  Value out = {
      .id = dst >= 0 ? dst : slot(c, to, false), .type = to, .deps = src.deps};
  if (src.type->kind == T_UNION) {
    List selected = {0};
    for (int j = 0; j < src.type->def->alternatives.n; j++) {
      if (error_overlap(to, src.type->def->alternatives.v[j])) {
        add(&selected, src.type->def->alternatives.v[j]);
      }
    }
    if (!selected.n) {
      fail(loc, "error cannot be represented by '%s'", type_name(to));
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
        nil_branch(c, test, yes, next, loc);
        nil_label(c, yes, loc);
      }
      pack_error(c, case_value(c, src, arm, loc), to, out.id, loc);
      nil_jump(c, end, loc);
      if (next) {
        nil_label(c, next, loc);
      }
    }
    nil_label(c, end, loc);
    return out;
  }
  if (to->kind == T_UNION) {
    for (int j = 0; j < to->def->alternatives.n; j++) {
      Type *arm = to->def->alternatives.v[j];
      if (only_errors(arm) && error_accepts(arm, src.type)) {
        Value part = pack_error(c, src, arm, -1, loc);
        return convert_value(c, part, to, out.id, loc);
      }
    }
  }
  fail(loc, "error '%s' is not included in '%s'", type_name(src.type),
       type_name(to));
}
/* End the process with a message: a string^, or the name of the error
 * variant held by subject. panic^ and an error leaving the entry file differ
 * only in the reported prefix. */
/* The source an error site names: under std/ for the standard library,
 * else as the compiler was given it, with / between directories. */
static char *site_source(Program *p, const char *file)
{
  char *out = str(file ? file : "");
  size_t n = p->stdlib_dir ? strlen(p->stdlib_dir) : 0;
  if (n && !strncmp(out, p->stdlib_dir, n) && (out[n] == '/' || out[n] == '\\')) {
    out += n + 1;
  }
  for (char *s = out; *s; s++) {
    *s = *s == '\\' ? '/' : *s;
  }
  return out;
}
static void terminate(Context *c, Value subject, bool panic, Loc loc);
/* r.end (i64^ | nil^, nil^ when open) or r.len. */
static Value range_member(Context *c, Value r, const char *name, Loc loc)
{
  Type *range = r.type, *whole = type_builtin(c->p, T_I64);
  Value start = field_value(c, r, range->def->fields.v[0], loc);
  Value end = field_value(c, r, range->def->fields.v[1], loc);
  Value open = operate(c, "=", end, constant(c, T_I64, RANGE_OPEN, loc), loc);
  int yes = ++c->in->labels, no = ++c->in->labels, done = ++c->in->labels;
  if (!strcmp(name, "end")) {
    Type *t = type_union(c->p, whole, type_builtin(c->p, T_NIL), loc);
    Value out = value(c, t);
    nil_branch(c, open, yes, no, loc);
    nil_label(c, yes, loc);
    convert_value(c, nil_literal(c, loc), t, out.id, loc);
    nil_jump(c, done, loc);
    nil_label(c, no, loc);
    convert_value(c, end, t, out.id, loc);
    nil_jump(c, done, loc);
    nil_label(c, done, loc);
    return out;
  }
  if (strcmp(name, "len")) {
    fail(loc, "range^ has start, end, len and contains(i)");
  }
  const char *message = "an open range has no length";
  nil_branch(c, open, yes, no, loc);
  nil_label(c, yes, loc);
  Expr text = {.kind = E_STRING, .loc = loc, .text = (char *)message, .length = strlen(message)};
  terminate(c, expression(c, &text, type_builtin(c->p, T_STRING)), true, loc);
  c->terminated = false;
  nil_label(c, no, loc);
  Value out = value(c, whole), length = operate(c, "-", end, start, loc);
  copy_to(c, out, length, loc, true);
  int negative = ++c->in->labels;
  nil_branch(c, operate(c, "<", length, constant(c, T_I64, "0", loc), loc), negative, done, loc);
  nil_label(c, negative, loc);
  copy_to(c, out, constant(c, T_I64, "0", loc), loc, true);
  nil_jump(c, done, loc);
  nil_label(c, done, loc);
  return out;
}
/* r.contains(i): start <= i and, unless r is open, i < end. */
static Value range_contains(Context *c, Value r, Value i, Loc loc)
{
  Value start = field_value(c, r, r.type->def->fields.v[0], loc);
  Value end = field_value(c, r, r.type->def->fields.v[1], loc);
  Value open = operate(c, "=", end, constant(c, T_I64, RANGE_OPEN, loc), loc);
  Value below = operate(c, "bitor^", open, operate(c, "<", i, end, loc), loc);
  return operate(c, "bitand^", operate(c, "<=", start, i, loc), below, loc);
}
static Value invoke_operator(Context *c, Function *f, Value a, Value b, Expr *origin);
/* The message() of error err, by the variant it holds when it is a union. */
static Value error_message(Context *c, Value err, Loc loc)
{
  bool sum = err.type->kind == T_UNION;
  int arms = sum ? err.type->def->alternatives.n : 1, end = ++c->in->labels;
  Value out = value(c, type_builtin(c->p, T_STRING));
  for (int j = 0; j < arms; j++) {
    Type *arm = sum ? err.type->def->alternatives.v[j] : err.type;
    int next = ++c->in->labels;
    if (j + 1 < arms) {
      Value test = value(c, type_builtin(c->p, T_BOOL));
      Instr *is = emit(c, I_CASE_IS, loc);
      is->dst = test.id;
      is->a = err.id;
      is->type = err.type;
      is->label = j;
      int here = ++c->in->labels;
      nil_branch(c, test, here, next, loc);
      nil_label(c, here, loc);
    }
    Value v = sum ? (Value){.id = case_at(c, err, arm, false, loc), .type = arm, .deps = err.deps,
                            .storage = err.storage, .var = err.var, .addressable = err.addressable}
                  : err;
    Function *m = NULL;
    for (int k = 0; k < arm->def->methods.n; k++) {
      Function *candidate = arm->def->methods.v[k];
      m = !strcmp(candidate->name, "message") && candidate->params.n == 1 ? candidate : m;
    }
    if (!m) {
      fail(loc, "'%s' has no message() taking only self^", type_name(arm));
    }
    Value r = invoke_operator(c, m, v, v, &(Expr){.loc = loc});
    out.deps |= r.deps;
    copy_to(c, out, r, loc, true);
    nil_jump(c, end, loc);
    if (j + 1 < arms) {
      nil_label(c, next, loc);
    }
  }
  nil_label(c, end, loc);
  return out;
}
static void terminate(Context *c, Value subject, bool panic, Loc loc)
{
  bool sum = subject.type->kind == T_UNION;
  bool text = subject.type->kind == T_STRING;
  int arms = sum ? subject.type->def->alternatives.n : 1;
  for (int j = 0; j < arms; j++) {
    int next = ++c->in->labels;
    if (j + 1 < arms) {
      Value test = value(c, type_builtin(c->p, T_BOOL));
      Instr *is = emit(c, I_CASE_IS, loc);
      is->dst = test.id;
      is->a = subject.id;
      is->type = subject.type;
      is->label = j;
      int here = ++c->in->labels;
      nil_branch(c, test, here, next, loc);
      nil_label(c, here, loc);
    }
    int message = subject.id;
    if (!text) {
      char *name = type_name(sum ? subject.type->def->alternatives.v[j] : subject.type);
      Expr literal = {.kind = E_STRING, .loc = loc, .text = name, .length = strlen(name)};
      message = expression(c, &literal, type_builtin(c->p, T_STRING)).id;
    }
    Type *arm = sum ? subject.type->def->alternatives.v[j] : subject.type;
    bool error = !text && arm->kind == T_STRUCT && arm->def->error_variant;
    Value detail = {0}, site = {0};
    if (error) {
      /* An error also reports its message() and where it was built. */
      Value v = sum ? (Value){.id = case_at(c, subject, arm, false, loc), .type = arm,
                              .addressable = subject.addressable}
                    : subject;
      detail = error_message(c, v, loc);
      site = field_value(c, v, arm->def->fields.v[0], loc);
    }
    Instr *i = emit(c, I_TERMINATE, loc);
    i->a = message;
    i->label = panic;
    i->other = error;
    i->b = detail.id;
    i->dst = site.id;
    if (j + 1 < arms) {
      nil_label(c, next, loc);
    }
  }
  c->terminated = true;
}
static void route_error(Context *c, Value error, Loc loc)
{
  if (c->handlers.n) {
    ErrorTarget *target = c->handlers.v[c->handlers.n - 1];
    target->deps |= error.deps;
    pack_error(c, error, target->errors, target->slot, loc);
    List edge = save_facts(&c->vars);
    edge.n = target->bindings;
    if (!target->facts.n) {
      target->facts = edge;
    }
    else {
      for (int j = 0; j < edge.n; j++) {
        NilFact *a = target->facts.v[j], *b = edge.v[j];
        if (a->type != b->type) {
          a->type = NULL;
        }
        if (a->paths.v != b->paths.v) {
          a->paths = (List){0};
        }
        if (a->version < b->version) {
          a->version = b->version;
        }
        if (a->path_version < b->path_version) {
          a->path_version = b->path_version;
        }
      }
    }
    escape_cleanup(c, target->bindings, target->pending, loc);
    if (c->building_result && target->checkpoint >= 0) {
      Instr *i = emit(c, I_STACK_REWIND, loc);
      i->a = c->in->result_slot;
      i->dst = target->checkpoint;
      i->type = c->in->result;
    }
    nil_jump(c, target->label, loc);
  }
  else {
    if (c->in->fn == c->p->entry) {
      /* The entry file has no caller: report the variant and exit. */
      terminate(c, error, false, loc);
      c->terminated = true;
      return;
    }
    if (!error_accepts(c->in->result, error.type)) {
      fail(loc,
           "try^ error '%s' is not included in the function result '%s'; add "
           "catch^ or widen the result",
           type_name(error.type), type_name(c->in->result));
    }
    Value out = pack_error(c, error, c->in->result, c->in->result_slot, loc);
    for (int j = 0; j < c->root_count; j++) {
      if ((out.deps & (UINT64_C(1) << j)) && !c->root_external[j]) {
        fail(loc, "cannot return a reference rooted in a local owner");
      }
    }
    escape_cleanup(c, 0, 0, loc);
    Instr *i = emit(c, I_RETURN, loc);
    i->type = c->in->result;
  }
  c->terminated = true;
}
static void error_focus(Context *c, Value error, Loc loc)
{
  Var *v = mem(sizeof(*v));
  v->name = "it^";
  v->type = error.type;
  v->slot = error.id;
  v->depth = c->depth;
  v->lexical = c->lexical;
  v->deps = error.deps;
  v->root = root(c, c->depth, false, loc);
  add(&c->vars, v);
}
static Value try_value(Context *c, Expr *e, int dst)
{
  Value src = held_operand(c, e->a, dst);
  if (dead(src)) {
    return src;
  }
  Type *plain = success_type(c->p, src.type, e->loc);
  Type *errors = error_leaves(c->p, src.type, e->loc);
  if (!errors) {
    return src;
  }
  if (plain->kind == T_NEVER) {
    route_error(c, pack_error(c, src, errors, -1, e->loc), e->loc);
    return abrupt(c);
  }
  int yes = ++c->in->labels, no = ++c->in->labels;
  nil_branch(c, error_test(c, src, e->loc), no, yes, e->loc);
  nil_label(c, no, e->loc);
  route_error(c, pack_error(c, src, errors, -1, e->loc), e->loc);
  c->terminated = false;
  nil_label(c, yes, e->loc);
  return convert_value(c, src, plain, -1, e->loc);
}
static Value catch_value(Context *c, Expr *e, Type *expected, int dst)
{
  Type *inferred = expression_type(c, e, expected);
  if (!inferred) {
    fail(e->loc, "cannot infer catch^ result type");
  }
  Type *result = expected ? expected : inferred;
  if (result->owned && dst < 0) {
    fail(e->loc, "owned catch^ result requires a construction destination");
  }
  Value src = held_operand(c, e->a, dst);
  if (dead(src)) {
    return src;
  }
  Type *plain = success_type(c->p, src.type, e->loc);
  Type *errors = error_leaves(c->p, src.type, e->loc);
  if (!errors) {
    fail(e->loc, "catch^ requires an error alternative");
  }
  Value out = result->kind == T_NEVER
                  ? abrupt(c)
                  : (Value){.id = dst >= 0 ? dst : slot(c, result, false),
                            .type = result};
  int failed = ++c->in->labels, end = ++c->in->labels;
  List facts = save_facts(&c->vars);
  if (plain->kind != T_NEVER) {
    int yes = ++c->in->labels;
    nil_branch(c, error_test(c, src, e->loc), failed, yes, e->loc);
    nil_label(c, yes, e->loc);
    Value normal = convert_value(c, src, plain, -1, e->loc);
    out.deps |= convert_value(c, normal, result, out.id, e->loc).deps;
    nil_jump(c, end, e->loc);
  }
  else {
    nil_jump(c, failed, e->loc);
  }
  nil_label(c, failed, e->loc);
  int begin = c->vars.n;
  c->depth++;
  c->lexical++;
  error_focus(c, pack_error(c, src, errors, -1, e->loc), e->loc);
  Value replacement = construct(c, e->b, expected ? expected : result, out.id);
  out.deps |= replacement.deps;
  bool ended = c->terminated;
  if (!ended) {
    for (int j = 0; j < c->root_count; j++) {
      if ((replacement.deps & (UINT64_C(1) << j)) &&
          c->root_depth[j] > c->depth - 1) {
        fail(e->loc, "catch^ expression cannot escape a local error focus");
      }
    }
    nil_jump(c, end, e->loc);
  }
  c->vars.n = begin;
  c->depth--;
  c->lexical--;
  restore_facts(&c->vars, facts);
  c->terminated = plain->kind == T_NEVER && ended;
  if (!c->terminated) {
    nil_label(c, end, e->loc);
  }
  return c->terminated ? abrupt(c) : out;
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
/* this^ selects a declaration, independently of its runtime activation. */
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
    fail(target->loc,
         "%s requires an enclosing function or procedure at that nesting depth",
         target->text);
  }
  return f;
}
Def *outer_receiver(Function *f, Type *type, Loc loc)
{
  if (type->kind != T_REF || type->elem->kind != T_STRUCT ||
      !derives(type->elem->def, f->owner)) {
    fail(loc, "outer method call requires an explicit 'ref^ %s' receiver",
         f->owner->name);
  }
  return type->elem->def;
}
static void check_callable(Program *p, Type *want, Type *actual, Loc loc)
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
/* Whether a value of type part could be stored inside a value of type whole. */
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
/* Whether a value of type t may refer to a location inside an element of
 * type elem. */
static bool may_reference(Type *t, Type *elem)
{
  if (!t->refs) {
    return false;
  }
  if (t->kind == T_REF || t->kind == T_SLICE) {
    return contains(elem, t->elem);
  }
  if (t->kind == T_STRING) {
    return false; /* Strings view literals or other strings, never arrays. */
  }
  if (t->kind == T_DYN) {
    return elem->kind == T_STRUCT; /* Any composer of the dyn^ def. */
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
/* The element type of the flex^ storage an argument of type t leads a
 * callee to (the array itself or a struct's flex^ tail), or NULL. */
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
/* Shrinking flex^ storage returns its tail to the data stack and the OS, so
 * no variable in scope or value still in use may refer into it. skip is the
 * shrinking argument itself. */
static void shrink_check(Context *c, uint64_t storage, Type *elem, Value *skip, Loc loc)
{
  for (int j = 0; j < c->vars.n; j++) {
    Var *v = c->vars.v[j];
    if (!(v->root & storage) && (v->deps & storage) && may_reference(v->type, elem)) {
      fail(loc, "cannot shrink flex^ storage while '%s' may refer into it; end its scope first",
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
/* Why method f of iface cannot occupy a dyn^ table entry, or NULL. Entries
 * share one signature across every concrete type. */
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
/* An omitted result means the method returns nothing through dyn^. */
static Type *dyn_result(Program *p, Function *f)
{
  return f->result ? type_spec(p, f->result) : type_builtin(p, T_VOID);
}
/* The table of concrete's implementations of iface's methods. */
static DynTable *dyn_table(Program *p, Def *concrete, Def *iface, Loc loc)
{
  for (int j = 0; j < p->dyn_tables.n; j++) {
    DynTable *t = p->dyn_tables.v[j];
    if (t->concrete == concrete && t->iface == iface) {
      return t;
    }
  }
  /* A dyn^ call has no shrink summary, so it must not reach flex^ storage. */
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
/* Pair a reference to a concrete composing type with its method table. */
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
/* Call a method through the table carried by a ref^ dyn^ receiver. */
static Value dyn_call(Context *c, Expr *e, Value receiver, Type *expected,
                      int destination)
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
    /* The same mutation rules as a static call to a receiver in that storage. */
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
    /* Every table entry was specialized for exactly the declared types. */
    if (v.type != want) {
      fail(e->loc, "argument to '%s' through dyn^ must be '%s', found '%s'",
           f->name, type_name(want), type_name(v.type));
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
               .id = t->kind == T_VOID ? -1
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
/* char^(x): the one character of string^ x, or integer x as a scalar
 * value, else std.text.Error.Invalid. */
static Value char_cast(Context *c, Expr *e, int destination)
{
  Program *p = c->p;
  Type *t = char_cast_type(p, e->loc);
  if (e->args.n != 1) {
    fail(e->loc, "char^(...) casts one string^ or integer");
  }
  Value v = expression(c, e->args.v[0], NULL);
  if (dead(v)) {
    return v;
  }
  bool text = v.type->kind == T_STRING;
  if (!text && (!kind_integer(v.type->kind) || numeric_bits(v.type->kind) > 64)) {
    fail(e->loc, "char^(...) casts a string^ or an integer of at most 64 bits");
  }
  if (!text && v.type->kind != T_I64) {
    Value whole = value(c, type_builtin(p, T_I64));
    Instr *i = emit(c, I_CONVERT, e->loc);
    i->dst = whole.id;
    i->a = v.id;
    i->type = whole.type;
    v = whole;
  }
  Value code = value(c, type_builtin(p, T_I64));
  Instr *i = emit(c, I_CHAR, e->loc);
  i->dst = code.id;
  i->a = v.id;
  i->type = v.type;
  Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t};
  int good = ++c->in->labels, bad = ++c->in->labels, done = ++c->in->labels;
  nil_branch(c, operate(c, ">=", code, constant(c, T_I64, "0", e->loc), e->loc), good, bad, e->loc);
  nil_label(c, good, e->loc);
  Value scalar = value(c, type_builtin(p, T_CHAR));
  i = emit(c, I_CONVERT, e->loc);
  i->dst = scalar.id;
  i->a = code.id;
  i->type = scalar.type;
  convert_value(c, scalar, t, out.id, e->loc);
  nil_jump(c, done, e->loc);
  nil_label(c, bad, e->loc);
  Expr message = {.kind = E_STRING, .loc = e->loc,
                  .text = text ? "not exactly one Unicode scalar value"
                               : "not a Unicode scalar value"};
  message.length = strlen(message.text);
  Expr invalid = {.kind = E_RECORD, .loc = e->loc, .text = "std.text.Error.Invalid"};
  add(&invalid.names, "message");
  add(&invalid.args, &message);
  construct(c, &invalid, t, out.id);
  nil_jump(c, done, e->loc);
  nil_label(c, done, e->loc);
  return out;
}
/* dst = helper(args..., line), or no result when t is void. */
static Value helper(Context *c, const char *name, Value *args, int n, int k, Loc loc)
{
  Value out = k == T_VOID ? (Value){.id = -1, .type = type_builtin(c->p, T_VOID)}
                          : value(c, type_builtin(c->p, T_I64));
  Instr *i = emit(c, I_HELPER, loc);
  i->dst = out.id;
  i->text = (char *)name;
  for (int j = 0; j < n; j++) {
    add(&i->args, (void *)(intptr_t)(args[j].id + 1));
  }
  return out;
}
/* string^(bytes): the bytes as a string^ when they are UTF-8, else
 * std.text.Error.Invalid. */
static Value text_cast(Context *c, Expr *e, int destination)
{
  Program *p = c->p;
  Type *t = text_cast_type(p, e->loc), *text = type_builtin(p, T_STRING);
  if (e->args.n != 1) {
    fail(e->loc, "string^(...) casts one u8^[:]");
  }
  Value bytes = expression(c, e->args.v[0], NULL);
  if (dead(bytes)) {
    return bytes;
  }
  if (bytes.type->kind != T_SLICE || bytes.type->elem->kind != T_U8) {
    fail(e->loc, "string^(...) casts a u8^[:] of UTF-8");
  }
  Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t,
               .deps = bytes.deps};
  int good = ++c->in->labels, bad = ++c->in->labels, done = ++c->in->labels;
  Value valid = helper(c, "rh_text_valid", &bytes, 1, T_I64, e->loc);
  nil_branch(c, operate(c, "!=", valid, constant(c, T_I64, "0", e->loc), e->loc), good, bad, e->loc);
  nil_label(c, good, e->loc);
  Value view = {.id = slot(c, text, false), .type = text, .deps = bytes.deps};
  Instr *i = emit(c, I_COPY, e->loc);
  i->dst = view.id;
  i->a = bytes.id;
  i->type = text;
  convert_value(c, view, t, out.id, e->loc);
  nil_jump(c, done, e->loc);
  nil_label(c, bad, e->loc);
  Expr message = {.kind = E_STRING, .loc = e->loc, .text = "not valid UTF-8"};
  message.length = strlen(message.text);
  Expr invalid = {.kind = E_RECORD, .loc = e->loc, .text = "std.text.Error.Invalid"};
  add(&invalid.names, "message");
  add(&invalid.args, &message);
  construct(c, &invalid, t, out.id);
  nil_jump(c, done, e->loc);
  nil_label(c, done, e->loc);
  return out;
}
/* x.write_text(out) appends x to Builder out; x.tostring() builds a new
 * Builder at the destination with it. x is a number, bool^, char^ or
 * string^; the others are written into a local buffer first. */
static void write_value(Context *c, Value x, Value into, Expr *format, Loc loc);
static Value value_text(Context *c, Expr *e, int destination)
{
  Program *p = c->p;
  Expr *target = e->a;
  bool build = !strcmp(target->text, "tostring");
  Def *builder = lookup_def(p, "std.text.Builder");
  if (!builder) {
    fail(e->loc, "internal: %s needs std.text", target->text);
  }
  if (e->args.n != !build && e->args.n != !build + 1) {
    fail(e->loc, build ? "x.tostring() takes an optional format"
                       : "x.write_text(out) takes a ref^ std.text.Builder and an optional format");
  }
  Value x = expression(c, target->a, NULL);
  if (dead(x)) {
    return x;
  }
  Value out = {.id = -1, .type = type_builtin(p, T_VOID)}, into;
  if (build) {
    if (destination < 0) {
      fail(e->loc, "x.tostring() builds a std.text.Builder; bind it, as in let^ s = x.tostring()");
    }
    Expr empty = {.kind = E_RECORD, .loc = e->loc, .text = builder->name};
    out = into = construct(c, &empty, builder->type, destination);
  }
  else {
    into = deref(c, expression(c, e->args.v[0], type_wrap(p, T_REF, builder->type, 0)), e->loc);
    if (into.type != builder->type) {
      fail(e->loc, "x.write_text(out) takes a ref^ std.text.Builder, found '%s'", type_name(into.type));
    }
  }
  write_value(c, x, into, e->args.n > !build ? e->args.v[e->args.n - 1] : NULL, e->loc);
  return out;
}
/* Append x, written with the C format format when there is one, to the
 * Builder into. */
static void write_value(Context *c, Value x, Value into, Expr *format, Loc loc)
{
  Program *p = c->p;
  Def *builder = into.type->def;
  bool number = kind_integer(x.type->kind) || kind_float(x.type->kind);
  if (format && !number) {
    fail(loc, "a format applies to a number, not '%s'", type_name(x.type));
  }
  Value text = x;
  if (x.type->kind != T_STRING) {
    if (x.type->kind == T_F8 || x.type->kind == T_F16) {
      Value wide = value(c, type_builtin(p, T_F64));
      Instr *i = emit(c, I_CONVERT, loc);
      i->dst = wide.id;
      i->a = x.id;
      i->type = wide.type;
      x = wide;
    }
    Type *buffer = type_wrap(p, T_ARRAY, type_builtin(p, T_U8), 256);
    Type *view = type_wrap(p, T_SLICE, type_builtin(p, T_U8), 0);
    int bytes = slot(c, buffer, false);
    Expr none = {.kind = E_STRING, .loc = loc, .text = ""};
    Value args[4] = {{.id = slot(c, view, false), .type = view},
                     {.id = slot(c, type_wrap(p, T_REF, x.type, 0), false)},
                     constant(c, T_I64,
                              fmt("%d", numeric_bits(x.type->kind) |
                                            (kind_unsigned(x.type->kind) ? 1 : kind_float(x.type->kind) ? 2
                                             : x.type->kind == T_BOOL ? 3 : x.type->kind == T_CHAR ? 4 : 0)
                                                << 8),
                              loc),
                     expression(c, format ? format : &none, type_builtin(p, T_STRING))};
    if (args[3].type->kind != T_STRING) {
      fail(loc, "a format is a string^, as in \"%%5.2f\"");
    }
    Instr *i = emit(c, I_SLICE, loc);
    i->dst = args[0].id;
    i->a = bytes;
    i->b = i->other = -1;
    i->type = buffer;
    i = emit(c, I_ADDR, loc);
    i->dst = args[1].id;
    i->a = x.id;
    i->type = ((Slot *)c->in->slots.v[args[1].id])->type;
    Value length = helper(c, "rh_text_format", args, 4, T_I64, loc);
    text = (Value){.id = slot(c, type_builtin(p, T_STRING), false), .type = type_builtin(p, T_STRING)};
    Value start = constant(c, T_I64, "0", loc);
    i = emit(c, I_SLICE, loc);
    i->dst = text.id;
    i->a = bytes;
    i->b = start.id;
    i->other = length.id;
    i->type = buffer;
  }
  Field *bytes = field(builder, "bytes");
  int place = slot(c, bytes->type, true);
  Instr *i = emit(c, I_FIELD, loc);
  i->dst = place;
  i->a = into.id;
  i->type = builder->type;
  i->label = bytes->index;
  i = emit(c, I_APPEND, loc);
  i->a = place;
  i->b = text.id;
  i->type = bytes->type;
}
/* Append the text and holes of $"..." e to the Builder into. */
static void interp_into(Context *c, Expr *e, Value into)
{
  for (int j = 0; j < e->args.n && !c->terminated; j++) {
    Expr *part = e->args.v[j];
    char *format = e->names.v[j];
    if (part->kind == E_INTERP && !*format) {
      /* A nested $"..." goes on in the same Builder. */
      interp_into(c, part, into);
      continue;
    }
    Value x = expression(c, part, NULL);
    if (dead(x)) {
      return;
    }
    if (x.type == into.type && !*format) {
      /* A Builder in a hole gives its text. */
      Function *text = NULL;
      for (int k = 0; k < into.type->def->methods.n; k++) {
        Function *m = into.type->def->methods.v[k];
        text = !strcmp(m->name, "text") ? m : text;
      }
      x = invoke_operator(c, text, x, x, part);
    }
    if (!textual(x.type)) {
      fail(part->loc, "an interpolation hole takes a number, bool^, char^ or string^, found '%s'",
           type_name(x.type));
    }
    Expr spec = {.kind = E_STRING, .loc = part->loc, .text = format, .length = strlen(format)};
    write_value(c, x, into, *format ? &spec : NULL, part->loc);
  }
}
/* $"..." for print^: built in a Builder living until the print, which
 * shows its text. */
static Value interp_print(Context *c, Expr *e, List *temps)
{
  Def *builder = lookup_def(c->p, "std.text.Builder");
  int temp = slot(c, builder->type, false);
  Instr *i = emit(c, I_STACK_BEGIN, e->loc);
  i->dst = temp;
  i->type = builder->type;
  i->text = "$\"...\"";
  add(&c->pending_owners, (void *)(intptr_t)(temp + 1));
  add(temps, (void *)(intptr_t)temp);
  Value out = construct(c, e, builder->type, temp);
  Function *text = NULL;
  for (int j = 0; j < builder->methods.n; j++) {
    Function *m = builder->methods.v[j];
    text = !strcmp(m->name, "text") ? m : text;
  }
  return dead(out) ? out : invoke_operator(c, text, out, out, e);
}
/* The built-in methods of string^, which address it by byte offsets. */
static Value text_method(Context *c, Expr *e, int destination)
{
  Program *p = c->p;
  Expr *target = e->a;
  const char *name = target->text;
  Type *t = text_method_type(p, name, e->loc), *whole = type_builtin(p, T_I64);
  Value s = deref(c, expression(c, target->a, NULL), e->loc);
  if (dead(s)) {
    return s;
  }
  bool unary = !strcmp(name, "count_chars") || !strcmp(name, "chars");
  if (e->args.n != !unary) {
    fail(e->loc, "s.%s takes %s", name, unary ? "no arguments" : "one argument");
  }
  Type *want = !strcmp(name, "slice") ? type_range(p) : !strcmp(name, "decode") ? whole
                                                                               : type_builtin(p, T_STRING);
  Value arg = unary ? s : expression(c, e->args.v[0], want);
  if (dead(arg)) {
    return arg;
  }
  if (!unary && arg.type != want) {
    fail(e->loc, "s.%s takes '%s', found '%s'", name, type_name(want), type_name(arg.type));
  }
  Value both[2] = {s, arg};
  if (!strcmp(name, "count_chars")) {
    return helper(c, "rh_text_count", both, 1, T_I64, e->loc);
  }
  if (!strcmp(name, "starts_with") || !strcmp(name, "ends_with")) {
    Value r = helper(c, name[0] == 's' ? "rh_text_starts" : "rh_text_ends", both, 2, T_I64, e->loc);
    return operate(c, "!=", r, constant(c, T_I64, "0", e->loc), e->loc);
  }
  if (!strcmp(name, "find")) {
    Value at = helper(c, "rh_text_find", both, 2, T_I64, e->loc);
    Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t};
    int none = ++c->in->labels, found = ++c->in->labels, done = ++c->in->labels;
    nil_branch(c, operate(c, "<", at, constant(c, T_I64, "0", e->loc), e->loc), none, found, e->loc);
    nil_label(c, none, e->loc);
    convert_value(c, nil_literal(c, e->loc), t, out.id, e->loc);
    nil_jump(c, done, e->loc);
    nil_label(c, found, e->loc);
    convert_value(c, at, t, out.id, e->loc);
    nil_jump(c, done, e->loc);
    nil_label(c, done, e->loc);
    return out;
  }
  if (!strcmp(name, "slice")) {
    /* A byte range of s whose ends fall between characters. */
    Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t,
                 .deps = s.deps};
    int lo = field_value(c, arg, arg.type->def->fields.v[0], e->loc).id;
    int hi = field_value(c, arg, arg.type->def->fields.v[1], e->loc).id;
    Instr *i = emit(c, I_SLICE, e->loc);
    i->dst = out.id;
    i->a = s.id;
    i->b = lo;
    i->other = hi;
    i->text = "text";
    i->type = s.type;
    return out;
  }
  if (!strcmp(name, "decode")) {
    Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t};
    Value code = helper(c, "rh_text_decode", both, 2, T_I64, e->loc);
    Value scalar = value(c, type_builtin(p, T_CHAR));
    Instr *i = emit(c, I_CONVERT, e->loc);
    i->dst = scalar.id;
    i->a = code.id;
    i->type = scalar.type;
    copy_to(c, field_value(c, out, t->def->fields.v[0], e->loc), scalar, e->loc, true);
    copy_to(c, field_value(c, out, t->def->fields.v[1], e->loc),
            helper(c, "rh_text_next", both, 2, T_I64, e->loc), e->loc, true);
    return out;
  }
  /* chars(): decode every character, pushed onto an array built at the destination. */
  if (destination < 0) {
    fail(e->loc, "s.chars() builds an owned array; bind it, as in let^ cs = s.chars()");
  }
  Expr empty = {.kind = E_ARRAY, .loc = e->loc};
  Value out = construct(c, &empty, t, destination);
  Value at = value(c, whole), length = value(c, whole);
  copy_to(c, at, constant(c, T_I64, "0", e->loc), e->loc, true);
  Instr *i = emit(c, I_LENGTH, e->loc);
  i->dst = length.id;
  i->a = s.id;
  i->type = s.type;
  int head = ++c->in->labels, body = ++c->in->labels, done = ++c->in->labels;
  nil_jump(c, head, e->loc);
  nil_label(c, head, e->loc);
  nil_branch(c, operate(c, "<", at, length, e->loc), body, done, e->loc);
  nil_label(c, body, e->loc);
  Value from[2] = {s, at};
  Value code = helper(c, "rh_text_decode", from, 2, T_I64, e->loc);
  Value scalar = value(c, type_builtin(p, T_CHAR));
  i = emit(c, I_CONVERT, e->loc);
  i->dst = scalar.id;
  i->a = code.id;
  i->type = scalar.type;
  Value pushed = value(c, type_wrap(p, T_REF, scalar.type, 0));
  i = emit(c, I_PUSH, e->loc);
  i->dst = pushed.id;
  i->a = out.id;
  i->b = scalar.id;
  i->type = out.type;
  copy_to(c, at, helper(c, "rh_text_next", from, 2, T_I64, e->loc), e->loc, true);
  nil_jump(c, head, e->loc);
  nil_label(c, done, e->loc);
  return out;
}
static Value call(Context *c, Expr *e, Type *expected, int destination) {
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
    return destination >= 0 ? copy_to(c, (Value){.id = destination, .type = out.type}, out, e->loc, true)
                            : out;
  }
  if (target->kind == E_NAME) {
    for (int k = T_U8; k <= T_F64; k++) {
      if (strcmp(target->text, builtin_name(k)))
        continue;
      if (e->args.n != 1)
        fail(e->loc, "numeric conversion expects one argument");
      Type *t = type_builtin(p, k);
      Value v = expression(c, e->args.v[0], NULL);
      if (c->terminated) {
        return abrupt(c);
      }
      if (!kind_integer(v.type->kind) && !kind_float(v.type->kind) &&
          !(v.type->kind == T_CHAR && kind_integer(k)))
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
  if (target->kind == E_NAME && !strcmp(target->text, "char^")) {
    return char_cast(c, e, destination);
  }
  if (target->kind == E_NAME && !strcmp(target->text, "string^")) {
    return text_cast(c, e, destination);
  }
  if (target->kind == E_MEMBER && !strcmp(target->text, "append") && e->args.n == 1 &&
      ((Expr *)e->args.v[0])->kind == E_INTERP) {
    Value into = deref(c, expression(c, target->a, NULL), e->loc);
    if (dead(into)) {
      return into;
    }
    if (into.type->kind != T_STRUCT || strcmp(into.type->def->name, "std.text.Builder")) {
      fail(e->loc, "$\"...\" is appended to a std.text.Builder, not '%s'", type_name(into.type));
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
    if (print && !c->in->fn->procedure)
      fail(e->loc, "print^ requires p^");
    if (!print && e->args.n != 1)
      fail(e->loc, "assert^ expects one bool");
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
          v.type->kind != T_STRING && v.type->kind != T_BOOL && v.type->kind != T_CHAR)
        fail(e->loc, "print^ does not support '%s'", type_name(v.type));
      if (!print && v.type->kind != T_BOOL)
        fail(e->loc, "assert^ requires bool");
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
    if (dead(receiver)) {
      return receiver;
    }
    if (c->terminated) {
      return abrupt(c);
    }
    Value base = deref(c, receiver, e->loc);
    if (base.type->kind == T_GROW && !strcmp(target->text, "truncate")) {
      if (e->args.n != 1)
        fail(e->loc, "truncate expects one new length");
      Value len = expression(c, e->args.v[0], type_builtin(p, T_I64));
      if (c->terminated) {
        return abrupt(c);
      }
      if (len.type->kind != T_I64)
        fail(e->loc, "truncate length must be i64^");
      store_check(c, base, (Value){0}, e->loc);
      if (base.type->count) {
        shrink_check(c, base.storage, base.type->elem, NULL, e->loc);
      }
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
      if (c->terminated) {
        return abrupt(c);
      }
      if (!((src.type->kind == T_SLICE && src.type->elem == base.type->elem) ||
            (src.type->kind == T_STRING && base.type->elem->kind == T_U8)))
        fail(e->loc, "append needs a slice with the same element type");
      Value dependencies = {.deps = base.type->elem->refs ? src.deps : 0};
      store_check(c, base, dependencies, e->loc);
      forget_roots(&c->vars, base.storage | base.deps, false);
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
      if (base.type->count) {
        shrink_check(c, base.storage, base.type->elem, NULL, e->loc);
      }
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
      if (c->terminated) {
        return abrupt(c);
      }
      if (v.type != base.type->elem)
        fail(e->loc, "push element type mismatch");
      store_check(c, base, v, e->loc);
      forget_roots(&c->vars, base.storage | base.deps, false);
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
    if (base.type->kind == T_DYN) {
      return dyn_call(c, e, snapshot(c, base, e->loc), expected, destination);
    }
    if (base.type->kind != T_STRUCT)
      fail(e->loc, "method call requires a structure");
    self = base.type->def;
    List candidates = overload_candidates(p, self, target->text, e->args.n + 1);
    f = select_call(c, &candidates, e, type_wrap(p, T_REF, base.type, 0));
    if (!f) {
      f = method(self, target->text);
    }
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
    Var *binding = selected(c, target);
    List candidates = binding ? callable_candidates(binding->type, e->args.n)
                              : overload_candidates(p, NULL, target->text, e->args.n);
    f = select_call(c, &candidates, e, NULL);
    if (!f)
      fail(e->loc, "unknown function '%s'", target->text);
  }
  else {
    Value target_value = expression(c, target, NULL);
    List candidates = callable_candidates(target_value.type, e->args.n);
    f = select_call(c, &candidates, e, NULL);
    if (!f) {
      fail(e->loc, "callable has no statically selected implementation");
    }
  }
  if (f->abstract)
    fail(e->loc, "cannot call abstract method '%s'", f->name);
  Var *alias = target->kind == E_NAME ? selected(c, target) : NULL;
  if (alias && callable_type(alias->type)) {
    tool_record(p, target->loc, alias->declaration, alias->name, alias->type,
                NULL, NULL, 13, false);
  }
  else {
    tool_record(p, target->name_loc.file ? target->name_loc : target->loc,
                f->declaration.file ? f->declaration : f->loc, f->name, NULL, f,
                f->owner, f->owner ? 6 : 12, false);
  }
  inferred_call(c->in->fn, f, e->loc);
  int offset = values.n;
  Value *right_self = offset && f->right_receiver ? values.v[0] : NULL;
  if (right_self) {
    values.n = actual.n = 0;
  }
  e = pack_arguments(f, e, offset);
  if (f->params.n != e->args.n + offset)
    fail(e->loc, "'%s' expects %s%d arguments", f->name,
         f->variadic ? "at least " : "", f->params.n - offset - f->variadic);
  if (f->procedure && !c->in->fn->procedure)
    fail(e->loc, "f^ cannot call p^ '%s'", f->name);
  /* Arguments evaluated so far stay in use while later ones may shrink. */
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
    if (v.type->kind == T_VOID)
      fail(e->loc, "void argument");
    if (want) {
      if (callable_type(want)) {
        check_callable(p, want, v.type, e->loc);
      }
      Type *a = v.type, *b = want;
      if (a != b) {
        bool okay = parameter_accepts(b, a);
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
      fail(e->loc, "pass owned values by reference with 'ref^'");
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
  /* A callee still being lowered has no summary yet; assume it shrinks. */
  for (int j = 0; j < values.n && j < 64; j++) {
    Value *v = values.v[j];
    Type *elem = flex_elements(v->type);
    if (elem && (in->state != 2 || ((in->shrinks >> j) & 1)) && v->deps) {
      shrink_check(c, v->deps, elem, v, e->loc);
    }
  }
  c->inflight.n = inflight;
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
  /* C may write through every pointer it receives. */
  if (f->procedure || f->mutable_self || f->external) {
    forget_roots(&c->vars, deps, false);
  }
  return out;
}
Type *union_literal_hint(Type *t, Expr *e)
{
  if (!t || t->kind != T_UNION || e->constant_spec)
    return NULL;
  Expr *literal = e;
  if (literal->kind == E_UNARY && !strcmp(literal->text, "-"))
    literal = literal->a;
  if (literal->kind != E_INT && literal->kind != E_FLOAT &&
      literal->kind != E_ARRAY &&
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
         (a->kind == T_ARRAY || a->kind == T_GROW))) {
      /* A mixed result must not implicitly choose an error constructor. */
      if (literal->kind == E_RECORD && a->def->error_variant) {
        return NULL;
      }
      if (hint)
        return NULL;
      hint = a;
    }
  }
  return hint;
}
static Value construct(Context *c, Expr *e, Type *t, int dst) {
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
    bool literal = e->a->kind == E_INT || e->a->kind == E_FLOAT ||
                   e->a->kind == E_RECORD || e->a->kind == E_ARRAY;
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
      fail(e->loc, "$\"...\" builds a std.text.Builder; bind it, or pass it to print^ or append");
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
      nil_branch(c, operate(c, "=", ends[1], constant(c, T_I64, "9223372036854775807", e->loc), e->loc),
                 bad, good, e->loc);
      nil_label(c, bad, e->loc);
      Expr text = {.kind = E_STRING, .loc = e->loc, .text = "the end of a to^ range overflows",
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
    if (e->kind == E_RECORD && e->text && !strcmp(e->text, t->def->name)) {
      fail(e->loc, "construct union '%s' from a named alternative",
           type_name(t));
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
    if (built && built->owned && (forwarded || (built != t && union_alternative(t, built) >= 0))) {
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
      fail(e->loc, "'%s' is not an alternative of nominal union '%s'",
           type_name(arm.type), type_name(t));
    }
    return convert_value(c, arm, t, dst, e->loc);
  }
  if (e->kind == E_CALL && !guarded_chain(e)) {
    return call(c, e, t, dst);
  }
  if (e->kind == E_IF) {
    if (!t)
      t = expression_type(c, e, NULL);
    if (!t || t->kind == T_VOID)
      fail(e->loc, "if^ expression must produce a value on every branch");
    Value out =
        t->kind == T_NEVER
            ? abrupt(c)
            : (Value){.id = dst >= 0 ? dst : slot(c, t, false), .type = t};
    if (t->owned && dst < 0)
      fail(e->loc, "owned conditional requires a construction destination");
    Value condition = expression(c, e->a, type_builtin(c->p, T_BOOL));
    if (dead(condition)) {
      return condition;
    }
    if (condition.type->kind != T_BOOL)
      fail(e->loc, "condition requires bool");
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
      if (!d)
        fail(e->loc, "unknown constructor '%s'", e->text);
      if (t && t != d->type)
        fail(e->loc, "constructor has nominal type '%s', expected '%s'",
             d->name, type_name(t));
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
    if (!t || t->kind != T_STRUCT)
      fail(e->loc, "contextual construction requires a nominal struct type; "
                   "write Type{field=value}");
    Module *scope =
        c->in_default && c->access ? c->access->module : c->in->fn->module;
    if (!e->text && t->def->module && t->def->module != scope &&
        !t->def->public) {
      fail(e->loc, "contextual construction requires a public type outside "
                   "its module");
    }
    if (e->text) {
      tool_record(c->p, e->loc, t->def->loc, t->def->name, t, NULL, t->def, 23,
                  false);
    }
    if (t->def->abstract)
      fail(e->loc, "cannot construct abstract def^ '%s'", t->def->name);
    if (dst < 0 && t->owned)
      fail(e->loc, "owned construction requires a destination");
    Value out = {0};
    out.type = t;
    out.id = dst >= 0 ? dst : slot(c, t, false);
    /* Fields built so far stay in use while later ones are evaluated. */
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
      if (t->def->error_variant && !f->index) {
        /* The hidden site records where this error is built and its message. */
        char *message = t->def->name;
        for (int k = 0; k < e->names.n; k++) {
          Expr *given = e->args.v[k];
          if (!strcmp(e->names.v[k], "message") && given->kind != E_STRING) {
            fail(given->loc, "an error's message is a string literal; pass details as fields");
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
      else if (init)
        v = construct(c, init, f->type, dest.id);
      else if (f->type->kind == T_REF || f->type->kind == T_STRING ||
               f->type->kind == T_SLICE || f->type->kind == T_UNION ||
               (f->type->kind == T_ARRAY && f->type->refs))
        fail(e->loc, "field '%s' needs an initializer", f->name);
      else if (f->type->kind == T_STRUCT) {
        Expr empty = {
            .kind = E_RECORD, .loc = e->loc, .text = f->type->def->name};
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
    bool grow = e->text != NULL; /* grow^/flex^ [...] or grow^/flex^ T[] */
    int flex = grow && !strcmp(e->text, "flex^");
    if (e->test_spec) {
      Type *named = type_spec(c->p, e->test_spec);
      if (t && t != named)
        fail(e->loc, "'%s' cannot initialize '%s'", type_name(named), type_name(t));
      t = named;
    }
    if (grow && t && (t->kind != T_GROW || t->count != flex))
      fail(e->loc, "%s array literal cannot initialize '%s'", e->text, type_name(t));
    Value first = {0};
    if (!t) {
      if (!e->args.n)
        fail(e->loc, "empty array needs a type annotation");
      first = expression(c, e->args.v[0], NULL);
    }
    if (!t && grow)
      t = type_wrap(c->p, T_GROW, first.type, flex);
    if (!t) {
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
    int inflight = c->inflight.n;
    add(&c->inflight, &out);
    for (int j = 0; j < e->args.n; j++) {
      if (t->kind == T_GROW) {
        Value v = j || !first.type ? expression(c, e->args.v[j], t->elem) : first;
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
static void operator_contract(Function *f, Type *result, Loc loc)
{
  if (!strcmp(f->name, "op^=") && result->kind != T_BOOL) {
    fail(loc, "op^= must return bool^");
  }
  if (!strcmp(f->name, "op^<=>") &&
      (!kind_integer(result->kind) || kind_unsigned(result->kind))) {
    fail(loc, "op^<=> must return a signed integer");
  }
}
static Value invoke_operator(Context *c, Function *f, Value a, Value b, Expr *origin)
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
/* A range index borrows a view: [lo :: hi] excludes hi, [lo to^ hi] includes
 * it, and an omitted bound is the start or the length. */
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
    fail(e->loc, "string^ has no []; slice it with s.slice(a :: b), or its bytes with s.bytes[a :: b]");
  }
  if (base.type->kind != T_ARRAY && base.type->kind != T_GROW &&
      base.type->kind != T_SLICE) {
    fail(e->loc, "slicing requires an array or slice");
  }
  if ((base.type->kind == T_ARRAY || base.type->kind == T_GROW) &&
      !base.addressable) {
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
  Type *elem =
      base.type->kind == T_STRING ? type_builtin(p, T_U8) : base.type->elem;
  Type *t = type_wrap(p, T_SLICE, elem, 0);
  if (expected && expected != t) {
    fail(e->loc, "slice is '%s', expected '%s'", type_name(t), type_name(expected));
  }
  Value out = {.id = slot(c, t, false),
               .type = t,
               .deps = base.deps | ((base.type->kind == T_ARRAY ||
                                     base.type->kind == T_GROW)
                                        ? base.storage
                                        : 0)};
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
/* Collect trailing arguments into a temporary array borrowed for one call,
 * or view a forwarded sequence. */
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
    /* No arguments: an empty view of one zeroed element. */
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
static Value expression(Context *c, Expr *e, Type *expected) {
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
      fail(e->loc, "panic^ takes a string^ or an error value, found '%s'",
           type_name(v.type));
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
        Expr pair = {.kind = E_BIN,
                     .loc = e->loc,
                     .text = e->names.v[j],
                     .a = &left,
                     .b = &right};
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
    Value src = e->a->kind == E_NAME ? place_expression(c, e->a)
                                     : expression(c, e->a, NULL);
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
    if (!v && e->kind == E_NAME && !e->scope_kind) {
      Type *type = callable_group(p, NULL, e->text);
      if (type) {
        for (int j = 0; j < p->functions.n; j++) {
          Function *member = p->functions.v[j];
          if (!strcmp(member->name, e->text)) {
            tool_record(p, e->loc, member->declaration, e->text, type,
                        NULL, NULL, 12, false);
          }
        }
        return callable_value(c, type, expected, e->loc);
      }
    }
    if (!v)
      fail(e->loc,
           "unknown value '%s' (closures and global values are not supported)",
           e->text);
    if (v->unavailable)
      fail(e->loc, "first^ binding '%s' is not definitely initialized here",
           v->name);
    tool_record(p, e->loc, v->declaration, v->name,
                v->narrowed && !c->raw_name ? v->narrowed : v->type, NULL, NULL,
                v->parameter ? TOOL_PARAMETER : 13, false);
    Value result = variable(v);
    if (v->narrowed && !c->raw_name) {
      return convert_value(c, result, v->narrowed, -1, e->loc);
    }
    return result;
  }
  if (e->kind == E_CALL)
    return call(c, e, expected, -1);
  if (e->kind == E_CHAR) {
    Value v = value(c, type_builtin(p, T_CHAR));
    Instr *i = emit(c, I_CONST, e->loc);
    i->dst = v.id;
    i->type = v.type;
    i->text = e->text;
    return v;
  }
  if (e->kind == E_RECORD || e->kind == E_ARRAY || e->kind == E_IF ||
      e->kind == E_SCOPE || e->kind == E_RANGE || e->kind == E_INTERP)
    return construct(c, e, expected, -1);
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
      /* s.bytes views the same bytes; string^ itself has no len. */
      if (strcmp(e->text, "bytes")) {
        fail(e->loc, "string^ has no %s; use s.bytes for bytes, or s.decode(at), s.slice(r) and "
                     "s.count_chars()", e->text);
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
      /* Every error variant starts with the same fields, so the first one reads them. */
      base = (Value){.id = case_at(c, base, view, false, e->loc), .type = view,
                     .deps = base.deps, .storage = base.storage, .var = base.var,
                     .addressable = base.addressable};
    }
    int site = error_site(base.type, e->text);
    if (site >= 0) {
      Value where = deref(c, field_value(c, base, base.type->def->fields.v[0], e->loc), e->loc);
      return field_value(c, where, where.type->def->fields.v[site], e->loc);
    }
    if (base.type->kind != T_STRUCT)
      fail(e->loc, "field access requires a structure");
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
    if (!f)
      fail(e->loc, "'%s' has no field '%s'", base.type->def->name, e->text);
    Value result = field_value(c, base, f, e->loc);
    Var *root = NULL;
    char *path = NULL;
    if (c->raw_place != e && stable_path(c, e, &root, &path)) {
      Type *type = path_type(root, path);
      if (type) {
        result = convert_value(c, result, type, -1, e->loc);
      }
    }
    tool_record(p, e->name_loc.file ? e->name_loc : e->loc, f->loc, f->name,
                result.type, NULL, f->owner, 8, false);
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
      fail(e->loc, "string^ has no []; read bytes with s.bytes[i], or characters with s.decode(at)");
    }
    if (base.type->kind != T_ARRAY && base.type->kind != T_GROW &&
        base.type->kind != T_SLICE)
      fail(e->loc, "indexing requires an array");
    Value index = expression(c, e->b, type_builtin(p, T_I64));
    if (dead(index)) {
      return index;
    }
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
        !strcmp(e->text, "ref^")
            ? place_expression(c, e->a)
            : expression(c, e->a,
                         (!strcmp(e->text, "-") || !strcmp(e->text, "bitnot^"))
                             ? expected
                             : NULL);
    if (dead(a)) {
      return a;
    }
    if (!strcmp(e->text, "ref^")) {
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
    bool literal_left = e->a->kind == E_INT || e->a->kind == E_FLOAT ||
                        (e->a->kind == E_UNARY && !strcmp(e->a->text, "-") &&
                         e->a->a->kind == E_INT);
    Type *left_hint = !logic && literal_left
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
      if (a.type->kind != T_BOOL)
        fail(e->loc, "logical operands must be bool");
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
                  : expression(c, e->b, operand_hint ? operand_hint : shift || nil_rhs ? NULL : a.type);
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
        Expr compare = {.kind = E_BIN, .loc = e->loc, .text = e->text,
                        .a = &left, .b = &zero};
        return expression(c, &compare, NULL);
      }
      return result;
    }
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
    if (cmp && a.type->kind == T_STRING) {
      /* Byte order, which UTF-8 makes the order of scalar values. */
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
        !(cmp && a.type->kind == T_CHAR))
      fail(e->loc, "operator '%s' is not defined for '%s'", e->text,
           type_name(a.type));
    if (kind_float(a.type->kind) && !strcmp(e->text, "%"))
      fail(e->loc, "floating remainder is not supported");
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
    if (!strcmp(e->text, "//") || !strcmp(e->text, "**") ||
        !strcmp(e->text, "..") || !strcmp(e->text, "cross^") || !strcmp(e->text, "dot^")) {
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
  Loop target = {.end = end,
                 .update = update,
                 .bindings = c->vars.n,
                 .outside = c->vars.n,
                 .phase = 1};
  add(&c->loops, &target);
  branch_to(c, test, s->loc);
  int code = c->in->code.n, slots = c->in->slots.n, labels = c->in->labels,
      roots = c->root_count;
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
    for (int j = 0; j < bindings; j++)
      before[j] = ((Var *)c->vars.v[j])->deps;
    label_at(c, test, s->loc);
    statements(c, &s->pre, false);
    Value cond = expression(c, s->a, type_builtin(c->p, T_BOOL));
    if (dead(cond)) {
      c->loops.n--;
      c->vars.n = bindings;
      return;
    }
    if (cond.type->kind != T_BOOL)
      fail(s->loc, "condition requires bool");
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
      if (live) {
        statements(c, &s->update, true);
      }
      c->lexical++;
    }
    if (!c->terminated)
      branch_to(c, test, s->loc);
    c->terminated = false;
    restore_facts(&c->vars, turn_facts);
    bool changed = false;
    for (int j = 0; j < bindings; j++)
      changed |= before[j] != ((Var *)c->vars.v[j])->deps ||
                 (before_fact[j] && !((Var *)c->vars.v[j])->narrowed) ||
                 (before_paths[j] && !((Var *)c->vars.v[j])->paths.n);
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
  Loop target = {.end = finish,
                 .update = update,
                 .bindings = c->vars.n,
                 .outside = scope,
                 .first = first};
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
          exits |= edge->label == finish ||
                   (edge->op == I_CBR && edge->other == finish);
          updates |= edge->label == update ||
                     (edge->op == I_CBR && edge->other == update);
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
    if (cond.type->kind != T_BOOL)
      fail(s->loc, "condition requires bool");
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
    if (live) {
      statements(c, &init, false);
    }
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
    restore_facts(&c->vars, turn_facts);
    bool changed = false;
    for (int j = 0; j < persistent; j++)
      changed |= before[j] != ((Var *)c->vars.v[j])->deps ||
                 (before_fact[j] && !((Var *)c->vars.v[j])->narrowed) ||
                 (before_paths[j] && !((Var *)c->vars.v[j])->paths.n);
    if (!changed)
      break;
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
      pair->pending =
          dead(b) ? b
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
    a->path_version = (a->path_version > b->path_version ? a->path_version
                                                         : b->path_version) +
                      1;
  }
  return left;
}
static void handle_statements(Context *c, Stmt *s)
{
  Stmt local = *s, branch = {0};
  List head_body = {0};
  if (s->a) {
    Value head =
        snapshot(c, expression(c, s->a, type_builtin(c->p, T_BOOL)), s->loc);
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
    fail(s->loc,
         "catch^ clause requires an error-producing try^ before its arms");
  }
  int base = c->vars.n, visible = base;
  if (s->loop_handler) {
    visible = ((Loop *)c->loops.v[c->loops.n - 1])->outside;
  }
  List before = save_facts(&c->vars);
  ErrorTarget target = {.errors = errors,
                        .slot = slot(c, errors, false),
                        .label = ++c->in->labels,
                        .bindings = base,
                        .pending = c->pending_owners.n,
                        .checkpoint = -1};
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
    } else if (s->kind == S_INIT) {
      Var *var = lookup(c, s->name);
      if (!var || !var->unavailable)
        fail(s->loc, "invalid first^ initialization");
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
      if (!t)
        t = expression_type(c, s->a, NULL);
      Value v = {0};
      bool alias_result = c->in->result_local &&
                          !strcmp(c->in->result_local, s->name) &&
                          c->depth == 1;
      int dest = alias_result ? result_place(c, t ? t : expression_type(c, s->a, NULL), s->loc)
                              : -1;
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
        if (t->kind == T_VOID)
          fail(s->loc, "binding cannot have void type");
        if (s->depth) {
          results_check(t, s->depth, s->loc);
        }
        else if (t->kind == T_STRUCT && t->def->tuple) {
          fail(s->loc, "multiple results '%s' need a name each, as in let^ a, b = ...",
               type_name(t));
        }
        if (dest < 0)
          dest = slot(c, t, false);
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
      } else {
        v = expression(c, s->a, NULL);
        if (dead(v)) {
          break;
        }
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
        tool_record(c->p, binding->loc, binding->loc, v->name, v->type, NULL,
                    NULL, 13, true);
        add(&c->vars, v);
      }
    }
    else if (s->kind == S_BIND_MANY) {
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
          tool_record(c->p, binding->loc, binding->loc, v->name, v->type, NULL,
                      NULL, 13, true);
        }
      }
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
        if (dead(dest[j])) {
          break;
        }
        if (!dest[j].addressable)
          fail(s->loc, "assignment destination is not addressable");
        if (dest[j].type->owned)
          fail(s->loc,
               "owned values cannot be reassigned while references may exist");
        if (s->op) {
          Value previous = dest[j];
          Var *root = NULL;
          char *path = NULL;
          if (previous.type->kind == T_UNION &&
              stable_path(c, target, &root, &path)) {
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
        src[j] = snapshot(c, expression(c, rhs, hint ? hint : s->op ? old[j].type : dest[j].type),
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
      if (c->terminated) {
        break;
      }
      if (c->terminated) {
        break;
      }
      for (int j = 0; j < count; j++) {
        if (!dest[j].type)
          continue;
        bool mut = dest[j].var && dest[j].var->mut;
        if (s->internal && dest[j].var)
          dest[j].var->mut = true;
        copy_to(c, dest[j], src[j], s->loc, false);
        Expr *target = s->targets.n ? s->targets.v[j] : s->a;
        if (s->op && target->kind == E_NAME && dest[j].var &&
            dest[j].type->kind == T_UNION &&
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
      if (dead(v)) {
        break;
      }
      if (v.type->owned)
        fail(s->loc, "owned temporary requires a binding");
      if (s->a->kind != E_CALL && s->a->kind != E_TRY &&
          s->a->kind != E_CATCH) {
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
          if (!r || (r->slot != c->in->result_slot && r->slot + 1 != c->result_part))
            fail(s->loc, "owned result binding was shadowed");
          v = variable(r);
          /* The owned last part is already in place; build the others. */
          Value base = {.id = c->in->result_slot, .type = t};
          for (int j = 0; s->a->tuple && t->kind == T_UNION && j < t->def->alternatives.n; j++) {
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
          v = construct(c,
                        &(Expr){.kind = E_NAME, .text = "nil^", .loc = s->loc},
                        t, c->in->result_slot);
        }
        else {
          fail(s->loc, "missing return value");
        }
      }
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
      if (dead(cond)) {
        break;
      }
      if (cond.type->kind != T_BOOL)
        fail(s->loc, "condition requires bool");
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
      if (s->internal)
        c->lexical--;
      List facts = save_facts(&c->vars);
      refine_condition(c->p, &c->vars, &c->flow, s->a, true);
      statements(c, &s->body, true);
      List after_yes = save_facts(&c->vars);
      rewind_facts(&c->vars, facts);
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
      bool b = c->terminated;
      if (!a && b) {
        rewind_facts(&c->vars, after_yes);
      }
      else if (!a && !b) {
        merge_facts(&c->vars, facts, after_yes);
      }
      if (s->internal)
        c->lexical++;
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
/* The local every return^ gives as the owned result; returns of another
 * alternative of a union result do not take part. */
static void return_names(List *body, char **name, bool *valid, bool others) {
  for (int j = 0; j < body->n; j++) {
    Stmt *s = body->v[j];
    if (s->kind == S_RETURN) {
      Expr *a = s->a && s->a->tuple ? s->a->args.v[s->a->args.n - 1] : s->a;
      if (!a || a->kind != E_NAME || !strcmp(a->text, "nil^")) {
        *valid &= others;
        continue;
      }
      if (*name && strcmp(*name, a->text))
        *valid = false;
      else
        *name = a->text;
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
    if (t->kind == T_STRUCT && t->def->tuple)
      fail(a->loc, "multiple results '%s' cannot be passed; bind them with let^ a, b = ...",
           type_name(t));
    if (t->owned)
      fail(a->loc, "owned by-value parameters are not supported; use 'ref^'");
    Var *v = mem(sizeof(*v));
    v->declaration = a->loc;
    tool_record(p, a->loc, a->loc, a->name, t, NULL, NULL, TOOL_PARAMETER,
                true);
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
    if (j < 64) {
      c.param_deps[j] = v->deps;
    }
    if (a->self)
      c.self_roots = v->deps;
    add(&c.vars, v);
  }
  statements(&c, &f->body, false);
  if (!c.terminated) {
    if (nullable(in->result) && has_error(in->result)) {
      construct(&c, &(Expr){.kind = E_NAME, .text = "nil^", .loc = f->loc},
                in->result, in->result_slot);
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
    if (!d->opaque) {
      layout(d->type, d->loc);
    }
    if (d->lhat_value || d->lhat_data) {
      lhat_def_validate(p, d);
    }
  }
  for (int j = 0; j < p->functions.n; j++) {
    Function *f = p->functions.v[j];
    if (lookup_def(p, f->name))
      fail(f->loc, "function conflicts with def^ '%s'", f->name);
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
        if ((base->kind == T_STRUCT && base->def->abstract) || callable_type(base))
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
