#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "rhat.h"
#include "tooling.h"
#ifndef RH_STDLIB_PATH
#define RH_STDLIB_PATH "stdlib"
#endif

static char *directory(const char *path) {
  char *s = str(path), *end = s;
  for (char *p = s; *p; p++)
    if (*p == '/' || *p == '\\')
      end = p;
  if (end == s)
    return str(*s == '/' || *s == '\\' ? "/" : ".");
  *end = 0;
  return s;
}
static bool exists(Program *p, const char *path)
{
  if (source_override(p, path)) {
    return true;
  }
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  fclose(f);
  return true;
}
static void load_dependencies(Program *p, Module *m) {
  m->state = 1;
  for (int n = 0; n < m->imports.n; n++) {
    Import *i = m->imports.v[n];
    Module *target = NULL;
    for (int j = 0; j < p->modules.n; j++) {
      Module *candidate = p->modules.v[j];
      if (candidate->name && !strcmp(candidate->name, i->name))
        target = candidate;
    }
    if (target && target->state == 1)
      fail(i->loc, "cyclic import of '%s'", i->name);
    if (!target) {
      char *relative = str(i->name);
      for (char *c = relative; *c; c++)
        if (*c == '.')
          *c = '/';
      char *path = NULL;
      if (!strncmp(i->name, "std.", 4)) {
        path = fmt("%s/%s.rh", p->stdlib_dir ? p->stdlib_dir : RH_STDLIB_PATH,
                   relative);
        if (!exists(p, path)) {
          path = NULL;
        }
      } else {
        for (int j = 0; j <= p->module_paths.n && !path; j++) {
          char *root =
              j < p->module_paths.n ? p->module_paths.v[j] : directory(p->path);
          char *candidate = fmt("%s/%s.rh", root, relative);
          if (exists(p, candidate)) {
            path = candidate;
          }
        }
      }
      if (!path)
        fail(i->loc, "cannot find module '%s'", i->name);
      target = mem(sizeof(*target));
      target->path = path;
      add(&p->modules, target);
      const char *root_path = p->path;
      char *root_source = p->source;
      Function *entry = p->entry;
      parse_file(p, path, target);
      p->entry = entry;
      p->path = root_path;
      p->source = root_source;
      if (!target->name || strcmp(target->name, i->name))
        fail(i->loc, "module '%s' must declare module^ %s", path, i->name);
      load_dependencies(p, target);
    }
    i->target = target;
  }
  m->state = 2;
}

/* Resolve lexical names before semantic analysis. Canonical declaration names
 * carry nominal identity; namespace paths themselves never become values. */
