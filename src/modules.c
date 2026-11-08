#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "rhat.h"
#include "tooling.h"
#include <errno.h>
#ifndef RH_STDLIB_PATH
#define RH_STDLIB_PATH "stdlib"
#endif

static char *directory(const char *path)
{
    char *s = str(path), *end = s;
    for (char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\') {
            end = p;
        }
    }
    if (end == s) {
        return str(*s == '/' || *s == '\\' ? "/" : ".");
    }
    *end = 0;
    return s;
}

static bool exists(Program *p, const char *path)
{
    if (source_override(p, path)) {
        return true;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

static void load_dependencies(Program *p, Module *m);

static void load_dependency(Program *p, Import *i)
{
    Module *target = NULL;
    for (int j = 0; j < p->modules.n; j++) {
        Module *candidate = p->modules.v[j];
        if (candidate->name && !strcmp(candidate->name, i->name)) {
            target = candidate;
        }
    }
    if (target && target->state == 1) {
        fail(i->loc, "cyclic import of '%s'", i->name);
    }
    if (!target) {
        char *relative = str(i->name);
        for (char *c = relative; *c; c++) {
            if (*c == '.') {
                *c = '/';
            }
        }
        char *path = NULL;
        if (!strncmp(i->name, "std.", 4)) {
            path = fmt("%s/%s.rh", p->stdlib_dir ? p->stdlib_dir : RH_STDLIB_PATH, relative);
            if (!exists(p, path)) {
                path = NULL;
            }
        }
        else {
            for (int j = 0; j <= p->module_paths.n && !path; j++) {
                char *root = j < p->module_paths.n ? p->module_paths.v[j] : directory(p->path);
                char *candidate = fmt("%s/%s.rh", root, relative);
                if (exists(p, candidate)) {
                    path = candidate;
                }
            }
        }
        if (!path) {
            fail(i->loc, "cannot find module '%s'", i->name);
        }
        target = mem(sizeof(*target));
        target->path = path;
        add(&p->modules, target);
        const char *root_path = p->path;
        char *root_source = p->source;
        Function *entry = p->entry;
        // Even a failed file read leaves a safe, empty module for indexing.
        target->entry = mem(sizeof(Function));
        parse_file(p, path, target);
        p->entry = entry;
        p->path = root_path;
        p->source = root_source;
        if (!target->name || strcmp(target->name, i->name)) {
            fail(i->loc, "module '%s' must declare module^ %s", path, i->name);
        }
        load_dependencies(p, target);
    }
    i->target = target;
}

static void dependency_checked(Program *p, Import *i)
{
    if (!p->tooling) {
        load_dependency(p, i);
        return;
    }
    const char *path = p->path;
    char *source = p->source;
    Function *entry = p->entry;
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        p->path = path;
        p->source = source;
        p->entry = entry;
        i->invalid = true;
        i->target = mem(sizeof(Module));
        i->target->name = i->name;
    }
    else {
        load_dependency(p, i);
    }
    tool_leave(&recovery);
}

static void load_dependencies(Program *p, Module *m)
{
    m->state = 1;
    for (int n = 0; n < m->imports.n; n++) {
        dependency_checked(p, m->imports.v[n]);
    }
    m->state = 2;
}

// Resolve lexical names before semantic analysis. Canonical declaration names
// carry nominal identity; namespace paths themselves never become values.
static char *canonical(Module *m, const char *name)
{
    return m->name ? fmt("%s.%s", m->name, name) : str(name);
}

static bool has_name(Module *m, const char *name, bool external, Constant **constant)
{
    for (int j = 0; j < m->aliases.n; j++) {
        TypeAlias *a = m->aliases.v[j];
        if (!strcmp(a->name, name)) {
            return !external || a->public;
        }
    }
    for (int j = 0; j < m->defs.n; j++) {
        Def *d = m->defs.v[j];
        if (!strcmp(d->name, name)) {
            return !external || d->public;
        }
    }
    for (int j = 0; j < m->functions.n; j++) {
        Function *f = m->functions.v[j];
        if (!strcmp(f->name, name)) {
            return !external || f->public;
        }
    }
    for (int j = 0; j < m->constants.n; j++) {
        Constant *c = m->constants.v[j];
        if (!strcmp(c->name, name)) {
            if (external && !c->public) {
                return false;
            }
            if (constant) {
                *constant = c;
            }
            return true;
        }
    }
    return false;
}

static bool visible_declaration(Module *m, const char *name)
{
    if (has_name(m, name, false, NULL)) {
        return true;
    }
    if (m->name) {
        size_t n = strlen(m->name);
        if (!strncmp(name, m->name, n) && name[n] == '.' &&
            has_name(m, name + n + 1, false, NULL)) {
            return true;
        }
    }
    for (int j = 0; j < m->imports.n; j++) {
        Import *i = m->imports.v[j];
        const char *prefix = i->alias ? i->alias : i->name;
        size_t n = strlen(prefix);
        if (!strncmp(name, prefix, n) && name[n] == '.' && i->invalid) {
            tool_abort();
        }
        if (!strncmp(name, prefix, n) && name[n] == '.' &&
            has_name(i->target, name + n + 1, true, NULL)) {
            return true;
        }
    }
    return false;
}

static char *resolve(Module *m, char *name, Loc loc, Constant **constant)
{
    if (has_name(m, name, false, constant)) {
        return canonical(m, name);
    }
    char *own_member = name;
    if (m->name && !strncmp(name, m->name, strlen(m->name)) && name[strlen(m->name)] == '.') {
        own_member = name + strlen(m->name) + 1;
        if (has_name(m, own_member, false, constant)) {
            return canonical(m, own_member);
        }
    }
    char *own_prefix = str(own_member);
    for (char *dot = strrchr(own_prefix, '.'); dot; dot = strrchr(own_prefix, '.')) {
        *dot = 0;
        if (has_name(m, own_prefix, false, NULL)) {
            return name;
        }
    }
    Import *best = NULL;
    for (int j = 0; j < m->imports.n; j++) {
        Import *i = m->imports.v[j];
        char *prefix = i->alias ? i->alias : i->name;
        size_t n = strlen(prefix);
        if (!strncmp(name, prefix, n) && name[n] == '.' &&
            (!best || n > strlen(best->alias ? best->alias : best->name))) {
            best = i;
        }
    }
    if (best) {
        if (best->invalid) {
            tool_abort();
        }
        char *prefix = best->alias ? best->alias : best->name;
        char *member = name + strlen(prefix) + 1;
        if (!has_name(best->target, member, true, constant)) {
            char *prefix_member = str(member);
            for (char *dot = strrchr(prefix_member, '.'); dot; dot = strrchr(prefix_member, '.')) {
                *dot = 0;
                if (has_name(best->target, prefix_member, true, NULL)) {
                    return name;
                }
            }
            fail(loc, "module '%s' does not export '%s'", best->name, member);
        }
        return canonical(best->target, member);
    }
    if (strchr(name, '.')) {
        fail(loc,
             "unknown or unavailable qualified name '%s'; import its module "
             "explicitly",
             name);
    }
    return name;
}

static char *resolve_type_name(Module *m, char *name, Loc loc)
{
    if (strchr(name, '.') && !visible_declaration(m, name)) {
        fail(loc,
             "unknown or unavailable type '%s'; its module must export it and be "
             "explicitly imported",
             name);
    }
    return resolve(m, name, loc, NULL);
}

static void resolve_spec(Module *m, Spec *s);
static void resolve_definition(Program *p, Def *d);

static void resolve_alias(TypeAlias *a)
{
    if (a->state == 2) {
        return;
    }
    if (a->state == 1) {
        fail(a->loc, "cyclic type alias '%s'", a->name);
    }
    a->state = 1;
    resolve_spec(a->module, a->spec);
    a->state = 2;
}

// Definition names are still module-local during this pass.
static Def *named_definition(Program *p, const char *name)
{
    if (!name) {
        return NULL;
    }
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        if (!strcmp(canonical(d->module, d->name), name)) {
            return d;
        }
    }
    return NULL;
}

