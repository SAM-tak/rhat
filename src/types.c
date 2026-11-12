#include "sema.h"
#include "tooling.h"
#include <limits.h>

Type *byte_view_type(Program *p, Type *source, Loc loc)
{
    Type *data = source;
    while (data->kind == T_REF) {
        data = data->elem;
    }
    if (data->kind == T_GROW || data->kind == T_BOUNDED || data->kind == T_SLICE) {
        data = data->elem;
    }
    layout(data, loc);
    if (!plain_data(data)) {
        fail(loc, "bytesof^ requires numbers, or arrays and structs containing only numbers");
    }
    return type_wrap(p, T_SLICE, type_builtin(p, T_U8), 1);
}

Def *lookup_def(Program *p, const char *n)
{
    for (int i = 0; i < p->defs.n; i++) {
        Def *d = p->defs.v[i];
        if (!strcmp(d->name, n)) {
            return d;
        }
    }
    return NULL;
}

Type *type_builtin(Program *p, int k)
{
    if (p->builtins[k]) {
        return p->builtins[k];
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
        if (kind_integer(k) || kind_float(k)) {
            t->size = t->align = (size_t)numeric_bits(k) / 8;
        }
        break;
    }
    p->builtins[k] = t;
    add(&p->types, t);
    return t;
}

Type *type_wrap(Program *p, int k, Type *e, int n)
{
    for (int i = 0; i < e->wrappers.n; i++) {
        Type *t = e->wrappers.v[i];
        if (t->kind == k && t->count == n) {
            return t;
        }
    }
    Type *t = mem(sizeof(*t));
    t->kind = k;
    t->elem = e;
    t->count = n;
    t->id = p->types.n;
    if (k == T_REF || k == T_CFUNC || k == T_CHANNEL) {
        t->size = t->align = 8;
        t->refs = k == T_REF;
        t->disposable = k == T_CHANNEL;
    }
    else if (k == T_SLICE || k == T_DYN) {
        t->size = 16;
        t->align = 8;
        t->refs = true;
    }
    else if (k == T_GROW) {
        t->size = 24;
        t->align = 8;
        t->owned = true;
        t->regions = 1;
        t->refs = e->refs;
    }
    add(&e->wrappers, t);
    add(&p->types, t);
    if (k == T_CHANNEL) {
        add(&p->transfer_types, t);
    }
    return t;
}

bool procedure_call(const Function *f)
{
    return f->procedure && !f->yields;
}

Type *frame_type(Program *p, Instance *in)
{
    Type *t = mem(sizeof(*t));
    t->kind = T_FRAME;
    t->id = p->types.n;
    t->coroutine = in;
    // A frame holds the references its arguments carry; its own locals cannot refer into it
    // across a suspension.
    for (int j = 0; j < in->params.n; j++) {
        t->refs |= ((Type *)in->params.v[j])->refs;
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
        identity = fmt("%s:%d:%d:%zu:%s", identity, arm->kind, arm->structural, strlen(name), name);
        d->name = fmt("%s%s%s", d->name, j ? " | " : "", name);
    }
    t->symbol = fmt("%%rh_sum_%s", link_identity(identity));
    add(&p->types, t);
    return t;
}

