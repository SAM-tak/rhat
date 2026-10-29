#include "sema.h"
#include "tooling.h"
#include <limits.h>

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
        if (kind_integer(k) || kind_float(k)) {
            t->size = t->align = (size_t)numeric_bits(k) / 8;
        }
        break;
    }
    add(&p->types, t);
    return t;
}

Type *type_wrap(Program *p, int k, Type *e, int n)
{
    for (int i = 0; i < p->types.n; i++) {
        Type *t = p->types.v[i];
        if (t->kind == k && t->elem == e && t->count == n) {
            return t;
        }
    }
    Type *t = mem(sizeof(*t));
    t->kind = k;
    t->elem = e;
    t->count = n;
    t->id = p->types.n;
    if (k == T_REF || k == T_CFUNC) {
        t->size = t->align = 8;
        t->refs = k == T_REF;
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
        identity = fmt("%s:%d:%d:%zu:%s", identity, arm->kind, arm->structural, strlen(name), name);
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

Type *type_spec(Program *p, Spec *s)
{
    if (!s) {
        return type_builtin(p, T_VOID);
    }
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
        return callable_intersection(p, type_spec(p, s->elem), type_spec(p, s->other), s->loc);
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
    if (s->kind == SPEC_ARRAY && !s->count) {
        fail(s->loc, "an array type without a length needs grow^, as in grow^ T[]");
    }
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
    if (s->kind != SPEC_NAME) {
        return type_wrap(p,
                           s->kind == SPEC_REF   ? T_REF
                         : s->kind == SPEC_GROW  ? T_GROW
                         : s->kind == SPEC_SLICE ? T_SLICE
                                                 : T_ARRAY,
                         type_spec(p, s->elem), s->count);
    }
    for (int k = 0; k < T_STRUCT; k++) {
        if (!strcmp(s->name, builtin_name(k))) {
            return type_builtin(p, k);
        }
    }
    if (!strcmp(s->name, "f128^")) {
        fail(s->loc, "f128^ is pending; binary128 is not implemented");
    }
    Def *d = lookup_def(p, s->name);
    if (!d) {
        fail(s->loc, "unknown type '%s'", s->name);
    }
    tool_record(p, s->loc, d->loc, d->name, d->type, NULL, d, 23, false);
    return d->type;
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
    if (t->kind == T_FUNCTION) {
        char *name = str(t->procedure ? "p^" : "f^");
        for (int j = 0; j < t->params.n; j++) {
            name = fmt("%s%s%s", name, j ? ", " : " ", type_name(t->params.v[j]));
        }
        return fmt("%s -> %s;", name, type_name(t->result));
    }
    if (t->kind < T_STRUCT) {
        return (char *)builtin_name(t->kind);
    }
    if (t->kind == T_STRUCT || t->kind == T_UNION) {
        return t->def->name;
    }
    if (t->kind == T_REF) {
        return fmt(t->elem->kind == T_UNION ? "ref^ (%s)" : "ref^ %s", type_name(t->elem));
    }
    if (t->kind == T_DYN) {
        return fmt("ref^ dyn^ %s", type_name(t->elem));
    }
    if (t->kind == T_SLICE) {
        return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION) ? "(%s)[:]" : "%s[:]",
                   type_name(t->elem));
    }
    if (t->kind == T_GROW) {
        return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION) ? "%s (%s)[]" : "%s %s[]",
                   t->count ? "flex^" : "grow^", type_name(t->elem));
    }
    return fmt((t->elem->kind == T_REF || t->elem->kind == T_UNION) ? "(%s)[%d]" : "%s[%d]",
               type_name(t->elem), t->count);
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

bool sum_accepts(Type *to, Type *from)
{
    if (to == from || union_alternative(to, from) >= 0) {
        return true;
    }
    if (to->kind != T_UNION || from->kind != T_UNION || (!to->structural && !from->structural)) {
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

bool fits_type(Type *from, Type *to)
{
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
    if (a->params.n != b->params.n || a->procedure != b->procedure ||
        a->mutable_self != b->mutable_self || a->templ != b->templ || (ar && br && ar != br)) {
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

void compose(Program *p, Def *d)
{
    if (d->state == 2) {
        return;
    }
    if (d->state == 1) {
        fail(d->loc, "cyclic def^ composition at '%s'", d->name);
    }
    d->state = 1;
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
    for (int i = 0; i < d->base_names.n; i++) {
        char *n = d->base_names.v[i];
        Def *b = lookup_def(p, n);
        if (!b) {
            fail(d->loc, "unknown composition base '%s'", n);
        }
        compose(p, b);
        if (b->type->kind != T_STRUCT) {
            fail(d->loc, "composition base must be a structure, found union '%s'", b->name);
        }
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
        f->type = type_spec(p, f->spec);
        if (f->abstract && f->priv) {
            fail(f->loc, "private abstract fields cannot be implemented by another def^");
        }
        if (f->type->kind == T_VOID) {
            fail(f->loc, "field cannot have void type");
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
        if (!f->operator_member && (!f->params.n || !((Param *)f->params.v[0])->self)) {
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
    if ((t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_SLICE) && callable_type(t->elem)) {
        fail(l, "callable array storage is not supported; use statically selected let^ bindings");
    }
    if (t->kind == T_GROW) {
        layout(t->elem, l);
        if (t->elem->owned || !t->elem->size) {
            fail(l, "growable elements must be nonempty fixed-size values");
        }
        t->refs = t->elem->refs;
        return;
    }
    if (t->kind < T_STRUCT || t->kind == T_REF) {
        return;
    }
    if (t->kind == T_STRUCT && t->def->opaque) {
        fail(l, "C type '%s' is opaque; use it only through ref^", t->def->name);
    }
    if (t->size == SIZE_MAX) {
        fail(l, "recursive by-value layout in '%s'", type_name(t));
    }
    if (t->align) {
        return;
    }
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
    else if (t->kind == T_ARRAY) {
        layout(t->elem, l);
        if (t->elem->owned || !t->elem->size) {
            fail(l, "array elements must be nonempty fixed-size values");
        }
        if (t->elem->size > SIZE_MAX / (size_t)t->count) {
            fail(l, "array layout overflows");
        }
        size = t->elem->size * (size_t)t->count;
        align = t->elem->align;
        t->refs = t->elem->refs;
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