static TypeAlias *named_alias(Program *p, const char *name)
{
    for (int j = 0; j < p->modules.n; j++) {
        Module *m = p->modules.v[j];
        for (int k = 0; k < m->aliases.n; k++) {
            TypeAlias *a = m->aliases.v[k];
            if (!strcmp(canonical(m, a->name), name)) {
                return a;
            }
        }
    }
    return NULL;
}

static char *spec_identity(Spec *s);

static void identity_parts(List *parts, Spec *s, int kind)
{
    if (s->kind == kind) {
        identity_parts(parts, s->elem, kind);
        identity_parts(parts, s->other, kind);
        return;
    }
    char *name = spec_identity(s);
    int at = 0;
    while (at < parts->n && strcmp(parts->v[at], name) < 0) {
        at++;
    }
    if (at < parts->n && !strcmp(parts->v[at], name)) {
        return;
    }
    add(parts, name);
    memmove(parts->v + at + 1, parts->v + at, (size_t)(parts->n - at - 1) * sizeof(void *));
    parts->v[at] = name;
}

static char *spec_identity(Spec *s)
{
    if (s->kind == SPEC_NAME) {
        return s->self_type ? canonical(s->self_type->module, s->self_type->name) : s->name;
    }
    if (s->kind == SPEC_UNION || s->kind == SPEC_INTERSECTION) {
        List parts = {0};
        identity_parts(&parts, s, s->kind);
        char *key = parts.v[0];
        for (int j = 1; j < parts.n; j++) {
            key = fmt("%s %s %s", key, s->kind == SPEC_UNION ? "|" : "&", parts.v[j]);
        }
        return parts.n == 1 ? key : fmt("(%s)", key);
    }
    char *elem = s->elem ? spec_identity(s->elem) : "";
    switch (s->kind) {
    case SPEC_REF:
        return fmt("ref^%s (%s)", s->count ? "read^" : "", elem);
    case SPEC_DYN:
        return fmt("dyn^ (%s)", elem);
    case SPEC_ARRAY:
        return s->count ? fmt("(%s)[%d]", elem, s->count) : fmt("(%s)[]", elem);
    case SPEC_GROW:
        return fmt("(%s)[%s]", elem,
                   s->count == ARRAY_SIZED    ? ""
                   : s->count == ARRAY_CAPPED ? "max^"
                   : s->count                 ? "flex^"
                                              : "grow^");
    case SPEC_SLICE:
        return fmt("%s(%s)[..]", s->count ? "read^ " : "", elem);
    case SPEC_BOUNDED:
        return fmt("(%s)[max^%d]", elem, s->count);
    default:
        break;
    }
    char *key = s->kind == SPEC_FUNCTION ? (s->procedure ? "p^(" : "f^(") : "(";
    for (int j = 0; j < s->params.n; j++) {
        key = fmt("%s%s%s", key, j ? "," : "", spec_identity(s->params.v[j]));
    }
    return s->kind == SPEC_FUNCTION ? fmt("%s%s)->%s;", s->foreign ? "externC^ " : "", key, elem)
                                    : fmt("%s)", key);
}

static void apply_definition(Module *m, Spec *s, Def *source)
{
    Program *p = m->program;
    if (source->invalid) {
        tool_abort();
    }
    if (s->type_args.n != source->type_params.n) {
        fail(s->loc, "type '%s' expects %d type arguments, got %d", source->name,
             source->type_params.n, s->type_args.n);
    }
    char *name = fmt("%s<", source->name);
    for (int j = 0; j < s->type_args.n; j++) {
        resolve_spec(m, s->type_args.v[j]);
        name = fmt("%s%s%s", name, j ? "," : "", spec_identity(s->type_args.v[j]));
    }
    name = fmt("%s>", name);
    Def *instance = NULL;
    for (int j = 0; j < source->instances.n; j++) {
        Def *d = source->instances.v[j];
        if (!strcmp(name, d->name)) {
            instance = d;
            break;
        }
    }
    if (!instance) {
        if (p->defs.n >= 4096 || source->instances.n >= 512) {
            fail(s->loc, "generic type specialization limit exceeded at '%s'", source->name);
        }
        instance = specialize_definition(source, &s->type_args, name);
        instance->id = p->defs.n;
        add(&source->instances, instance);
        add(&p->defs, instance);
        add(&source->module->defs, instance);
        resolve_definition(p, instance);
    }
    tool_record(p, s->loc, source->loc, source->name, NULL, NULL, source, 23, false);
    s->name = canonical(instance->module, instance->name);
    s->self_type = instance;
    s->type_args = (List){0};
}

static Stmt *top_binding(const char *name);
static Function *top_user; // The function whose body is being resolved.
static char *path_of(Expr *e);
static char *resolve(Module *m, char *name, Loc loc, Constant **constant);

// The value of an integer constant expression: literals, unary -, + - * / %,
// and names of entry-file top-level let^ bindings or module constants bound to
// such expressions. Names in locals (when given) are not constants.
static bool constant_integer(Module *m, List *locals, Expr *e, int depth, long long *out)
{
    const long long limit = 1LL << 40;
    if (!e || depth > 64) {
        return false;
    }
    if (e->kind == E_INT) {
        errno = 0;
        char *end = NULL;
        *out = strtoll(e->text, &end, 10);
        return !errno && end && !*end && *out <= limit;
    }
    if (e->kind == E_UNARY && !strcmp(e->text, "-")) {
        if (!constant_integer(m, locals, e->a, depth + 1, out)) {
            return false;
        }
        *out = -*out;
        return true;
    }
    if (e->kind == E_BIN) {
        long long a = 0, b = 0;
        if (!constant_integer(m, locals, e->a, depth + 1, &a) ||
            !constant_integer(m, locals, e->b, depth + 1, &b)) {
            return false;
        }
        const char *op = e->text;
        if ((!strcmp(op, "/") || !strcmp(op, "%")) && !b) {
            return false;
        }
        *out = !strcmp(op, "+")   ? a + b
               : !strcmp(op, "-") ? a - b
               : !strcmp(op, "*") ? a * b
               : !strcmp(op, "/") ? a / b
               : !strcmp(op, "%") ? a % b
                                  : limit + 1;
        return *out >= -limit && *out <= limit;
    }
    char *path = path_of(e);
    if (!path) {
        return false;
    }
    Stmt *binding = m->root ? top_binding(path) : NULL;
    // The top level's own names list its bindings; any other entry is a local.
    int seen = 0;
    for (int j = 0; locals && j < locals->n; j++) {
        seen += !strcmp(locals->v[j], path);
    }
    if (seen > (binding && !top_user ? 1 : 0)) {
        return false;
    }
    if (binding) {
        int kind = binding->spec && binding->spec->kind == SPEC_NAME
                       ? numeric_kind(binding->spec->name)
                       : -1;
        if (binding->mut || (binding->spec && !kind_integer(kind))) {
            return false;
        }
        return constant_integer(m, NULL, binding->a, depth + 1, out);
    }
    Constant *c = NULL;
    if (visible_declaration(m, path)) {
        resolve(m, path, e->loc, &c);
    }
    return c && constant_integer(m, NULL, c->binding->a, depth + 1, out);
}