static Type *record_sequence(Program *p, List *elems, Loc loc, bool pack)
{
    for (int j = 0; j < p->types.n; j++) {
        Type *t = p->types.v[j];
        if (t->kind != T_STRUCT || (pack ? !t->def->pack : !t->def->tuple) ||
            t->def->fields.n != elems->n) {
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
    d->tuple = !pack;
    d->pack = pack;
    char *identity = str(pack ? "arguments" : "tuple");
    d->name = str(pack ? "arguments(" : "(");
    for (int j = 0; j < elems->n; j++) {
        Type *elem = elems->v[j];
        layout(elem, loc);
        if (elem->kind == T_VOID || elem->kind == T_NEVER) {
            fail(loc, "a result cannot have void type");
        }
        if (argument_pack(elem) || (pack && elem->owned)) {
            fail(loc, "argument pack elements cannot own storage or contain packs; use ref^");
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

Type *type_tuple(Program *p, List *elems, Loc loc)
{
    return record_sequence(p, elems, loc, false);
}

Type *type_pack(Program *p, List *elems, Loc loc)
{
    if (elems->n > 4096) {
        fail(loc, "heterogeneous argument pack exceeds 4096 elements");
    }
    return record_sequence(p, elems, loc, true);
}

Type *type_range(Program *p, Type *elem, Loc loc)
{
    if (elem && !range_element(elem)) {
        fail(loc, "a range counts in an integer that fits i64^, not '%s'", type_name(elem));
    }
    for (int j = 0; j < p->types.n; j++) {
        Type *t = p->types.v[j];
        if (range_type(t) && t->elem == elem) {
            return t;
        }
    }
    Type *t = mem(sizeof(*t));
    Def *d = mem(sizeof(*d));
    t->kind = T_STRUCT;
    t->id = p->types.n;
    t->def = d;
    t->elem = elem;
    t->symbol = elem ? fmt("%%rh_range_%d", elem->kind) : "%rh_range";
    d->type = t;
    d->name = elem ? fmt("range^<%s>", type_name(elem)) : "range^";
    d->state = 2;
    d->public = true;
    for (int j = 0; j < 2; j++) {
        Field *f = mem(sizeof(*f));
        f->name = j ? "\001end" : "\001start";
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
    return t && t->kind == T_STRUCT && !strncmp(t->def->name, "range^", 6);
}

bool range_element(Type *t)
{
    return t->kind == T_I64 || (kind_integer(t->kind) && numeric_bits(t->kind) < 64);
}

Type *range_bound(Program *p, Type *range)
{
    return range->elem ? range->elem : type_builtin(p, T_I64);
}

static Type *resolve_spec_type(Program *p, Spec *s)
{
    if (s->kind == SPEC_NAME && !strncmp(s->name, "channel_handle^", 15)) {
        if (s->type_args.n != 1) {
            fail(s->loc, "channel_handle^ expects one element type");
        }
        return type_wrap(p, T_CHANNEL, type_spec(p, s->type_args.v[0]), 0);
    }
    if (s->kind == SPEC_TYPEOF) {
        fail(s->loc, "typeof^ types are allowed only in annotations inside a function body");
    }
    if (s->kind == SPEC_NAME && !strncmp(s->name, "range^", 6)) {
        return type_range(p, s->type_args.n ? type_spec(p, s->type_args.v[0]) : NULL, s->loc);
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
        return callable_intersection(p, type_spec(p, s->elem), type_spec(p, s->other), s->loc);
    }
    if (s->kind == SPEC_FUNCTION) {
        List params = {0};
        for (int j = 0; j < s->params.n; j++) {
            add(&params, type_spec(p, s->params.v[j]));
        }
        Type *t = callable_signature(p, &params, type_spec(p, s->elem), s->procedure, NULL);
        return s->foreign ? type_wrap(p, T_CFUNC, t, 0) : t;
    }
    if (s->kind == SPEC_ARRAY && s->count < 0) {
        fail(s->length->loc,
             "cannot infer array length _^; provide elements or a constant repeat^ count");
    }
    if (s->length && !s->count) {
        fail(s->loc, "array length was not resolved");
    }
    if (s->kind == SPEC_ARRAY && !s->count) {
        return type_wrap(p, T_GROW, type_spec(p, s->elem), ARRAY_SIZED);
    }
    if (s->kind == SPEC_DYN) {
        fail(s->loc, "dyn^ is a reference; write ref^ dyn^ T");
    }
    if (s->kind == SPEC_REF && s->elem->kind == SPEC_DYN) {
        Type *t = type_spec(p, s->elem->elem);
        if (t->kind != T_STRUCT) {
            fail(s->loc, "dynamic^ needs a def^ type, found '%s'", type_name(t));
        }
        return type_wrap(p, T_DYN, t, s->count);
    }
    if (s->kind != SPEC_NAME) {
        return type_wrap(p,
                         s->kind == SPEC_REF       ? T_REF
                         : s->kind == SPEC_GROW    ? T_GROW
                         : s->kind == SPEC_SLICE   ? T_SLICE
                         : s->kind == SPEC_BOUNDED ? T_BOUNDED
                                                   : T_ARRAY,
                         type_spec(p, s->elem), s->count);
    }
    for (int k = T_BOOL; k < T_STRUCT; k++) {
        if (!strcmp(s->name, builtin_name(k))) {
            return type_builtin(p, k);
        }
    }
    if (!strcmp(s->name, "f128^")) {
        fail(s->loc, "f128^ is pending; binary128 is not implemented");
    }
    Def *d = s->self_type ? s->self_type : lookup_def(p, s->name);
    if (!d) {
        fail(s->loc, "unknown type '%s'", s->name);
    }
    Def *declaration = d->generic ? d->generic : d;
    tool_record(p, s->loc, declaration->loc, declaration->name, d->type, NULL, declaration, 23,
                false);
    if (d->invalid) {
        tool_abort();
    }
    return d->type;
}

Type *type_spec(Program *p, Spec *s)
{
    if (!s) {
        return type_builtin(p, T_VOID);
    }
    if (s->invalid) {
        tool_abort();
    }
    if (!s->type) {
        if (!p->tooling) {
            s->type = resolve_spec_type(p, s);
        }
        else {
            ToolRecovery recovery;
            tool_enter(&recovery);
            if (setjmp(recovery.escape)) {
                s->invalid = true;
                tool_leave(&recovery);
                tool_abort();
            }
            s->type = resolve_spec_type(p, s);
            tool_leave(&recovery);
        }
    }
    return s->type;
}

Type *state_field_type(Program *p, Field *f)
{
    if (f->type) {
        return f->type;
    }
    if (f->state_checking) {
        fail(f->loc, "cyclic static variable type at '%s.%s'", f->owner ? f->owner->name : "entry",
             f->name);
    }
    f->state_checking = true;
    if (f->spec) {
        f->type = type_spec(p, f->spec);
    }
    else if (f->binding) {
        f->type = top_binding_type(p, f->binding);
        if (!f->type) {
            infer_results(p);
            f->type = top_binding_type(p, f->binding);
        }
    }
    else {
        Function *scope = mem(sizeof(*scope));
        *scope = (Function){.name = "<static-initializer>",
                            .module = f->owner->module,
                            .owner = f->owner,
                            .procedure = true,
                            .loc = f->loc};
        Instance *in = mem(sizeof(*in));
        in->fn = scope;
        Context c = {.p = p, .in = in, .access = f->owner};
        f->type = expression_type(&c, f->init, NULL);
        if (!f->type) {
            infer_results(p);
            f->type = expression_type(&c, f->init, NULL);
        }
    }
    f->state_checking = false;
    if (!f->type || f->type->kind == T_VOID || f->type->kind == T_NEVER) {
        fail(f->loc, "cannot infer type of static variable '%s'; add a type annotation", f->name);
    }
    return f->type;
}

char *spec_read(Spec *s)
{
    if (!s->read_from.n) {
        return s->count ? "read^" : "";
    }
    char *text = str("read^(");
    for (int j = 0; j < s->read_from.n; j++) {
        text = fmt("%s%s%s", text, j ? ", " : "", (char *)s->read_from.v[j]);
    }
    return fmt("%s)", text);
}

char *array_spec_shape(Spec *s, Spec **base)
{
    Spec *elem = s->elem;
    char *refs = "";
    while (elem->kind == SPEC_REF && !elem->count && !elem->read_from.n &&
           elem->elem->kind != SPEC_DYN) {
        refs = fmt("%s^", *refs ? refs : "ref");
        elem = elem->elem;
    }
    char *kind = s->kind == SPEC_SLICE     ? ".."
                 : s->kind == SPEC_GROW    ? (s->count == ARRAY_CAPPED  ? "max^"
                                              : s->count == ARRAY_SIZED ? ""
                                              : s->count                ? "flex^"
                                                                        : "grow^")
                 : s->kind == SPEC_BOUNDED ? fmt("max^%d", s->count)
                 : s->count < 0            ? "_^"
                 : s->count                ? fmt("%d", s->count)
                                           : "";
    char *policy = fmt("%s%s%s", refs, *refs && *kind ? " " : "", kind);
    if (elem->kind == SPEC_ARRAY || elem->kind == SPEC_GROW || elem->kind == SPEC_BOUNDED ||
        elem->kind == SPEC_SLICE) {
        char *inner = array_spec_shape(elem, base);
        bool comma = !*refs && elem->kind == SPEC_ARRAY && elem->count &&
                     ((s->kind == SPEC_ARRAY && s->count) || (s->kind == SPEC_GROW && !s->count));
        return comma ? fmt("%s, %s", policy, inner)
                     : fmt("%s%s%s[%s]", policy, *policy ? " " : "",
                           elem->kind == SPEC_SLICE ? spec_read(elem) : "", inner);
    }
    *base = elem;
    return policy;
}

// Display array policies from outside to inside, joining fixed dimensions.
static char *array_shape_text(Type *t, Type **base)
{
    Type *elem = t->elem;
    char *refs = "";
    while (elem->kind == T_REF && !elem->count) {
        refs = fmt("%s^", *refs ? refs : "ref");
        elem = elem->elem;
    }
    char *kind = t->kind == T_SLICE     ? ".."
                 : t->kind == T_GROW    ? (t->count == ARRAY_SIZED    ? ""
                                           : t->count == ARRAY_CAPPED ? "max^"
                                           : t->count                 ? "flex^"
                                                                      : "grow^")
                 : t->kind == T_BOUNDED ? fmt("max^%d", t->count)
                                        : fmt("%d", t->count);
    char *policy = fmt("%s%s%s", refs, *refs && *kind ? " " : "", kind);
    if (elem->kind == T_ARRAY || elem->kind == T_GROW || elem->kind == T_BOUNDED ||
        elem->kind == T_SLICE) {
        char *inner = array_shape_text(elem, base);
        bool comma = !*refs && elem->kind == T_ARRAY &&
                     (t->kind == T_ARRAY || (t->kind == T_GROW && !t->count));
        return comma ? fmt("%s, %s", policy, inner)
                     : fmt("%s%s%s[%s]", policy, *policy ? " " : "",
                           elem->kind == T_SLICE && elem->count ? "read^" : "", inner);
    }
    *base = elem;
    return policy;
}

char *type_name(Type *t)
{
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
    if (t->kind == T_CHANNEL) {
        return fmt("channel_handle^<%s>", type_name(t->elem));
    }
    if (t->kind == T_FRAME) {
        return fmt("frame^ %s", t->coroutine->fn->name);
    }
    if (t->kind == T_FUNCTION) {
        char *name = str(t->procedure ? "p^" : "f^");
        for (int j = 0; j < t->params.n; j++) {
            name = fmt("%s%s%s", name, j ? ", " : " ", type_name(t->params.v[j]));
        }
        return t->result && t->result->kind != T_VOID ? fmt("%s -> %s;", name, type_name(t->result))
                                                      : fmt("%s;", name);
    }
    if (t->kind < T_STRUCT) {
        return (char *)builtin_name(t->kind);
    }
    if (t->kind == T_STRUCT || t->kind == T_UNION || t->kind == T_ENUM) {
        return t->def->name;
    }
    if (t->kind == T_REF) {
        return fmt(t->elem->kind == T_UNION ? "ref^%s (%s)" : "ref^%s %s", t->count ? "read^" : "",
                   type_name(t->elem));
    }
    if (t->kind == T_DYN) {
        return fmt("ref^%s dyn^ %s", t->count ? "read^" : "", type_name(t->elem));
    }
    Type *elem;
    char *shape = array_shape_text(t, &elem);
    bool wrap = elem->kind == T_UNION || elem->kind == T_DYN || elem->kind == T_REF;
    return fmt(wrap ? "%s(%s)[%s]" : "%s%s[%s]", read_reference(t) ? "read^" : "", type_name(elem),
               shape);
}

bool owning_rows(Type *t)
{
    return (t->kind == T_ARRAY || t->kind == T_BOUNDED) && t->elem->owned;
}

static Type *copy_shape_type(Program *p, Spec *s, Type *source)
{
    if (s->kind != SPEC_ARRAY && s->kind != SPEC_GROW && s->kind != SPEC_BOUNDED) {
        return type_spec(p, s);
    }
    int count = s->count;
    if (count < 0 && s->kind == SPEC_ARRAY) {
        if (!source || source->kind != T_ARRAY) {
            fail(s->loc, "cannot infer array length _^ from a runtime-sized source");
        }
        count = source->count;
    }
    bool array = source && (source->kind == T_ARRAY || source->kind == T_GROW ||
                            source->kind == T_BOUNDED || source->kind == T_SLICE);
    Type *elem = copy_shape_type(p, s->elem, array ? source->elem : NULL);
    int kind = s->kind == SPEC_GROW ? T_GROW : s->kind == SPEC_BOUNDED ? T_BOUNDED : T_ARRAY;
    if (s->kind == SPEC_ARRAY && !count) {
        kind = T_GROW;
        count = ARRAY_SIZED;
    }
    return type_wrap(p, kind, elem, count);
}

Type *array_literal_type(Program *p, Expr *e, Type *expected, Type *first)
{
    if (argument_pack(first)) {
        fail(e->loc, "argument packs cannot be stored in arrays");
    }
    Spec *attrs = e->array_literal ? e->array_literal : e->test_spec;
    if (attrs && attrs->kind == SPEC_SLICE) {
        fail(e->loc, "a slice borrows existing storage; use array[start .. end]");
    }
    bool copy = e->text && !strcmp(e->text, "copy");
    if (copy && first) {
        if (first->kind == T_REF) {
            first = first->elem;
        }
        if (first->kind != T_ARRAY && first->kind != T_GROW && first->kind != T_BOUNDED &&
            first->kind != T_SLICE) {
            fail(e->loc, "source... requires an array or slice");
        }
    }
    Type *t =
        copy && attrs ? copy_shape_type(p, attrs, first)
        : attrs       ? type_spec(p, attrs)
        : expected    ? expected
        : copy && first
            ? (first->kind == T_SLICE ? type_wrap(p, T_GROW, first->elem, ARRAY_SIZED) : first)
        : first ? type_wrap(p, T_ARRAY, first, e->args.n)
                : NULL;
    if (e->repeat_count == -1 && t && t->kind != T_ARRAY) {
        fail(e->loc, "repeat^max^ requires a fixed-length array type");
    }
    // Inference also visits literals before their uses; reject invalid storage here.
    if (t) {
        layout(t, e->loc);
    }
    return t;
}

bool derives(Def *d, Def *b)
{
    if (d == b) {
        return true;
    }
    for (int i = 0; i < d->bases.n; i++) {
        if (derives(d->bases.v[i], b)) {
            return true;
        }
    }
    return false;
}

int union_alternative(Type *sum, Type *arm)
{
    if (sum->kind == T_UNION) {
        for (int i = 0; i < sum->def->alternatives.n; i++) {
            if (sum->def->alternatives.v[i] == arm) {
                return i;
            }
        }
    }
    return -1;
}

bool enum_backs(Type *to, Type *from)
{
    return from->kind == T_ENUM && from->elem == to;
}

bool sum_accepts(Type *to, Type *from)
{
    if (read_reference(to) && from->kind == to->kind && from->elem == to->elem) {
        return true;
    }
    if (to == from || union_alternative(to, from) >= 0) {
        return true;
    }
    if (range_type(to) && !to->elem && range_type(from)) {
        return true;
    }
    if (to->kind == T_UNION && from->kind != T_UNION) {
        for (int j = 0; j < to->def->alternatives.n; j++) {
            if (sum_accepts(to->def->alternatives.v[j], from)) {
                return true;
            }
        }
    }
    if (to->kind != T_UNION || from->kind != T_UNION || (!to->structural && !from->structural)) {
        return false;
    }
    for (int j = 0; j < from->def->alternatives.n; j++) {
        if (!sum_accepts(to, from->def->alternatives.v[j])) {
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

bool fits_type(Type *from, Type *to)
{
    if (read_reference(from) && !read_reference(to) && from->kind == to->kind) {
        return false;
    }
    if (sum_accepts(to, from) || callable_accepts(to, from)) {
        return true;
    }
    if (from->kind == T_STRUCT && to->kind == T_STRUCT) {
        return derives(from->def, to->def);
    }
    return from->kind == T_REF && to->kind == T_REF && from->elem->kind == T_STRUCT &&
           to->elem->kind == T_STRUCT && derives(from->elem->def, to->elem->def);
}

// Preserve concrete alternatives: a base-type test does not slice a value.
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
        return t->def->group;
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
        return t->kind == T_STRUCT && t->def->group;
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
        out = error_join(p, out, error_leaves(p, t->def->alternatives.v[j], loc), loc);
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

bool error_accepts(Type *to, Type *from)
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

bool error_overlap(Type *to, Type *from)
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
        if (!strcmp(f->name, n)) {
            return f;
        }
    }
    return NULL;
}

Function *method(Def *d, const char *n)
{
    for (int i = 0; i < d->methods.n; i++) {
        Function *f = d->methods.v[i];
        if (!strcmp(f->name, n)) {
            return f;
        }
    }
    return NULL;
}

static int origin_parameter(Function *f, const char *name)
{
    for (int j = 0; j < f->params.n; j++) {
        if (!strcmp(((Param *)f->params.v[j])->name, name)) {
            return j;
        }
    }
    return -1;
}

static bool same_origins(Function *a, Spec *x, Function *b, Spec *y)
{
    if (!x || !y) {
        return x == y || (!read_contract(x) && !read_contract(y));
    }
    if (x->read_from.n != y->read_from.n) {
        return false;
    }
    for (int j = 0; j < x->read_from.n; j++) {
        bool found = false;
        for (int k = 0; k < y->read_from.n; k++) {
            found |=
                origin_parameter(a, x->read_from.v[j]) == origin_parameter(b, y->read_from.v[k]);
        }
        if (!found) {
            return false;
        }
    }
    if (!same_origins(a, x->elem, b, y->elem) || !same_origins(a, x->other, b, y->other)) {
        return false;
    }
    if (read_contract(x) || read_contract(y)) {
        if (x->params.n != y->params.n) {
            return false;
        }
        for (int j = 0; j < x->params.n; j++) {
            if (!same_origins(a, x->params.v[j], b, y->params.v[j])) {
                return false;
            }
        }
    }
    return true;
}

/// Orders what a method may do: f^ < f^mutable^self^ < p^.
static int effect_rank(const Function *f)
{
    return procedure_call(f) ? 2 : f->mutable_self ? 1 : 0;
}

static void compatible(Program *p, Function *a, Function *b)
{
    Type *ar = a->result ? type_spec(p, a->result) : a->required_result;
    Type *br = b->result ? type_spec(p, b->result) : b->required_result;
    if (a->abstract && !ar) {
        ar = type_builtin(p, T_VOID);
    }
    if (b->abstract && !br) {
        br = type_builtin(p, T_VOID);
    }
    // An implementation may promise fewer effects than the contract it fills, never more.
    // Two abstract contracts merge only when they promise the same.
    int effects = effect_rank(b) - effect_rank(a);
    if (a->params.n != b->params.n || effects > 0 || (b->abstract && effects != 0) ||
        a->templ != b->templ || (ar && br && ar != br) ||
        !same_origins(a, a->result, b, b->result)) {
        fail(b->loc, "override of '%s' changes its contract", a->name);
    }
    if (ar && !br) {
        b->required_result = ar;
    }
    if (br && !ar) {
        a->required_result = br;
    }
    for (int i = 0; i < a->params.n; i++) {
        Param *x = a->params.v[i], *y = b->params.v[i];
        if (x->self != y->self ||
            ((x->spec || y->spec) && type_spec(p, x->spec) != type_spec(p, y->spec))) {
            fail(b->loc, "override of '%s' changes parameter types", a->name);
        }
    }
    if (!a->priv && b->priv) {
        fail(b->loc, "override cannot make a public method private");
    }
}

static void compose_unchecked(Program *p, Def *d)
{
    if (d->state == 2) {
        return;
    }
    if (d->state == 1) {
        fail(d->loc, "cyclic def^ composition at '%s'", d->name);
    }
    d->state = 1;
    if (d->backing) {
        d->state = 2;
        return;
    }
    if (d->union_specs.n) {
        for (int i = 0; i < d->union_specs.n; i++) {
            Spec *alternative = d->union_specs.v[i];
            Def *arm = alternative->kind == SPEC_NAME ? lookup_def(p, alternative->name) : NULL;
            if (arm) {
                compose(p, arm);
                if (arm->abstract) {
                    fail(d->loc, "union alternative '%s' must be concrete", arm->name);
                }
            }
            if (alternative->kind == SPEC_NAME && !arm) {
                bool builtin = false;
                for (int k = T_BOOL; k < T_STRUCT; k++) {
                    builtin |= !strcmp(alternative->name, builtin_name(k));
                }
                if (!builtin) {
                    fail(d->loc, "unknown union alternative '%s'", alternative->name);
                }
            }
            Type *at = type_spec(p, alternative);
            char *name = type_name(at);
            for (int j = 0; j < d->alternatives.n; j++) {
                if (d->alternatives.v[j] == at) {
                    fail(d->loc, "duplicate union alternative '%s'", name);
                }
            }
            add(&d->alternatives, at);
        }
        d->state = 2;
        return;
    }
    d->disposable = !strcmp(d->name, "std.sys.Disposable");
    for (int i = 0; i < d->base_names.n; i++) {
        char *n = d->base_names.v[i];
        Def *b = lookup_def(p, n);
        if (!b) {
            fail(d->loc, "unknown composition base '%s'", n);
        }
        Spec *s = d->base_specs.v[i];
        Def *declaration = b->generic ? b->generic : b;
        tool_record(p, s->loc, declaration->loc, declaration->name, b->type, NULL, declaration, 23,
                    false);
        compose(p, b);
        if (b->type->kind != T_STRUCT) {
            fail(d->loc, "composition base must be a structure, found union '%s'", b->name);
        }
        add(&d->bases, b);
        d->disposable |= b->disposable;
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
                }
                else if (old->owner != f->owner || old->init != f->init ||
                         old->abstract != f->abstract) {
                    fail(d->loc,
                         "conflicting inherited field '%s'; use explicit named component "
                         "fields",
                         f->name);
                }
            }
            else {
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
                if (!strcmp(candidate->name, m->name) && overload_overlap(p, candidate, m)) {
                    old = candidate;
                    break;
                }
            }
            if (old && old != m) {
                if (old->abstract || m->abstract) {
                    compatible(p, old->abstract ? old : m, old->abstract ? m : old);
                    if (old->abstract && !m->abstract) {
                        for (int k = 0; k < d->methods.n; k++) {
                            if (d->methods.v[k] == old) {
                                d->methods.v[k] = m;
                            }
                        }
                    }
                    continue;
                }
                bool resolved = false;
                for (int k = 0; k < d->own_methods.n; k++) {
                    Function *own = d->own_methods.v[k];
                    if (!strcmp(own->name, m->name) && overload_overlap(p, old, own) &&
                        overload_overlap(p, m, own)) {
                        compatible(p, old, own);
                        compatible(p, m, own);
                        resolved = true;
                        break;
                    }
                }
                if (!resolved) {
                    fail(d->loc,
                         "inherited method '%s' is ambiguous; provide an explicit "
                         "override",
                         m->name);
                }
            }
            else if (!old) {
                add(&d->methods, m);
            }
        }
    }
    for (int i = 0; i < d->own_fields.n; i++) {
        Field *f = d->own_fields.v[i];
        for (int j = 0; j < i; j++) {
            if (!strcmp(f->name, ((Field *)d->own_fields.v[j])->name)) {
                fail(f->loc, "duplicate field '%s'", f->name);
            }
        }
        if (f->spec) {
            f->type = type_spec(p, f->spec);
        }
        else {
            // Defaults have their declaration's scope, without a caller's locals.
            Function *scope = mem(sizeof(*scope));
            *scope = (Function){.name = "<field-default>",
                                .module = d->module,
                                .owner = d,
                                .loc = f->loc,
                                .procedure = true};
            Instance *in = mem(sizeof(*in));
            in->fn = scope;
            Context context = {.p = p, .in = in, .access = d, .in_default = true};
            f->type = expression_type(&context, f->init, NULL);
            if (!f->type) {
                infer_results(p);
                f->type = expression_type(&context, f->init, NULL);
            }
            if (!f->type || f->type->kind == T_VOID || f->type->kind == T_NEVER) {
                fail(f->loc, "cannot infer type of field '%s'; add a type annotation", f->name);
            }
        }
        if (f->abstract && f->priv) {
            fail(f->loc, "private abstract fields cannot be implemented by another def^");
        }
        Field *old = field(d, f->name);
        if (old) {
            if (!old->abstract || f->abstract || old->priv || old->type != f->type ||
                old->priv != f->priv) {
                fail(f->loc, "field '%s' cannot replace its inherited slot", f->name);
            }
            old->abstract = false;
            old->init = f->init;
            old->initializer_owner = f->initializer_owner;
        }
        else {
            f->index = d->fields.n;
            add(&d->fields, f);
        }
    }
    for (int i = 0; i < d->own_methods.n; i++) {
        Function *f = d->own_methods.v[i];
        if (field(d, f->name)) {
            fail(f->loc, "field/method name collision '%s'", f->name);
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
            if (candidate->static_member != f->static_member) {
                fail(f->loc, "static member and method '%s' cannot share a name", f->name);
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
                fail(f->loc, "duplicate method '%s'; use overload^ for disjoint signatures",
                     f->name);
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
    for (int i = 0; i < d->fields.n; i++) {
        if (((Field *)d->fields.v[i])->abstract) {
            d->abstract = true;
        }
    }
    for (int i = 0; i < d->methods.n; i++) {
        if (field(d, ((Function *)d->methods.v[i])->name)) {
            fail(d->loc, "inherited field/method name collision '%s'",
                 ((Function *)d->methods.v[i])->name);
        }
        else if (((Function *)d->methods.v[i])->abstract) {
            d->abstract = true;
        }
    }
    d->state = 2;
}

void compose(Program *p, Def *d)
{
    if (d->invalid) {
        tool_abort();
    }
    if (!p->tooling) {
        compose_unchecked(p, d);
        return;
    }
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        d->invalid = true;
        tool_leave(&recovery);
        tool_abort();
    }
    compose_unchecked(p, d);
    tool_leave(&recovery);
}

static void layout_unchecked(Type *t, Loc l)
{
    if (t->kind == T_FRAME) {
        // The frame is laid out once its instance has been lowered; until then it has no size.
        if (t->coroutine->state == 1) {
            fail(l, "coroutine '%s' cannot hold a frame of itself", t->coroutine->fn->name);
        }
        t->size = t->coroutine->frame_size;
        t->align = t->coroutine->frame_align;
        return;
    }
    if (callable_type(t)) {
        if (t->kind == T_FUNCTION && !t->function) {
            fail(l, "callable storage requires a statically selected implementation");
        }
        for (int j = 0; j < t->arms.n; j++) {
            layout(t->arms.v[j], l);
        }
        return;
    }
    if ((t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_SLICE || t->kind == T_BOUNDED) &&
        callable_type(t->elem)) {
        fail(l, "callable array storage is not supported; use statically selected let^ bindings");
    }
    if (t->kind == T_GROW) {
        layout(t->elem, l);
        if (t->elem->owned || !t->elem->size) {
            fail(l, "owning array elements must be nonempty fixed-size values");
        }
        t->refs = t->elem->refs;
        t->disposable = t->elem->disposable;
        return;
    }
    if (t->kind < T_STRUCT || t->kind == T_REF) {
        return;
    }
    if (t->kind == T_STRUCT && t->def->opaque) {
        fail(l, "C type '%s' is opaque; use it only through ref^", t->def->name);
    }
    // State inference can request storage before the declaration pass composes defs.
    // Populate fields and union alternatives before caching their size and alignment.
    if ((t->kind == T_STRUCT || t->kind == T_UNION) && t->def->state != 2) {
        compose(t->def->module->program, t->def);
    }
    if (t->size == SIZE_MAX) {
        fail(l, "recursive by-value layout in '%s'", type_name(t));
    }
    if (t->align) {
        return;
    }
    t->size = SIZE_MAX;
    t->disposable = t->def && t->def->disposable;
    size_t size = 0, align = 1;
    if (t->kind == T_UNION) {
        for (int j = 0; j < t->def->alternatives.n; j++) {
            Type *arm = t->def->alternatives.v[j];
            layout(arm, l);
            if (arm->kind == T_STRUCT && arm->def->abstract) {
                fail(l, "union alternative '%s' must be concrete", type_name(arm));
            }
            t->owned |= arm->owned;
            if (arm->regions > t->regions) {
                t->regions = arm->regions;
            }
            t->disposable |= arm->disposable;
            if (arm->size > size) {
                size = arm->size;
            }
            if (arm->align > align) {
                align = arm->align;
            }
            t->refs |= arm->refs;
        }
        t->payload_align = align;
        t->payload_bytes = size;
        t->payload_offset = (4 + align - 1) / align * align;
        if (size > SIZE_MAX - t->payload_offset) {
            fail(l, "union layout overflows");
        }
        size += t->payload_offset;
        if (align < 4) {
            align = 4;
        }
    }
    else if (t->kind == T_ARRAY || t->kind == T_BOUNDED) {
        layout(t->elem, l);
        if (!t->elem->size) {
            fail(l, "array elements must be nonempty fixed-size values");
        }
        if (t->elem->size > (SIZE_MAX - 16) / (size_t)t->count) {
            fail(l, "array layout overflows");
        }
        size = t->elem->size * (size_t)t->count;
        align = t->elem->align;
        if (t->kind == T_BOUNDED || t->elem->owned) {
            // Owned fixed arrays also track their initialized prefix for unwinding.
            align = align > 8 ? align : 8;
            size += align;
        }
        t->refs = t->elem->refs;
        t->disposable = t->elem->disposable;
        if (t->elem->owned) {
            if (t->elem->regions > INT_MAX / t->count) {
                fail(l, "data stack count overflows");
            }
            t->owned = true;
            t->regions = t->count * t->elem->regions;
            if (size > SIZE_MAX - 7 || (size_t)t->regions > (SIZE_MAX - 8) / 8) {
                fail(l, "array layout overflows");
            }
            size = (size + 7) / 8 * 8;
            size_t metadata = 8 + (size_t)t->regions * 8;
            if (size > SIZE_MAX - metadata) {
                fail(l, "array layout overflows");
            }
            size += metadata;
            if (align < 8) {
                align = 8;
            }
        }
    }
    else {
        for (int i = 0; i < t->def->fields.n; i++) {
            Field *f = t->def->fields.v[i];
            layout(f->type, f->loc);
            if (f->type->owned && i != t->def->fields.n - 1) {
                fail(f->loc, "a growable field must be the final field");
            }
            if (size > SIZE_MAX - (f->type->align - 1)) {
                fail(f->loc, "structure layout overflows");
            }
            size = (size + f->type->align - 1) / f->type->align * f->type->align;
            if (f->type->size > SIZE_MAX - size) {
                fail(f->loc, "structure layout overflows");
            }
            size += f->type->size;
            if (f->type->align > align) {
                align = f->type->align;
            }
            t->refs |= f->type->refs;
            t->owned |= f->type->owned;
            if (f->type->regions > t->regions) {
                t->regions = f->type->regions;
            }
            t->disposable |= f->type->disposable;
        }
    }
    t->align = align;
    if (size > SIZE_MAX - (align - 1)) {
        fail(l, "type layout overflows");
    }
    t->size = (size + align - 1) / align * align;
    if (!t->size) {
        t->size = 1;
    }
}

void layout(Type *t, Loc l)
{
    if (t->invalid || (t->def && t->def->invalid)) {
        tool_abort();
    }
    if (!compiler_hooks) {
        layout_unchecked(t, l);
        return;
    }
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        t->invalid = true;
        tool_leave(&recovery);
        tool_abort();
    }
    layout_unchecked(t, l);
    tool_leave(&recovery);
}
