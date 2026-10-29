#include "sema.h"

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

Value pack_error(Context *c, Value src, Type *to, int dst, Loc loc)
{
    if (sum_accepts(to, src.type)) {
        return convert_value(c, src, to, dst, loc);
    }
    Value out = {.id = dst >= 0 ? dst : slot(c, to, false), .type = to, .deps = src.deps};
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
    fail(loc, "error '%s' is not included in '%s'", type_name(src.type), type_name(to));
}

// End the process with a message: a string^, or the name of the error
// variant held by subject. panic^ and an error leaving the entry file differ
// only in the reported prefix.
// The source an error site names: under std/ for the standard library,
// else as the compiler was given it, with / between directories.
char *site_source(Program *p, const char *file)
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

// The message() of error err, by the variant it holds when it is a union.
Value error_message(Context *c, Value err, Loc loc)
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
        Value v = sum ? (Value){.id = case_at(c, err, arm, false, loc),
                                .type = arm,
                                .deps = err.deps,
                                .storage = err.storage,
                                .var = err.var,
                                .addressable = err.addressable}
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

void terminate(Context *c, Value subject, bool panic, Loc loc)
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
        bool error = !text && arm->kind == T_STRUCT && arm->def->group;
        Value detail = {0}, site = {0};
        if (error) {
            // An error also reports its message() and where it was built.
            Value v = sum ? (Value){.id = case_at(c, subject, arm, false, loc),
                                    .type = arm,
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

void route_error(Context *c, Value error, Loc loc)
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
            // The entry file has no caller: report the variant and exit.
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

void error_focus(Context *c, Value error, Loc loc)
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

Value try_value(Context *c, Expr *e, int dst)
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

Value catch_value(Context *c, Expr *e, Type *expected, int dst)
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
                    : (Value){.id = dst >= 0 ? dst : slot(c, result, false), .type = result};
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
            if ((replacement.deps & (UINT64_C(1) << j)) && c->root_depth[j] > c->depth - 1) {
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
