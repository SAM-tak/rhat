#include "sema.h"

int thread_operation(Function *caller, Expr *target)
{
    if (target->kind != E_NAME) {
        return THREAD_NONE;
    }
    const char *names[] = {"", "__rh_channel_new", "__rh_channel_send", "__rh_channel_receive",
                           "__rh_thread_spawn"};
    for (int j = THREAD_NEW; j <= THREAD_SPAWN; j++) {
        if (strcmp(target->text, names[j])) {
            continue;
        }
        const char *module = j == THREAD_SPAWN ? "std.thread" : "std.channel";
        if (!caller->module || !caller->module->name || strcmp(caller->module->name, module)) {
            fail(target->loc, "private thread operation is restricted to %s", module);
        }
        return j;
    }
    return THREAD_NONE;
}

Type *thread_result(Program *p, Function *caller, int operation)
{
    return operation == THREAD_RECEIVE ? type_spec(p, caller->result) : type_builtin(p, T_I64);
}

static bool channel_type(Type *t)
{
    return t->kind == T_STRUCT && t->def->generic && t->def->generic->module->name &&
           !strcmp(t->def->generic->module->name, "std.channel") &&
           !strcmp(t->def->generic->name, "std.channel.Channel");
}

static void transferable(Program *p, Type *t, bool arguments, Loc loc)
{
    layout(t, loc);
    if (channel_type(t)) {
        if (!arguments) {
            fail(loc, "Channel cannot be sent through a Channel, including in aggregates");
        }
    }
    else if (t->kind == T_STRUCT) {
        if (t->def->disposable) {
            fail(loc, "thread transfer refuses Disposable '%s'", type_name(t));
        }
        for (int j = 0; j < t->def->fields.n; j++) {
            transferable(p, ((Field *)t->def->fields.v[j])->type, arguments, loc);
        }
    }
    else if (t->kind == T_UNION) {
        for (int j = 0; j < t->def->alternatives.n; j++) {
            transferable(p, t->def->alternatives.v[j], arguments, loc);
        }
    }
    else if (t->kind == T_ARRAY || t->kind == T_GROW || t->kind == T_BOUNDED) {
        transferable(p, t->elem, arguments, loc);
    }
    else if (!(kind_integer(t->kind) || kind_float(t->kind) || t->kind == T_BOOL ||
               t->kind == T_CHAR || t->kind == T_NIL || t->kind == T_ENUM)) {
        fail(loc, "thread transfer refuses '%s': use owned data instead of references or views",
             type_name(t));
    }
    for (int j = 0; j < p->transfer_types.n; j++) {
        if (p->transfer_types.v[j] == t) {
            return;
        }
    }
    add(&p->transfer_types, t);
}

Value thread_call(Context *c, Expr *e, int operation, int destination)
{
    if (!c->in->fn->procedure) {
        fail(e->loc, "thread operations require p^");
    }
    int count = operation == THREAD_SEND ? 3 : 2;
    if (e->args.n != count) {
        fail(e->loc, "invalid private thread operation arguments");
    }
    Type *result = thread_result(c->p, c->in->fn, operation);
    Value out = {.type = result, .id = destination >= 0 ? destination : slot(c, result, false)};
    if (result->owned && destination < 0) {
        fail(e->loc, "owned receive requires a destination");
    }
    Value args[3] = {0};
    for (int j = 0; j < count; j++) {
        args[j] =
            expression(c, e->args.v[j],
                       operation == THREAD_NEW || (j == count - 1 && operation != THREAD_SPAWN)
                           ? type_builtin(c->p, T_I64)
                           : NULL);
    }
    Type *payload = NULL;
    Instance *worker = NULL;
    if (operation == THREAD_SPAWN) {
        Type *fn = args[0].type;
        Function *f = fn->kind == T_FUNCTION ? fn->function : NULL;
        if (!f || f->external || f->environment || f->uses.n || f->params.n != 1 || !f->procedure) {
            fail(e->loc, "spawn requires a static p^ with one reference parameter and no captures");
        }
        if (args[1].type->kind != T_REF) {
            fail(e->loc, "spawn arguments must be borrowed with ref^");
        }
        payload = args[1].type->elem;
        transferable(c->p, payload, true, e->loc);
        List actual = {0};
        Type *param = parameter_type(c->p, f->params.v[0]);
        if (!param) {
            param = type_wrap(c->p, T_REF, payload, 0);
        }
        if (param->kind != T_REF || param->elem != payload) {
            fail(e->loc, "worker parameter must reference its transferred argument type");
        }
        add(&actual, param);
        worker = instantiate(c->p, f, NULL, &actual, e->loc);
        Type *success = success_type(c->p, worker->result, e->loc);
        if (worker->result->kind != T_VOID && success && success->kind != T_NIL &&
            success->kind != T_NEVER) {
            fail(e->loc, "worker must return no value or nil^; send results through a Channel");
        }
        worker->thread_entry = true;
    }
    else {
        Def *owner = c->in->self ? c->in->self : c->in->fn->owner;
        if (!owner || owner->type_args.n != 1) {
            fail(e->loc, "channel operation requires Channel<T>");
        }
        payload = type_spec(c->p, owner->type_args.v[0]);
        transferable(c->p, payload, false, e->loc);
        if (operation == THREAD_SEND &&
            (args[1].type->kind != T_REF || args[1].type->elem != payload)) {
            fail(e->loc, "send requires a reference to the Channel's element type");
        }
    }
    Instr *i = emit(c, I_THREAD, e->loc);
    i->dst = out.id;
    i->label = operation;
    i->type = payload;
    i->callee = worker;
    for (int j = 0; j < count; j++) {
        add(&i->args, (void *)(intptr_t)(args[j].id + 1));
    }
    return out;
}

