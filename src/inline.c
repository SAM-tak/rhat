#include "sema.h"
#include <errno.h>

bool argument_pack(Type *t)
{
    return t && t->kind == T_STRUCT && t->def->pack;
}

static bool integer_value(List *vars, Expr *e, int depth, int64_t *out)
{
    const int64_t limit = INT64_C(1) << 30;
    if (!e || depth > 64) {
        return false;
    }
    if (e->kind == E_INT || e->kind == E_CHAR) {
        char *end;
        errno = 0;
        *out = strtoll(e->text, &end, 10);
        return !errno && !*end && *out >= -limit && *out <= limit;
    }
    if (e->kind == E_NAME) {
        for (int j = vars->n - 1; j >= 0; j--) {
            Var *v = vars->v[j];
            if (!strcmp(v->name, e->text)) {
                *out = v->integer;
                return v->constant;
            }
        }
    }
    if (e->kind == E_MEMBER && !strcmp(e->text, "len") && e->a->kind == E_NAME) {
        for (int j = vars->n - 1; j >= 0; j--) {
            Var *v = vars->v[j];
            if (!strcmp(v->name, e->a->text)) {
                Type *t = v->type;
                t = t && t->kind == T_REF ? t->elem : t;
                if (argument_pack(t) || (t && t->kind == T_ARRAY)) {
                    *out = argument_pack(t) ? t->def->fields.n : t->count;
                    return true;
                }
                return false;
            }
        }
    }
    if (e->kind == E_UNARY && !strcmp(e->text, "-") && integer_value(vars, e->a, depth + 1, out)) {
        *out = -*out;
        return true;
    }
    if (e->kind == E_BIN) {
        int64_t a, b;
        if (!integer_value(vars, e->a, depth + 1, &a) ||
            !integer_value(vars, e->b, depth + 1, &b)) {
            return false;
        }
        if ((!strcmp(e->text, "/") || !strcmp(e->text, "%")) && !b) {
            return false;
        }
        *out = !strcmp(e->text, "+")   ? a + b
               : !strcmp(e->text, "-") ? a - b
               : !strcmp(e->text, "*") ? a * b
               : !strcmp(e->text, "/") ? a / b
               : !strcmp(e->text, "%") ? a % b
                                       : limit + 1;
        return *out >= -limit && *out <= limit;
    }
    return false;
}

bool static_integer(List *vars, Expr *e, int64_t *out)
{
    return integer_value(vars, e, 0, out);
}

int inline_length(Type *t, Loc loc)
{
    if (argument_pack(t)) {
        return t->def->fields.n;
    }
    if (t && t->kind == T_ARRAY) {
        return t->count;
    }
    fail(loc, "inline^ for^ requires a fixed array or heterogeneous argument pack");
}

Type *inline_element(Type *t, int index)
{
    return argument_pack(t) ? ((Field *)t->def->fields.v[index])->type : t->elem;
}

int argument_index(List *vars, Type *t, Expr *index, Loc loc)
{
    int64_t n;
    if (!static_integer(vars, index, &n)) {
        fail(loc, "argument pack index must be a compile-time integer constant");
    }
    if (n < 0 || n >= t->def->fields.n) {
        fail(loc, "argument pack index is out of bounds");
    }
    return (int)n;
}

static Expr *copy_inline_expr(Expr *e)
{
    if (!e) {
        return NULL;
    }
    Expr *r = mem(sizeof(*r));
    *r = *e;
    r->a = copy_inline_expr(e->a);
    r->b = copy_inline_expr(e->b);
    r->c = copy_inline_expr(e->c);
    r->match = copy_inline_expr(e->match);
    r->bindings = inline_body(&e->bindings);
    r->args = (List){0};
    for (int j = 0; j < e->args.n; j++) {
        add(&r->args, copy_inline_expr(e->args.v[j]));
    }
    return r;
}