static char *canonical(Module *m, const char *name) {
  return m->name ? fmt("%s.%s", m->name, name) : str(name);
}
static bool has_name(Module *m, const char *name, bool external,
                     Constant **constant) {
  for (int j = 0; j < m->defs.n; j++) {
    Def *d = m->defs.v[j];
    if (!strcmp(d->name, name))
      return !external || d->public;
  }
  for (int j = 0; j < m->functions.n; j++) {
    Function *f = m->functions.v[j];
    if (!strcmp(f->name, name))
      return !external || f->public;
  }
  for (int j = 0; j < m->constants.n; j++) {
    Constant *c = m->constants.v[j];
    if (!strcmp(c->name, name)) {
      if (external && !c->public)
        return false;
      if (constant)
        *constant = c;
      return true;
    }
  }
  return false;
}
static bool visible_declaration(Module *m, const char *name) {
  if (has_name(m, name, false, NULL))
    return true;
  if (m->name) {
    size_t n = strlen(m->name);
    if (!strncmp(name, m->name, n) && name[n] == '.' &&
        has_name(m, name + n + 1, false, NULL))
      return true;
  }
  for (int j = 0; j < m->imports.n; j++) {
    Import *i = m->imports.v[j];
    const char *prefix = i->alias ? i->alias : i->name;
    size_t n = strlen(prefix);
    if (!strncmp(name, prefix, n) && name[n] == '.' &&
        has_name(i->target, name + n + 1, true, NULL))
      return true;
  }
  return false;
}
static char *resolve(Module *m, char *name, Loc loc, Constant **constant) {
  if (has_name(m, name, false, constant))
    return canonical(m, name);
  char *own_member = name;
  if (m->name && !strncmp(name, m->name, strlen(m->name)) &&
      name[strlen(m->name)] == '.') {
    own_member = name + strlen(m->name) + 1;
    if (has_name(m, own_member, false, constant))
      return canonical(m, own_member);
  }
  char *own_prefix = str(own_member);
  for (char *dot = strrchr(own_prefix, '.'); dot;
       dot = strrchr(own_prefix, '.')) {
    *dot = 0;
    if (has_name(m, own_prefix, false, NULL))
      return name;
  }
  Import *best = NULL;
  for (int j = 0; j < m->imports.n; j++) {
    Import *i = m->imports.v[j];
    char *prefix = i->alias ? i->alias : i->name;
    size_t n = strlen(prefix);
    if (!strncmp(name, prefix, n) && name[n] == '.' &&
        (!best || n > strlen(best->alias ? best->alias : best->name)))
      best = i;
  }
  if (best) {
    char *prefix = best->alias ? best->alias : best->name;
    char *member = name + strlen(prefix) + 1;
    if (!has_name(best->target, member, true, constant)) {
      char *prefix_member = str(member);
      for (char *dot = strrchr(prefix_member, '.'); dot;
           dot = strrchr(prefix_member, '.')) {
        *dot = 0;
        if (has_name(best->target, prefix_member, true, NULL))
          return name;
      }
      fail(loc, "module '%s' does not export '%s'", best->name, member);
    }
    return canonical(best->target, member);
  }
  if (strchr(name, '.'))
    fail(loc,
         "unknown or unavailable qualified name '%s'; import its module "
         "explicitly",
         name);
  return name;
}
static char *resolve_type_name(Module *m, char *name, Loc loc) {
  if (strchr(name, '.') && !visible_declaration(m, name))
    fail(loc,
         "unknown or unavailable type '%s'; its module must export it and be "
         "explicitly imported",
         name);
  return resolve(m, name, loc, NULL);
}
static void resolve_spec(Module *m, Spec *s) {
  if (!s || s->resolved)
    return;
  s->resolved = true;
  if (s->kind == SPEC_NAME)
    s->name = resolve_type_name(m, s->name, s->loc);
  else {
    resolve_spec(m, s->elem);
    resolve_spec(m, s->other);
    for (int j = 0; j < s->params.n; j++) {
      resolve_spec(m, s->params.v[j]);
    }
  }
}
static bool local(List *names, const char *name) {
  size_t n = strcspn(name, ".");
  for (int j = names->n - 1; j >= 0; j--)
    if (strlen(names->v[j]) == n && !strncmp(names->v[j], name, n))
      return true;
  return false;
}
static char *path_of(Expr *e) {
  if (!e)
    return NULL;
  if (e->kind == E_NAME)
    return e->text;
  if (e->kind == E_MEMBER && !e->optional) {
    char *a = path_of(e->a);
    if (a)
      return fmt("%s.%s", a, e->text);
  }
  return NULL;
}
static void resolve_expr(Program *, Module *, List *, Expr *);
static void resolve_body(Program *, Module *, List *, List *);
static void resolve_function(Program *p, Module *m, Function *f) {
  f->module = m;
  List names = {0};
  for (int j = 0; j < f->params.n; j++) {
    Param *a = f->params.v[j];
    resolve_spec(m, a->spec);
    add(&names, a->name);
  }
  resolve_spec(m, f->result);
  resolve_body(p, m, &names, &f->body);
}
static bool constant_expr(Expr *e) {
  if (!e)
    return false;
  if (e->kind == E_INT || e->kind == E_FLOAT || e->kind == E_BOOL ||
      e->kind == E_STRING)
    return true;
  if (e->kind == E_NAME && !strcmp(e->text, "nil^"))
    return true;
  if (e->kind == E_UNARY)
    return constant_expr(e->a);
  if (e->kind == E_FITS) {
    return !e->a || constant_expr(e->a);
  }
  if (e->kind == E_BIN)
    return constant_expr(e->a) && constant_expr(e->b);
  if (e->kind == E_CHAIN) {
    for (int j = 0; j < e->args.n; j++)
      if (!constant_expr(e->args.v[j]))
        return false;
    return true;
  }
  return false;
}
static Expr *copy_expr(Expr *e) {
  if (!e)
    return NULL;
  Expr *r = mem(sizeof(*r));
  *r = *e;
  r->a = copy_expr(e->a);
  r->b = copy_expr(e->b);
  r->c = copy_expr(e->c);
  r->args = (List){0};
  for (int j = 0; j < e->args.n; j++)
    add(&r->args, copy_expr(e->args.v[j]));
  return r;
}
static void resolve_expr(Program *p, Module *m, List *names, Expr *e) {
  if (!e)
    return;
  char *path = path_of(e);
  if (path && !local(names, path)) {
    Constant *c = NULL;
    char *resolved = resolve(m, path, e->loc, &c);
    if (c) {
      tool_record(p, e->name_loc.file ? e->name_loc : e->loc, c->binding->loc,
                  resolved, NULL, NULL, NULL, 14, false);
      Expr *r = copy_expr(c->binding->a);
      r->constant_spec = c->binding->spec;
      *e = *r;
      return;
    }
    bool declaration = visible_declaration(m, path);
    for (int j = 0; j < m->imports.n; j++) {
      Import *i = m->imports.v[j];
      const char *prefix = i->alias ? i->alias : i->name;
      size_t n = strlen(prefix);
      if (!strncmp(path, prefix, n) && path[n] == '.' &&
          has_name(i->target, path + n + 1, true, NULL))
        declaration = true;
    }
    if (strcmp(path, resolved) || declaration) {
      e->kind = E_NAME;
      e->text = resolved;
      e->a = e->b = NULL;
      return;
    }
  }
  if (e->kind == E_RECORD && e->text)
    e->text = resolve_type_name(m, e->text, e->loc);
  if (e->test_spec) {
    resolve_spec(m, e->test_spec);
  }
  if (e->kind == E_FUNCTION) {
    resolve_function(p, m, e->function);
    return;
  }
  if (e->kind == E_SCOPE) {
    int begin = names->n;
    resolve_body(p, m, names, &e->bindings);
    for (int j = 0; j < e->bindings.n; j++)
      add(names, ((Stmt *)e->bindings.v[j])->name);
    resolve_expr(p, m, names, e->a);
    names->n = begin;
    return;
  }
  if (e->kind == E_CATCH) {
    resolve_expr(p, m, names, e->a);
    int begin = names->n;
    add(names, "it^");
    resolve_expr(p, m, names, e->b);
    names->n = begin;
    return;
  }
  resolve_expr(p, m, names, e->a);
  resolve_expr(p, m, names, e->b);
  resolve_expr(p, m, names, e->c);
  for (int j = 0; j < e->args.n; j++)
    resolve_expr(p, m, names, e->args.v[j]);
}
static void bound_names(List *names, List *body) {
  for (int j = 0; j < body->n; j++) {
    Stmt *s = body->v[j];
    if (s->kind == S_BIND)
      add(names, s->name);
    else if (s->kind == S_BIND_MANY)
      bound_names(names, &s->body);
  }
}
static void resolve_body(Program *p, Module *m, List *names, List *body) {
  int old = names->n;
  for (int j = 0; j < body->n; j++) {
    Stmt *s = body->v[j];
    if (s->kind == S_HANDLE) {
      resolve_body(p, m, names, &s->body);
      for (int k = 0; k < s->handlers.n; k++) {
        CatchArm *arm = s->handlers.v[k];
        resolve_spec(m, arm->spec);
        int begin = names->n;
        add(names, "it^");
        resolve_body(p, m, names, &arm->body);
        names->n = begin;
      }
      continue;
    }
    if (s->kind == S_WHILE) {
      int begin = names->n;
      resolve_body(p, m, names, &s->prolog);
      bound_names(names, &s->prolog);
      int persistent = names->n;
      resolve_body(p, m, names, &s->pre);
      bound_names(names, &s->pre);
      resolve_expr(p, m, names, s->a);
      resolve_body(p, m, names, &s->first);
      bound_names(names, &s->first);
      if (s->body.n == 1 && ((Stmt *)s->body.v[0])->kind == S_HANDLE &&
          ((Stmt *)s->body.v[0])->loop_handler) {
        Stmt *handle = s->body.v[0];
        resolve_body(p, m, names, &handle->body);
        int saved = names->n;
        names->n = begin;
        for (int k = 0; k < handle->handlers.n; k++) {
          CatchArm *arm = handle->handlers.v[k];
          resolve_spec(m, arm->spec);
          int outer = names->n;
          add(names, "it^");
          resolve_body(p, m, names, &arm->body);
          names->n = outer;
        }
        names->n = saved;
      }
      else {
        resolve_body(p, m, names, &s->body);
      }
      names->n = persistent;
      bound_names(names, &s->first);
      resolve_body(p, m, names, &s->update);
      resolve_body(p, m, names, &s->last);
      resolve_body(p, m, names, &s->epilog);
      names->n = begin;
      continue;
    }
    if (s->kind == S_BIND_MANY) {
      for (int k = 0; k < s->body.n; k++) {
        Stmt *b = s->body.v[k];
        resolve_spec(m, b->spec);
        resolve_expr(p, m, names, b->a);
      }
      for (int k = 0; k < s->body.n; k++)
        add(names, ((Stmt *)s->body.v[k])->name);
      continue;
    }
    resolve_spec(m, s->spec);
    if (s->kind == S_ASSIGN && s->targets.n) {
      for (int k = 0; k < s->targets.n; k++)
        resolve_expr(p, m, names, s->targets.v[k]);
      for (int k = 0; k < s->values.n; k++)
        resolve_expr(p, m, names, s->values.v[k]);
    } else {
      resolve_expr(p, m, names, s->a);
      resolve_expr(p, m, names, s->b);
    }
    resolve_body(p, m, names, &s->body);
    resolve_body(p, m, names, &s->other);
    resolve_body(p, m, names, &s->update);
    if (s->kind == S_BIND)
      add(names, s->name);
  }
  names->n = old;
}
static void check_names(Module *m) {
  List declared = {0};
  for (int j = 0; j < m->defs.n; j++)
    add(&declared, ((Def *)m->defs.v[j])->name);
  for (int j = 0; j < m->constants.n; j++)
    add(&declared, ((Constant *)m->constants.v[j])->name);
  for (int j = 0; j < m->functions.n; j++) {
    Function *f = m->functions.v[j];
    bool overload = false;
    for (int k = 0; k < j; k++)
      if (!strcmp(f->name, ((Function *)m->functions.v[k])->name))
        overload = true;
    if (!overload)
      add(&declared, f->name);
  }
  for (int j = 0; j < declared.n; j++) {
    char *name = declared.v[j];
    for (int k = 0; k < j; k++)
      if (!strcmp(name, declared.v[k]))
        fail((Loc){m->path, 1, 1}, "duplicate declaration '%s'", name);
    for (int k = 0; k < m->imports.n; k++) {
      Import *i = m->imports.v[k];
      char *root = str(i->alias ? i->alias : i->name);
      char *dot = strchr(root, '.');
      if (dot)
        *dot = 0;
      if (!strcmp(root, name))
        fail(i->loc, "import namespace conflicts with declaration '%s'", name);
    }
  }
  List bindings = {0};
  bound_names(&bindings, &m->entry->body);
  for (int j = 0; j < m->imports.n; j++) {
    Import *i = m->imports.v[j];
    char *a = i->alias ? i->alias : i->name;
    char *root = str(a);
    char *root_dot = strchr(root, '.');
    if (root_dot)
      *root_dot = 0;
    for (int n = 0; n < bindings.n; n++)
      if (!strcmp(bindings.v[n], root))
        fail(i->loc, "binding conflicts with import namespace '%s'", root);
    for (int k = 0; k < j; k++) {
      Import *other = m->imports.v[k];
      char *b = other->alias ? other->alias : other->name;
      if (!strcmp(a, b) && strcmp(i->name, other->name))
        fail(i->loc, "conflicting import alias '%s'", a);
      {
        size_t n = strlen(b);
        if (!strncmp(a, b, n) && a[n] == '.') {
          char *member = str(a + n + 1);
          char *dot = strchr(member, '.');
          if (dot)
            *dot = 0;
          if (has_name(other->target, member, true, NULL))
            fail(i->loc, "module child conflicts with export '%s'", member);
        }
        n = strlen(a);
        if (!strncmp(b, a, n) && b[n] == '.') {
          char *member = str(b + n + 1);
          char *dot = strchr(member, '.');
          if (dot)
            *dot = 0;
          if (has_name(i->target, member, true, NULL))
            fail(i->loc, "module child conflicts with export '%s'", member);
        }
      }
    }
  }
}
void resolve_modules(Program *p) {
  /* Names remain local until every module's references have been resolved. */
  for (int j = 0; j < p->modules.n; j++) {
    Module *m = p->modules.v[j];
    List names = {0};
    check_names(m);
    for (int k = 0; k < m->constants.n; k++) {
      Constant *c = m->constants.v[k];
      if (!constant_expr(c->binding->a))
        fail(c->binding->loc,
             "module constant requires scalar literal operations");
      resolve_spec(m, c->binding->spec);
      Function *f = mem(sizeof(*f));
      f->name = fmt("<constant:%s>", c->name);
      f->module = m;
      f->loc = c->binding->loc;
      f->result = c->binding->spec;
      Stmt *ret = mem(sizeof(*ret));
      ret->kind = S_RETURN;
      ret->loc = f->loc;
      ret->a = copy_expr(c->binding->a);
      add(&f->body, ret);
      add(&p->functions, f);
      add(&m->functions, f);
    }
    for (int k = 0; k < m->defs.n; k++) {
      Def *d = m->defs.v[k];
      for (int n = 0; n < d->base_names.n; n++)
        d->base_names.v[n] = resolve_type_name(m, d->base_names.v[n], d->loc);
      for (int n = 0; n < d->union_specs.n; n++) {
        resolve_spec(m, d->union_specs.v[n]);
      }
      for (int n = 0; n < d->own_fields.n; n++) {
        Field *f = d->own_fields.v[n];
        resolve_spec(m, f->spec);
        resolve_expr(p, m, &names, f->init);
      }
      for (int n = 0; n < d->own_methods.n; n++)
        resolve_function(p, m, d->own_methods.v[n]);
    }
    for (int k = 0; k < m->functions.n; k++)
      resolve_function(p, m, m->functions.v[k]);
    resolve_body(p, m, &names, &m->entry->body);
  }
  for (int j = 0; j < p->modules.n; j++) {
    Module *m = p->modules.v[j];
    for (int k = 0; k < m->defs.n; k++) {
      Def *d = m->defs.v[k];
      d->name = canonical(m, d->name);
    }
    for (int k = 0; k < m->functions.n; k++) {
      Function *f = m->functions.v[k];
      f->name = canonical(m, f->name);
    }
  }
}
void parse(Program *p, const char *path) {
  Module *m = mem(sizeof(*m));
  m->root = !p->library_root;
  m->path = str(path);
  add(&p->modules, m);
  parse_file(p, path, m);
  p->entry->module = m;
  if (m->name && !strncmp(m->name, "std.", 4)) {
    char *relative = str(m->name);
    for (char *c = relative; *c; c++)
      if (*c == '.')
        *c = '/';
    char *expected = fmt(
        "%s/%s.rh", p->stdlib_dir ? p->stdlib_dir : RH_STDLIB_PATH, relative);
#if defined(_WIN32)
    char *a = _fullpath(NULL, path, 0), *b = _fullpath(NULL, expected, 0);
    bool same = a && b && !_stricmp(a, b);
#else
    char *a = realpath(path, NULL), *b = realpath(expected, NULL);
    bool same = a && b && !strcmp(a, b);
#endif
    free(a);
    free(b);
    if (!same) {
      fail((Loc){path, 1, 1},
           "std.* is reserved for the bundled standard library");
    }
  }
  load_dependencies(p, m);
  resolve_modules(p);
}