static size_t field_offset(Type *t, int index)
{
    size_t at = 0;
    for (int j = 0; j <= index; j++) {
        Type *part = ((Field *)t->def->fields.v[j])->type;
        at = (at + part->align - 1) / part->align * part->align;
        if (j == index) {
            return at;
        }
        at += part->size;
    }
    return at;
}

void emit_thread_support(Program *p, FILE *f)
{
    if (!p->transfer_types.n) {
        return;
    }
    fputs("\n%RhTransferType = type { i64, i64, i64, i64, i64, ptr, ptr }\n"
          "%RhTransferField = type { i64, ptr }\n"
          "declare i64 @rh_channel_new(ptr, i64, i64)\n"
          "declare i64 @rh_channel_send(i64, ptr, i64)\n"
          "declare void @rh_channel_receive(i64, ptr, ptr, i64, i32, i32, i32, i64)\n"
          "declare i64 @rh_thread_spawn(ptr, ptr, ptr, i64)\n"
          "declare void @rh_thread_unhandled() cold noreturn\n",
          f);
    for (int j = 0; j < p->transfer_types.n; j++) {
        Type *t = p->transfer_types.v[j];
        int kind = channel_type(t)        ? 6
                   : t->kind == T_STRUCT  ? 1
                   : t->kind == T_ARRAY   ? 2
                   : t->kind == T_GROW    ? 3
                   : t->kind == T_BOUNDED ? 4
                   : t->kind == T_UNION   ? 5
                                          : 0;
        size_t count = kind == 1                ? (size_t)t->def->fields.n
                       : kind == 5              ? (size_t)t->def->alternatives.n
                       : kind == 2 || kind == 4 ? (size_t)t->count
                                                : 0;
        size_t offset = kind == 5   ? t->payload_offset
                        : kind == 4 ? (t->elem->align > 8 ? t->elem->align : 8)
                        : kind == 3 ? (size_t)t->count
                                    : 0;
        if (kind == 6) {
            for (int k = 0; k < t->def->fields.n; k++) {
                if (!strcmp(((Field *)t->def->fields.v[k])->name, "handle")) {
                    offset = field_offset(t, k);
                }
            }
        }
        if (kind == 1 || kind == 5) {
            fprintf(f, "@rh_transfer_fields_%d = private constant [%zu x %%RhTransferField] [",
                    t->id, count);
            for (size_t k = 0; k < count; k++) {
                Type *part =
                    kind == 1 ? ((Field *)t->def->fields.v[k])->type : t->def->alternatives.v[k];
                fprintf(f, "%s%%RhTransferField { i64 %zu, ptr @rh_transfer_%d }", k ? ", " : "",
                        kind == 1 ? field_offset(t, (int)k) : 0, part->id);
            }
            fputs("]\n", f);
        }
        fprintf(
            f,
            "@rh_transfer_%d = private constant %%RhTransferType { i64 %d, i64 %zu, i64 %zu, i64 %zu, i64 %zu, ptr %s, ptr %s }\n",
            t->id, kind, t->size, t->align, count, offset,
            kind >= 2 && kind <= 4 ? fmt("@rh_transfer_%d", t->elem->id) : "null",
            kind == 1 || kind == 5 ? fmt("@rh_transfer_fields_%d", t->id) : "null");
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->thread_entry && in->emitted) {
            Type *r = in->result;
            bool direct = r->kind != T_VOID && !r->owned && r->size <= 16;
            fprintf(f, "define internal void @rh_worker_%d(ptr %%args) {\nentry:\n", in->id);
            if (r->kind != T_VOID) {
                fprintf(f, "  %%result = alloca %s\n", llvm_type(r));
            }
            fprintf(f, "  %scall %s @%s(i64 1%s%s, ptr %%args)\n", direct ? "%value = " : "",
                    direct ? llvm_type(r) : "void", in->symbol,
                    p->state_fields.n ? ", ptr null" : "",
                    r->kind != T_VOID && !direct ? ", ptr %result" : "");
            if (direct) {
                fprintf(f, "  store %s %%value, ptr %%result\n", llvm_type(r));
            }
            if (r->kind == T_UNION && has_error(r)) {
                int nil = union_alternative(r, type_builtin(p, T_NIL));
                fputs("  %tag = load i32, ptr %result\n", f);
                fprintf(
                    f,
                    "  %%ok = icmp eq i32 %%tag, %d\n  br i1 %%ok, label %%done, label %%error\nerror:\n  call void @rh_thread_unhandled()\n  unreachable\ndone:\n",
                    nil);
            }
            else if (only_errors(r)) {
                fputs("  call void @rh_thread_unhandled()\n  unreachable\n", f);
            }
            if (!only_errors(r)) {
                fputs("  ret void\n", f);
            }
            fputs("}\n", f);
        }
    }
}
