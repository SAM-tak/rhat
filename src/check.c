#include "sema.h"
#include "tooling.h"

// Body requirements resolve signatures first; local call shapes fill gaps.
Param *inferred_parameter(Function *f)
{
    for (int j = 0; j < f->params.n; j++) {
        Param *param = f->params.v[j];
        if (!param->self && !param->spec && !param->inferred) {
            return param;
        }
    }
    return NULL;
}

void inferred_export(Function *f)
{
    Param *param = inferred_parameter(f);
    if (!f->templ && param) {
        fail(param->loc,
             "public function '%s' requires a type annotation for '%s' "
             "or explicit template^",
             f->name, param->name);
    }
}

void inferred_call(Function *caller, Function *callee, Loc loc)
{
    if (!callee->templ && inferred_parameter(callee) && caller->module != callee->module) {
        fail(loc,
             "unannotated parameters of '%s' require calls in its declaring "
             "module; use template^ for cross-module specialization",
             callee->name);
    }
}

static bool c_scalar(Type *t)
{
    return t->kind == T_ENUM || t->kind == T_BOOL || (t->kind >= T_U8 && t->kind <= T_U64) ||
           (t->kind >= T_I8 && t->kind <= T_I64) || t->kind == T_F32 || t->kind == T_F64;
}

// A value C can lay out the same way: a scalar, or a non-empty def^ or
// fixed array built only of such values (no references, strings or unions).
static bool c_flat(Type *t)
{
    if (c_scalar(t)) {
        return true;
    }
    if (t->kind == T_ARRAY) {
        return c_flat(t->elem);
    }
    if (t->kind != T_STRUCT || t->def->abstract || t->def->tuple || !t->def->fields.n) {
        return false;
    }
    for (int j = 0; j < t->def->fields.n; j++) {
        if (!c_flat(((Field *)t->def->fields.v[j])->type)) {
            return false;
        }
    }
    return true;
}

// The reference a C pointer stands for: ref^ to a flat or opaque def^ (or
// a scalar), a C function pointer, or either | nil^ for one that may be
// NULL.
static Type *c_pointer(Type *t)
{
    if (t->kind == T_UNION && t->def->alternatives.n == 2) {
        Type *a = t->def->alternatives.v[0], *b = t->def->alternatives.v[1];
        t = a->kind == T_NIL ? b : b->kind == T_NIL ? a : t;
    }
    if (t->kind == T_CFUNC) {
        return t;
    }
    if (t->kind != T_REF) {
        return NULL;
    }
    bool opaque = t->elem->kind == T_STRUCT && t->elem->def->opaque;
    return opaque || c_flat(t->elem) ? t : NULL;
}

// A header's prototype names its own struct types, so a def^ passed by
// value to a function from includeC^ headers must name its C type.
static void c_named(Function *f, Type *t, Loc loc)
{
    if (t->kind == T_STRUCT && f->module->c_headers.n && !t->def->c_name) {
        fail(loc, "'%s' crosses an includeC^ header by value; declare it with externC^ def^",
             t->def->name);
    }
}

static bool same_signature(Program *p, Function *a, Function *b)
{
    if (a->procedure != b->procedure || a->params.n != b->params.n ||
        (a->result ? type_spec(p, a->result) : NULL) !=
            (b->result ? type_spec(p, b->result) : NULL)) {
        return false;
    }
    for (int j = 0; j < a->params.n; j++) {
        if (parameter_type(p, a->params.v[j]) != parameter_type(p, b->params.v[j])) {
            return false;
        }
    }
    return true;
}

Expr *pack_arguments(Function *f, Expr *e, int offset)
{
    int fixed = f && f->variadic ? f->params.n - 1 - offset : -1;
    if (fixed < 0 || e->args.n < fixed) {
        return e;
    }
    Expr *call = mem(sizeof(*call)), *pack = mem(sizeof(*pack));
    *call = *e;
    call->args = (List){0};
    pack->kind = E_PACK;
    pack->loc = e->args.n > fixed ? ((Expr *)e->args.v[fixed])->loc : e->loc;
    for (int j = 0; j < e->args.n; j++) {
        add(j < fixed ? &call->args : &pack->args, e->args.v[j]);
    }
    for (int j = 0; j < pack->args.n; j++) {
        Expr *arg = pack->args.v[j];
        // A lone ... forwards the caller's collected arguments.
        if (arg->spread ||
            (pack->args.n == 1 && arg->kind == E_NAME && !strcmp(arg->text, "..."))) {
            if (pack->args.n != 1) {
                fail(arg->loc, "a forwarded sequence must be the only variadic argument");
            }
            pack->a = mem(sizeof(*pack->a));
            *pack->a = *arg;
            pack->a->spread = false;
        }
    }
    add(&call->args, pack);
    return call;
}

bool error_messages(Program *p, Expr *target, int arguments, Type *base)
{
    (void)p;
    if (base && base->kind == T_REF) {
        base = base->elem;
    }
    return target->kind == E_MEMBER && !strcmp(target->text, "message") && !arguments && base &&
           base->kind == T_UNION && only_errors(base);
}

Type *parse_type(Program *p, int kind, Loc loc)
{
    Def *invalid = lookup_def(p, "std.text.Error.Invalid");
    Def *range = lookup_def(p, "std.text.Error.OutOfRange");
    if (!invalid || !range) {
        fail(loc, "internal: parse needs std.text");
    }
    return type_union(p, type_union(p, type_builtin(p, kind), invalid->type, loc), range->type,
                      loc);
}

Type *text_cast_type(Program *p, Loc loc)
{
    Def *invalid = lookup_def(p, "std.text.Error.Invalid");
    if (!invalid) {
        fail(loc, "internal: string^(...) needs std.text");
    }
    return type_union(p, type_builtin(p, T_STRING), invalid->type, loc);
}

Type *text_method_type(Program *p, const char *name, Loc loc)
{
    Type *whole = type_builtin(p, T_I64), *text = type_builtin(p, T_STRING);
    if (!strcmp(name, "slice")) {
        return text;
    }
    if (!strcmp(name, "count_chars")) {
        return whole;
    }
    if (!strcmp(name, "find")) {
        return type_union(p, whole, type_builtin(p, T_NIL), loc);
    }
    if (!strcmp(name, "starts_with") || !strcmp(name, "ends_with")) {
        return type_builtin(p, T_BOOL);
    }
    if (!strcmp(name, "decode")) {
        List parts = {0};
        add(&parts, type_builtin(p, T_CHAR));
        add(&parts, whole);
        return type_tuple(p, &parts, loc);
    }
    if (!strcmp(name, "chars")) {
        return type_wrap(p, T_GROW, type_builtin(p, T_CHAR), 0);
    }
    return NULL;
}

bool textual(Type *t)
{
    return t && (kind_integer(t->kind) || kind_float(t->kind) || t->kind == T_BOOL ||
                 t->kind == T_CHAR || t->kind == T_STRING);
}

// Reject reading a string^ as bytes without .bytes.
static void text_bytes_only(Type *t, const char *what, Loc loc)
{
    if (t && t->kind == T_STRING) {
        fail(
            loc,
            "string^ has no %s; use s.bytes for bytes, or s.decode(at), s.slice(r) and s.count_chars()",
            what);
    }
}

Type *char_cast_type(Program *p, Loc loc)
{
    Def *invalid = lookup_def(p, "std.text.Error.Invalid");
    if (!invalid) {
        fail(loc, "internal: char^(...) needs std.text");
    }
    return type_union(p, type_builtin(p, T_CHAR), invalid->type, loc);
}

Type *enum_cast_type(Program *p, Type *t, Loc loc)
{
    Def *failure = lookup_def(p, "std.sys.Error.CastFail");
    if (!failure) {
        fail(loc, "internal: enum^ conversion needs std.sys");
    }
    return type_union(p, t, failure->type, loc);
}

Type *error_view(Type *t, const char *name)
{
    bool shared = !strcmp(name, "source") || !strcmp(name, "line") || !strcmp(name, "column");
    return t && t->kind == T_UNION && only_errors(t) && shared ? t->def->alternatives.v[0] : t;
}

int error_site(Type *t, const char *name)
{
    if (!t || t->kind != T_STRUCT || !t->def->group) {
        return -1;
    }
    return !strcmp(name, "source")     ? 0
           : !strcmp(name, "line")     ? 1
           : !strcmp(name, "column")   ? 2
           : !strcmp(name, "\001text") ? 3
                                       : -1;
}

void results_check(Type *t, int names, Loc loc)
{
    if (t->kind != T_STRUCT || !t->def->tuple) {
        fail(loc, "'%s' is not multiple results%s", type_name(t),
             t->kind == T_UNION ? "; take them out with try^, catch^, ?? or fits^" : "");
    }
    if (t->def->fields.n != names) {
        fail(loc, "%d names for the %d results of '%s'", names, t->def->fields.n, type_name(t));
    }
}

Def *lhat_data_def(Type *t)
{
    if (t->kind == T_UNION && t->def->alternatives.n == 2) {
        Type *a = t->def->alternatives.v[0], *b = t->def->alternatives.v[1];
        t = a->kind == T_NIL ? b : b->kind == T_NIL ? a : t;
    }
    return t->kind == T_REF && t->elem->kind == T_STRUCT && t->elem->def->lhat_data ? t->elem->def
                                                                                    : NULL;
}

Def *error_group(Type *t)
{
    if (t->kind == T_STRUCT) {
        return t->def->group;
    }
    Type *first = t->kind == T_UNION ? t->def->alternatives.v[0] : NULL;
    return first && first->kind == T_STRUCT && first->def->group == t->def ? t->def : NULL;
}

Type *lhat_success(Type *t)
{
    Type *success = NULL;
    int errors = 0, others = 0;
    for (int j = 0; t->kind == T_UNION && j < t->def->alternatives.n; j++) {
        Type *arm = t->def->alternatives.v[j];
        bool error = error_group(arm), other = !error && arm->kind != T_NIL;
        errors += error;
        others += other;
        success = other ? arm : success;
    }
    return !errors ? t : others == 1 ? success : NULL;
}

// A value L^ hands a host function: number^ (integers to 64 bits and
// floats), bool^, string^, an exportL^ def^ host value or a reference to
// an exportL^ ref^ def^ host data.
static bool l_value(Type *t)
{
    return t->kind == T_STRING || (c_scalar(t) && t->kind != T_ENUM) ||
           (t->kind == T_STRUCT && t->def->lhat_value) || lhat_data_def(t);
}

// An ASCII C identifier other than main, which the entry file defines.
static bool c_identifier(const char *s)
{
    bool identifier = *s && !(*s >= '0' && *s <= '9');
    for (const char *c = s; *c; c++) {
        identifier &= *c == '_' || (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') ||
                      (*c >= 'A' && *c <= 'Z');
    }
    return identifier && strcmp(s, "main");
}

void export_validate(Program *p, Function *f)
{
    const char *s = strrchr(f->name, '.') ? strrchr(f->name, '.') + 1 : f->name;
    const char *kind = f->exported ? "exportC^" : "exportL^";
    if (!f->owner && (!c_identifier(s) || !strncmp(s, "rh_", 3))) {
        fail(f->declaration, "%s name '%s' is not a usable C identifier", kind, s);
    }
    if (f->templ || f->variadic || (f->owner && !f->owner->lhat_value && !f->owner->lhat_data)) {
        fail(f->loc, "%s function cannot be template^, variadic or a method", kind);
    }
    if (f->mutable_self && f->owner->lhat_value) {
        fail(f->loc, "exportL^ def^ method '%s' cannot change self^; L^ hands host values by copy",
             s);
    }
    if (!f->result && !f->procedure) {
        fail(f->loc, "%s f^ needs a result type annotation", kind);
    }
    for (int j = 0; f->exported && j < p->functions.n; j++) {
        Function *other = p->functions.v[j];
        const char *name = strrchr(other->name, '.') ? strrchr(other->name, '.') + 1 : other->name;
        if (other != f && (other->exported || other->external) && !strcmp(name, s)) {
            fail(f->declaration, "exportC^ symbol '%s' is already declared at %s:%d", s,
                 other->declaration.file, other->declaration.line);
        }
    }
    bool header = false;
    for (int j = 0; j < p->modules.n; j++) {
        header |= ((Module *)p->modules.v[j])->c_headers.n > 0;
    }
    if (f->lhat_export && !header) {
        fail(f->declaration, "exportL^ needs a module with includeC^ \"lhat.h\"");
    }
    // C passes no lengths, so slices and strings cannot come in from C. L^
    // passes numbers, booleans and strings.
    for (int j = 0; j < f->params.n; j++) {
        Param *param = f->params.v[j];
        Type *t = param->spec ? type_spec(p, param->spec) : NULL;
        if (param->self) {
            continue;
        }
        if (!t || (f->exported && !((c_flat(t) && t->kind != T_ARRAY) || c_pointer(t))) ||
            (f->lhat_export && !l_value(t))) {
            fail(param->loc, "%s parameter '%s' needs a %s type annotation, not '%s'", kind,
                 param->name, f->exported ? "C" : "number, bool^, string^ or exportL^ def^",
                 t ? type_name(t) : "?");
        }
    }
    Type *result = f->result ? type_spec(p, f->result) : NULL;
    if (result && f->exported &&
        !((c_flat(result) && result->kind != T_ARRAY) || c_pointer(result))) {
        fail(f->result->loc, "exportC^ result must be a C scalar, a flat def^ or a C pointer");
    }
    // A result failing with exportL^ errors answers the L^ error instead;
    // multiple results answer each part.
    for (int j = 0;
         result && f->lhat_export && result->kind == T_UNION && j < result->def->alternatives.n;
         j++) {
        Def *group = error_group(result->def->alternatives.v[j]);
        if (group && !group->lhat_error) {
            fail(f->result->loc, "errordef^ '%s' needs exportL^ to fail an exportL^ result",
                 group->name);
        }
    }
    // Of what may fail, only a ref^ to host data may also be nil^.
    Type *success = result && f->lhat_export ? lhat_success(result) : NULL;
    for (int j = 0; success && success != result && j < result->def->alternatives.n; j++) {
        Type *arm = result->def->alternatives.v[j];
        success = arm->kind == T_NIL && !lhat_data_def(success) ? NULL : success;
    }
    bool tuple = success && success->kind == T_STRUCT && success->def->tuple;
    for (int j = 0; result && f->lhat_export && j < (tuple ? success->def->fields.n : 1); j++) {
        Type *part = !success ? NULL : tuple ? ((Field *)success->def->fields.v[j])->type : success;
        if (!part || !l_value(part)) {
            fail(f->result->loc, "exportL^ result must be a number, bool^, string^, exportL^ def^ "
                                 "or ref^ to an exportL^ ref^ def^, with | errors if it fails");
        }
    }
}

void lhat_def_validate(Program *p, Def *d)
{
    if (d->lhat_value && (d->opaque || d->abstract || !c_flat(d->type))) {
        fail(d->loc,
             "exportL^ def^ '%s' needs a flat def^ of scalars and flat def^ values; "
             "exportL^ ref^ def^ hands L^ a reference instead",
             d->name);
    }
    if (d->lhat_value && d->type->size > 248) {
        fail(d->loc, "exportL^ def^ '%s' is %zu bytes; an L^ host value holds at most 248", d->name,
             d->type->size);
    }
    for (int j = 0; j < d->own_methods.n; j++) {
        Function *f = d->own_methods.v[j];
        if (!f->priv) {
            f->lhat_export = true;
            export_validate(p, f);
        }
    }
}

void external_validate(Program *p, Function *f)
{
    const char *s = f->external;
    bool standard = f->module && f->module->name && !strncmp(f->module->name, "std.", 4);
    if (f->intrinsic) {
        if (!standard || strncmp(s, "llvm.", 5)) {
            fail(f->declaration,
                 "externLLVM^ binds llvm.* intrinsics in the standard library only");
        }
    }
    else {
        if (!c_identifier(s)) {
            fail(f->declaration, "externC^ symbol '%s' is not a usable C identifier", s);
        }
        if (!strncmp(s, "rh_", 3) && !standard && strcmp(s, "rh_thread_join") &&
            strcmp(s, "rh_thread_spawn")) {
            fail(f->declaration, "externC^ symbols starting with rh_ are reserved for the runtime");
        }
    }
    if (f->templ || f->variadic) {
        fail(f->loc, "externC^ declaration cannot be template^ or variadic");
    }
    // Scalars and flat def^ values cross as their C types, the latter through
    // a generated C shim; ref^, slices and strings as a data pointer.
    for (int j = 0; j < f->params.n; j++) {
        Param *param = f->params.v[j];
        Type *t = parameter_type(p, param);
        if (!c_pointer(t)) {
            layout(t, param->loc);
        }
        if (!(c_flat(t) && t->kind != T_ARRAY) && t->kind != T_STRING && !c_pointer(t) &&
            !(t->kind == T_SLICE && c_flat(t->elem))) {
            fail(param->loc, "externC^ parameter '%s' has no C shape: '%s'", param->name,
                 type_name(t));
        }
        c_named(f, t, param->loc);
    }
    Type *result = f->result ? type_spec(p, f->result) : NULL;
    if (result && !c_pointer(result)) {
        layout(result, f->result->loc);
    }
    if (result && (!c_flat(result) || result->kind == T_ARRAY) && !c_pointer(result)) {
        fail(f->result->loc,
             "externC^ result must be a C scalar, a flat def^, a C pointer, or omitted");
    }
    if (result) {
        c_named(f, result, f->result->loc);
    }
    for (int j = 0; j < p->functions.n; j++) {
        Function *other = p->functions.v[j];
        if (other != f && other->external && !strcmp(other->external, s) &&
            !same_signature(p, f, other)) {
            fail(f->declaration, "externC^ symbol '%s' is declared with another signature at %s:%d",
                 s, other->declaration.file, other->declaration.line);
        }
    }
}

void inferred_unused(Program *p, Function *f)
{
    if (f->captures.n && !f->environment) {
        return;
    }
    Param *param = inferred_parameter(f);
    if (f->invalid || f->templ || !param) {
        return;
    }
    // Captures precede written parameters, but are not annotations the user omitted.
    for (int j = 0; j < f->params.n; j++) {
        Param *candidate = f->params.v[j];
        if (!candidate->capture && !candidate->self && !candidate->spec && !candidate->inferred) {
            param = candidate;
            break;
        }
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->fn == f && in->state == 2) {
            return;
        }
    }
    if (param->capture) {
        // Any failed analysis, at the top level or inside a function body, can
        // stop before the only calls that would give the capture its shape.
        for (int j = 0; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            if (in->invalid) {
                return;
            }
        }
        fail(f->declaration,
             "cannot infer captured binding '%s' of '%s' without a call shape; "
             "add a type annotation to the binding or call the function",
             param->capture->name, f->name);
    }
    fail(param->loc,
         "cannot infer parameter '%s' of '%s' without a call shape; "
         "add a type annotation or template^",
         param->name, f->name);
}