static void resolve_spec_unchecked(Module *m, Spec *s)
{
    if (!s || s->resolved) {
        return;
    }
    s->resolved = true;
    if (s->kind == SPEC_TYPEOF) {
        return; // The operand is resolved with the enclosing expression's locals.
    }
    if (s->length) {
        long long n = 0;
        if (!constant_integer(m, NULL, s->length, 0, &n)) {
            fail(s->length->loc, "array length must be a constant integer expression");
        }
        if (n <= 0 || n > 1000000) {
            fail(s->length->loc, "array length out of range (1..1000000)");
        }
        s->count = (int)n;
    }
    if (s->self_type && s->type_args.n) {
        fail(s->loc, "Self^ does not accept type arguments");
    }
    if (s->kind == SPEC_NAME && !strcmp(s->name, "range^") && s->type_args.n) {
        if (s->type_args.n != 1) {
            fail(s->loc, "type 'range^' expects 1 type argument, got %d", s->type_args.n);
        }
        resolve_spec(m, s->type_args.v[0]);
        s->name = fmt("range^<%s>", spec_identity(s->type_args.v[0]));
    }
    else if (s->kind == SPEC_NAME && !s->self_type) {
        s->name = resolve_type_name(m, s->name, s->loc);
        TypeAlias *a = named_alias(m->program, s->name);
        if (a) {
            if (s->type_args.n) {
                fail(s->loc, "type alias '%s' does not accept type arguments", a->name);
            }
            resolve_alias(a);
            tool_record(m->program, s->loc, a->loc, canonical(a->module, a->name), NULL, NULL, NULL,
                        23, false);
            Loc loc = s->loc;
            *s = *a->spec;
            s->loc = loc;
            return;
        }
        Def *d = named_definition(m->program, s->name);
        if (s->type_args.n || (d && d->type_params.n)) {
            if (!d) {
                fail(s->loc, "unknown generic type '%s'", s->name);
            }
            apply_definition(m, s, d);
        }
    }
    else {
        resolve_spec(m, s->elem);
        resolve_spec(m, s->other);
        for (int j = 0; j < s->params.n; j++) {
            resolve_spec(m, s->params.v[j]);
        }
    }
}

static void resolve_spec(Module *m, Spec *s)
{
    if (!s || s->resolved) {
        return;
    }
    if (!compiler_hooks) {
        resolve_spec_unchecked(m, s);
        return;
    }
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        s->invalid = true;
        s->resolved = true;
    }
    else {
        resolve_spec_unchecked(m, s);
    }
    tool_leave(&recovery);
}

static bool local(List *names, const char *name)
{
    size_t n = strcspn(name, ".");
    for (int j = names->n - 1; j >= 0; j--) {
        if (strlen(names->v[j]) == n && !strncmp(names->v[j], name, n)) {
            return true;
        }
    }
    return false;
}

// A type bound by let^ inside a body. A names list holds this exact name string, so the list's
// scoping and shadowing apply to types as they do to values.
typedef struct {
    char *name;
    Spec *spec;
    Loc loc;
} LocalType;

static List local_types;

// What a name means at this point of a body when it is a local type, else NULL.
static Spec *local_type(Program *p, List *names, const char *name, Loc use)
{
    for (int j = names->n - 1; j >= 0; j--) {
        if (strcmp(names->v[j], name)) {
            continue;
        }
        for (int k = 0; k < local_types.n; k++) {
            LocalType *t = local_types.v[k];
            if (t->name == names->v[j]) {
                tool_record(p, use, t->loc, t->name, NULL, NULL, NULL, 23, false);
                return t->spec;
            }
        }
        return NULL;
    }
    return NULL;
}

// Reads a local type's name in an expression as the type.
static void read_local_type(Program *p, List *names, Expr *e)
{
    Spec *s = e->kind == E_NAME ? local_type(p, names, e->text, e->loc) : NULL;
    if (s) {
        *e = (Expr){.kind = E_TYPE, .loc = e->loc, .type_expr = s};
    }
}

static char *path_of(Expr *e)
{
    if (!e) {
        return NULL;
    }
    if (e->kind == E_NAME) {
        // The variadic arguments ... never name a module member.
        return strcmp(e->text, "...") ? e->text : NULL;
    }
    if (e->kind == E_MEMBER && !e->optional) {
        char *a = path_of(e->a);
        if (a) {
            return fmt("%s.%s", a, e->text);
        }
    }
    return NULL;
}

static void resolve_expr(Program *, Module *, List *, Expr *);
static void resolve_body(Program *, Module *, List *, List *);

// The entry file's functions see its top-level bindings. Each function that
// reaches one, itself or through a call, takes it as a hidden leading
// parameter: a var^ by reference, a let^ by value. The lifetime, shrink and
// effect checks then treat it as any argument. Other files never see them.
typedef struct {
    Expr *call;
    Function *caller; // NULL in the entry body itself.
    List names;       // The entry body's names in scope at the call.
} TopCall;

static List top_bindings;   // S_BIND statements of the entry's top level.
static List top_calls;      // TopCall pointers.
static List top_references; // Function names used other than as a callee.

static char *hidden_name(Stmt *binding)
{
    return fmt("\001top_%s", binding->name);
}

static Stmt *top_binding(const char *name)
{
    for (int j = 0; j < top_bindings.n; j++) {
        Stmt *s = top_bindings.v[j];
        if (!strcmp(s->name, name)) {
            return s;
        }
    }
    return NULL;
}

static void use_binding(Function *f, Stmt *binding)
{
    for (int j = 0; j < f->uses.n; j++) {
        if (f->uses.v[j] == binding) {
            return;
        }
    }
    add(&f->uses, binding);
}

static Expr *name_expr(const char *name, Loc loc)
{
    Expr *e = mem(sizeof(*e));
    e->kind = E_NAME;
    e->text = (char *)name;
    e->loc = loc;
    return e;
}

static Expr *unary_expr(const char *op, Expr *a)
{
    Expr *e = mem(sizeof(*e));
    e->kind = E_UNARY;
    e->text = (char *)op;
    e->loc = a->loc;
    e->a = a;
    return e;
}

static void resolve_function(Program *p, Module *m, Function *f)
{
    if (f->resolved) {
        return;
    }
    f->resolved = true;
    Function *outer = top_user;
    top_user = f;
    f->module = m;
    List names = {0};
    for (int j = 0; j < f->params.n; j++) {
        Param *a = f->params.v[j];
        resolve_spec(m, a->spec);
        add(&names, a->name);
    }
    resolve_spec(m, f->result);
    resolve_body(p, m, &names, &f->body);
    top_user = outer;
}

