#include "rhat.h"

// Each specialization owns its mutable AST, inferred signatures and caches.
// Only names and immutable literal bytes are shared with the template.
typedef struct {
    Def *source, *target;
    List functions, copies;
} Clone;

static Spec *copy_spec(Clone *c, Spec *s)
{
    if (!s) {
        return NULL;
    }
    if (s->parameter_owner == c->source) {
        if (s->type_args.n) {
            fail(s->loc, "type parameter '%s' does not accept type arguments", s->name);
        }
        for (int j = 0; j < c->source->type_params.n; j++) {
            if (!strcmp(s->name, c->source->type_params.v[j])) {
                return c->target->type_args.v[j];
            }
        }
    }
    Spec *r = mem(sizeof(*r));
    *r = *s;
    r->type = NULL;
    r->resolved = false;
    r->elem = copy_spec(c, s->elem);
    r->other = copy_spec(c, s->other);
    r->params = r->type_args = (List){0};
    if (r->self_type == c->source) {
        r->self_type = c->target;
    }
    for (int j = 0; j < s->params.n; j++) {
        add(&r->params, copy_spec(c, s->params.v[j]));
    }
    for (int j = 0; j < s->type_args.n; j++) {
        add(&r->type_args, copy_spec(c, s->type_args.v[j]));
    }
    return r;
}

static Expr *copy_expr(Clone *, Expr *);
static List copy_body(Clone *, List *);

static Function *copy_function(Clone *c, Function *f)
{
    if (!f) {
        return NULL;
    }
    for (int j = 0; j < c->functions.n; j++) {
        if (c->functions.v[j] == f) {
            return c->copies.v[j];
        }
    }
    Function *r = mem(sizeof(*r));
    *r = *f;
    add(&c->functions, f);
    add(&c->copies, r);
    r->resolved = false;
    r->params = r->instances = r->uses = (List){0};
    r->required_result = NULL;
    if (r->owner == c->source) {
        r->owner = c->target;
    }
    r->enclosing = copy_function(c, f->enclosing);
    r->result = copy_spec(c, f->result);
    for (int j = 0; j < f->params.n; j++) {
        Param *a = mem(sizeof(*a));
        *a = *(Param *)f->params.v[j];
        a->spec = copy_spec(c, a->spec);
        a->inferred = NULL;
        add(&r->params, a);
    }
    r->body = copy_body(c, &f->body);
    return r;
}

static Expr *copy_expr(Clone *c, Expr *e)
{
    if (!e) {
        return NULL;
    }
    Expr *r = mem(sizeof(*r));
    *r = *e;
    r->a = copy_expr(c, e->a);
    r->b = copy_expr(c, e->b);
    r->c = copy_expr(c, e->c);
    r->match = copy_expr(c, e->match);
    r->test_spec = copy_spec(c, e->test_spec);
    r->constant_spec = copy_spec(c, e->constant_spec);
    r->type_expr = copy_spec(c, e->type_expr);
    r->array_literal = copy_spec(c, e->array_literal);
    r->function = copy_function(c, e->function);
    r->compiler_value = NULL;
    r->args = (List){0};
    for (int j = 0; j < e->args.n; j++) {
        add(&r->args, copy_expr(c, e->args.v[j]));
    }
    r->bindings = copy_body(c, &e->bindings);
    return r;
}

static List copy_body(Clone *c, List *body)
{
    List result = {0};
    for (int j = 0; j < body->n; j++) {
        Stmt *s = body->v[j], *r = mem(sizeof(*r));
        *r = *s;
        r->spec = copy_spec(c, s->spec);
        r->a = copy_expr(c, s->a);
        r->b = copy_expr(c, s->b);
        r->body = copy_body(c, &s->body);
        r->other = copy_body(c, &s->other);
        r->update = copy_body(c, &s->update);
        r->prolog = copy_body(c, &s->prolog);
        r->pre = copy_body(c, &s->pre);
        r->first = copy_body(c, &s->first);
        r->last = copy_body(c, &s->last);
        r->epilog = copy_body(c, &s->epilog);
        // Loop focus names are immutable strings, not statements.
        r->focuses = s->focuses;
        r->targets = r->values = r->handlers = (List){0};
        for (int k = 0; k < s->targets.n; k++) {
            add(&r->targets, copy_expr(c, s->targets.v[k]));
        }
        for (int k = 0; k < s->values.n; k++) {
            add(&r->values, copy_expr(c, s->values.v[k]));
        }
        for (int k = 0; k < s->handlers.n; k++) {
            CatchArm *a = mem(sizeof(*a));
            *a = *(CatchArm *)s->handlers.v[k];
            a->spec = copy_spec(c, a->spec);
            a->body = copy_body(c, &a->body);
            add(&r->handlers, a);
        }
        add(&result, r);
    }
    return result;
}

Def *specialize_definition(Def *source, List *arguments, const char *name)
{
    Def *d = mem(sizeof(*d));
    *d = *source;
    d->name = str(name);
    d->generic = source;
    d->type_args = *arguments;
    d->type_params = d->instances = d->bases = d->base_names = (List){0};
    d->fields = d->methods = d->alternatives = (List){0};
    d->own_fields = d->own_methods = d->union_specs = d->base_specs = d->constants = (List){0};
    d->state = 0;
    d->resolved = false;
    d->type = NULL;
    Clone c = {.source = source, .target = d};
    for (int j = 0; j < source->base_specs.n; j++) {
        add(&d->base_specs, copy_spec(&c, source->base_specs.v[j]));
    }
    for (int j = 0; j < source->union_specs.n; j++) {
        add(&d->union_specs, copy_spec(&c, source->union_specs.v[j]));
    }
    for (int j = 0; j < source->own_fields.n; j++) {
        Field *f = mem(sizeof(*f));
        *f = *(Field *)source->own_fields.v[j];
        f->owner = f->initializer_owner = d;
        f->spec = copy_spec(&c, f->spec);
        f->init = copy_expr(&c, f->init);
        f->type = NULL;
        add(&d->own_fields, f);
    }
    for (int j = 0; j < source->own_methods.n; j++) {
        add(&d->own_methods, copy_function(&c, source->own_methods.v[j]));
    }
    for (int j = 0; j < source->constants.n; j++) {
        Constant *value = source->constants.v[j], *r = mem(sizeof(*r));
        *r = *value;
        r->owner = d;
        r->state = 0;
        List one = {.v = (void **)&value->binding, .n = 1};
        r->binding = copy_body(&c, &one).v[0];
        add(&d->constants, r);
    }
    return d;
}