List inline_body(List *body)
{
    List out = {0};
    for (int j = 0; j < body->n; j++) {
        Stmt *s = body->v[j], *r = mem(sizeof(*r));
        *r = *s;
        r->a = copy_inline_expr(s->a);
        r->b = copy_inline_expr(s->b);
        r->body = inline_body(&s->body);
        r->other = inline_body(&s->other);
        r->update = inline_body(&s->update);
        r->prolog = inline_body(&s->prolog);
        r->pre = inline_body(&s->pre);
        r->first = inline_body(&s->first);
        r->last = inline_body(&s->last);
        r->epilog = inline_body(&s->epilog);
        r->targets = r->values = r->handlers = (List){0};
        for (int k = 0; k < s->targets.n; k++) {
            add(&r->targets, copy_inline_expr(s->targets.v[k]));
        }
        for (int k = 0; k < s->values.n; k++) {
            add(&r->values, copy_inline_expr(s->values.v[k]));
        }
        for (int k = 0; k < s->handlers.n; k++) {
            CatchArm *a = mem(sizeof(*a));
            *a = *(CatchArm *)s->handlers.v[k];
            a->body = inline_body(&a->body);
            add(&r->handlers, a);
        }
        add(&out, r);
    }
    return out;
}

static bool jumps_to(Context *c, int begin, int label)
{
    for (int j = begin; j < c->in->code.n; j++) {
        Instr *i = c->in->code.v[j];
        if (i->op == I_BR && i->label == label) {
            return true;
        }
    }
    return false;
}

static void inline_label(Context *c, int label, Loc loc)
{
    Instr *i = emit(c, I_LABEL, loc);
    i->label = label;
}

void inline_loop(Context *c, Stmt *s)
{
    Value source = snapshot(c, deref(c, expression(c, s->a, NULL), s->loc), s->loc);
    if (dead(source)) {
        return;
    }
    int count = inline_length(source.type, s->loc);
    if (count > 4096 - c->inline_steps) {
        fail(s->loc, "inline^ expansion limit exceeded (4096 iterations per function)");
    }
    c->inline_steps += count;
    int mark = c->vars.n, begin = c->in->code.n;
    Loop loop = {.end = ++c->in->labels,
                 .bindings = mark,
                 .outside = mark,
                 .disposals = c->pending_disposals.n};
    add(&c->loops, &loop);
    for (int j = 0; j < count; j++) {
        loop.update = ++c->in->labels;
        int first = c->in->code.n;
        c->depth++;
        c->lexical++;
        Value item;
        if (argument_pack(source.type)) {
            item = field_value(c, source, source.type->def->fields.v[j], s->loc);
        }
        else {
            Expr base = {.kind = E_SAVED, .loc = s->loc, .compiler_value = &source};
            Expr index = {.kind = E_INT, .loc = s->loc, .text = fmt("%d", j)};
            Expr at = {.kind = E_INDEX, .loc = s->loc, .a = &base, .b = &index};
            item = expression(c, &at, NULL);
        }
        Expr saved = {.kind = E_SAVED, .loc = s->loc, .compiler_value = &item};
        Stmt binding = {.kind = S_BIND, .loc = s->loc, .name = s->name, .a = &saved};
        List body = {0};
        add(&body, &binding);
        List expanded = inline_body(&s->body);
        for (int k = 0; k < expanded.n; k++) {
            add(&body, expanded.v[k]);
        }
        statements(c, &body, false);
        bool next = !c->terminated || jumps_to(c, first, loop.update);
        if (!c->terminated) {
            cleanup(c, mark, s->loc);
            Instr *br = emit(c, I_BR, s->loc);
            br->label = loop.update;
        }
        c->vars.n = mark;
        c->depth--;
        read_scope_end(c, c->depth);
        c->lexical--;
        if (!next) {
            break;
        }
        inline_label(c, loop.update, s->loc);
        c->terminated = false;
    }
    c->loops.n--;
    if (!c->terminated || jumps_to(c, begin, loop.end)) {
        if (!c->terminated) {
            Instr *br = emit(c, I_BR, s->loc);
            br->label = loop.end;
        }
        inline_label(c, loop.end, s->loc);
        c->terminated = false;
    }
}