// Return inference is a fixed point over concrete call instances. Unknown
// results stay unknown across back edges until a return or operation seeds
// them.
typedef struct {
    Program *p;
    Instance *in;
    List vars;
    List handlers, loops, initializers, inline_flows;
    Type *propagated;
    Type *returned;
    Expr *return_expr;
    Type *yielded; // What the body's yield^ sites agree on, for a yielding instance.
    Expr *yield_expr;
    Type *received; // What the yield^ sites that bind a value agree to receive.
    bool value_return;
    int lexical;
    NilFlow flow;
    bool raw_name;
    Expr *raw_place;
    bool terminated;
    int inline_steps;
} Infer;

// Jumps can make an expanded iteration reachable even when its body terminates.
typedef struct {
    int level;
    bool next, stop;
} InlineFlow;

static Infer infer_context(Context *c)
{
    Infer out = {.p = c->p, .in = c->in, .lexical = c->lexical};
    for (int j = 0; j < c->loops.n; j++) {
        add(&out.loops, (void *)(intptr_t)(((Loop *)c->loops.v[j])->outside + 1));
    }
    Var *vars = mem((size_t)c->vars.n * sizeof(*vars));
    for (int j = 0; j < c->vars.n; j++) {
        Var *v = &vars[j];
        *v = *(Var *)c->vars.v[j];
        v->alias = NULL;
        v->source = NULL;
        add(&out.vars, v);
    }
    for (int j = 0; j < c->vars.n; j++) {
        Var *old = c->vars.v[j];
        if (!old->alias && !old->source) {
            continue;
        }
        for (int k = 0; k < c->vars.n; k++) {
            if (old->alias && old->alias == c->vars.v[k]) {
                ((Var *)out.vars.v[j])->alias = out.vars.v[k];
            }
            if (old->source && old->source == c->vars.v[k]) {
                ((Var *)out.vars.v[j])->source = out.vars.v[k];
            }
        }
    }
    for (int j = 0; j < c->flow.guards.n; j++) {
        NilGuard *old = c->flow.guards.v[j], *g = mem(sizeof(*g));
        *g = *old;
        g->var = NULL;
        for (int k = 0; k < c->vars.n; k++) {
            if (old->var == c->vars.v[k]) {
                g->var = out.vars.v[k];
            }
        }
        add(&out.flow.guards, g);
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
    Context scope = {.p = c->p, .vars = c->vars, .lexical = c->lexical};
    remember_guard(&c->flow, &scope, test, name, positive);
}

static Var *infer_alias(Infer *c, Expr *e)
{
    if (e->kind == E_UNARY && !strcmp(e->text, "ref^") && e->a->kind == E_NAME) {
        return infer_variable(c, e->a);
    }
    Var *v = infer_variable(c, e);
    return v ? v->alias : NULL;
}

static void infer_mutation(Infer *c, Expr *call, List *types, int offset)
{
    if (offset && types->n) {
        forget_paths(&c->vars);
        Type *receiver = types->v[0];
        if (receiver && receiver->kind == T_REF) {
            layout(receiver->elem, call->loc);
            if (receiver->elem->refs) {
                forget_roots(&c->vars, (Roots){0}, true);
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
        if (target && target->type) {
            layout(target->type, arg->loc);
            if (!target->type->refs) {
                target->version++;
                target->path_version++;
                target->narrowed = NULL;
                target->paths = (List){0};
                forget_paths(&c->vars);
                continue;
            }
        }
        // Unknown references may alias a local whose address was passed earlier.
        Var *v = infer_variable(c, arg);
        if (target || !v || !v->type || v->type->refs) {
            forget_roots(&c->vars, (Roots){0}, true);
            return;
        }
    }
}

static Type *infer_expr(Infer *c, Expr *e, Type *expected);
static Expr *part_node(int kind, Loc loc, char *text, Expr *a, Expr *b);
static Type *infer_type(Infer *c, Spec *s);

// Preserve read capabilities when taking a reference to a projected place.
static bool infer_read_place(Infer *c, Expr *e)
{
    if (e->kind == E_MEMBER || e->kind == E_INDEX ||
        (e->kind == E_UNARY && !strcmp(e->text, "*"))) {
        Type *base = infer_expr(c, e->a, NULL);
        return (base && read_reference(base)) || infer_read_place(c, e->a);
    }
    return false;
}

static Var *infer_variable(Infer *c, Expr *e);

// Whether e is a place in a let^ binding's own storage, not behind a reference or slice.
// A function reaches a top-level binding through a hidden reference, written *name.
static bool infer_let_place(Infer *c, Expr *e)
{
    if (e->kind == E_CAPTURE) {
        Param *capture = c->in->fn->captures.v[e->capture_index];
        return capture->capture && !capture->capture->mut;
    }
    if (e->kind == E_STATE) {
        return e->state_field->binding && !e->state_field->binding->mut;
    }
    if (e->kind == E_UNARY && !strcmp(e->text, "*")) {
        Var *hidden = e->a->kind == E_NAME ? infer_variable(c, e->a) : NULL;
        return hidden && hidden->captured_let;
    }
    if (e->kind == E_MEMBER || e->kind == E_INDEX) {
        Type *base = infer_expr(c, e->a, NULL);
        return base && base->kind != T_REF && base->kind != T_SLICE && infer_let_place(c, e->a);
    }
    Var *v = e->kind == E_NAME ? infer_variable(c, e) : NULL;
    return v && v->let_bound;
}

// The slice of source, whose type without its reference is base; slice_view decides the same.
static Type *pure_argument_type(Infer *c, Function *f, Expr *arg, Type *type)
{
    if (type && type->kind == T_REF && !f->procedure && arg && arg->kind == E_UNARY &&
        !strcmp(arg->text, "ref^") && infer_let_place(c, arg->a)) {
        return type_wrap(c->p, T_REF, type->elem, 1);
    }
    return type;
}

static Type *infer_view(Infer *c, Expr *source, Type *base)
{
    if (base && base->kind == T_STRING) {
        return type_wrap(c->p, T_SLICE, type_builtin(c->p, T_U8), 1);
    }
    if (!base || (base->kind != T_ARRAY && base->kind != T_GROW && base->kind != T_BOUNDED &&
                  base->kind != T_SLICE)) {
        return NULL;
    }
    Type *raw = infer_expr(c, source, NULL);
    bool read = read_reference(base) || (raw && read_reference(raw)) || infer_read_place(c, source);
    if (base->kind != T_SLICE) {
        read |= infer_let_place(c, source);
    }
    return type_wrap(c->p, T_SLICE, base->elem, read);
}

static Type *infer_lookup(Infer *c, const char *name)
{
    for (int j = c->vars.n - 1; j >= 0; j--) {
        Var *v = c->vars.v[j];
        if (v->hidden) {
            continue;
        }
        if (!strcmp(v->name, name)) {
            if (v->invalid) {
                tool_abort();
            }
            return !c->raw_name && v->narrowed ? v->narrowed : v->type;
        }
    }
    return NULL;
}

static bool integer_literal(Expr *e)
{
    return e && !e->constant_spec &&
           (e->kind == E_INT || (e->kind == E_UNARY && !strcmp(e->text, "-") &&
                                 e->a->kind == E_INT && !e->a->constant_spec));
}

static bool numeric(Type *t)
{
    return t && (kind_integer(t->kind) || kind_float(t->kind));
}

static Type *join_types(Program *p, Type *a, Expr *ae, Type *b, Expr *be, Loc loc)
{
    if (a && a->kind == T_NEVER) {
        return b;
    }
    if (b && b->kind == T_NEVER) {
        return a;
    }
    if (!a) {
        return b;
    }
    if (!b || a == b) {
        return a;
    }
    if (has_error(a) || has_error(b)) {
        if (a->kind == T_VOID) {
            a = type_builtin(p, T_NIL);
        }
        if (b->kind == T_VOID) {
            b = type_builtin(p, T_NIL);
        }
        return type_union(p, a, b, loc);
    }
    if (enum_backs(b, a)) {
        return b;
    }
    if (enum_backs(a, b)) {
        return a;
    }
    if (integer_literal(ae) && numeric(b)) {
        return b;
    }
    if (integer_literal(be) && numeric(a)) {
        return a;
    }
    if (ae && !ae->constant_spec && ae->kind == E_FLOAT && b && kind_float(b->kind)) {
        return b;
    }
    if (be && !be->constant_spec && be->kind == E_FLOAT && a && kind_float(a->kind)) {
        return a;
    }
    if (sum_accepts(a, b)) {
        return a;
    }
    if (sum_accepts(b, a)) {
        return b;
    }
    if (a->kind == T_NIL || b->kind == T_NIL || a->structural || b->structural) {
        return type_union(p, a, b, loc);
    }
    fail(loc, "inferred types disagree: '%s' and '%s'", type_name(a), type_name(b));
}

static Type *infer_call(Infer *c, Expr *e, Type *expected)
{
    Expr *target = e->a;
    int thread = thread_operation(target);
    if (thread) {
        Type *element = NULL;
        if (thread == THREAD_NEW) {
            element = channel_new_element(c->p, e);
        }
        else if (thread >= THREAD_SEND && thread <= THREAD_VALID) {
            if (!e->args.n) {
                fail(e->loc, "invalid thread operation arguments");
            }
            Type *handle = infer_expr(c, e->args.v[0], NULL);
            if (!handle) {
                return NULL;
            }
            element = channel_element(handle, e->loc);
        }
        return thread_result(c->p, c->in->fn, thread, element);
    }
    if (target->kind == E_MEMBER && !strcmp(target->text, "message") && !e->args.n &&
        error_messages(c->p, target, 0, infer_expr(c, target->a, NULL))) {
        return type_builtin(c->p, T_STRING);
    }
    if (target->kind == E_MEMBER &&
        (!strcmp(target->text, "tostring") || !strcmp(target->text, "write_text"))) {
        Type *base = infer_expr(c, target->a, NULL);
        Def *builder = lookup_def(c->p, "std.text.Builder");
        if (textual(base) && builder) {
            for (int j = 0; j < e->args.n; j++) {
                infer_expr(c, e->args.v[j], NULL);
            }
            return target->text[0] == 't' ? builder->type : type_builtin(c->p, T_VOID);
        }
    }
    if (target->kind == E_MEMBER &&
        (!strcmp(target->text, "trim") || !strcmp(target->text, "split"))) {
        Expr *library = text_library_call(c->p, e, infer_expr(c, target->a, NULL));
        if (library) {
            return infer_call(c, library, expected);
        }
    }
    if (target->kind == E_MEMBER && text_method_type(c->p, target->text, e->loc)) {
        Type *base = infer_expr(c, target->a, NULL);
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        if (base && base->kind == T_STRING) {
            for (int j = 0; j < e->args.n; j++) {
                infer_expr(c, e->args.v[j], NULL);
            }
            return c->terminated ? type_builtin(c->p, T_NEVER)
                                 : text_method_type(c->p, target->text, e->loc);
        }
    }
    if (target->kind == E_MEMBER && !strcmp(target->text, "contains") && e->args.n == 1) {
        Type *range = infer_expr(c, target->a, NULL);
        if (range_type(range)) {
            infer_expr(c, e->args.v[0], range_bound(c->p, range));
            return type_builtin(c->p, T_BOOL);
        }
    }
    Function *f = NULL;
    Def *self = NULL;
    List actual = {0}, candidates = {0};
    if (target->kind == E_FUNCTION) {
        Context scope = {.p = c->p, .in = c->in, .vars = c->vars, .lexical = c->lexical};
        Type *type = closure_type(&scope, target->function);
        f = type ? type->function : NULL;
    }
    else if (target->kind == E_MEMBER && !strcmp(target->text, "from_bits") &&
             target->a->kind == E_NAME && kind_float(numeric_kind(target->a->text))) {
        int k = numeric_kind(target->a->text);
        for (int j = 0; j < e->args.n; j++) {
            infer_expr(c, e->args.v[j], type_builtin(c->p, bits_kind(k)));
        }
        return c->terminated ? type_builtin(c->p, T_NEVER) : type_builtin(c->p, k);
    }
    else if (target->kind == E_MEMBER && !strcmp(target->text, "parse") &&
             target->a->kind == E_NAME && numeric_kind(target->a->text) >= 0) {
        for (int j = 0; j < e->args.n; j++) {
            infer_expr(c, e->args.v[j], NULL);
        }
        return c->terminated ? type_builtin(c->p, T_NEVER)
                             : parse_type(c->p, numeric_kind(target->a->text), e->loc);
    }
    else if (target->kind == E_TYPE) {
        for (int j = 0; j < e->args.n; j++) {
            infer_expr(c, e->args.v[j], NULL);
        }
        return c->terminated ? type_builtin(c->p, T_NEVER)
                             : enum_cast_type(c->p, type_spec(c->p, target->type_expr), e->loc);
    }
    else if (target->kind == E_NAME &&
             (!strcmp(target->text, "char^") || !strcmp(target->text, "string^"))) {
        for (int j = 0; j < e->args.n; j++) {
            infer_expr(c, e->args.v[j], NULL);
        }
        return c->terminated            ? type_builtin(c->p, T_NEVER)
               : target->text[0] == 'c' ? char_cast_type(c->p, e->loc)
                                        : text_cast_type(c->p, e->loc);
    }
    else if (target->kind == E_NAME) {
        for (int k = T_U8; k <= T_F64; k++) {
            if (!strcmp(target->text, builtin_name(k))) {
                for (int j = 0; j < e->args.n; j++) {
                    infer_expr(c, e->args.v[j], NULL);
                }
                if (c->terminated) {
                    return type_builtin(c->p, T_NEVER);
                }
                return type_builtin(c->p, k);
            }
        }
        if (!strcmp(target->text, "print^") || !strcmp(target->text, "assert^")) {
            for (int j = 0; j < e->args.n; j++) {
                infer_expr(c, e->args.v[j], NULL);
            }
            if (c->terminated) {
                return type_builtin(c->p, T_NEVER);
            }
            if (!strcmp(target->text, "assert^") && e->args.n == 1) {
                refine_condition(c->p, &c->vars, &c->flow, e->args.v[0], true);
            }
            return type_builtin(c->p, T_VOID);
        }
        if ((f = this_function(c->p, c->in, target))) {
            self = f == c->in->fn ? c->in->self : NULL;
            if (self && !f->static_member) {
                add(&actual, infer_lookup(c, "self^"));
            }
        }
        else {
            Type *binding = infer_lookup(c, target->text);
            candidates = binding ? callable_candidates(binding, e->args.n)
                                 : overload_candidates(c->p, NULL, target->text, e->args.n);
            f = candidates.n ? candidates.v[0] : NULL;
        }
    }
    else if ((self = static_owner(c->p, target))) {
        candidates = overload_candidates(c->p, self, target->text, e->args.n);
        f = candidates.n ? candidates.v[0] : method(self, target->text);
        if (!f || !f->static_member) {
            fail(target->name_loc.file ? target->name_loc : e->loc,
                 "'%s' has no static member '%s'", self->name, target->text);
        }
    }
    else if (target->kind == E_MEMBER) {
        if (target->a->kind == E_NAME && !strcmp(target->text, "new")) {
            Def *d = lookup_def(c->p, target->a->text);
            if (d) {
                Expr record = {.kind = E_RECORD, .loc = e->loc, .text = d->name};
                return infer_expr(c, &record, NULL);
            }
        }
        Type *base = infer_expr(c, target->a, NULL);
        if (base && base->kind == T_NEVER) {
            return base;
        }
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        if (base && base->kind == T_STRUCT) {
            compose(c->p, base->def);
        }
        if (base && base->kind == T_FRAME) {
            Instance *co = base->coroutine;
            if (!strcmp(target->text, "start") || !strcmp(target->text, "resume")) {
                for (int j = 0; j < e->args.n; j++) {
                    infer_expr(c, e->args.v[j], co->received);
                }
                forget_paths(&c->vars);
                return co->turn;
            }
            if (!strcmp(target->text, "done")) {
                return type_builtin(c->p, T_BOOL);
            }
            if (!strcmp(target->text, "result")) {
                return co->final;
            }
            fail(target->loc, "a coroutine frame has start, resume, done and result, not '%s'",
                 target->text);
        }
        if (base && (base->kind == T_GROW || base->kind == T_BOUNDED) &&
            !strcmp(target->text, "pop")) {
            layout(base, e->loc);
            return base->elem->owned ? type_builtin(c->p, T_VOID) : base->elem;
        }
        if (base && (base->kind == T_GROW || base->kind == T_BOUNDED) &&
            !strcmp(target->text, "truncate")) {
            for (int j = 0; j < e->args.n; j++) {
                infer_expr(c, e->args.v[j], type_builtin(c->p, T_I64));
            }
            return type_builtin(c->p, T_VOID);
        }
        if (base && (base->kind == T_GROW || base->kind == T_BOUNDED) &&
            !strcmp(target->text, "append")) {
            for (int j = 0; j < e->args.n; j++) {
                infer_expr(c, e->args.v[j], NULL);
            }
            forget_paths(&c->vars);
            return type_builtin(c->p, T_VOID);
        }
        if (base && (base->kind == T_GROW || base->kind == T_BOUNDED) &&
            !strcmp(target->text, "clear")) {
            return type_builtin(c->p, T_VOID);
        }
        if (base && (base->kind == T_GROW || base->kind == T_BOUNDED) &&
            !strcmp(target->text, "push")) {
            for (int j = 0; j < e->args.n; j++) {
                infer_expr(c, e->args.v[j], base->elem);
            }
            forget_paths(&c->vars);
            return type_wrap(c->p, T_REF, base->elem, 0);
        }
        if (base && base->kind == T_DYN) {
            // Lowering checks the call; inference needs only the declared result.
            Function *m = method(base->elem->def, target->text);
            Instance call = {.fn = m};
            add(&call.params, base);
            for (int j = 0; j < e->args.n; j++) {
                Param *param = m && j + 1 < m->params.n ? m->params.v[j + 1] : NULL;
                add(&call.params,
                    infer_expr(c, e->args.v[j], param ? parameter_type(c->p, param) : NULL));
            }
            if (c->terminated) {
                return type_builtin(c->p, T_NEVER);
            }
            if (m && (m->procedure || m->mutable_self)) {
                forget_paths(&c->vars);
            }
            return !m          ? NULL
                   : m->result ? function_result(c->p, &call, m->result)
                               : type_builtin(c->p, T_VOID);
        }
        if (base && base->kind == T_STRUCT) {
            self = base->def;
            candidates = overload_candidates(c->p, self, target->text, e->args.n + 1);
            f = candidates.n ? candidates.v[0] : method(self, target->text);
            if (!f) {
                fail(target->name_loc.file ? target->name_loc : e->loc, "'%s' has no method '%s'",
                     self->name, target->text);
            }
            Type *receiver = infer_expr(c, target->a, NULL);
            bool read = (receiver && read_reference(receiver)) || infer_read_place(c, target->a);
            add(&actual, type_wrap(c->p, T_REF, base, read));
        }
    }
    if (!f && target->kind != E_NAME && target->kind != E_MEMBER) {
        Type *type = infer_expr(c, target, NULL);
        candidates = callable_candidates(type, e->args.n);
        f = candidates.n ? candidates.v[0] : NULL;
    }
    if (!f) {
        return NULL;
    }
    int offset = actual.n;
    Type *right_self = offset && f->right_receiver ? actual.v[0] : NULL;
    if (right_self) {
        actual.n = 0;
    }
    e = pack_arguments(f, e, offset);
    bool known = true;
    for (int j = 0; j < e->args.n; j++) {
        int index = right_self ? j : j + offset;
        Param *param = index < f->params.n ? f->params.v[index] : NULL;
        Type *t =
            infer_expr(c, e->args.v[j],
                       candidates.n > 1 ? overload_hint(c->p, &candidates, j + offset, e->args.v[j])
                                        : parameter_hint(c->p, param, e->args.v[j]));
        if (c->terminated || (t && t->kind == T_NEVER)) {
            return type_builtin(c->p, T_NEVER);
        }
        if (!t) {
            known = false;
        }
        add(&actual, t);
    }
    if (right_self) {
        add(&actual, right_self);
    }
    if (candidates.n > 1) {
        f = overload_select(c->p, &candidates, &actual, e, offset, expected, e->loc, false);
        if (!f) {
            if (known) {
                // The arms' results may break a tie between literal fits; infer
                // them, and decide on a later pass.
                for (int j = 0; expected && j < candidates.n; j++) {
                    callable_declaration(c->p, candidates.v[j]);
                }
                overload_select(c->p, &candidates, &actual, e, offset, expected, e->loc, true);
            }
            return NULL;
        }
    }
    inferred_call(c->in->fn, f, e->loc);
    for (int j = offset; j < actual.n && j < f->params.n; j++) {
        Param *param = f->params.v[j];
        int index = j - (offset && !right_self ? offset : 0);
        Expr *arg = index >= 0 && index < e->args.n ? e->args.v[index] : NULL;
        Type *t = pure_argument_type(c, f, arg, actual.v[j]);
        actual.v[j] = t;
        Type *want = function_parameter(c->p, f, param, t);
        if (param->spec && t && want && !parameter_accepts(want, t)) {
            // Wait for provisional argument types instead of specializing a
            // typed callee with a numeric guess for a pending operator result.
            return f->result ? type_spec(c->p, f->result) : f->required_result;
        }
        if (param->spec && t && want &&
            (sum_accepts(want, t) || c_function_arm(want, t) ||
             (want->kind == T_DYN && parameter_accepts(want, t)))) {
            actual.v[j] = want;
        }
        else if (param->spec && t && want && read_reference(want) && t->kind == T_REF &&
                 parameter_accepts(want, t)) {
            actual.v[j] = type_wrap(c->p, T_REF, t->elem, 1);
        }
    }
    if (f->procedure || f->mutable_self) {
        infer_mutation(c, e, &actual, offset);
    }
    if (!known || actual.n != f->params.n) {
        return f->result ? type_spec(c->p, f->result) : f->required_result;
    }
    for (int j = 0; !self && f->owner && j < f->params.n; j++) {
        if (((Param *)f->params.v[j])->self) {
            self = outer_receiver(f, actual.v[j], e->loc);
        }
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
    if (base->kind == T_NEVER) {
        return base;
    }
    bool can_skip = false;
    for (int j = 0; j < steps.n; j++) {
        Expr *step = steps.v[j];
        if (step_optional(step)) {
            Type *subject = nil_subject(base);
            can_skip |= subject->kind == T_NIL || nullable(subject);
            base = without_nil(c->p, subject, step->loc);
            if (!base) {
                return type_builtin(c->p, T_NIL);
            }
        }
        Value value = {.id = -1, .type = base};
        Expr receiver = {.kind = E_SAVED, .loc = step->loc, .compiler_value = &value}, member = {0};
        Expr copy = step_expression(step, &receiver, &member);
        base = infer_expr(c, &copy, NULL);
        if (!base) {
            return NULL;
        }
        if (c->terminated) {
            if (lift && can_skip) {
                c->terminated = false;
                return type_builtin(c->p, T_NIL);
            }
            return type_builtin(c->p, T_NEVER);
        }
    }
    return !lift || base->kind == T_VOID
               ? base
               : type_union(c->p, base, type_builtin(c->p, T_NIL), e->loc);
}

static void infer_statements(Infer *c, List *body, bool scoped);

static void infer_route(Infer *c, Type *errors, Loc loc)
{
    if (!errors) {
        return;
    }
    if (c->handlers.n) {
        ErrorTarget *target = c->handlers.v[c->handlers.n - 1];
        target->errors = error_join(c->p, target->errors, errors, loc);
    }
    else {
        c->propagated = error_join(c->p, c->propagated, errors, loc);
    }
}

static void infer_focus(Infer *c, Type *type)
{
    Var *v = mem(sizeof(*v));
    v->name = "it^";
    v->type = type;
    v->lexical = c->lexical;
    add(&c->vars, v);
}

static void infer_handle(Infer *c, Stmt *s)
{
    Stmt local = *s, branch = {0};
    List body = {0};
    Value head = {0};
    Expr saved = {0};
    if (s->a) {
        head.type = infer_expr(c, s->a, type_builtin(c->p, T_BOOL));
        if (c->terminated) {
            return;
        }
        saved = (Expr){.kind = E_SAVED, .loc = s->a->loc, .compiler_value = &head, .a = s->a};
        branch = *(Stmt *)s->body.v[0];
        branch.a = &saved;
        add(&body, &branch);
        local.body = body;
        local.a = NULL;
        s = &local;
    }
    int base = c->vars.n, visible = base;
    if (s->loop_handler && c->loops.n) {
        visible = (int)(intptr_t)c->loops.v[c->loops.n - 1] - 1;
    }
    List before = save_facts(&c->vars);
    ErrorTarget target = {0};
    add(&c->handlers, &target);
    infer_statements(c, &s->body, false);
    bool normal = !c->terminated;
    List exits = normal ? save_facts(&c->vars) : (List){0};
    exits.n = normal ? base : 0;
    c->vars.n = base;
    c->handlers.n--;
    c->terminated = false;
    Type *remaining = target.errors;
    bool *hidden = mem((size_t)(base + 1) * sizeof(*hidden));
    for (int j = 0; j < base; j++) {
        hidden[j] = ((Var *)c->vars.v[j])->hidden;
    }
    for (int k = 0; k < s->handlers.n; k++) {
        CatchArm *arm = s->handlers.v[k];
        Type *asked = arm->spec ? type_spec(c->p, arm->spec) : remaining;
        if (asked && !only_errors(asked)) {
            fail(arm->loc, "catch^ arm type must contain only declared errors");
        }
        if (!remaining) {
            continue;
        }
        Type *yes = fits_subset(c->p, remaining, asked, true, arm->loc);
        Type *no = fits_subset(c->p, remaining, asked, false, arm->loc);
        if (yes) {
            rewind_facts(&c->vars, before);
            // A failed try may follow a mutation on any path through the main body.
            forget_roots(&c->vars, (Roots){0}, true);
            for (int j = visible; j < base; j++) {
                ((Var *)c->vars.v[j])->hidden = true;
            }
            if (s->scoped_handlers) {
                c->lexical++;
            }
            infer_focus(c, yes);
            infer_statements(c, &arm->body, false);
            if (!c->terminated) {
                List exit = save_facts(&c->vars);
                exit.n = base;
                exits = normal ? join_exit_facts(exits, exit) : exit;
                normal = true;
            }
            c->vars.n = base;
            c->terminated = false;
            if (s->scoped_handlers) {
                c->lexical--;
            }
            for (int j = 0; j < base; j++) {
                ((Var *)c->vars.v[j])->hidden = hidden[j];
            }
        }
        remaining = no;
    }
    infer_route(c, remaining, s->loc);
    c->vars.n = base;
    c->terminated = !normal;
    if (normal) {
        rewind_facts(&c->vars, exits);
    }
}

// What the arms of a match name: the alternatives that no arm fits yet (NULL
// once none remain) and the constants they equal.
typedef struct {
    Type *rest;
    List values;
} Cover;

// A bool^ literal or a constant of an enum, as the text that tells them apart.
static char *cover_key(Expr *e)
{
    return e->kind == E_BOOL || (e->kind == E_INT && e->constant_spec) ? e->text : NULL;
}

static void cover_arm(Infer *c, Expr *e, Cover *cover)
{
    if (e->kind == E_BIN && !strcmp(e->text, "or^")) {
        cover_arm(c, e->a, cover);
        cover_arm(c, e->b, cover);
    }
    else if (e->kind == E_FITS && cover->rest) {
        cover->rest = fits_subset(c->p, cover->rest, infer_type(c, e->test_spec), false, e->loc);
    }
    else if (e->kind == E_BIN && !strcmp(e->text, "=") && cover_key(e->b)) {
        add(&cover->values, cover_key(e->b));
    }
    else if (e->kind == E_BIN && !strcmp(e->text, "=") && e->b->kind == E_NAME &&
             !strcmp(e->b->text, "nil^") && cover->rest) {
        cover->rest = without_nil(c->p, cover->rest, e->loc);
    }
}

static bool covered(Cover *cover, const char *key)
{
    for (int j = 0; j < cover->values.n; j++) {
        if (!strcmp(cover->values.v[j], key)) {
            return true;
        }
    }
    return false;
}

// Whether the values the arms name are every value of t: bool^ or an enum.
static bool values_covered(Cover *cover, Type *t)
{
    if (t->kind == T_BOOL) {
        return covered(cover, "true^") && covered(cover, "false^");
    }
    for (int j = 0; t->kind == T_ENUM && j < t->def->members.n; j++) {
        if (!covered(cover, ((EnumMember *)t->def->members.v[j])->value)) {
            return false;
        }
    }
    return t->kind == T_ENUM;
}

// Fails unless the arms of a value match without other^ cover its subject:
// every bool^ value, enum member or alternative of a union. A statement match
// may leave values unmatched.
static void match_cover(Infer *c, Expr *subject, List *arms, Loc loc)
{
    Type *t = infer_expr(c, subject, NULL);
    if (c->terminated || !t) {
        return;
    }
    Cover cover = {.rest = t};
    for (int j = 0; j < arms->n; j++) {
        cover_arm(c, arms->v[j], &cover);
    }
    char *missing = NULL;
    if (t->kind == T_BOOL) {
        missing = !covered(&cover, "true^")    ? "true^"
                  : !covered(&cover, "false^") ? "false^"
                                               : NULL;
    }
    else if (t->kind == T_UNION && cover.rest) {
        // What no fits^ arm took is covered if its values are all named.
        Type *rest = cover.rest;
        int n = rest->kind == T_UNION ? rest->def->alternatives.n : 1;
        for (int j = 0; j < n && !missing; j++) {
            Type *alternative = rest->kind == T_UNION ? rest->def->alternatives.v[j] : rest;
            missing = values_covered(&cover, alternative) ? NULL : type_name(alternative);
        }
    }
    else if (t->kind == T_UNION) {
        missing = NULL;
    }
    else if (t->kind == T_ENUM) {
        for (int j = 0; j < t->def->members.n && !missing; j++) {
            EnumMember *member = t->def->members.v[j];
            missing =
                covered(&cover, member->value) ? NULL : fmt("%s.%s", t->def->name, member->name);
        }
    }
    else {
        missing = "every value";
    }
    if (missing) {
        fail(loc, "value match expression requires other^: or arms covering %s of '%s'", missing,
             type_name(t));
    }
}

static Expr *part_node(int kind, Loc loc, char *text, Expr *a, Expr *b)
{
    Expr *e = mem(sizeof(*e));
    e->kind = kind;
    e->loc = e->name_loc = loc;
    e->text = text;
    e->a = a;
    e->b = b;
    return e;
}

static Expr *part_call(Loc loc, Expr *base, char *method)
{
    return part_node(E_CALL, loc, NULL, part_node(E_MEMBER, loc, method, base, NULL), NULL);
}

Expr *text_library_call(Program *p, Expr *e, Type *base)
{
    Expr *target = e->a;
    base = base && base->kind == T_REF ? base->elem : base;
    if (target->kind != E_MEMBER || !base || base->kind != T_STRING ||
        (strcmp(target->text, "trim") && strcmp(target->text, "split"))) {
        return NULL;
    }
    if (!e->forms[0]) {
        char *name = fmt("std.text.%s", target->text);
        Function *f = NULL;
        for (int j = 0; !f && j < p->functions.n; j++) {
            Function *candidate = p->functions.v[j];
            f = !strcmp(candidate->name, name) ? candidate : NULL;
        }
        Expr *callee = part_node(E_FUNCTION, e->loc, NULL, NULL, NULL);
        callee->function = f;
        Expr *out = part_node(E_CALL, e->loc, NULL, callee, NULL);
        add(&out->args, target->a);
        for (int j = 0; j < e->args.n; j++) {
            add(&out->args, e->args.v[j]);
        }
        e->forms[0] = out;
    }
    return e->forms[0];
}

Expr *loop_part(Program *p, Expr *e, Type *t)
{
    (void)p;
    t = t && t->kind == T_REF ? t->elem : t;
    bool array = t && (t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_BOUNDED ||
                       t->kind == T_SLICE);
    int form = range_type(t) ? 0 : array ? 1 : 2;
    bool borrow = !strcmp(e->text, "ref") || !strcmp(e->text, "read");
    if (borrow && form != 1) {
        fail(e->loc, "for^ref^ and for^read^ require an array or slice");
    }
    if (borrow && !strcmp(e->text, "ref") && read_reference(t)) {
        fail(e->loc, "for^ref^ requires writable elements; use for^read^ for read-only storage");
    }
    if (e->forms[form]) {
        return e->forms[form];
    }
    if (form == 2 && (!t || t->kind != T_STRUCT)) {
        fail(e->loc, "for^ in^ needs a range^, an array or a type with iter(), found '%s'",
             type_name(t));
    }
    Loc l = e->loc;
    Expr *zero = part_node(E_INT, l, "0", NULL, NULL), *out;
    Expr *len = part_node(E_MEMBER, l, "len", e->a, NULL);
    if (!strcmp(e->text, "iter")) {
        out = form == 0   ? e->a
              : form == 1 ? part_node(E_SLICE, l, "..", e->a, NULL)
                          : part_call(l, e->a, "iter");
    }
    else if (!strcmp(e->text, "start")) {
        out = form == 0   ? part_node(E_MEMBER, l, "\001start", e->a, NULL)
              : form == 1 ? part_node(E_EXPECT, l, NULL, zero, len)
                          : zero;
    }
    else if (!strcmp(e->text, "next")) {
        out = form == 2 ? part_call(l, e->a, "next") : e->b;
    }
    else if (!strcmp(e->text, "done")) {
        Expr *contains = part_call(l, e->a, "contains");
        add(&contains->args, e->b);
        out = form == 0   ? part_node(E_UNARY, l, "!", contains, NULL)
              : form == 1 ? part_node(E_BIN, l, ">=", e->b, len)
                          : part_node(E_BOOL, l, "false^", NULL, NULL);
    }
    else {
        out = form == 1 ? part_node(E_INDEX, l, NULL, e->a, e->b) : e->b;
        if (form == 1) {
            out->index_count = -1;
        }
        if (borrow) {
            // read^ is an internal borrow expression; surface syntax uses it in the loop header.
            out = part_node(E_UNARY, l, !strcmp(e->text, "read") ? "read^" : "ref^", out, NULL);
        }
    }
    return e->forms[form] = out;
}

static Type *infer_expr(Infer *c, Expr *e, Type *expected)
{
    if (c->terminated) {
        return type_builtin(c->p, T_NEVER);
    }
    if (!e) {
        return type_builtin(c->p, T_VOID);
    }
    if (e->kind == E_STATE || e->kind == E_DEFAULT) {
        return state_field_type(c->p, e->state_field);
    }
    if (e->kind == E_LOOP_PART) {
        Type *t = infer_expr(c, e->a, NULL);
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        return t ? infer_expr(c, loop_part(c->p, e, t), expected) : NULL;
    }
    if (e->invalid) {
        tool_abort();
    }
    empty_array_braces(e, expected);
    if (e->constant_spec) {
        return type_spec(c->p, e->constant_spec);
    }
    if (expected && expected->kind == T_UNION && e->kind != E_IF && e->kind != E_CATCH) {
        Type *actual = infer_expr(c, e, union_literal_hint(expected, e));
        return actual && sum_accepts(expected, actual) ? expected : actual;
    }
    if (guarded_chain(e)) {
        return infer_chain(c, e, true);
    }
    if (type_signature(e)) {
        return type_builtin(c->p, T_STRING);
    }
    if (type_comparison(e)) {
        Context scope = {.p = c->p, .in = c->in, .vars = c->vars, .lexical = c->lexical};
        compare_types(&scope, &c->flow, e);
        return type_builtin(c->p, T_BOOL);
    }
    switch (e->kind) {
    case E_LAYOUT:
        infer_type(c, e->type_expr);
        return type_builtin(c->p, T_I64);
    case E_BYTESOF: {
        Type *source = infer_expr(c, e->a, NULL);
        return source ? byte_view_type(c->p, source, e->loc) : NULL;
    }
    case E_TYPE:
        fail(e->loc, "'%s' names a type, not a value",
             e->type_expr->name ? e->type_expr->name : "typeof^");
    case E_STATIC:
        fail(e->loc, "static member '%s' must be called", e->text);
    case E_TRY: {
        bool literal = e->a->kind == E_INT || e->a->kind == E_FLOAT || e->a->kind == E_RECORD ||
                       e->a->kind == E_ARRAY;
        Type *type = infer_expr(c, e->a, literal ? expected : NULL);
        if (!type || type->kind == T_NEVER) {
            return type;
        }
        Type *plain = success_type(c->p, type, e->loc);
        infer_route(c, error_leaves(c->p, type, e->loc), e->loc);
        c->terminated = plain->kind == T_NEVER;
        return plain;
    }
    case E_CATCH: {
        Type *type = infer_expr(c, e->a, NULL);
        if (!type || type->kind == T_NEVER) {
            return type;
        }
        Type *plain = success_type(c->p, type, e->loc);
        Type *errors = error_leaves(c->p, type, e->loc);
        if (!errors) {
            fail(e->loc, "catch^ requires an error alternative");
        }
        int begin = c->vars.n;
        c->lexical++;
        List facts = save_facts(&c->vars);
        infer_focus(c, errors);
        Type *fallback = infer_expr(c, e->b,
                                    expected                 ? expected
                                    : plain->kind == T_NEVER ? NULL
                                                             : plain);
        c->vars.n = begin;
        c->lexical--;
        restore_facts(&c->vars, facts);
        c->terminated = plain->kind == T_NEVER && fallback && fallback->kind == T_NEVER;
        if (!fallback) {
            return NULL;
        }
        if (fallback->kind == T_VOID) {
            fail(e->loc, "catch^ requires a value-producing fallback; use a catch^ clause");
        }
        if (plain->kind == T_NEVER) {
            return fallback;
        }
        if (fallback->kind == T_NEVER) {
            return plain;
        }
        if (plain->kind == T_VOID || fallback->kind == T_VOID) {
            fail(e->loc, "catch^ requires value-producing alternatives; use nil^ for "
                         "unit success");
        }
        return plain == fallback ? plain : type_union(c->p, plain, fallback, e->loc);
    }
    case E_FITS: {
        Type *type = infer_expr(c, e->a, NULL);
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        if (type && type->kind == T_DYN) {
            Context scope = {.p = c->p, .vars = c->vars, .lexical = c->lexical};
            Type *yes = dyn_fits_target(c->p, e->test_spec, e->loc);
            if (read_reference(type)) {
                yes = type_wrap(c->p, yes->kind, yes->elem, 1);
            }
            remember_test(&c->flow, &scope, e, e->a, yes, type);
            return type_builtin(c->p, T_BOOL);
        }
        Type *target = infer_type(c, e->test_spec);
        if (type && type->kind == T_VOID) {
            fail(e->loc, "fits^ requires a value type");
        }
        if (type) {
            Context scope = {.p = c->p, .vars = c->vars, .lexical = c->lexical};
            remember_test(&c->flow, &scope, e, e->a, fits_subset(c->p, type, target, true, e->loc),
                          fits_subset(c->p, type, target, false, e->loc));
        }
        return type_builtin(c->p, T_BOOL);
    }
    case E_PRESENT: {
        bool raw = c->raw_name;
        c->raw_name = e->a->kind == E_NAME;
        Type *type = infer_expr(c, e->a, NULL);
        c->raw_name = raw;
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        if (type && type->kind == T_VOID) {
            fail(e->loc, "presence test requires a value");
        }
        infer_guard(c, e, e->a, true);
        return type_builtin(c->p, T_BOOL);
    }
    case E_COALESCE: {
        Type *base = infer_expr(c, e->a, NULL);
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        if (!base) {
            return NULL;
        }
        if (!nullable(base)) {
            fail(e->loc, "?? requires a nil alternative");
        }
        Type *plain = without_nil(c->p, base, e->loc);
        Type *fallback = infer_expr(c, e->b, plain);
        if (plain) {
            c->terminated = false;
        }
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
        return expected && kind_float(expected->kind) ? expected : type_builtin(c->p, T_F64);
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

        if (!strcmp(e->text, "nil^")) {
            return type_builtin(c->p, T_NIL);
        }
        {
            Type *type = infer_lookup(c, e->text);
            return type ? type : callable_group(c->p, NULL, e->text);
        }
    case E_SELF:
        return infer_lookup(c, e->text);
    case E_FUNCTION: {
        Context scope = {.p = c->p, .in = c->in, .vars = c->vars, .lexical = c->lexical};
        return closure_type(&scope, e->function);
    }
    case E_CAPTURE: {
        Context scope = {.p = c->p, .in = c->in, .vars = c->vars};
        Var *v = captured_variable(&scope, e);
        return v->type ? v->type->elem : NULL;
    }
    case E_CALL:
        return infer_call(c, e, expected);
    case E_RECORD: {
        Type *type = expected;
        if (e->tuple && (!(type && type->kind == T_STRUCT && type->def->tuple) ||
                         (c->in && !c->in->fn->procedure))) {
            // An unannotated result takes the types of the values returned.
            List elems = {0};
            for (int j = 0; j < e->args.n; j++) {
                Type *hint =
                    type && type->kind == T_STRUCT && type->def->tuple && j < type->def->fields.n
                        ? ((Field *)type->def->fields.v[j])->type
                        : NULL;
                Type *elem = infer_expr(c, e->args.v[j], hint);
                elem = hint && elem ? result_capabilities(c->p, hint, elem, e->loc) : elem;
                if (!elem || c->terminated) {
                    return c->terminated ? type_builtin(c->p, T_NEVER) : NULL;
                }
                add(&elems, elem);
            }
            return type_tuple(c->p, &elems, e->loc);
        }
        if (e->text) {
            Def *d = lookup_def(c->p, e->text);
            type = d ? d->type : NULL;
            if (!type) {
                return NULL;
            }
            if (type->kind == T_UNION) {
                fail(e->loc, "cannot construct union directly; construct one of its "
                             "alternatives");
            }
        }
        if (!type || type->kind != T_STRUCT) {
            fail(e->loc, "contextual construction requires a single nominal struct "
                         "type; write Type{field=value}");
        }
        compose(c->p, type->def);
        layout(type, e->loc);
        for (int j = 0; j < type->def->fields.n; j++) {
            Field *field = type->def->fields.v[j];
            Expr *init = field->init;
            for (int k = 0; k < e->names.n; k++) {
                if (!strcmp(e->names.v[k], field->name)) {
                    init = e->args.v[k];
                }
            }
            if (init) {
                bool def = init == field->init;
                if (def) {
                    for (int k = 0; k < c->initializers.n; k++) {
                        if (c->initializers.v[k] == init) {
                            fail(e->loc, "recursive default construction for '%s'", field->name);
                        }
                    }
                    add(&c->initializers, init);
                }
                infer_expr(c, init, field->type);
                if (def) {
                    c->initializers.n--;
                }
            }
            else if (field->type->kind == T_STRUCT) {
                Expr empty = {.kind = E_RECORD, .loc = e->loc, .text = field->type->def->name};
                infer_expr(c, &empty, field->type);
            }
            if (c->terminated) {
                return type_builtin(c->p, T_NEVER);
            }
        }
        return type;
    }
    case E_ARRAY: {
        if (e->text) {
            if (!strcmp(e->text, "copy")) {
                Type *source = infer_expr(c, e->args.v[0], NULL);
                return source ? array_literal_type(c->p, e, expected, source) : expected;
            }
            Type *type = array_literal_type(c->p, e, expected, NULL);
            for (int j = 0; j < e->args.n; j++) {
                Type *actual = infer_expr(c, e->args.v[j], array_form_hint(c->p, e, type, j));
                if (!type && !expected && !j) {
                    type = repeat_type(c->p, e, actual);
                }
            }
            return c->terminated ? type_builtin(c->p, T_NEVER) : type;
        }
        Type *type = array_literal_type(c->p, e, expected, NULL);
        for (int j = 0; j < e->args.n; j++) {
            Type *actual = infer_expr(c, e->args.v[j], type && !e->array_refs ? type->elem : NULL);
            if (actual && e->array_refs && actual->kind != T_REF && actual->kind != T_DYN) {
                actual = type_wrap(c->p, T_REF, actual, 0);
            }
            if (!type && actual) {
                type = array_literal_type(c->p, e, expected, actual);
            }
        }
        return c->terminated ? type_builtin(c->p, T_NEVER) : type;
    }
    case E_MEMBER: {
        Type *base = infer_expr(c, e->a, NULL);
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        base = error_view(base, e->text);
        if (base && base->kind == T_STRUCT) {
            compose(c->p, base->def);
        }
        if (argument_pack(base)) {
            if (strcmp(e->text, "len")) {
                fail(e->loc,
                     "argument packs only support .len, constant indexing and inline^ for^");
            }
            return type_builtin(c->p, T_I64);
        }
        if (error_site(base, e->text) >= 0) {
            return type_builtin(c->p, error_site(base, e->text) % 3 ? T_U32 : T_STRING);
        }
        if (base && base->kind == T_STRING) {
            if (strcmp(e->text, "bytes")) {
                text_bytes_only(base, e->text, e->name_loc.file ? e->name_loc : e->loc);
            }
            return type_wrap(c->p, T_SLICE, type_builtin(c->p, T_U8), 1);
        }
        if (base && !range_type(base) && !strcmp(e->text, "\001start")) {
            fail(e->loc, "expected 'range^', found '%s'", type_name(base));
        }
        if (range_type(base)) {
            Type *bound = range_bound(c->p, base);
            if (strstr(e->text, "start")) {
                return bound;
            }
            if (strcmp(e->text, "end") && strcmp(e->text, "len")) {
                fail(e->name_loc.file ? e->name_loc : e->loc,
                     "range^ has start, end, len and contains(i)");
            }
            return !strcmp(e->text, "len")
                       ? type_builtin(c->p, T_I64)
                       : type_union(c->p, bound, type_builtin(c->p, T_NIL), e->loc);
        }
        if (!base) {
            return NULL;
        }
        if ((base->kind == T_ARRAY || base->kind == T_GROW || base->kind == T_BOUNDED ||
             base->kind == T_STRING || base->kind == T_SLICE) &&
            !strcmp(e->text, "len")) {
            return type_builtin(c->p, T_I64);
        }
        if ((base->kind == T_ARRAY || base->kind == T_GROW || base->kind == T_BOUNDED ||
             base->kind == T_SLICE) &&
            !strcmp(e->text, "bytes")) {
            fail(e->loc, "arrays have no .bytes; use bytesof^array");
        }
        if (kind_float(base->kind) && !strcmp(e->text, "bits")) {
            return type_builtin(c->p, bits_kind(base->kind));
        }
        Field *f = base->kind == T_STRUCT ? field(base->def, e->text) : NULL;
        if (base->kind == T_STRUCT && !f) {
            Type *type = callable_group(c->p, base->def, e->text);
            if (type) {
                return type;
            }
            fail(e->name_loc.file ? e->name_loc : e->loc, "'%s' has no field '%s'", base->def->name,
                 e->text);
        }
        if (f && c->raw_place != e) {
            Context scope = {.p = c->p, .vars = c->vars, .lexical = c->lexical};
            Var *root = NULL;
            char *path = NULL;
            if (stable_path(&scope, e, &root, &path)) {
                Type *type = path_type(root, path);
                if (type) {
                    return type;
                }
            }
        }
        if (f && (f->type->kind == T_REF || f->type->kind == T_SLICE || f->type->kind == T_DYN) &&
            infer_read_place(c, e)) {
            return type_wrap(c->p, f->type->kind, f->type->elem, 1);
        }
        return f ? f->type : NULL;
    }
    case E_PANIC:
        infer_expr(c, e->a, NULL);
        c->terminated = true;
        return type_builtin(c->p, T_NEVER);
    case E_YIELD: {
        Type *t = e->a ? infer_expr(c, e->a, NULL) : type_builtin(c->p, T_NIL);
        if (t && t->kind != T_NEVER) {
            c->yielded = join_types(c->p, c->yielded, c->yield_expr, t, e->a, e->loc);
            if (!c->yield_expr || !integer_literal(e->a)) {
                c->yield_expr = e->a;
            }
        }
        // What a resume hands in is what the binding receiving it is written to hold.
        if (!expected || expected->kind == T_VOID) {
            return type_builtin(c->p, T_VOID);
        }
        if (c->received && c->received != expected) {
            fail(e->loc, "yield^ receives '%s' here but '%s' at another yield^",
                 type_name(expected), type_name(c->received));
        }
        c->received = expected;
        return expected;
    }
    case E_CHAR:
        return type_builtin(c->p, T_CHAR);
    case E_INTERP: {
        for (int j = 0; j < e->args.n; j++) {
            infer_expr(c, e->args.v[j], NULL);
        }
        Def *builder = lookup_def(c->p, "std.text.Builder");
        return c->terminated ? type_builtin(c->p, T_NEVER) : builder ? builder->type : NULL;
    }
    case E_RANGE: {
        // An expected element wins; otherwise a typed bound decides and literals follow it.
        bool wanted = range_type(expected);
        Type *elem = wanted ? expected->elem : NULL;
        Expr *ends[2] = {e->a, e->b};
        bool unknown = false;
        for (int j = 0; j < 2; j++) {
            if (!elem && ends[j] && !integer_literal(ends[j])) {
                elem = infer_expr(c, ends[j], NULL);
                elem = elem && elem->kind == T_REF ? elem->elem : elem;
                unknown = !elem;
            }
        }
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        if (unknown) {
            return NULL;
        }
        elem = elem ? elem : type_builtin(c->p, wanted ? T_I64 : T_I32);
        infer_expr(c, e->a, elem);
        infer_expr(c, e->b, elem);
        return c->terminated ? type_builtin(c->p, T_NEVER) : type_range(c->p, elem, e->loc);
    }
    case E_PACK:
        if (e->a) {
            Type *t = infer_expr(c, e->a, NULL);
            if (!t || c->terminated) {
                return c->terminated ? type_builtin(c->p, T_NEVER) : NULL;
            }
            if (expected) {
                return expected;
            }
            t = t->kind == T_REF ? t->elem : t;
            if (argument_pack(t)) {
                return t;
            }
            int count = inline_length(t, e->loc);
            if (count > 4096) {
                fail(e->loc, "heterogeneous argument pack exceeds 4096 elements");
            }
            List elems = {0};
            for (int j = 0; j < count; j++) {
                add(&elems, inline_element(t, j));
            }
            return type_pack(c->p, &elems, e->loc);
        }
        else {
            List elems = {0};
            for (int j = 0; j < e->args.n; j++) {
                Type *t = infer_expr(c, e->args.v[j], expected ? expected->elem : NULL);
                if (!t || c->terminated) {
                    return c->terminated ? type_builtin(c->p, T_NEVER) : NULL;
                }
                add(&elems, t);
            }
            return expected ? expected : type_pack(c->p, &elems, e->loc);
        }
    case E_SLICE: {
        Type *base = infer_expr(c, e->a, NULL);
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        infer_expr(c, e->b, type_builtin(c->p, T_I64));
        infer_expr(c, e->c, type_builtin(c->p, T_I64));
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        return infer_view(c, e->a, base);
    }
    case E_INDEX: {
        Type *base = infer_expr(c, e->a, NULL);
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        if (argument_pack(base)) {
            return inline_element(base, argument_index(&c->vars, base, e->b, e->loc));
        }
        Type *index = infer_expr(c, e->b, type_builtin(c->p, T_I64));
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        text_bytes_only(base, "[]", e->loc);
        if (range_type(index)) {
            if (e->index_count) {
                fail(e->b->loc, "multidimensional indexing requires integer indices");
            }
            return infer_view(c, e->a, base);
        }
        if (base && (base->kind == T_ARRAY || base->kind == T_BOUNDED || base->kind == T_GROW ||
                     base->kind == T_SLICE)) {
            layout(base, e->loc);
            if (base->elem->owned && c->raw_place != e) {
                return type_wrap(c->p, T_REF, base->elem,
                                 infer_read_place(c, e) || infer_let_place(c, e));
            }
        }
        return base && base->kind == T_STRING ? type_builtin(c->p, T_U8)
               : base && (base->kind == T_ARRAY || base->kind == T_GROW ||
                          base->kind == T_BOUNDED || base->kind == T_SLICE)
                   ? base->elem
                   : NULL;
    }
    case E_UNARY: {
        bool raw = c->raw_name;
        Expr *old_place = c->raw_place;
        if (!strcmp(e->text, "ref^") || !strcmp(e->text, "read^")) {
            c->raw_name = e->a->kind == E_NAME;
            c->raw_place = e->a;
        }
        Type *a = infer_expr(
            c, e->a, (!strcmp(e->text, "-") || !strcmp(e->text, "bitnot^")) ? expected : NULL);
        c->raw_name = raw;
        c->raw_place = old_place;
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        if (!strcmp(e->text, "!")) {
            return type_builtin(c->p, T_BOOL);
        }
        if (!a) {
            return NULL;
        }
        if (!strcmp(e->text, "ref^") || !strcmp(e->text, "read^")) {
            return type_wrap(c->p, T_REF, a,
                             !strcmp(e->text, "read^") || infer_read_place(c, e->a));
        }
        if (!strcmp(e->text, "*")) {
            return a->kind == T_REF ? a->elem : NULL;
        }
        if (!strcmp(e->text, "-")) {
            Function *op = operator_select(c->p, "-", a, NULL, e->loc);
            if (op) {
                return operator_result(c->p, op, a, NULL, e->loc);
            }
        }
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
        for (int j = 1; j < e->args.n; j++) {
            Expr *operand = e->args.v[j];
            if (!strcmp((char *)e->names.v[j - 1], "fits^")) {
                Value value = {.type = t};
                Expr saved = {.kind = E_SAVED, .compiler_value = &value};
                Expr pair = *operand;
                pair.a = &saved;
                if (t) {
                    infer_expr(c, &pair, NULL);
                }
            }
            else {
                t = infer_expr(c, operand, t);
            }
        }
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        return type_builtin(c->p, T_BOOL);
    }
    case E_IF: {
        if (e->match) {
            List arms = {0};
            for (Expr *arm = e; arm && arm->kind == E_IF; arm = arm->c) {
                add(&arms, arm->a);
            }
            match_cover(c, e->match, &arms, e->loc);
        }
        infer_expr(c, e->a, type_builtin(c->p, T_BOOL));
        if (c->terminated) {
            return type_builtin(c->p, T_NEVER);
        }
        List facts = save_facts(&c->vars);
        int known = condition_truth(&c->flow, e->a);
        if (known) {
            refine_condition(c->p, &c->vars, &c->flow, e->a, known > 0);
            Type *type = infer_expr(c, known > 0 ? e->b : e->c, expected);
            restore_facts(&c->vars, facts);
            return type;
        }
        Type *hint = expected;
        for (int pass = 0;; pass++) {
            refine_condition(c->p, &c->vars, &c->flow, e->a, true);
            Type *a = infer_expr(c, e->b, hint);
            bool ended = c->terminated;
            c->terminated = false;
            List after_yes = save_facts(&c->vars);
            rewind_facts(&c->vars, facts);
            refine_condition(c->p, &c->vars, &c->flow, e->a, false);
            Type *b = infer_expr(c, e->c, hint ? hint : numeric(a) ? a : NULL);
            c->terminated &= ended;
            // A nested conditional can provisionally take a pending call's
            // literal fallback width. Let either sibling supply numeric context
            // before joining; already typed operands keep their own types.
            if (!pass && !expected && numeric(a) && numeric(b) && a != b) {
                hint = b;
                rewind_facts(&c->vars, facts);
                c->terminated = false;
                continue;
            }
            merge_facts(&c->vars, facts, after_yes);
            return join_types(c->p, a, e->b, b, e->c, e->loc);
        }
    }
    case E_BIN: {
        bool logic = !strcmp(e->text, "and^") || !strcmp(e->text, "or^") ||
                     !strcmp(e->text, "&&") || !strcmp(e->text, "||");
        bool comparison = strchr("=!<>", e->text[0]) != NULL;
        bool literal_left =
            !e->a->constant_spec &&
            (e->a->kind == E_INT || e->a->kind == E_FLOAT ||
             (e->a->kind == E_UNARY && !strcmp(e->a->text, "-") && e->a->a->kind == E_INT));
        Type *right_type = !logic && literal_left ? infer_expr(c, e->b, NULL) : NULL;
        // Numeric lookahead must not fix contextual literals at their default
        // widths, especially while a nested call's result is still pending.
        if (numeric(expected) && numeric(right_type)) {
            right_type = NULL;
        }
        Type *left_hint = operator_hint(c->p, e->text, right_type, true, e->a);
        bool literal_right =
            !e->b->constant_spec && (e->b->kind == E_INT || e->b->kind == E_FLOAT ||
                                     (e->b->kind == E_UNARY && !strcmp(e->b->text, "-") &&
                                      (e->b->a->kind == E_INT || e->b->a->kind == E_FLOAT)));
        if (!left_hint && right_type && strcmp(e->text, "bitshift^") && !literal_right &&
            ((e->a->kind == E_FLOAT && kind_float(right_type->kind)) ||
             (e->a->kind != E_FLOAT && kind_integer(right_type->kind)))) {
            left_hint = right_type;
        }
        Type *a = infer_expr(c, e->a,
                             left_hint             ? left_hint
                             : comparison || logic ? NULL
                                                   : expected);
        if (a && a->kind == T_NEVER) {
            return a;
        }
        List facts = {0};
        if (logic) {
            int known = condition_truth(&c->flow, e->a);
            bool andop = !strcmp(e->text, "and^") || !strcmp(e->text, "&&");
            if (known && ((known > 0) != andop)) {
                return type_builtin(c->p, T_BOOL);
            }
            facts = save_facts(&c->vars);
            refine_condition(c->p, &c->vars, &c->flow, e->a, !strcmp(e->text, "and^"));
        }
        Type *hint = operator_hint(c->p, e->text, a, false, e->b);
        bool against_union = comparison && a && a->kind == T_UNION;
        Type *b = right_type ? right_type
                             : infer_expr(c, e->b,
                                          hint ? hint
                                          : !strcmp(e->text, "bitshift^") || against_union ||
                                                  (a && (a->kind == T_STRUCT || a->kind == T_REF))
                                              ? NULL
                                              : a);
        if (b && b->kind == T_NEVER && !logic) {
            return b;
        }
        if (logic) {
            c->terminated = false;
            restore_facts(&c->vars, facts);
        }
        if (comparison && (nil_name(e->a) || nil_name(e->b)) &&
            (!strcmp(e->text, "=") || !strcmp(e->text, "!="))) {
            infer_guard(c, e, nil_name(e->a) ? e->b : e->a, !strcmp(e->text, "!="));
        }
        Function *op = a && b ? operator_select(c->p, e->text, a, b, e->loc) : NULL;
        if (op) {
            Type *result = operator_result(c->p, op, a, b, e->loc);
            return comparison && strcmp(e->text, "<=>") ? type_builtin(c->p, T_BOOL) : result;
        }
        // A failed nominal dispatch has no arithmetic fallback. Propagating an
        // operand's type here would blame an enclosing call for this operator.
        if ((!a || !b) && ((a && (a->kind == T_STRUCT || a->kind == T_REF)) ||
                           (b && (b->kind == T_STRUCT || b->kind == T_REF)) ||
                           (expected && (expected->kind == T_STRUCT || expected->kind == T_REF)))) {
            return NULL;
        }
        if (!logic && a && b &&
            !(comparison &&
              (a->kind == T_NIL || b->kind == T_NIL || a->kind == T_UNION || b->kind == T_UNION)) &&
            (a->kind == T_STRUCT || a->kind == T_REF || b->kind == T_STRUCT || b->kind == T_REF)) {
            fail(e->loc, "operator '%s' has no matching overload for '%s' and '%s'", e->text,
                 type_name(a), type_name(b));
        }
        if (!strcmp(e->text, "<=>")) {
            return type_builtin(c->p, T_I32);
        }
        if (comparison || logic) {
            return type_builtin(c->p, T_BOOL);
        }
        if (!strcmp(e->text, "bitshift^")) {
            return a;
        }
        return join_types(c->p, a, e->a, b, e->b, e->loc);
    }
    default:
        return NULL;
    }
}

// Whether control always leaves body by break^ or next^. Inference does not treat those as ending
// the statements around them, but what follows an if^ is reached only from the arm that falls out.
static bool ends_in_jump(List *body)
{
    if (!body->n) {
        return false;
    }
    Stmt *s = body->v[body->n - 1];
    return s->kind == S_BREAK || s->kind == S_NEXT ||
           (s->kind == S_BLOCK && ends_in_jump(&s->body)) ||
           (s->kind == S_IF && ends_in_jump(&s->body) && ends_in_jump(&s->other));
}

static void infer_statements_unchecked(Infer *c, List *body, bool scoped)
{
    int mark = c->vars.n;
    if (scoped) {
        c->lexical++;
    }
    for (int j = 0; j < body->n; j++) {
        if (c->terminated) {
            break;
        }
        Stmt *s = body->v[j];
        if (s->kind == S_HANDLE) {
            infer_handle(c, s);
        }
        else if (s->kind == S_BIND) {
            Type *t = infer_type(c, s->spec);
            Type *actual = infer_expr(c, s->a, t);
            if (c->terminated && !t) {
                break;
            }
            if (s->depth && actual) {
                results_check(actual, s->depth, s->loc);
            }
            Var *v = mem(sizeof(*v));
            v->name = s->name;
            v->declaration = s->loc;
            v->mut = s->mut;
            v->lexical = c->lexical;
            v->type = callable_type(actual) ? actual : t ? t : actual;
            v->constant = !s->mut && v->type && kind_integer(v->type->kind) &&
                          static_integer(&c->vars, s->a, &v->integer);
            v->alias = infer_alias(c, s->a);
            v->let_bound = !s->mut;
            if (s->flow_alias && s->a->kind == E_NAME) {
                v->source = infer_variable(c, s->a);
                v->source_version = v->source ? v->source->version : 0;
            }
            add(&c->vars, v);
        }
        else if (s->kind == S_BIND_MANY) {
            infer_statements(c, &s->prolog, false);
            List pending = {0};
            for (int k = 0; k < s->body.n; k++) {
                Stmt *b = s->body.v[k];
                Type *t = infer_type(c, b->spec);
                Type *actual = infer_expr(c, b->a, t);
                Var *v = mem(sizeof(*v));
                v->name = b->name;
                v->declaration = b->loc;
                v->mut = b->mut;
                v->lexical = c->lexical;
                v->type = callable_type(actual) ? actual : t ? t : actual;
                v->alias = infer_alias(c, b->a);
                add(&pending, v);
            }
            for (int k = 0; k < pending.n; k++) {
                add(&c->vars, pending.v[k]);
            }
        }
        else if (s->kind == S_ASSIGN) {
            int count = s->targets.n ? s->targets.n : 1;
            for (int k = 0; k < count; k++) {
                Expr *dst = s->targets.n ? s->targets.v[k] : s->a;
                Var *v = infer_variable(c, dst);
                if (!v) {
                    infer_expr(c, dst, NULL);
                }
                Type *hint = v ? v->type : NULL;
                if (s->op && v && v->narrowed) {
                    hint = v->narrowed;
                }
                if (s->op && s->optional && hint) {
                    hint = without_nil(c->p, hint, s->loc);
                }
                infer_expr(c, s->values.n ? s->values.v[k] : s->b, hint);
                if (s->optional) {
                    c->terminated = false;
                }
            }
            for (int k = 0; k < count; k++) {
                Expr *dst = s->targets.n ? s->targets.v[k] : s->a;
                Var *v = infer_variable(c, dst);
                if (v) {
                    Type *narrow = v->narrowed;
                    v->version++;
                    v->path_version++;
                    v->narrowed = s->op ? narrow : NULL;
                    v->paths = (List){0};
                    for (int q = 0; q < c->vars.n; q++) {
                        Var *alias = c->vars.v[q];
                        if (alias != v && alias->path_sensitive) {
                            alias->path_version++;
                            alias->paths = (List){0};
                        }
                    }
                    if (!s->op) {
                        v->alias = infer_alias(c, s->values.n ? s->values.v[k] : s->b);
                    }
                }
                else {
                    Context scope = {.vars = c->vars, .lexical = c->lexical};
                    Var *root = NULL;
                    char *path = NULL;
                    if (stable_path(&scope, dst, &root, &path) && *path) {
                        forget_paths(&c->vars);
                    }
                    else {
                        forget_roots(&c->vars, (Roots){0}, true);
                    }
                }
            }
        }
        else if (s->kind == S_RETURN) {
            Type *hint = c->in->fn->result ? function_result(c->p, c->in, c->in->fn->result)
                                           : c->in->fn->required_result;
            Type *t = infer_expr(c, s->a, hint);
            if (!s->a && hint && nullable(hint) && has_error(hint)) {
                t = hint;
            }
            c->value_return |= s->a != NULL;
            c->returned = join_types(c->p, c->returned, c->return_expr, t, s->a, s->loc);
            if (!c->return_expr || !integer_literal(s->a)) {
                c->return_expr = s->a;
            }
            c->terminated = true;
        }
        else if (s->kind == S_INLINE) {
            Type *t = infer_expr(c, s->a, NULL);
            t = t && t->kind == T_REF ? t->elem : t;
            if (!t || c->terminated) {
                continue;
            }
            int count = inline_length(t, s->loc), iteration_mark = c->vars.n;
            if (count > 4096 - c->inline_steps) {
                fail(s->loc, "inline^ expansion limit exceeded (4096 iterations per function)");
            }
            c->inline_steps += count;
            add(&c->loops, (void *)(intptr_t)(iteration_mark + 1));
            InlineFlow flow = {.level = c->loops.n};
            add(&c->inline_flows, &flow);
            for (int k = 0; k < count; k++) {
                Var *v = mem(sizeof(*v));
                v->name = s->name;
                v->type = inline_element(t, k);
                v->lexical = c->lexical + 1;
                add(&c->vars, v);
                flow.next = false;
                List expanded = inline_body(&s->body);
                infer_statements(c, &expanded, true);
                c->vars.n = iteration_mark;
                if (c->terminated && !flow.next) {
                    break;
                }
                c->terminated = false;
            }
            if (flow.stop) {
                c->terminated = false;
            }
            c->inline_flows.n--;
            c->loops.n--;
        }
        else if (s->kind == S_BREAK || s->kind == S_NEXT) {
            for (int k = 0; k < c->inline_flows.n; k++) {
                InlineFlow *flow = c->inline_flows.v[k];
                if (flow->level == c->loops.n - s->depth + 1) {
                    flow->next |= s->kind == S_NEXT;
                    flow->stop |= s->kind == S_BREAK;
                }
            }
            c->terminated = c->inline_flows.n != 0;
        }
        else if (s->kind == S_WHILE) {
            int begin = c->vars.n;
            add(&c->loops, (void *)(intptr_t)(begin + 1));
            List loop_facts = save_facts(&c->vars);
            infer_statements(c, &s->prolog, false);
            if (c->terminated) {
                c->vars.n = begin;
                c->loops.n--;
                continue;
            }
            infer_statements(c, &s->pre, false);
            if (!c->terminated) {
                infer_expr(c, s->a, type_builtin(c->p, T_BOOL));
            }
            if (c->terminated) {
                c->vars.n = begin;
                c->loops.n--;
                restore_facts(&c->vars, loop_facts);
                continue;
            }
            bool live = condition_truth(&c->flow, s->a) >= 0;
            refine_condition(c->p, &c->vars, &c->flow, s->a, true);
            if (live) {
                infer_statements(c, &s->first, false);
            }
            c->lexical--;
            if (live) {
                infer_statements(c, &s->body, true);
            }
            c->lexical++;
            c->lexical--;
            c->terminated = false;
            if (live) {
                infer_statements(c, &s->update, true);
            }
            restore_facts(&c->vars, loop_facts);
            infer_statements(c, &s->last, true);
            c->terminated = false;
            infer_statements(c, &s->epilog, true);
            c->lexical++;
            c->vars.n = begin;
            restore_facts(&c->vars, loop_facts);
            c->terminated = false;
            c->loops.n--;
        }
        else if (s->kind == S_IF) {
            infer_expr(c, s->a, type_builtin(c->p, T_BOOL));
            if (c->terminated) {
                break;
            }
            if (s->internal) {
                c->lexical--;
            }
            List facts = save_facts(&c->vars);
            int known = condition_truth(&c->flow, s->a);
            if (known) {
                refine_condition(c->p, &c->vars, &c->flow, s->a, known > 0);
                infer_statements(c, known > 0 ? &s->body : &s->other, true);
                restore_facts(&c->vars, facts);
                if (s->internal) {
                    c->lexical++;
                }
                continue;
            }
            refine_condition(c->p, &c->vars, &c->flow, s->a, true);
            infer_statements(c, &s->body, true);
            bool yes_ended = c->terminated;
            bool a = yes_ended || ends_in_jump(&s->body);
            c->terminated = false;
            List after_yes = save_facts(&c->vars);
            rewind_facts(&c->vars, facts);
            refine_condition(c->p, &c->vars, &c->flow, s->a, false);
            infer_statements(c, &s->other, true);
            bool b = c->terminated || ends_in_jump(&s->other);
            if (!a && b) {
                rewind_facts(&c->vars, after_yes);
            }
            else if (!a && !b) {
                merge_facts(&c->vars, facts, after_yes);
            }
            c->terminated &= yes_ended;
            if (s->internal) {
                c->lexical++;
            }
            infer_statements(c, &s->update, true);
        }
        else if (s->kind == S_BLOCK) {
            infer_statements(c, &s->body, true);
        }
        else if (s->kind == S_TYPE) {
            Context scope = {.p = c->p, .in = c->in, .vars = c->vars, .lexical = c->lexical};
            pin_local_type(&scope, s->spec);
        }
        else {
            infer_expr(c, s->a, NULL);
            if (s->b) {
                infer_expr(c, s->b, NULL);
            }
        }
    }
    if (scoped) {
        c->vars.n = mark;
        c->lexical--;
    }
}

// Roll back only the failed statement, including nested scopes and flow facts.
static void infer_statement_checked(Infer *c, Stmt *statement)
{
    static const size_t lists[] = {offsetof(Infer, vars),         offsetof(Infer, handlers),
                                   offsetof(Infer, loops),        offsetof(Infer, initializers),
                                   offsetof(Infer, inline_flows), offsetof(Infer, flow.guards)};
    ToolState *state = tool_save(c, sizeof(*c), lists, sizeof(lists) / sizeof(*lists));
    ToolState *vars = tool_save_objects(&c->vars, sizeof(Var));
    ToolState *handlers = tool_save_objects(&c->handlers, sizeof(ErrorTarget));
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        tool_restore(state);
        tool_restore(vars);
        tool_restore(handlers);
        if (c->in) {
            c->in->invalid = true;
        }
        tool_failed_binding(c->p, &c->vars, statement, 1, c->lexical);
        if (statement->kind == S_RETURN) {
            c->value_return = true;
        }
    }
    else {
        List one = {.v = (void **)&statement, .n = 1};
        infer_statements_unchecked(c, &one, false);
    }
    tool_leave(&recovery);
    mem_free(handlers);
    mem_free(vars);
    mem_free(state);
}

static void infer_statements(Infer *c, List *body, bool scoped)
{
    if (!c->p->tooling) {
        infer_statements_unchecked(c, body, scoped);
        return;
    }
    int mark = c->vars.n;
    if (scoped) {
        c->lexical++;
    }
    for (int j = 0; j < body->n && !c->terminated; j++) {
        infer_statement_checked(c, body->v[j]);
    }
    if (scoped) {
        c->vars.n = mark;
        c->lexical--;
    }
}

Type *top_binding_type(Program *p, Stmt *binding)
{
    Instance in = {.fn = p->entry};
    Infer c = {.p = p, .in = &in, .lexical = 1};
    for (int j = 0; j < p->entry->params.n; j++) {
        Param *param = p->entry->params.v[j];
        Var *v = mem(sizeof(*v));
        *v = (Var){.name = param->name, .type = parameter_type(p, param), .lexical = 1};
        add(&in.params, v->type);
        add(&c.vars, v);
    }
    for (int j = 0; j < p->entry->body.n; j++) {
        Stmt *s = p->entry->body.v[j];
        // Only declarations are needed; executing calls here can recurse through methods.
        if (s->kind != S_BIND && s->kind != S_BIND_MANY) {
            continue;
        }
        if (s->state_field && !s->state_field->binding) {
            continue;
        }
        List one = {0};
        add(&one, s);
        infer_statements(&c, &one, false);
        bool found = s == binding;
        for (int k = 0; s->kind == S_BIND_MANY && k < s->body.n; k++) {
            found |= s->body.v[k] == binding;
        }
        if (found) {
            for (int k = c.vars.n - 1; k >= 0; k--) {
                Var *v = c.vars.v[k];
                if (!strcmp(v->name, binding->name)) {
                    return v->type;
                }
            }
            break;
        }
    }
    return NULL;
}

List first_types(Context *c, Stmt *s)
{
    Infer infer = infer_context(c);
    infer_statements(&infer, &s->pre, false);
    infer_expr(&infer, s->a, type_builtin(c->p, T_BOOL));
    if (condition_truth(&infer.flow, s->a) < 0) {
        return (List){0};
    }
    refine_condition(c->p, &infer.vars, &infer.flow, s->a, true);
    int begin = infer.vars.n;
    infer_statements(&infer, &s->first, false);
    List out = {0};
    for (int j = begin; j < infer.vars.n; j++) {
        Var *v = infer.vars.v[j];
        for (int k = c->vars.n - 1; k >= 0; k--) {
            Var *old = c->vars.v[k];
            if (old->depth < c->depth) {
                break;
            }
            if (!strcmp(old->name, v->name)) {
                fail(s->loc, "duplicate binding '%s'", v->name);
            }
        }
        for (int k = 0; k < out.n; k++) {
            if (!strcmp(((Var *)out.v[k])->name, v->name)) {
                fail(s->loc, "duplicate binding '%s'", v->name);
            }
        }
        for (int k = 0; k < s->first.n; k++) {
            Stmt *b = s->first.v[k];
            if (b->kind == S_BIND && !strcmp(b->name, v->name)) {
                v->mut = b->mut;
            }
            if (b->kind == S_BIND_MANY) {
                for (int q = 0; q < b->body.n; q++) {
                    Stmt *part = b->body.v[q];
                    if (!strcmp(part->name, v->name)) {
                        v->mut = part->mut;
                    }
                }
            }
        }
        add(&out, v);
    }
    return out;
}

// A yielding instance answers its frame; what it yields, what it finally returns and their
// union are what a turn of that frame answers. Each may still change while inference runs.
static bool coroutine_result(Program *p, Instance *in, Type *final, Type *yielded, Type *received)
{
    if (in->fn->result) {
        fail(in->fn->loc, "a yielding function's result is the coroutine it makes; leave it "
                          "unwritten (Coroutine<R, Y, T> annotations are not supported yet)");
    }
    if (!final || final->kind == T_VOID) {
        final = type_builtin(p, T_NIL);
    }
    if (!yielded) {
        yielded = type_builtin(p, T_NIL);
    }
    if (!in->frame) {
        in->frame = frame_type(p, in);
    }
    bool changed = in->result != in->frame || in->final != final || in->yielded != yielded ||
                   in->received != received;
    if (changed && in->state) {
        fail(in->fn->loc, "return inference changed after code generation");
    }
    in->result = in->frame;
    in->final = final;
    in->yielded = yielded;
    in->received = received;
    in->turn = final == yielded ? final : type_union(p, yielded, final, in->fn->loc);
    return changed;
}

static bool infer_instance_result(Program *p, Instance *in)
{
    if (in->fn->external) {
        if (!in->result) {
            in->result = type_builtin(p, T_VOID);
            return true;
        }
        return false;
    }
    bool changed = false;
    Infer c = {.p = p, .in = in, .lexical = in->fn->lexical ? in->fn->lexical : 1};
    for (int k = 0; k < in->params.n; k++) {
        Param *param =
            k < in->fn->params.n ? in->fn->params.v[k] : in->fn->captures.v[k - in->fn->params.n];
        Var *v = mem(sizeof(*v));
        v->name = param->name;
        v->lexical = c.lexical;
        v->type = in->params.v[k];
        v->declaration = param->capture ? param->capture->loc : param->loc;
        v->mut = true;
        v->captured_let = param->capture && !param->capture->mut;
        add(&c.vars, v);
    }
    infer_statements(&c, &in->fn->body, false);
    Type *result = c.returned;
    if (c.propagated) {
        if (result && result->kind == T_VOID) {
            result = type_builtin(p, T_NIL);
        }
        else if (!result || result->kind == T_NEVER) {
            result = c.terminated ? NULL : type_builtin(p, T_NIL);
        }
        result = error_join(p, result, c.propagated, in->fn->loc);
    }
    if (!result && !c.value_return) {
        result = type_builtin(p, T_VOID);
    }
    if (in->fn->yields) {
        return coroutine_result(p, in, result, c.yielded, c.received);
    }
    Type *contract =
        in->fn->result ? function_result(p, in, in->fn->result) : in->fn->required_result;
    if (contract && result && !callable_type(contract) && !in->fn->procedure && !in->fn->external &&
        !read_contract(in->fn->result)) {
        result = result_capabilities(p, contract, result, in->fn->loc);
        contract = NULL;
    }
    if (callable_type(contract) && result && !callable_accepts(contract, result)) {
        fail(in->fn->loc, "callable result does not satisfy '%s'", type_name(contract));
    }
    if ((!contract || callable_type(contract)) && result && result != in->result) {
        if (in->state) {
            fail(in->fn->loc, "return inference changed after code generation");
        }
        in->result = result;
        changed = true;
    }
    return changed;
}

void infer_results(Program *p)
{
    if (p->inferred_instances == p->instances.n) {
        return;
    }
    // At a fixed point, all dependencies of these instances already exist.
    // New call shapes cannot change their immutable parameter/result graph.
    int first = p->inferred_instances;
    for (int round = 0; round < 1024; round++) {
        bool changed = false;
        int count = p->instances.n;
        for (int j = first; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            ToolRecovery recovery;
            tool_enter(&recovery);
            recovery.instance = in;
            if (!setjmp(recovery.escape)) {
                changed |= infer_instance_result(p, in);
            }
            tool_leave(&recovery);
        }
        if (count != p->instances.n) {
            changed = true;
        }
        if (!changed) {
            p->inferred_instances = p->instances.n;
            return;
        }
    }
    fail((Loc){p->path, 1, 1}, "return type inference did not converge");
}

Type *handler_errors(Context *c, Stmt *s)
{
    Infer infer = infer_context(c);
    ErrorTarget target = {0};
    add(&infer.handlers, &target);
    infer_statements(&infer, &s->body, false);
    return target.errors;
}

Type *chain_type(Context *c, Expr *e)
{
    Infer in = infer_context(c);
    return infer_chain(&in, e, true);
}

Type *chain_place_type(Context *c, Expr *e)
{
    Infer in = infer_context(c);
    return infer_chain(&in, e, false);
}

bool type_signature(Expr *e)
{
    return e->kind == E_MEMBER && e->a && e->a->kind == E_TYPE && !strcmp(e->text, "signature^");
}

// A function whose parameter types are left to call sites has no single type.
static bool open_callable(Type *t)
{
    for (int j = 0; t->kind == T_INTERSECTION && j < t->arms.n; j++) {
        if (open_callable(t->arms.v[j])) {
            return true;
        }
    }
    return t->kind == T_FUNCTION && t->function && inferred_parameter(t->function);
}

// The typeof^ operand emits no code; still show editors which locals it names.
static void record_operand(Context *c, Expr *e)
{
    if (!e) {
        return;
    }
    Var *v = (e->kind == E_NAME || e->kind == E_SELF) && e->text ? selected(c, e) : NULL;
    if (v) {
        tool_record(c->p, e->loc, v->declaration, v->name, v->type, NULL, NULL,
                    v->parameter ? TOOL_PARAMETER
                    : v->mut     ? 13
                                 : 14,
                    false);
    }
    record_operand(c, e->a);
    record_operand(c, e->b);
    record_operand(c, e->c);
    for (int j = 0; j < e->args.n; j++) {
        record_operand(c, e->args.v[j]);
    }
}

bool dependent_spec(Spec *s)
{
    if (!s) {
        return false;
    }
    bool found = s->kind == SPEC_TYPEOF || dependent_spec(s->elem) || dependent_spec(s->other);
    for (int j = 0; !found && j < s->params.n; j++) {
        found = dependent_spec(s->params.v[j]);
    }
    for (int j = 0; !found && j < s->type_args.n; j++) {
        found = dependent_spec(s->type_args.v[j]);
    }
    return found;
}

// Specs are shared by every specialization, so a typeof^ inside one is fixed
// on a private copy rather than in the spec's cached type.
static Spec *fixed_spec(Context *c, Spec *s)
{
    if (!dependent_spec(s)) {
        return s;
    }
    Spec *r = mem(sizeof(*r));
    *r = *s;
    r->type = NULL;
    if (s->pins) {
        for (int j = 0; j < s->pins->n; j += 2) {
            if (s->pins->v[j] == c->in) {
                r->type = s->pins->v[j + 1];
                return r;
            }
        }
        fail(s->loc, "type '%s' is used before its binding", s->name);
    }
    if (s->kind == SPEC_TYPEOF) {
        r->type = expression_type(c, s->operand, NULL);
        // A typeof-only call may discover a capability specialization during lowering.
        if (!r->type && c->in && c->in->state > 0) {
            infer_results(c->p);
            r->type = expression_type(c, s->operand, NULL);
        }
        record_operand(c, s->operand);
        if (!r->type || r->type->kind == T_NEVER) {
            fail(s->loc, "typeof^ cannot determine the type of its operand");
        }
        if (open_callable(r->type)) {
            fail(s->loc, "typeof^ of a function with parameters left to call sites; each "
                         "specialization has its own type");
        }
        return r;
    }
    r->elem = fixed_spec(c, s->elem);
    r->other = fixed_spec(c, s->other);
    r->params = r->type_args = (List){0};
    for (int j = 0; j < s->params.n; j++) {
        add(&r->params, fixed_spec(c, s->params.v[j]));
    }
    for (int j = 0; j < s->type_args.n; j++) {
        add(&r->type_args, fixed_spec(c, s->type_args.v[j]));
    }
    return r;
}

Type *context_type(Context *c, Spec *s)
{
    return s ? type_spec(c->p, fixed_spec(c, s)) : NULL;
}

void pin_local_type(Context *c, Spec *s)
{
    if (!s->pins) {
        return;
    }
    Type *t = context_type(c, s->elem);
    for (int j = 0; j < s->pins->n; j += 2) {
        if (s->pins->v[j] == c->in) {
            s->pins->v[j + 1] = t;
            return;
        }
    }
    add(s->pins, c->in);
    add(s->pins, t);
}

bool type_comparison(Expr *e)
{
    return e->kind == E_BIN && (!strcmp(e->text, "=") || !strcmp(e->text, "!=")) &&
           e->a->kind == E_TYPE && e->b->kind == E_TYPE;
}

bool compare_types(Context *c, NilFlow *flow, Expr *e)
{
    bool same = context_type(c, e->a->type_expr) == context_type(c, e->b->type_expr);
    bool truth = same == !strcmp(e->text, "=");
    // Like a static fits^, a decided comparison prunes the branch it rules out.
    Type *known = type_builtin(c->p, T_BOOL);
    remember_test(flow, c, e, e, truth ? known : NULL, truth ? NULL : known);
    return truth;
}

// Annotations inside an inferred body may name typeof^ types.
static Type *infer_type(Infer *c, Spec *s)
{
    Context scope = {.p = c->p, .in = c->in, .vars = c->vars, .lexical = c->lexical};
    return context_type(&scope, s);
}

Type *expression_type(Context *c, Expr *e, Type *expected)
{
    Infer inference = infer_context(c);
    return infer_expr(&inference, e, expected);
}

Type *function_argument_type(Context *c, Function *f, Param *param, Expr *arg)
{
    Infer inference = infer_context(c);
    Type *type = infer_expr(&inference, arg, parameter_hint(c->p, param, arg));
    return pure_argument_type(&inference, f, arg, type);
}

Instance *instance_for(Program *p, Function *f, Def *self, List *actual, Loc at)
{
    if (f->invalid || (self && self->invalid)) {
        tool_abort();
    }
    List arguments = {0};
    for (int j = 0; j < actual->n; j++) {
        add(&arguments, actual->v[j]);
    }
    for (int j = 0; j < f->captures.n; j++) {
        Param *capture = f->captures.v[j];
        if (!capture->inferred) {
            fail(at, "capture '%s' requires its enclosing call", capture->capture->name);
        }
        add(&arguments, capture->inferred);
    }
    actual = &arguments;
    for (int j = 0; j < f->instances.n; j++) {
        Instance *in = f->instances.v[j];
        if (in->self != self || in->params.n != actual->n) {
            continue;
        }
        bool same = true;
        for (int k = 0; k < actual->n; k++) {
            if (in->params.v[k] != actual->v[k]) {
                same = false;
            }
        }
        if (same) {
            tool_instance_use(in, at);
            if (in->invalid && !in->result) {
                tool_abort();
            }
            return in;
        }
    }
    if (p->instances.n >= 512) {
        fail(at, "function specialization limit exceeded");
    }
    Instance *in = mem(sizeof(*in));
    in->fn = f;
    in->self = self;
    in->params = *actual;
    in->result = f->result ? function_result(p, in, f->result) : f->required_result;
    if (!f->procedure && !f->external && !f->abstract && in->result && type_writes(in->result) &&
        !read_contract(f->result)) {
        in->result = NULL;
    }
    in->id = p->instances.n;
    in->symbol = fmt("rh_fn_%d", in->id);
    if (f->external) {
        in->symbol = f->external;
    }
    else if (p->modules.n > 1 && f != p->entry) {
        char *identity = fmt("%s:%s:%s", f->module && f->module->name ? f->module->name : "<entry>",
                             f->name, self ? self->name : "");
        for (int j = 0; j < actual->n; j++) {
            identity = fmt("%s:%s", identity, callable_identity(actual->v[j]));
        }
        in->symbol = fmt("rh_fn_%s", link_identity(identity));
    }
    in->result_slot = -1;
    add(&f->instances, in);
    add(&p->instances, in);
    tool_instance_use(in, at);
    return in;
}

Type *parameter_type(Program *p, Param *param)
{
    return !param ? NULL : param->spec ? type_spec(p, param->spec) : param->inferred;
}

Type *function_parameter(Program *p, Function *f, Param *param, Type *actual)
{
    Type *want = parameter_type(p, param);
    if (want && actual && !f->procedure && !(f->mutable_self && param->self)) {
        return result_capabilities(p, want, actual, param->loc);
    }
    return want;
}

bool read_contract(Spec *s)
{
    if (!s) {
        return false;
    }
    if (s->read_from.n || read_contract(s->elem) || read_contract(s->other)) {
        return true;
    }
    for (int j = 0; j < s->params.n; j++) {
        if (read_contract(s->params.v[j])) {
            return true;
        }
    }
    return false;
}

static int read_source(Function *f, const char *name, Loc loc)
{
    for (int j = 0; j < f->params.n; j++) {
        if (!strcmp(((Param *)f->params.v[j])->name, name)) {
            return j;
        }
    }
    fail(loc, "read^(%s) names no parameter of '%s'", name, f->name);
}

Type *function_result(Program *p, Instance *in, Spec *s)
{
    Type *t = type_spec(p, s);
    if (!s) {
        return t;
    }
    if (read_contract(s)) {
        for (int j = 0; j < in->params.n; j++) {
            if (!in->params.v[j]) {
                return NULL;
            }
        }
    }
    if ((s->kind == SPEC_REF || s->kind == SPEC_SLICE) && read_contract(s->elem)) {
        Type *elem = function_result(p, in, s->elem);
        if (!elem) {
            return NULL;
        }
        t = type_wrap(p, t->kind, elem, t->count);
    }
    if (s->read_from.n) {
        bool read = false;
        for (int j = 0; j < s->read_from.n; j++) {
            int k = read_source(in->fn, s->read_from.v[j], s->loc);
            if (k >= in->params.n || !in->params.v[k]) {
                return NULL;
            }
            Type *param = in->params.v[k];
            if (param->kind != T_REF && param->kind != T_SLICE && param->kind != T_DYN) {
                fail(s->loc, "read^(%s) requires a reference or slice parameter",
                     (char *)s->read_from.v[j]);
            }
            read |= read_reference(param);
        }
        return type_wrap(p, t->kind, t->elem, read);
    }
    if (s->kind == SPEC_UNION) {
        Type *a = function_result(p, in, s->elem), *b = function_result(p, in, s->other);
        return a && b ? type_union(p, a, b, s->loc) : NULL;
    }
    if (s->kind == SPEC_TUPLE) {
        List parts = {0};
        for (int j = 0; j < s->params.n; j++) {
            Type *part = function_result(p, in, s->params.v[j]);
            if (!part) {
                return NULL;
            }
            add(&parts, part);
        }
        return type_tuple(p, &parts, s->loc);
    }
    if (in->fn->external && !in->fn->procedure &&
        (t->kind == T_REF || t->kind == T_SLICE || t->kind == T_DYN) && !read_reference(t)) {
        fail(
            s->loc,
            "cannot infer reference result capability without a body; add read^(parameter) or read^ to the result");
    }
    return t;
}

Type *result_capabilities(Program *p, Type *contract, Type *observed, Loc loc)
{
    if (!contract || !observed) {
        return contract;
    }
    if ((contract->kind == T_REF || contract->kind == T_SLICE || contract->kind == T_DYN) &&
        read_reference(observed)) {
        Type *read = type_wrap(p, contract->kind, contract->elem, 1);
        if (parameter_accepts(read, observed)) {
            return read;
        }
    }
    if ((contract->kind == T_REF || contract->kind == T_SLICE || contract->kind == T_DYN) &&
        contract->kind == observed->kind && contract->elem == observed->elem) {
        return type_wrap(p, contract->kind, contract->elem,
                         read_reference(contract) || read_reference(observed));
    }
    if (contract->kind == T_STRUCT && contract->def->tuple && observed->kind == T_STRUCT &&
        observed->def->tuple && contract->def->fields.n == observed->def->fields.n) {
        List parts = {0};
        for (int j = 0; j < contract->def->fields.n; j++) {
            add(&parts, result_capabilities(p, ((Field *)contract->def->fields.v[j])->type,
                                            ((Field *)observed->def->fields.v[j])->type, loc));
        }
        return type_tuple(p, &parts, loc);
    }
    if (contract->kind == T_UNION) {
        Type *out = NULL;
        bool changed = false;
        for (int j = 0; j < contract->def->alternatives.n; j++) {
            Type *arm = contract->def->alternatives.v[j];
            if (observed->kind == T_UNION) {
                for (int k = 0; k < observed->def->alternatives.n; k++) {
                    arm = result_capabilities(p, arm, observed->def->alternatives.v[k], loc);
                }
            }
            else {
                arm = result_capabilities(p, arm, observed, loc);
            }
            changed |= arm != contract->def->alternatives.v[j];
            out = out ? type_union(p, out, arm, loc) : arm;
        }
        return changed ? out : contract;
    }
    return contract;
}

bool parameter_accepts(Type *want, Type *actual)
{
    if (want == actual || callable_accepts(want, actual) || c_function_arm(want, actual)) {
        return true;
    }
    if (sum_accepts(want, actual)) {
        return true;
    }
    if (want->kind == T_DYN) {
        return (!read_reference(actual) || read_reference(want)) && actual->kind == T_REF &&
               actual->elem->kind == T_STRUCT && !actual->elem->def->abstract &&
               derives(actual->elem->def, want->elem->def);
    }
    if (want->kind == T_REF && actual->kind == T_REF) {
        if (read_reference(actual) && !read_reference(want)) {
            return false;
        }
        want = want->elem;
        actual = actual->elem;
    }
    return want->kind == T_STRUCT && actual->kind == T_STRUCT && derives(actual->def, want->def);
}

Type *parameter_hint(Program *p, Param *param, Expr *arg)
{
    Type *t = parameter_type(p, param);
    if (!t || param->spec || arg->kind == E_IF) {
        return t;
    }
    Expr *literal = arg;
    if (literal->kind == E_UNARY && !strcmp(literal->text, "-")) {
        literal = literal->a;
    }
    if (literal->kind == E_INT || literal->kind == E_FLOAT || literal->kind == E_ARRAY ||
        (literal->kind == E_RECORD && !literal->text)) {
        return t->kind == T_UNION ? union_literal_hint(t, arg) : t;
    }
    return NULL;
}

bool declaration_instance_needed(Program *p, Function *f)
{
    if (f->public || f->exported || f->lhat_export || (f->owner && f->owner->public && !f->priv)) {
        return true;
    }
    bool inferred = false;
    for (int j = 0; j < f->params.n; j++) {
        inferred |= ((Param *)f->params.v[j])->inferred != NULL;
    }
    if (inferred) {
        for (int j = 0; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            if (in->fn == f && in->state == 2) {
                return false;
            }
        }
    }
    return true;
}

// Body requirements never inspect concrete call instances. Unknown nominal
// receivers remain unknown; a member name does not identify a struct.
typedef struct BodyNode BodyNode;

typedef struct {
    Param *param;
    List required;
    uint64_t kinds;
} BodyParam;

struct BodyNode {
    Type *type;
    bool readonly;
    BodyParam *param;
    BodyNode *a, *b;
    int kind;
    const char *op;
    List children;
};

typedef struct {
    Program *p;
    Function *fn;
    Context scope;
    List nodes, params;
    Expr *raw_place;
    bool blocked, ended;
} BodyCheck;

static uint64_t body_numbers(bool integers)
{
    uint64_t mask = 0;
    for (int k = T_U8; k <= T_F64; k++) {
        if (!integers || kind_integer(k)) {
            mask |= UINT64_C(1) << k;
        }
    }
    return mask;
}

static BodyNode *body_node(Type *t)
{
    BodyNode *n = mem(sizeof(*n));
    n->type = t;
    n->kind = -1;
    return n;
}

static void body_require(BodyCheck *c, BodyNode *n, Type *want, uint64_t kinds)
{
    if (!n || c->blocked) {
        return;
    }
    if (n->param) {
        n->param->kinds &= kinds;
        if (want) {
            for (int j = 0; j < n->param->required.n; j++) {
                if (n->param->required.v[j] == want) {
                    return;
                }
            }
            add(&n->param->required, want);
        }
        return;
    }
    if (n->kind == E_UNARY) {
        if (!strcmp(n->op, "ref^") || !strcmp(n->op, "read^")) {
            if (want && want->kind == T_REF) {
                body_require(c, n->a, want->elem, UINT64_MAX);
            }
        }
        else if (!strcmp(n->op, "*")) {
            if (want) {
                body_require(c, n->a, type_wrap(c->p, T_REF, want, 0), UINT64_MAX);
            }
        }
        else {
            body_require(c, n->a, want, kinds);
        }
    }
    else if (n->kind == E_BIN) {
        body_require(c, n->a, want, kinds);
        if (strcmp(n->op, "bitshift^")) {
            body_require(c, n->b, want, kinds);
        }
    }
    else if (n->kind == E_ARRAY && want &&
             (want->kind == T_ARRAY || want->kind == T_GROW || want->kind == T_BOUNDED ||
              want->kind == T_SLICE)) {
        for (int j = 0; j < n->children.n; j++) {
            body_require(c, n->children.v[j], want->elem, UINT64_MAX);
        }
    }
}

static BodyNode *body_expr(BodyCheck *c, Expr *e, Type *want);
static void body_statements(BodyCheck *c, List *list, bool scoped);

static void body_bind(BodyCheck *c, const char *name, BodyNode *n)
{
    Var *v = mem(sizeof(*v));
    v->name = (char *)name;
    v->lexical = c->scope.lexical;
    v->type = n->type;
    add(&c->scope.vars, v);
    add(&c->nodes, n);
}

static BodyNode *body_expr(BodyCheck *c, Expr *e, Type *want)
{
    if (e && e->invalid) {
        tool_abort();
    }
    BodyNode *n = body_node(NULL);
    if (!e || c->blocked) {
        return n;
    }
    empty_array_braces(e, want);
    n->kind = e->kind;
    n->op = e->text;
    if (e->optional) {
        body_expr(c, e->a, NULL);
        c->blocked = true;
        return n;
    }
    switch (e->kind) {
    case E_STATE:
    case E_DEFAULT:
        n->type = state_field_type(c->p, e->state_field);
        break;
    case E_NAME:
    case E_SELF: {
        Var *v = selected(&c->scope, e);
        if (v) {
            for (int j = 0; j < c->scope.vars.n; j++) {
                if (v == c->scope.vars.v[j]) {
                    n = c->nodes.v[j];
                    break;
                }
            }
        }
        else if (e->kind == E_NAME && (n->type = callable_group(c->p, NULL, e->text))) {
            break;
        }
        else if (e->text && !strcmp(e->text, "nil^")) {
            n->type = type_builtin(c->p, T_NIL);
        }
        else if (e->constant_spec) {
            n->type = type_spec(c->p, e->constant_spec);
        }
        break;
    }
    case E_BOOL:
        n->type = type_builtin(c->p, T_BOOL);
        break;
    case E_STRING:
        n->type = type_builtin(c->p, T_STRING);
        break;
    case E_INT:
    case E_FLOAT:
        // Unbound numeric literals are contextual, not evidence of a width.
        n->type = e->constant_spec ? type_spec(c->p, e->constant_spec) : want;
        break;
    case E_LAYOUT:
        n->type = type_builtin(c->p, T_I64);
        break;
    case E_BYTESOF:
        n->a = body_expr(c, e->a, NULL);
        n->type = n->a->type ? byte_view_type(c->p, n->a->type, e->loc)
                             : type_wrap(c->p, T_SLICE, type_builtin(c->p, T_U8), 1);
        n->kind = -1;
        break;
    case E_UNARY: {
        if (e->constant_spec) {
            n->type = type_spec(c->p, e->constant_spec);
            break;
        }
        Expr *old_place = c->raw_place;
        if (!strcmp(e->text, "ref^") || !strcmp(e->text, "read^")) {
            c->raw_place = e->a;
        }
        n->a = body_expr(c, e->a, NULL);
        c->raw_place = old_place;
        if (!strcmp(e->text, "!")) {
            body_require(c, n->a, type_builtin(c->p, T_BOOL), UINT64_MAX);
            n->type = type_builtin(c->p, T_BOOL);
            n->kind = -1;
        }
        else if (!strcmp(e->text, "ref^") || !strcmp(e->text, "read^")) {
            if (n->a->type) {
                n->type =
                    type_wrap(c->p, T_REF, n->a->type, !strcmp(e->text, "read^") || n->a->readonly);
            }
        }
        else if (!strcmp(e->text, "*")) {
            if (n->a->type && n->a->type->kind == T_REF) {
                n->type = n->a->type->elem;
                n->readonly = n->a->readonly || read_reference(n->a->type);
            }
        }
        else if (!strcmp(e->text, "-") && n->a->type &&
                 (n->a->type->kind == T_STRUCT || n->a->type->kind == T_REF)) {
            Function *op = operator_select(c->p, "-", n->a->type, NULL, e->loc);
            n->type = op && op->result ? type_spec(c->p, op->result) : NULL;
        }
        else {
            body_require(c, n->a, NULL, body_numbers(!strcmp(e->text, "bitnot^")));
            n->type = n->a->type;
        }
        break;
    }
    case E_BIN: {
        if (type_comparison(e)) {
            // Each specialization decides it, so later demands are conditional.
            n->type = type_builtin(c->p, T_BOOL);
            c->blocked = true;
            break;
        }
        bool logical = !strcmp(e->text, "and^") || !strcmp(e->text, "or^") ||
                       !strcmp(e->text, "&&") || !strcmp(e->text, "||");
        bool compare = !strcmp(e->text, "=") || !strcmp(e->text, "!=") || !strcmp(e->text, "<") ||
                       !strcmp(e->text, ">") || !strcmp(e->text, "<=") || !strcmp(e->text, ">=");
        n->a = body_expr(c, e->a, NULL);
        n->b = body_expr(c, e->b, NULL);
        Type *a = n->a->type, *b = n->b->type;
        Type *receiver = a && a->kind == T_REF ? a->elem : a;
        Type *right = b && b->kind == T_REF ? b->elem : b;
        if ((receiver && receiver->kind == T_STRUCT) || (right && right->kind == T_STRUCT)) {
            Type *left_hint = operator_hint(c->p, e->text, b, true, e->a);
            if (left_hint && !a) {
                body_require(c, n->a, left_hint, UINT64_MAX);
                a = left_hint;
            }
            Type *hint = operator_hint(c->p, e->text, a, false, e->b);
            if (hint) {
                body_require(c, n->b, hint, UINT64_MAX);
                b = hint;
            }
            Function *op = operator_select(c->p, e->text, a, b, e->loc);
            n->type = compare            ? type_builtin(c->p, T_BOOL)
                      : op && op->result ? type_spec(c->p, op->result)
                                         : NULL;
            break;
        }
        if (logical) {
            a = b = type_builtin(c->p, T_BOOL);
        }
        uint64_t mask = logical ? UINT64_MAX : body_numbers(!strncmp(e->text, "bit", 3));
        if (compare && ((!strcmp(e->text, "=") || !strcmp(e->text, "!=")))) {
            mask |= UINT64_C(1) << T_BOOL;
            if ((a && a->kind == T_NIL) || (b && b->kind == T_NIL)) {
                c->blocked = true;
            }
        }
        body_require(c, n->a, !strcmp(e->text, "bitshift^") ? NULL : b, mask);
        body_require(c, n->b, !strcmp(e->text, "bitshift^") ? NULL : a, mask);
        n->type = logical || compare ? type_builtin(c->p, T_BOOL) : a ? a : b;
        if (logical || compare) {
            n->kind = -1;
        }
        break;
    }
    case E_CHAIN: {
        BodyNode *previous = body_expr(c, e->args.v[0], NULL);
        for (int j = 1; j < e->args.n; j++) {
            const char *op = e->names.v[j - 1];
            if (!strcmp(op, "fits^")) {
                c->blocked = true;
                break;
            }
            BodyNode *next = body_expr(c, e->args.v[j], NULL);
            if ((previous->type && previous->type->kind == T_NIL) ||
                (next->type && next->type->kind == T_NIL)) {
                c->blocked = true;
                break;
            }
            uint64_t mask = body_numbers(false);
            if (!strcmp(op, "=") || !strcmp(op, "!=")) {
                mask |= UINT64_C(1) << T_BOOL;
            }
            body_require(c, previous, next->type, mask);
            body_require(c, next, previous->type, mask);
            previous = next;
        }
        n->type = type_builtin(c->p, T_BOOL);
        break;
    }
    case E_MEMBER: {
        if (type_signature(e)) {
            n->type = type_builtin(c->p, T_STRING);
            break;
        }
        n->a = body_expr(c, e->a, NULL);
        Type *base = n->a->type;
        n->readonly = n->a->readonly || (base && read_reference(base));
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        base = error_view(base, e->text);
        if (error_site(base, e->text) >= 0) {
            n->type = type_builtin(c->p, error_site(base, e->text) % 3 ? T_U32 : T_STRING);
        }
        else if (base && base->kind == T_STRUCT) {
            Field *f = field(base->def, e->text);
            n->type = f ? f->type : NULL;
        }
        else if (base && !strcmp(e->text, "len")) {
            n->type = type_builtin(c->p, T_I64);
        }
        break;
    }
    case E_SLICE: {
        n->a = body_expr(c, e->a, NULL);
        body_expr(c, e->b, type_builtin(c->p, T_I64));
        body_expr(c, e->c, type_builtin(c->p, T_I64));
        Type *base = n->a->type;
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        if (base && base->kind == T_STRING) {
            n->type = type_wrap(c->p, T_SLICE, type_builtin(c->p, T_U8), 1);
        }
        else if (base && (base->kind == T_ARRAY || base->kind == T_GROW ||
                          base->kind == T_BOUNDED || base->kind == T_SLICE)) {
            n->type = type_wrap(c->p, T_SLICE, base->elem, read_reference(base) || n->a->readonly);
        }
        break;
    }
    case E_INDEX: {
        n->a = body_expr(c, e->a, NULL);
        BodyNode *index = body_expr(c, e->b, NULL);
        body_require(c, index, NULL, body_numbers(true));
        Type *base = n->a->type;
        n->readonly = n->a->readonly || (base && read_reference(base));
        if (base && base->kind == T_REF) {
            base = base->elem;
        }
        if (base && (base->kind == T_ARRAY || base->kind == T_GROW || base->kind == T_BOUNDED ||
                     base->kind == T_SLICE)) {
            layout(base, e->loc);
            n->type = base->elem->owned && c->raw_place != e
                          ? type_wrap(c->p, T_REF, base->elem, n->readonly)
                          : base->elem;
        }
        break;
    }
    case E_CALL: {
        Function *f = NULL;
        List candidates = {0}, actual = {0}, arguments = {0};
        int offset = 0;
        Type *cast = NULL, *callable = NULL;
        if (e->a->kind == E_NAME) {
            candidates = overload_candidates(c->p, NULL, e->a->text, e->args.n);
            f = candidates.n ? candidates.v[0] : NULL;
            Var *binding = selected(&c->scope, e->a);
            if (binding && callable_type(binding->type)) {
                callable = binding->type;
                candidates = callable_candidates(callable, e->args.n);
                f = candidates.n ? candidates.v[0] : NULL;
            }
            for (int k = T_BOOL; k <= T_STRING; k++) {
                if (!strcmp(e->a->text, builtin_name(k))) {
                    cast = type_builtin(c->p, k);
                }
            }
        }
        else if (e->a->kind == E_FUNCTION) {
            f = e->a->function;
        }
        else if (static_owner(c->p, e->a)) {
            candidates = overload_candidates(c->p, static_owner(c->p, e->a), e->a->text, e->args.n);
            f = candidates.n ? candidates.v[0] : NULL;
        }
        else if (e->a->kind == E_MEMBER) {
            BodyNode *receiver = body_expr(c, e->a->a, NULL);
            Type *base = receiver->type;
            bool read = receiver->readonly || (base && read_reference(base));
            if (base && base->kind == T_REF) {
                base = base->elem;
            }
            if (base && base->kind == T_STRUCT) {
                candidates = overload_candidates(c->p, base->def, e->a->text, e->args.n + 1);
                f = candidates.n ? candidates.v[0] : method(base->def, e->a->text);
                offset = 1;
                add(&actual, type_wrap(c->p, T_REF, base, read));
            }
            else if (base && (base->kind == T_GROW || base->kind == T_BOUNDED ||
                              base->kind == T_ARRAY || base->kind == T_SLICE)) {
                if (!strcmp(e->a->text, "push") && e->args.n == 1) {
                    body_expr(c, e->args.v[0], base->elem);
                    return n;
                }
            }
        }
        e = pack_arguments(f, e, offset);
        for (int j = 0; j < e->args.n; j++) {
            Param *p = f && j + offset < f->params.n ? f->params.v[j + offset] : NULL;
            Type *hint = candidates.n > 1
                             ? overload_hint(c->p, &candidates, j + offset, e->args.v[j])
                         : callable ? callable_hint(callable, e->args.n, j)
                                    : parameter_type(c->p, p);
            if (e->a->kind == E_NAME && !strcmp(e->a->text, "assert^")) {
                hint = type_builtin(c->p, T_BOOL);
            }
            BodyNode *arg = body_expr(c, e->args.v[j], hint);
            add(&actual, arg->type);
            add(&arguments, arg);
            if (cast) {
                body_require(c, arg, NULL, body_numbers(false));
            }
        }
        if (candidates.n > 1) {
            f = overload_select(c->p, &candidates, &actual, e, offset, want, e->loc, false);
            for (int j = 0; f && j < arguments.n; j++) {
                body_require(c, arguments.v[j], parameter_type(c->p, f->params.v[j + offset]),
                             UINT64_MAX);
            }
        }
        Type *contract = callable_contract(callable, &actual);
        if (contract) {
            for (int j = 0; j < arguments.n; j++) {
                body_require(c, arguments.v[j], contract->params.v[j], UINT64_MAX);
            }
        }
        n->type = contract         ? contract->result
                  : cast           ? cast
                  : f && f->result ? type_spec(c->p, f->result)
                  : f              ? f->required_result
                                   : NULL;
        break;
    }
    case E_FUNCTION:
        n->type = callable_declaration(c->p, e->function);
        break;
    case E_RECORD: {
        Def *d = e->text ? lookup_def(c->p, e->text) : NULL;
        n->type = d ? d->type : want;
        for (int j = 0; j < e->args.n; j++) {
            Field *f =
                n->type && n->type->kind == T_STRUCT ? field(n->type->def, e->names.v[j]) : NULL;
            body_expr(c, e->args.v[j], f ? f->type : NULL);
        }
        break;
    }
    case E_PANIC:
    case E_YIELD:
        body_expr(c, e->a, NULL);
        break;
    case E_LOOP_PART:
        body_expr(c, e->a, NULL);
        break;
    case E_RANGE: {
        Type *elem = range_type(want) ? want->elem : NULL;
        body_expr(c, e->a, elem);
        body_expr(c, e->b, elem);
        n->type = range_type(want) ? want : NULL;
        break;
    }
    case E_PACK:
        if (e->a) {
            body_expr(c, e->a, NULL);
        }
        for (int j = 0; !e->a && j < e->args.n; j++) {
            body_expr(c, e->args.v[j], want ? want->elem : NULL);
        }
        n->type = want;
        break;
    case E_ARRAY:
        if (e->text && !strcmp(e->text, "copy")) {
            BodyNode *source = body_expr(c, e->args.v[0], NULL);
            n->type = source->type ? array_literal_type(c->p, e, want, source->type) : want;
            break;
        }
        n->type = array_literal_type(c->p, e, want, NULL);
        if (e->text) {
            for (int j = 0; j < e->args.n; j++) {
                BodyNode *arg = body_expr(c, e->args.v[j], array_form_hint(c->p, e, n->type, j));
                if (!n->type && !want && !j) {
                    n->type = repeat_type(c->p, e, arg->type);
                }
            }
            break;
        }
        for (int j = 0; j < e->args.n; j++) {
            Type *hint = n->type && !e->array_refs ? n->type->elem : NULL;
            BodyNode *child = body_expr(c, e->args.v[j], hint);
            Type *actual = child->type;
            if (e->array_refs && actual && actual->kind != T_REF && actual->kind != T_DYN) {
                actual = type_wrap(c->p, T_REF, actual, 0);
            }
            if (!n->type && actual) {
                n->type = array_literal_type(c->p, e, want, actual);
            }
            if (!e->array_refs) {
                add(&n->children, child);
            }
        }
        break;
    case E_IF:
        body_expr(c, e->a, type_builtin(c->p, T_BOOL));
        body_expr(c, e->b, want);
        body_expr(c, e->c, want);
        n->type = want;
        break;
    case E_FITS:
    case E_PRESENT:
    case E_TRY:
    case E_CATCH:
    case E_COALESCE:
        n->a = body_expr(c, e->a, NULL);
        // Type-dependent transfers may make every following statement unreachable
        // in some valid specializations. Keep only unconditional prefix demands.
        c->blocked = true;
        break;
    case E_EXPECT:
        // Counted-loop literals take their width from the bound at instantiation.
        body_expr(c, e->a, NULL);
        body_expr(c, e->b, NULL);
        break;
    case E_SCOPE: {
        int mark = c->scope.vars.n;
        c->scope.lexical++;
        body_statements(c, &e->bindings, false);
        n = body_expr(c, e->a, want);
        c->scope.lexical--;
        c->scope.vars.n = c->nodes.n = mark;
    } break;
    default:
        break;
    }
    body_require(c, n, want, UINT64_MAX);
    return n;
}

static void body_statements(BodyCheck *c, List *list, bool scoped)
{
    int mark = c->scope.vars.n;
    if (scoped) {
        c->scope.lexical++;
    }
    for (int j = 0; j < list->n && !c->blocked && !c->ended; j++) {
        Stmt *s = list->v[j];
        if (s->kind == S_BIND) {
            Type *hint = s->spec && !dependent_spec(s->spec) ? type_spec(c->p, s->spec) : NULL;
            BodyNode *n = body_expr(c, s->a, hint);
            if (hint && !callable_type(n->type)) {
                n = body_node(hint);
            }
            else if (s->a && !s->a->constant_spec &&
                     (s->a->kind == E_INT || s->a->kind == E_FLOAT)) {
                n = body_node(type_builtin(c->p, s->a->kind == E_INT ? T_I32 : T_F64));
            }
            body_bind(c, s->name, n);
        }
        else if (s->kind == S_BIND_MANY) {
            body_statements(c, &s->prolog, false);
            List pending = {0};
            for (int k = 0; k < s->body.n; k++) {
                Stmt *b = s->body.v[k];
                Type *hint = b->spec && !dependent_spec(b->spec) ? type_spec(c->p, b->spec) : NULL;
                BodyNode *n = body_expr(c, b->a, hint);
                if (hint) {
                    n = body_node(hint);
                }
                else if (b->a && !b->a->constant_spec &&
                         (b->a->kind == E_INT || b->a->kind == E_FLOAT)) {
                    n = body_node(type_builtin(c->p, b->a->kind == E_INT ? T_I32 : T_F64));
                }
                add(&pending, n);
            }
            for (int k = 0; k < s->body.n; k++) {
                body_bind(c, ((Stmt *)s->body.v[k])->name, pending.v[k]);
            }
        }
        else if (s->kind == S_RETURN || s->kind == S_EXPR) {
            Type *hint = s->kind == S_RETURN ? c->fn->result ? type_spec(c->p, c->fn->result)
                                                             : c->fn->required_result
                                             : NULL;
            body_expr(c, s->a, hint);
            c->ended = s->kind == S_RETURN;
        }
        else if (s->kind == S_ASSIGN) {
            if (s->optional) {
                c->blocked = true;
                continue;
            }
            int count = s->targets.n ? s->targets.n : 1;
            for (int k = 0; k < count; k++) {
                BodyNode *dst = body_expr(c, s->targets.n ? s->targets.v[k] : s->a, NULL);
                body_expr(c, s->values.n ? s->values.v[k] : s->b, dst->type);
            }
        }
        else if (s->kind == S_IF) {
            body_expr(c, s->a, type_builtin(c->p, T_BOOL));
            body_statements(c, &s->body, true);
            bool first = c->ended;
            c->ended = false;
            body_statements(c, &s->other, true);
            c->ended &= first;
            if (s->internal) {
                c->scope.lexical++;
            }
            body_statements(c, &s->update, true);
        }
        else if (s->kind == S_INLINE) {
            body_expr(c, s->a, NULL);
            // The element's shape belongs to the concrete specialization.
            c->blocked = true;
        }
        else if (s->kind == S_WHILE) {
            int loop_mark = c->scope.vars.n;
            int lexical = c->scope.lexical;
            body_statements(c, &s->prolog, false);
            body_statements(c, &s->pre, false);
            body_expr(c, s->a, type_builtin(c->p, T_BOOL));
            body_statements(c, &s->first, false);
            c->scope.lexical--;
            body_statements(c, &s->body, true);
            c->ended = false;
            body_statements(c, &s->update, true);
            body_statements(c, &s->last, true);
            body_statements(c, &s->epilog, true);
            c->ended = false;
            c->scope.lexical = lexical;
            c->scope.vars.n = c->nodes.n = loop_mark;
        }
        else if (s->kind == S_BREAK || s->kind == S_NEXT) {
            c->ended = true;
        }
        else {
            body_statements(c, &s->body, s->kind != S_BIND_MANY);
        }
    }
    if (scoped) {
        c->scope.lexical--;
        c->scope.vars.n = c->nodes.n = mark;
    }
}

static void body_candidates(List *out, Type *t)
{
    add(out, t);
    if (t->kind == T_UNION) {
        for (int j = 0; j < t->def->alternatives.n; j++) {
            body_candidates(out, t->def->alternatives.v[j]);
        }
    }
}

static bool body_function_unchecked(Program *p, Function *f)
{
    if (f->templ || f->abstract) {
        return false;
    }
    bool untyped = false;
    for (int j = 0; j < f->params.n; j++) {
        Param *a = f->params.v[j];
        untyped |= !a->spec && !a->self;
    }
    if (!untyped) {
        return false;
    }
    BodyCheck c = {.p = p, .fn = f, .scope = {.p = p, .lexical = f->lexical ? f->lexical : 1}};
    for (int j = 0; j < f->params.n; j++) {
        Param *a = f->params.v[j];
        Type *t =
            a->self && f->owner ? type_wrap(p, T_REF, f->owner->type, 0) : parameter_type(p, a);
        BodyNode *n = body_node(t);
        if (!a->spec && !a->self) {
            BodyParam *bp = mem(sizeof(*bp));
            bp->param = a;
            bp->kinds = UINT64_MAX;
            n->param = bp;
            add(&c.params, bp);
        }
        body_bind(&c, a->name, n);
    }
    body_statements(&c, &f->body, false);
    bool changed = false;
    for (int j = 0; j < c.params.n; j++) {
        BodyParam *bp = c.params.v[j];
        List candidates = {0};
        for (int k = 0; k < bp->required.n; k++) {
            body_candidates(&candidates, bp->required.v[k]);
        }
        if (!bp->required.n) {
            for (int k = T_BOOL; k <= T_NIL; k++) {
                if (bp->kinds == (UINT64_C(1) << k)) {
                    add(&candidates, type_builtin(p, k));
                }
            }
        }
        Type *best = NULL;
        for (int k = 0; k < candidates.n; k++) {
            Type *t = candidates.v[k];
            bool valid = (bp->kinds & (UINT64_C(1) << t->kind)) != 0;
            for (int q = 0; q < bp->required.n && valid; q++) {
                valid = parameter_accepts(bp->required.v[q], t);
            }
            if (valid) {
                if (!best || parameter_accepts(t, best)) {
                    best = t;
                }
                else if (!parameter_accepts(best, t)) {
                    best = NULL;
                    break;
                }
            }
        }
        bool nominal = false;
        for (int k = 0; k < bp->required.n; k++) {
            Type *t = bp->required.v[k];
            nominal |= t->kind == T_STRUCT || t->kind == T_REF || t->kind == T_UNION;
        }
        if (!bp->kinds || (!best && bp->required.n && !nominal)) {
            fail(bp->param->loc, "conflicting body requirements for parameter '%s'",
                 bp->param->name);
        }
        if (best != bp->param->inferred) {
            bp->param->inferred = best;
            changed = true;
        }
    }
    return changed;
}

static void body_collect_statements(List *functions, List *statements);

static void body_collect_expr(List *functions, Expr *e)
{
    if (!e) {
        return;
    }
    if (e->function) {
        bool found = false;
        for (int j = 0; j < functions->n; j++) {
            found |= functions->v[j] == e->function;
        }
        if (!found) {
            add(functions, e->function);
        }
    }
    body_collect_expr(functions, e->a);
    body_collect_expr(functions, e->b);
    body_collect_expr(functions, e->c);
    for (int j = 0; j < e->args.n; j++) {
        body_collect_expr(functions, e->args.v[j]);
    }
    body_collect_statements(functions, &e->bindings);
}

static void body_collect_statements(List *functions, List *statements)
{
    for (int j = 0; j < statements->n; j++) {
        Stmt *s = statements->v[j];
        body_collect_expr(functions, s->a);
        body_collect_expr(functions, s->b);
        for (int k = 0; k < s->targets.n; k++) {
            body_collect_expr(functions, s->targets.v[k]);
        }
        for (int k = 0; k < s->values.n; k++) {
            body_collect_expr(functions, s->values.v[k]);
        }
        List *sections[] = {&s->body,  &s->other, &s->prolog, &s->pre,
                            &s->first, &s->last,  &s->epilog, &s->update};
        for (int k = 0; k < 8; k++) {
            body_collect_statements(functions, sections[k]);
        }
        for (int k = 0; k < s->handlers.n; k++) {
            CatchArm *arm = s->handlers.v[k];
            body_collect_statements(functions, &arm->body);
        }
    }
}

static bool body_function(Program *p, Function *f)
{
    if (f->invalid) {
        return false;
    }
    if (!p->tooling) {
        return body_function_unchecked(p, f);
    }
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        f->invalid = true;
        tool_leave(&recovery);
        return false;
    }
    bool changed = body_function_unchecked(p, f);
    tool_leave(&recovery);
    return changed;
}

void infer_parameters(Program *p)
{
    List functions = {0};
    add(&functions, p->entry);
    for (int j = 0; j < p->functions.n; j++) {
        add(&functions, p->functions.v[j]);
    }
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        for (int k = 0; k < d->own_methods.n; k++) {
            add(&functions, d->own_methods.v[k]);
        }
        for (int k = 0; k < d->own_fields.n; k++) {
            body_collect_expr(&functions, ((Field *)d->own_fields.v[k])->init);
        }
    }
    for (int j = 0; j < functions.n; j++) {
        body_collect_statements(&functions, &((Function *)functions.v[j])->body);
    }
    int limit = functions.n + 1;
    for (int j = 0; j < functions.n; j++) {
        limit += ((Function *)functions.v[j])->params.n;
    }
    for (int round = 0; round < limit; round++) {
        bool changed = false;
        for (int j = 0; j < functions.n; j++) {
            Function *f = functions.v[j];
            TOOL_CHECK(p, changed |= body_function(p, f););
        }
        if (!changed) {
            return;
        }
    }
    fail(p->entry->loc, "body parameter inference did not converge");
}