static bool constant_expr(Expr *e, bool values)
{
    if (!e) {
        return false;
    }
    if (e->kind == E_INT || e->kind == E_FLOAT || e->kind == E_BOOL || e->kind == E_STRING) {
        return true;
    }
    if (e->kind == E_NAME && !strcmp(e->text, "nil^")) {
        return true;
    }
    if (e->kind == E_UNARY) {
        return constant_expr(e->a, values);
    }
    if (e->kind == E_FITS) {
        return !e->a || constant_expr(e->a, values);
    }
    if (e->kind == E_BIN) {
        return constant_expr(e->a, values) && constant_expr(e->b, values);
    }
    if (values && (e->kind == E_RECORD || e->kind == E_ARRAY)) {
        if (e->array_refs || (e->kind == E_ARRAY && e->test_spec)) {
            return false;
        }
        for (int j = 0; j < e->args.n; j++) {
            if (!constant_expr(e->args.v[j], values)) {
                return false;
            }
        }
        return true;
    }
    if (values && e->kind == E_MEMBER) {
        return constant_expr(e->a, values);
    }
    if (e->kind == E_CHAIN) {
        for (int j = 0; j < e->args.n; j++) {
            if (!constant_expr(e->args.v[j], values)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static Expr *copy_expr(Expr *e)
{
    if (!e) {
        return NULL;
    }
    Expr *r = mem(sizeof(*r));
    *r = *e;
    r->a = copy_expr(e->a);
    r->b = copy_expr(e->b);
    r->c = copy_expr(e->c);
    r->args = (List){0};
    for (int j = 0; j < e->args.n; j++) {
        add(&r->args, copy_expr(e->args.v[j]));
    }
    return r;
}

static Spec *expression_type(Module *m, Expr *e)
{
    if (!e) {
        return NULL;
    }
    if (e->kind == E_TYPE) {
        return e->type_expr;
    }
    char *path = path_of(e);
    if (!path) {
        return NULL;
    }
    bool builtin = numeric_kind(path) >= 0 || !strcmp(path, "bool^") || !strcmp(path, "string^") ||
                   !strcmp(path, "char^");
    if (!builtin && !visible_declaration(m, path)) {
        return NULL;
    }
    char *name = builtin ? path : resolve(m, path, e->loc, NULL);
    if (!builtin && !named_definition(m->program, name) && !named_alias(m->program, name)) {
        return NULL;
    }
    Spec *s = mem(sizeof(*s));
    *s = (Spec){.kind = SPEC_NAME, .loc = e->loc, .name = path};
    return s;
}

// The type e names at this point of a body, or NULL when it names a value.
static Spec *written_type(Program *p, Module *m, List *names, Expr *e)
{
    char *path = path_of(e);
    if (path && local(names, path)) {
        return e->kind == E_NAME ? local_type(p, names, path, e->loc) : NULL;
    }
    return expression_type(m, e);
}

static Constant *type_constant(Program *p, Def *d, const char *name, int depth)
{
    if (depth > 128) {
        fail(d->loc, "cyclic def^ composition at '%s'", d->name);
    }
    for (int j = 0; j < d->constants.n; j++) {
        Constant *c = d->constants.v[j];
        if (!strcmp(c->name, name)) {
            return c;
        }
    }
    Constant *result = NULL;
    for (int j = 0; j < d->base_specs.n; j++) {
        Spec *s = d->base_specs.v[j];
        resolve_spec(d->module, s);
        Def *base = s->self_type ? s->self_type : named_definition(p, s->name);
        Constant *c = base ? type_constant(p, base, name, depth + 1) : NULL;
        if (c && result && c != result) {
            fail(d->loc, "inherited type constant '%s' is ambiguous", name);
        }
        if (c) {
            result = c;
        }
    }
    return result;
}

// Whether d or a def^ it composes declares a static member name.
static bool static_member(Program *p, Def *d, const char *name, int depth)
{
    if (depth > 128) {
        fail(d->loc, "cyclic def^ composition at '%s'", d->name);
    }
    for (int j = 0; j < d->own_methods.n; j++) {
        Function *f = d->own_methods.v[j];
        if (f->static_member && !strcmp(f->name, name)) {
            return true;
        }
    }
    for (int j = 0; j < d->base_specs.n; j++) {
        Spec *s = d->base_specs.v[j];
        resolve_spec(d->module, s);
        Def *base = s->self_type ? s->self_type : named_definition(p, s->name);
        if (base && static_member(p, base, name, depth + 1)) {
            return true;
        }
    }
    return false;
}

static void resolve_constant(Program *p, Constant *c)
{
    if (c->state == 2) {
        return;
    }
    if (c->state == 1) {
        fail(c->binding->loc, "cyclic type constant '%s.%s'", c->owner->name, c->name);
    }
    c->state = 1;
    Function scope = {.owner = c->owner};
    Function *outer = top_user;
    top_user = &scope;
    List names = {0};
    resolve_expr(p, c->owner->module, &names, c->binding->a);
    top_user = outer;
    if (!constant_expr(c->binding->a, true)) {
        fail(c->binding->loc, "type constant requires literal operations and value construction");
    }
    c->binding->a->constant_owner = c->owner;
    c->state = 2;
}

static void expand_constant(Program *p, Constant *c, Expr *e)
{
    if (!c->public && (!top_user || top_user->owner != c->owner)) {
        fail(e->loc, "private type constant '%s.%s'", c->owner->name, c->name);
    }
    Loc loc = e->loc;
    resolve_constant(p, c);
    tool_record(p, loc, c->binding->loc, c->name, NULL, NULL, c->owner, 14, false);
    *e = *copy_expr(c->binding->a);
    e->loc = loc;
}

static bool module_function(Module *m, const char *name)
{
    for (int j = 0; j < m->functions.n; j++) {
        if (!strcmp(((Function *)m->functions.v[j])->name, name)) {
            return true;
        }
    }
    return false;
}

// A local annotation's typeof^ operands see the same locals as expressions.
static void resolve_local_spec(Program *p, Module *m, List *names, Spec *s)
{
    if (!s || s->resolved) {
        return;
    }
    Spec *alias = s->kind == SPEC_NAME ? local_type(p, names, s->name, s->loc) : NULL;
    if (alias) {
        if (s->type_args.n) {
            fail(s->loc, "type alias '%s' does not accept type arguments", s->name);
        }
        Loc loc = s->loc;
        *s = *alias;
        s->loc = loc;
        return;
    }
    resolve_expr(p, m, names, s->operand);
    resolve_local_spec(p, m, names, s->elem);
    resolve_local_spec(p, m, names, s->other);
    for (int j = 0; j < s->params.n; j++) {
        resolve_local_spec(p, m, names, s->params.v[j]);
    }
    for (int j = 0; j < s->type_args.n; j++) {
        resolve_local_spec(p, m, names, s->type_args.v[j]);
    }
    resolve_spec(m, s);
}

static void resolve_expr_unchecked(Program *p, Module *m, List *names, Expr *e)
{
    if (!e) {
        return;
    }
    long long count = 0;
    if (e->kind == E_ARRAY && e->text && !strcmp(e->text, "repeat") && e->args.n == 2 &&
        constant_integer(m, names, e->args.v[1], 0, &count) && count > 0 && count <= 1000000) {
        e->repeat_count = (int)count;
    }
    read_local_type(p, names, e);
    if (e->kind == E_TYPE) {
        resolve_local_spec(p, m, names, e->type_expr);
        return;
    }
    if (e->kind == E_IF && e->match && local(names, e->match->text) &&
        local_type(p, names, e->match->text, e->loc)) {
        fail(e->loc, "a match on a type requires other^:");
    }
    bool equality = e->kind == E_BIN && (!strcmp(e->text, "=") || !strcmp(e->text, "!="));
    if (equality) {
        read_local_type(p, names, e->a);
        read_local_type(p, names, e->b);
    }
    if (equality && (e->a->kind == E_TYPE || e->b->kind == E_TYPE)) {
        // typeof^(x) = i32^ compares types; the name side is read as a type.
        Expr **sides[] = {&e->a, &e->b};
        for (int j = 0; j < 2; j++) {
            Expr *side = *sides[j];
            Spec *s = written_type(p, m, names, side);
            if (!s) {
                fail(side->loc, "a type comparison needs a type on both sides");
            }
            if (side->kind != E_TYPE) {
                *sides[j] = mem(sizeof(Expr));
                **sides[j] = (Expr){.kind = E_TYPE, .loc = side->loc, .type_expr = s};
            }
            resolve_expr(p, m, names, *sides[j]);
        }
        return;
    }
    if (e->kind == E_CALL) {
        Spec *s = written_type(p, m, names, e->a);
        if (s) {
            resolve_spec(m, s);
            if (s->kind == SPEC_NAME && numeric_kind(s->name) >= 0) {
                *e->a = *name_expr(s->name, e->a->loc);
            }
            Def *d = s->kind == SPEC_NAME ? named_definition(p, s->name) : NULL;
            if (d && d->backing) {
                // Mode(n) converts an integer to enum^ Mode.
                Expr *type = mem(sizeof(*type));
                *type = (Expr){.kind = E_TYPE, .loc = e->a->loc, .type_expr = s};
                e->a = type;
            }
        }
    }
    if (e->kind == E_MEMBER && !e->optional) {
        Spec *s = written_type(p, m, names, e->a);
        if (!strcmp(e->text, "signature^")) {
            if (!s) {
                fail(e->loc, "signature^ is a member of types; write typeof^(value).signature^");
            }
            if (e->a->kind != E_TYPE) {
                Expr *type = mem(sizeof(*type));
                *type = (Expr){.kind = E_TYPE, .loc = e->a->loc, .type_expr = s};
                e->a = type;
            }
            resolve_expr(p, m, names, e->a);
            return;
        }
        if (s && s->kind == SPEC_TYPEOF) {
            fail(e->loc, "a typeof^ type provides only signature^");
        }
        if (s) {
            resolve_spec(m, s);
            int kind = s->kind == SPEC_NAME ? numeric_kind(s->name) : -1;
            if (kind >= 0 && (!strcmp(e->text, "ZERO") || !strcmp(e->text, "ONE"))) {
                e->kind = E_INT;
                e->text = !strcmp(e->text, "ZERO") ? "0" : "1";
                e->constant_spec = s;
                e->a = NULL;
                return;
            }
            Def *d = s->self_type ? s->self_type : named_definition(p, s->name);
            Constant *c = d ? type_constant(p, d, e->text, 0) : NULL;
            if (c) {
                expand_constant(p, c, e);
                return;
            }
            if (d && static_member(p, d, e->text, 0)) {
                // Type.member(...): the receiver stays a type, not a value.
                Expr *type = mem(sizeof(*type));
                *type = (Expr){.kind = E_TYPE, .loc = e->a->loc, .type_expr = s};
                e->kind = E_STATIC;
                e->a = type;
                return;
            }
            if ((kind >= 0 && (!strcmp(e->text, "parse") || !strcmp(e->text, "from_bits"))) ||
                (d && !strcmp(e->text, "new"))) {
                *e->a =
                    *name_expr(s->self_type ? canonical(d->module, d->name) : s->name, e->a->loc);
                return;
            }
            if (d && d->backing) {
                char *member = fmt("%s.%s", d->name, e->text);
                for (int j = 0; j < d->module->constants.n; j++) {
                    Constant *value = d->module->constants.v[j];
                    if (!strcmp(value->name, member)) {
                        Loc loc = e->name_loc.file ? e->name_loc : e->loc;
                        tool_record(p, loc, value->binding->loc, canonical(d->module, member), NULL,
                                    NULL, NULL, 14, false);
                        Loc use = e->loc;
                        *e = *copy_expr(value->binding->a);
                        e->loc = use;
                        e->constant_spec = s;
                        return;
                    }
                }
            }
            if (!d || !d->backing) {
                fail(e->loc, "type '%s' has no constant or static member '%s'", spec_identity(s),
                     e->text);
            }
        }
    }
    if (e->kind == E_NAME && top_user && top_user->owner && !local(names, e->text)) {
        Constant *c = type_constant(p, top_user->owner, e->text, 0);
        if (c) {
            expand_constant(p, c, e);
            return;
        }
    }
    if (e->kind == E_MEMBER && top_user && top_user->owner) {
        char *path = path_of(e);
        if (path && !local(names, path)) {
            char *root = str(path), *dot = strchr(root, '.');
            if (dot) {
                *dot = 0;
            }
            if (type_constant(p, top_user->owner, root, 0)) {
                resolve_expr(p, m, names, e->a);
                return;
            }
        }
    }
    if (m->root && e->kind == E_NAME && !local(names, e->text)) {
        Stmt *binding = top_user ? top_binding(e->text) : NULL;
        if (binding) {
            if (top_user->name && !strcmp(top_user->name, "<field-default>")) {
                fail(e->loc, "field default cannot capture caller locals");
            }
            // Inside a function the binding is its hidden parameter.
            if (top_user->owner || !module_function(m, top_user->name)) {
                fail(e->loc,
                     "only top-level functions can use the top-level binding '%s'; pass it "
                     "as an argument",
                     binding->name);
            }
            use_binding(top_user, binding);
            tool_record(p, e->loc, binding->loc, binding->name, NULL, NULL, NULL,
                        binding->mut ? 13 : 14, false);
            *e = *unary_expr("*", name_expr(hidden_name(binding), e->loc));
            return;
        }
        if (module_function(m, e->text)) {
            add(&top_references, e);
        }
    }
    if (m->root && e->kind == E_CALL && e->a && e->a->kind == E_NAME && !local(names, e->a->text) &&
        module_function(m, e->a->text)) {
        TopCall *call = mem(sizeof(*call));
        call->call = e;
        call->caller = top_user;
        for (int j = 0; !top_user && j < names->n; j++) {
            add(&call->names, names->v[j]);
        }
        add(&top_calls, call);
    }
    char *root = path_of(e);
    if (m->root && top_user && root && strchr(root, '.')) {
        root = str(root);
        *strchr(root, '.') = 0;
        // a.b of a top-level binding a: its leaf name is rewritten below. A
        // subexpression shared by a desugaring may already name the hidden one.
        if (!local(names, root) && (top_binding(root) || root[0] == '\001')) {
            resolve_expr(p, m, names, e->a);
            return;
        }
    }
    if (e->kind == E_MEMBER && e->a && e->a->kind == E_NAME && numeric_kind(e->a->text) >= 0) {
        return; // i32^.parse: a member of a built-in type, which no module declares
    }
    char *path = path_of(e);
    if (path && !local(names, path)) {
        Constant *c = NULL;
        char *resolved = resolve(m, path, e->loc, &c);
        if (c) {
            tool_record(p, e->name_loc.file ? e->name_loc : e->loc, c->binding->loc, resolved, NULL,
                        NULL, NULL, 14, false);
            Expr *r = copy_expr(c->binding->a);
            r->constant_spec = c->binding->spec;
            r->loc = e->loc;
            *e = *r;
            return;
        }
        bool declaration = visible_declaration(m, path);
        for (int j = 0; j < m->imports.n; j++) {
            Import *i = m->imports.v[j];
            const char *prefix = i->alias ? i->alias : i->name;
            size_t n = strlen(prefix);
            if (!strncmp(path, prefix, n) && path[n] == '.' &&
                has_name(i->target, path + n + 1, true, NULL)) {
                declaration = true;
            }
        }
        if (strcmp(path, resolved) || declaration) {
            e->kind = E_NAME;
            e->text = resolved;
            e->a = e->b = NULL;
            return;
        }
    }
    if (e->kind == E_RECORD && e->text) {
        Spec *alias = local_type(p, names, e->text, e->loc);
        if (alias && alias->kind == SPEC_TYPEOF) {
            fail(e->loc, "a typeof^ type cannot construct values; name the type");
        }
        Spec s = alias ? *alias : (Spec){.kind = SPEC_NAME, .loc = e->loc, .name = e->text};
        resolve_spec(m, &s);
        e->text = s.self_type ? canonical(s.self_type->module, s.self_type->name) : s.name;
    }
    if (e->kind == E_RECORD && e->type_expr) {
        if (e->type_expr->kind == SPEC_TYPEOF) {
            fail(e->loc, "a typeof^ type cannot construct values; name the type");
        }
        resolve_spec(m, e->type_expr);
        Spec *s = e->type_expr;
        e->text = s->self_type ? canonical(s->self_type->module, s->self_type->name) : s->name;
    }
    resolve_local_spec(p, m, names, e->test_spec);
    resolve_local_spec(p, m, names, e->array_literal);
    if (e->kind == E_FUNCTION) {
        resolve_function(p, m, e->function);
        return;
    }
    if (e->kind == E_SCOPE) {
        int begin = names->n;
        resolve_body(p, m, names, &e->bindings);
        for (int j = 0; j < e->bindings.n; j++) {
            add(names, ((Stmt *)e->bindings.v[j])->name);
        }
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
    for (int j = 0; j < e->args.n; j++) {
        resolve_expr(p, m, names, e->args.v[j]);
    }
}

static void bound_names(List *names, List *body)
{
    for (int j = 0; j < body->n; j++) {
        Stmt *s = body->v[j];
        if (s->kind == S_BIND || s->kind == S_TYPE) {
            add(names, s->name);
        }
        else if (s->kind == S_BIND_MANY) {
            bound_names(names, &s->body);
        }
    }
}

static void resolve_expr(Program *p, Module *m, List *names, Expr *e)
{
    if (!e || e->invalid) {
        return;
    }
    if (!p->tooling) {
        resolve_expr_unchecked(p, m, names, e);
        return;
    }
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        e->invalid = true;
    }
    else {
        resolve_expr_unchecked(p, m, names, e);
        e->invalid |= (e->a && e->a->invalid) || (e->b && e->b->invalid) || (e->c && e->c->invalid);
        for (int j = 0; j < e->args.n; j++) {
            e->invalid |= ((Expr *)e->args.v[j])->invalid;
        }
    }
    tool_leave(&recovery);
}

// let^ t = a type: t names it for the rest of the scope. One that depends on typeof^ is fixed
// here, per specialization, so later narrowing and shadowing of its operand do not change it.
static void bind_local_type(Program *p, Module *m, List *names, Stmt *s, Spec *type)
{
    resolve_local_spec(p, m, names, type);
    Spec *alias = type;
    s->name = fmt("%s", s->name);
    if (dependent_spec(type)) {
        alias = mem(sizeof(*alias));
        *alias = (Spec){.kind = SPEC_TYPEOF, .loc = s->loc, .name = s->name, .elem = type};
        alias->pins = mem(sizeof(List));
        alias->resolved = true;
    }
    s->kind = S_TYPE;
    s->spec = alias;
    s->a = NULL;
    LocalType *t = mem(sizeof(*t));
    *t = (LocalType){.name = s->name, .spec = alias, .loc = s->loc};
    add(&local_types, t);
    add(names, s->name);
    tool_record(p, s->loc, s->loc, s->name, NULL, NULL, NULL, 23, true);
}

static void resolve_body(Program *p, Module *m, List *names, List *body)
{
    int old = names->n;
    for (int j = 0; j < body->n; j++) {
        Stmt *s = body->v[j];
        if (s->kind == S_HANDLE) {
            resolve_body(p, m, names, &s->body);
            for (int k = 0; k < s->handlers.n; k++) {
                CatchArm *arm = s->handlers.v[k];
                resolve_local_spec(p, m, names, arm->spec);
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
                    resolve_local_spec(p, m, names, arm->spec);
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
        if (s->kind == S_INLINE) {
            resolve_expr(p, m, names, s->a);
            int begin = names->n;
            add(names, s->name);
            resolve_body(p, m, names, &s->body);
            names->n = begin;
            continue;
        }
        if (s->kind == S_BIND_MANY) {
            for (int k = 0; k < s->prolog.n; k++) {
                Stmt *b = s->prolog.v[k];
                resolve_expr(p, m, names, b->a);
                add(names, b->name);
            }
            for (int k = 0; k < s->body.n; k++) {
                Stmt *b = s->body.v[k];
                resolve_local_spec(p, m, names, b->spec);
                resolve_expr(p, m, names, b->a);
            }
            for (int k = 0; k < s->body.n; k++) {
                add(names, ((Stmt *)s->body.v[k])->name);
            }
            continue;
        }
        Spec *type = s->kind == S_BIND && !s->mut && !s->spec && s->a
                         ? written_type(p, m, names, s->a)
                         : NULL;
        if (type) {
            bind_local_type(p, m, names, s, type);
            continue;
        }
        resolve_local_spec(p, m, names, s->spec);
        if (s->kind == S_ASSIGN && s->targets.n) {
            for (int k = 0; k < s->targets.n; k++) {
                resolve_expr(p, m, names, s->targets.v[k]);
            }
            for (int k = 0; k < s->values.n; k++) {
                resolve_expr(p, m, names, s->values.v[k]);
            }
        }
        else {
            resolve_expr(p, m, names, s->a);
            resolve_expr(p, m, names, s->b);
        }
        resolve_body(p, m, names, &s->body);
        resolve_body(p, m, names, &s->other);
        resolve_body(p, m, names, &s->update);
        if (s->kind == S_BIND) {
            add(names, s->name);
        }
    }
    names->n = old;
}

static void check_names(Program *p, Module *m)
{
    List declared = {0}, places = {0}; // Names and their declaration Loc *.
    for (int j = 0; j < m->aliases.n; j++) {
        TypeAlias *a = m->aliases.v[j];
        add(&declared, a->name);
        add(&places, &a->loc);
    }
    for (int j = 0; j < m->defs.n; j++) {
        Def *d = m->defs.v[j];
        add(&declared, d->name);
        add(&places, &d->loc);
    }
    for (int j = 0; j < m->constants.n; j++) {
        Constant *c = m->constants.v[j];
        add(&declared, c->name);
        add(&places, &c->binding->loc);
    }
    for (int j = 0; j < m->functions.n; j++) {
        Function *f = m->functions.v[j];
        bool overload = false;
        for (int k = 0; k < j; k++) {
            if (!strcmp(f->name, ((Function *)m->functions.v[k])->name)) {
                overload = true;
            }
        }
        if (!overload) {
            add(&declared, f->name);
            add(&places, f->declaration.file ? &f->declaration : &f->loc);
        }
    }
    for (int j = 0; j < declared.n; j++) {
        char *name = declared.v[j];
        for (int k = 0; k < j; k++) {
            if (!strcmp(name, declared.v[k])) {
                // Report the later of the two in source order.
                Loc *a = places.v[j], *b = places.v[k];
                Loc *later = a->line > b->line || (a->line == b->line && a->col > b->col) ? a : b;
                TOOL_CHECK(p, fail(*later, "duplicate declaration '%s'", name););
            }
        }
        for (int k = 0; k < m->imports.n; k++) {
            Import *i = m->imports.v[k];
            char *root = str(i->alias ? i->alias : i->name);
            char *dot = strchr(root, '.');
            if (dot) {
                *dot = 0;
            }
            if (!strcmp(root, name)) {
                TOOL_CHECK(p,
                           fail(i->loc, "import namespace conflicts with declaration '%s'", name););
            }
        }
    }
    List bindings = {0};
    bound_names(&bindings, &m->entry->body);
    for (int j = 0; j < m->imports.n; j++) {
        Import *i = m->imports.v[j];
        char *a = i->alias ? i->alias : i->name;
        char *root = str(a);
        char *root_dot = strchr(root, '.');
        if (root_dot) {
            *root_dot = 0;
        }
        for (int n = 0; n < bindings.n; n++) {
            if (!strcmp(bindings.v[n], root)) {
                TOOL_CHECK(p, fail(i->loc, "binding conflicts with import namespace '%s'", root););
            }
        }
        for (int k = 0; k < j; k++) {
            Import *other = m->imports.v[k];
            char *b = other->alias ? other->alias : other->name;
            if (!strcmp(a, b) && strcmp(i->name, other->name)) {
                TOOL_CHECK(p, fail(i->loc, "conflicting import alias '%s'", a););
            }
            {
                size_t n = strlen(b);
                if (!strncmp(a, b, n) && a[n] == '.') {
                    char *member = str(a + n + 1);
                    char *dot = strchr(member, '.');
                    if (dot) {
                        *dot = 0;
                    }
                    if (has_name(other->target, member, true, NULL)) {
                        TOOL_CHECK(
                            p, fail(i->loc, "module child conflicts with export '%s'", member););
                    }
                }
                n = strlen(a);
                if (!strncmp(b, a, n) && b[n] == '.') {
                    char *member = str(b + n + 1);
                    char *dot = strchr(member, '.');
                    if (dot) {
                        *dot = 0;
                    }
                    if (has_name(i->target, member, true, NULL)) {
                        TOOL_CHECK(
                            p, fail(i->loc, "module child conflicts with export '%s'", member););
                    }
                }
            }
        }
    }
}

// The hidden parameters of the entry file's functions, and the arguments
// every call passes for them.
static void pass_top_bindings(Program *p, Module *m)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int j = 0; j < top_calls.n; j++) {
            TopCall *call = top_calls.v[j];
            for (int k = 0; call->caller && k < m->functions.n; k++) {
                Function *callee = m->functions.v[k];
                for (int n = 0; !strcmp(callee->name, call->call->a->text) && n < callee->uses.n;
                     n++) {
                    int before = call->caller->uses.n;
                    use_binding(call->caller, callee->uses.v[n]);
                    changed |= call->caller->uses.n != before;
                }
            }
        }
        // Overloads share their hidden parameters, so a call fits any of them.
        for (int j = 0; j < m->functions.n; j++) {
            Function *f = m->functions.v[j];
            for (int k = 0; k < m->functions.n; k++) {
                Function *g = m->functions.v[k];
                for (int n = 0; g != f && !strcmp(f->name, g->name) && n < g->uses.n; n++) {
                    int before = f->uses.n;
                    use_binding(f, g->uses.v[n]);
                    changed |= f->uses.n != before;
                }
            }
        }
    }
    for (int j = 0; j < top_calls.n; j++) {
        Function *caller = ((TopCall *)top_calls.v[j])->caller;
        if (caller && caller->uses.n && (caller->owner || !module_function(m, caller->name))) {
            TOOL_CHECK(p, fail(caller->loc,
                               "only top-level functions can use the top-level binding '%s'; "
                               "pass it as an argument",
                               ((Stmt *)caller->uses.v[0])->name););
            caller->invalid = true;
        }
    }
    // Identify value references once, rather than for every captured function.
    int values = 0;
    for (int j = 0; j < top_references.n; j++) {
        Expr *e = top_references.v[j];
        bool callee = false;
        for (int k = 0; !callee && k < top_calls.n; k++) {
            callee = ((TopCall *)top_calls.v[k])->call->a == e;
        }
        if (!callee) {
            top_references.v[values++] = e;
        }
    }
    top_references.n = values;
    for (int j = 0; j < m->functions.n; j++) {
        Function *f = m->functions.v[j];
        if (!f->uses.n) {
            continue;
        }
        Stmt *first = f->uses.v[0];
        if (f->owner) {
            TOOL_CHECK(
                p, fail(f->loc,
                        "a method cannot use the top-level binding '%s'; pass it as an argument",
                        first->name););
            f->invalid = true;
        }
        if (f->exported || f->lhat_export) {
            TOOL_CHECK(p, fail(f->loc, "an exported function cannot use the top-level binding '%s'",
                               first->name););
            f->invalid = true;
        }
        for (int k = 0; k < top_references.n; k++) {
            Expr *e = top_references.v[k];
            if (!strcmp(e->text, f->name)) {
                TOOL_CHECK(p, fail(e->loc,
                                   "'%s' uses the top-level binding '%s' and cannot be a value",
                                   f->name, first->name););
                e->invalid = true;
            }
        }
        // In declaration order, so that every overload lists them alike.
        List params = {0};
        for (int k = 0; k < top_bindings.n; k++) {
            Stmt *binding = top_bindings.v[k];
            for (int n = 0; n < f->uses.n; n++) {
                if (f->uses.v[n] != binding) {
                    continue;
                }
                Param *a = mem(sizeof(*a));
                a->loc = binding->loc;
                a->name = hidden_name(binding);
                a->capture = binding;
                a->spec = binding->spec;
                if (binding->spec) {
                    a->spec = mem(sizeof(*a->spec));
                    *a->spec = (Spec){.kind = SPEC_REF, .loc = binding->loc, .elem = binding->spec};
                }
                add(&params, a);
            }
        }
        for (int k = 0; k < f->params.n; k++) {
            add(&params, f->params.v[k]);
        }
        f->params = params;
    }
    for (int j = 0; j < top_calls.n; j++) {
        TopCall *call = top_calls.v[j];
        Function *callee = NULL;
        for (int k = 0; !callee && k < m->functions.n; k++) {
            Function *f = m->functions.v[k];
            callee = !strcmp(f->name, call->call->a->text) && f->uses.n ? f : NULL;
        }
        if (!callee) {
            continue;
        }
        List args = {0};
        for (int k = 0; k < callee->params.n; k++) {
            Param *a = callee->params.v[k];
            if (strncmp(a->name, "\001top_", 5)) {
                break;
            }
            Stmt *binding = top_binding(a->name + 5);
            Loc at = call->call->loc;
            if (call->caller) {
                add(&args, name_expr(a->name, at));
                continue;
            }
            int seen = 0;
            for (int n = 0; n < call->names.n; n++) {
                seen += !strcmp(call->names.v[n], binding->name);
            }
            if (!seen) {
                TOOL_CHECK(
                    p, fail(at, "'%s' uses the top-level binding '%s', declared later at line %d",
                            callee->name, binding->name, binding->loc.line););
                call->call->invalid = true;
            }
            if (seen > 1) {
                TOOL_CHECK(p, fail(at,
                                   "'%s' uses the top-level binding '%s', which a local hides here",
                                   callee->name, binding->name););
                call->call->invalid = true;
            }
            Expr *borrow = unary_expr("ref^", name_expr(binding->name, at));
            borrow->top_binding = !binding->mut;
            add(&args, borrow);
        }
        for (int k = 0; k < call->call->args.n; k++) {
            add(&args, call->call->args.v[k]);
        }
        call->call->args = args;
    }
}

static void resolve_definition(Program *p, Def *d)
{
    if (d->resolved || d->type_params.n || d->invalid) {
        return;
    }
    d->resolved = true;
    Module *m = d->module;
    List names = {0};
    for (int j = 0; j < d->base_specs.n; j++) {
        Spec *s = d->base_specs.v[j];
        resolve_spec(m, s);
        if (s->kind != SPEC_NAME) {
            fail(s->loc, "composition base must be a def^ type");
        }
        add(&d->base_names,
            s->self_type ? canonical(s->self_type->module, s->self_type->name) : s->name);
    }
    for (int j = 0; j < d->union_specs.n; j++) {
        resolve_spec(m, d->union_specs.v[j]);
    }
    for (int j = 0; j < d->own_fields.n; j++) {
        Field *f = d->own_fields.v[j];
        resolve_spec(m, f->spec);
        // Recorded top-level calls keep their caller beyond this loop.
        Function *scope = mem(sizeof(*scope));
        *scope = (Function){.owner = d, .name = "<field-default>", .loc = f->loc};
        Function *outer = top_user;
        top_user = scope;
        resolve_expr(p, m, &names, f->init);
        top_user = outer;
    }
    for (int j = 0; j < d->own_methods.n; j++) {
        resolve_function(p, m, d->own_methods.v[j]);
    }
    for (int j = 0; j < d->constants.n; j++) {
        Constant *c = d->constants.v[j];
        for (int k = 0; k < j; k++) {
            if (!strcmp(c->name, ((Constant *)d->constants.v[k])->name)) {
                fail(c->binding->loc, "duplicate type constant '%s'", c->name);
            }
        }
        for (int k = 0; k < d->own_fields.n; k++) {
            if (!strcmp(c->name, ((Field *)d->own_fields.v[k])->name)) {
                fail(c->binding->loc, "field/type constant name collision '%s'", c->name);
            }
        }
        for (int k = 0; k < d->own_methods.n; k++) {
            if (!strcmp(c->name, ((Function *)d->own_methods.v[k])->name)) {
                fail(c->binding->loc, "method/type constant name collision '%s'", c->name);
            }
        }
        resolve_constant(p, c);
        Function *f = mem(sizeof(*f));
        f->name = fmt("<type-constant:%s.%s>", d->name, c->name);
        f->loc = c->binding->loc;
        f->module = m;
        f->owner = d;
        f->resolved = f->constant_value = true;
        Stmt *ret = mem(sizeof(*ret));
        *ret = (Stmt){.kind = S_RETURN, .loc = f->loc, .a = copy_expr(c->binding->a)};
        add(&f->body, ret);
        add(&p->functions, f);
        add(&m->functions, f);
    }
}

static bool collect_alias(Module *m, Stmt *s, bool public)
{
    if (s->kind != S_BIND || s->mut || s->spec) {
        return false;
    }
    Spec *spec = expression_type(m, s->a);
    if (!spec || dependent_spec(spec)) {
        return false;
    }
    TypeAlias *a = mem(sizeof(*a));
    *a = (TypeAlias){.name = s->name, .loc = s->loc, .spec = spec, .module = m, .public = public};
    add(&m->aliases, a);
    // Type bindings occupy no runtime storage and capture no activation.
    s->kind = S_BLOCK;
    s->a = NULL;
    s->public_binding = false;
    return true;
}

static void collect_aliases(Program *p)
{
    bool changed;
    do {
        changed = false;
        for (int j = 0; j < p->modules.n; j++) {
            Module *m = p->modules.v[j];
            for (int k = 0; k < m->constants.n;) {
                Constant *c = m->constants.v[k];
                if (collect_alias(m, c->binding, c->public)) {
                    memmove(m->constants.v + k, m->constants.v + k + 1,
                            (size_t)(m->constants.n - k - 1) * sizeof(void *));
                    m->constants.n--;
                    changed = true;
                }
                else {
                    k++;
                }
            }
            for (int k = 0; m->root && k < m->entry->body.n; k++) {
                Stmt *s = m->entry->body.v[k];
                changed |= collect_alias(m, s, s->public_binding);
            }
        }
    } while (changed);
}

void resolve_modules(Program *p)
{
    top_bindings = top_calls = top_references = local_types = (List){0};
    top_user = NULL;
    collect_aliases(p);
    // Names remain local until every module's references have been resolved.
    for (int j = 0; j < p->modules.n; j++) {
        Module *m = p->modules.v[j];
        List names = {0};
        check_names(p, m);
        top_bindings = (List){0};
        top_calls = (List){0};
        top_references = (List){0};
        for (int k = 0; m->root && k < m->entry->body.n; k++) {
            Stmt *s = m->entry->body.v[k];
            if (s->public_binding) {
                TOOL_CHECK(
                    p, fail(s->loc,
                            "public^ executable bindings are not supported in the entry file"););
            }
            if (s->kind == S_BIND) {
                add(&top_bindings, s);
            }
            for (int n = 0; s->kind == S_BIND_MANY && n < s->body.n; n++) {
                add(&top_bindings, s->body.v[n]);
            }
        }
        for (int k = 0; k < m->aliases.n; k++) {
            TypeAlias *a = m->aliases.v[k];
            TOOL_CHECK(p, resolve_alias(a););
        }
        for (int k = 0; k < m->constants.n; k++) {
            Constant *c = m->constants.v[k];
            if (!constant_expr(c->binding->a, false) && !c->binding->a->invalid) {
                TOOL_CHECK(p, fail(c->binding->loc,
                                   "module constant requires scalar literal operations"););
                c->binding->a->invalid = true;
            }
            resolve_spec(m, c->binding->spec);
            Function *f = mem(sizeof(*f));
            f->name = fmt("<constant:%s>", c->name);
            f->invalid = c->binding->a->invalid;
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
            TOOL_CHECK(p, resolve_definition(p, d););
        }
        for (int k = 0; k < m->functions.n; k++) {
            resolve_function(p, m, m->functions.v[k]);
        }
        resolve_body(p, m, &names, &m->entry->body);
        if (m->root) {
            pass_top_bindings(p, m);
        }
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
    // Templates have no storage layout or executable bodies of their own.
    int concrete = 0;
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        if (!d->type_params.n) {
            p->defs.v[concrete++] = d;
        }
    }
    p->defs.n = concrete;
}

void parse(Program *p, const char *path)
{
    Module *m = mem(sizeof(*m));
    m->root = !p->library_root;
    m->path = str(path);
    add(&p->modules, m);
    parse_file(p, path, m);
    p->entry->module = m;
    if (m->name && !strncmp(m->name, "std.", 4)) {
        char *relative = str(m->name);
        for (char *c = relative; *c; c++) {
            if (*c == '.') {
                *c = '/';
            }
        }
        char *expected = fmt("%s/%s.rh", p->stdlib_dir ? p->stdlib_dir : RH_STDLIB_PATH, relative);
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
            fail((Loc){path, 1, 1}, "std.* is reserved for the bundled standard library");
        }
    }
    load_dependencies(p, m);
    resolve_modules(p);
}
