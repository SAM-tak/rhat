#include "sema.h"

int thread_operation(Expr *target)
{
    if (target->kind != E_NAME) {
        return THREAD_NONE;
    }
    const char *names[] = {"",
                           "channel_new^",
                           "channel_send^",
                           "channel_receive^",
                           "channel_clone^",
                           "channel_close^",
                           "channel_valid^",
                           "",
                           "thread_entry^",
                           "thread_transfer_type^",
                           "thread_args^",
                           "thread_stack_count^",
                           "channel_ready^",
                           "channel_watch^",
                           "task_submit^",
                           "task_handle^",
                           "task_take^"};
    for (int j = THREAD_NEW; j <= THREAD_TAKE; j++) {
        if (*names[j] && !strcmp(target->text, names[j])) {
            return j;
        }
    }
    return THREAD_NONE;
}

Type *channel_new_element(Program *p, Expr *e)
{
    Expr *type = e->args.n ? e->args.v[0] : NULL;
    if (!type || type->kind != E_TYPE) {
        fail(e->loc, "channel_new^ requires an element type as its first argument");
    }
    return type_spec(p, type->type_expr);
}

Type *channel_element(Type *reference, Loc loc)
{
    if (reference->kind != T_REF || reference->elem->kind != T_CHANNEL) {
        fail(loc, "channel operation requires a reference to channel_handle^<T>");
    }
    return reference->elem->elem;
}

static Type *thread_pointer(Program *p, const char *name, bool read)
{
    Def *d = lookup_def(p, name);
    if (!d) {
        fail((Loc){p->path, 1, 1}, "thread bridge operations require import^std.sys");
    }
    return type_wrap(p, T_REF, d->type, read);
}

Type *thread_result(Program *p, Function *caller, int operation, Type *element)
{
    if (operation == THREAD_NEW || operation == THREAD_CLONE) {
        return type_wrap(p, T_CHANNEL, element, 0);
    }
    if (operation == THREAD_VALID || operation == THREAD_READY) {
        return type_builtin(p, T_BOOL);
    }
    if (operation == THREAD_CLOSE || operation == THREAD_DROP || operation == THREAD_WATCH) {
        return type_builtin(p, T_VOID);
    }
    if (operation == THREAD_ENTRY) {
        List params = {0};
        add(&params, thread_pointer(p, "std.sys.ThreadArgs", false));
        Type *signature = callable_signature(p, &params, type_builtin(p, T_VOID), true, NULL);
        return type_wrap(p, T_CFUNC, signature, 0);
    }
    if (operation == THREAD_TRANSFER) {
        return thread_pointer(p, "std.sys.ThreadTransfer", true);
    }
    if (operation == THREAD_ARGS) {
        return thread_pointer(p, "std.sys.ThreadArgs", true);
    }
    if (operation == THREAD_TASK) {
        // Generic definitions stay with their modules, not among the program's definitions.
        Def *task = NULL;
        for (int j = 0; !task && j < p->modules.n; j++) {
            Module *m = p->modules.v[j];
            for (int k = 0; !task && m->name && !strcmp(m->name, "std.task") && k < m->defs.n;
                 k++) {
                Def *d = m->defs.v[k];
                task = d->type_params.n && !strcmp(d->name, "std.task.Task") ? d : NULL;
            }
        }
        if (!task) {
            fail(caller->loc, "task_handle^ belongs to std.task");
        }
        List types = {0};
        add(&types, element);
        return generic_type(p, task, &types, caller->loc);
    }
    return operation == THREAD_RECEIVE || operation == THREAD_TAKE ? type_spec(p, caller->result)
                                                                   : type_builtin(p, T_I64);
}

static bool channel_type(Type *t)
{
    return t->kind == T_CHANNEL ||
           (t->kind == T_STRUCT && t->def->generic && t->def->generic->module->name &&
            !strcmp(t->def->generic->module->name, "std.channel") &&
            !strcmp(t->def->generic->name, "std.channel.Channel"));
}

static void transferable(Program *p, Type *t, bool arguments, Loc loc);

// A frame moves to another thread as its bytes: nothing in it may point outside it. What it
// keeps across suspensions must be transferable, except that the ref^ and slice parameters of
// a frame it awaits (nested) are its own locals lent out, which move with it and are rebased
// at the next resume.
static void frame_transferable(Program *p, Instance *co, bool nested, Loc loc)
{
    const char *name = co->fn->name;
    if (co->keeps_borrow) {
        fail(loc,
             "thread transfer refuses coroutine '%s': it keeps a reference across a suspension",
             name);
    }
    if (co->dispose_points.n) {
        fail(loc,
             "thread transfer refuses coroutine '%s': it keeps a Disposable across a suspension",
             name);
    }
    if (co->received) {
        fail(loc, "thread transfer refuses coroutine '%s': it receives values at resume", name);
    }
    for (int k = 0; k < co->slots.n; k++) {
        if (!co->frame_offset[k]) {
            continue;
        }
        Type *t = ((Slot *)co->slots.v[k])->type;
        bool param = k > co->result_slot && k <= co->result_slot + co->params.n;
        if (nested && param && (t->kind == T_REF || t->kind == T_SLICE)) {
            continue;
        }
        if (t->kind == T_FRAME) {
            layout(t, loc);
            frame_transferable(p, t->coroutine, true, loc);
            continue;
        }
        transferable(p, t, false, loc);
    }
}

static void transferable(Program *p, Type *t, bool arguments, Loc loc)
{
    layout(t, loc);
    if (owning_rows(t)) {
        fail(loc, "thread transfer of owning row arrays is not supported");
    }
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
    else if (t->kind == T_FRAME) {
        frame_transferable(p, t->coroutine, false, loc);
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

// A frame a std.task pool runs: it yields std.async wait ids, moves as its bytes, and hands
// back a final result of fixed size.
static void task_job(Program *p, Type *frame, Loc loc)
{
    if (frame->kind != T_FRAME) {
        fail(loc, "a task runs a coroutine frame, as in pool.async(gen(args)); found '%s'",
             type_name(frame));
    }
    layout(frame, loc);
    Instance *co = frame->coroutine;
    if (co->yielded != type_builtin(p, T_I64)) {
        fail(loc, "a task yields std.async wait ids: coroutine '%s' must yield i64^", co->fn->name);
    }
    if (co->final->owned) {
        fail(loc, "a task cannot return owned data yet; send it through a Channel");
    }
    transferable(p, frame, false, loc);
    transferable(p, co->final, false, loc);
    co->task_job = true;
}

Value thread_call(Context *c, Expr *e, int operation, int destination)
{
    if (!c->in->fn->procedure) {
        fail(e->loc, "thread operations require p^");
    }
    int count = operation == THREAD_STACKS ? 0
                : operation == THREAD_TRANSFER || operation == THREAD_ARGS ||
                        operation == THREAD_CLONE || operation == THREAD_CLOSE ||
                        operation == THREAD_VALID || operation == THREAD_READY ||
                        operation == THREAD_TAKE
                    ? 1
                : operation == THREAD_SEND || operation == THREAD_NEW ? 3
                                                                      : 2;
    if (e->args.n != count) {
        fail(e->loc, "invalid thread operation arguments");
    }
    Value args[3] = {0};
    // task_handle^ reads only the type of its job.
    int evaluated = operation == THREAD_TASK ? 1 : count;
    for (int j = operation == THREAD_NEW ? 1 : 0; j < evaluated; j++) {
        bool task =
            (operation == THREAD_SUBMIT || operation == THREAD_TASK || operation == THREAD_TAKE) &&
            j == 0;
        bool integer = operation == THREAD_NEW || task ||
                       ((operation == THREAD_SEND || operation == THREAD_RECEIVE ||
                         operation == THREAD_WATCH) &&
                        j == count - 1);
        args[j] = expression(c, e->args.v[j], integer ? type_builtin(c->p, T_I64) : NULL);
    }
    Type *payload = NULL;
    Instance *worker = NULL;
    if (operation == THREAD_ENTRY) {
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
    else if (operation == THREAD_TRANSFER || operation == THREAD_ARGS) {
        if (args[0].type->kind != T_REF) {
            fail(e->loc, "thread bridge arguments must be borrowed with ref^");
        }
        payload = args[0].type->elem;
        transferable(c->p, payload, true, e->loc);
    }
    else if (operation == THREAD_NEW) {
        payload = channel_new_element(c->p, e);
        transferable(c->p, payload, false, e->loc);
    }
    else if (operation == THREAD_SUBMIT) {
        task_job(c->p, args[1].type, e->loc);
        worker = args[1].type->coroutine;
        payload = args[1].type;
    }
    else if (operation == THREAD_TASK) {
        Type *frame = expression_type(c, e->args.v[1], NULL);
        task_job(c->p, frame, e->loc);
        payload = frame->coroutine->final;
    }
    else if (operation == THREAD_TAKE) {
        payload = NULL;
    }
    else if (operation != THREAD_STACKS) {
        payload = channel_element(args[0].type, e->loc);
        transferable(c->p, payload, false, e->loc);
        if (operation == THREAD_SEND &&
            (args[1].type->kind != T_REF || args[1].type->elem != payload)) {
            fail(e->loc, "send requires a reference to the handle's element type '%s'",
                 type_name(payload));
        }
    }
    Type *result = thread_result(c->p, c->in->fn, operation, payload);
    if (operation == THREAD_RECEIVE) {
        bool message = false, closed = false, empty = false;
        if (result->kind == T_UNION && result->def->alternatives.n == 3) {
            for (int j = 0; j < result->def->alternatives.n; j++) {
                Type *arm = result->def->alternatives.v[j];
                if (arm->kind != T_STRUCT) {
                    continue;
                }
                layout(arm, e->loc);
                Def *d = arm->def;
                message |= d->generic && !strcmp(d->generic->name, "std.channel.Message") &&
                           d->fields.n == 1 && ((Field *)d->fields.v[0])->type == payload;
                closed |= !strcmp(d->name, "std.channel.Closed");
                empty |= !strcmp(d->name, "std.channel.Empty");
            }
        }
        if (!message || !closed || !empty) {
            fail(
                e->loc,
                "channel_receive^ result must be std.channel.Message<%s> | std.channel.Closed | std.channel.Empty",
                type_name(payload));
        }
    }
    Value out = {.type = result,
                 .id = result->kind == T_VOID ? -1
                       : destination >= 0     ? destination
                                              : slot(c, result, false)};
    if (result->owned && destination < 0) {
        fail(e->loc, "owned receive requires a destination");
    }
    if (operation == THREAD_ARGS) {
        out.deps = args[0].deps;
    }
    Instr *i = emit(c, I_THREAD, e->loc);
    i->dst = out.id;
    i->label = operation;
    i->type = payload;
    i->callee = worker;
    for (int j = operation == THREAD_NEW ? 1 : 0; j < evaluated; j++) {
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

// Copies the bytes of a value of type t from at to to, if it has any.
static void copy_bytes(FILE *f, Type *t, const char *from, const char *to, int *tmp)
{
    if (!t->size) {
        return;
    }
    int value = ++*tmp;
    fprintf(f, "  %%j%d = load %s, ptr %s\n  store %s %%j%d, ptr %s\n", value, llvm_type(t), from,
            llvm_type(t), value, to);
}

// What a std.task pool calls for frames of co: step resumes the frame once and answers -1 when
// it has finished, else the wait id it yielded; finish moves its final result out of the turn.
static void emit_job(Program *p, Instance *co, FILE *f)
{
    Type *turn = co->turn, *final = co->final;
    size_t at = co->frame_offset[co->result_slot];
    int tmp = 0;
    fprintf(f, "define internal i64 @rh_job_step_%d(ptr %%frame) {\nentry:\n", co->id);
    fprintf(f, "  call void @%s(i64 1%s, ptr %%frame)\n", co->symbol,
            p->state_fields.n ? ", ptr null" : "");
    fprintf(f,
            "  %%tag = load i32, ptr %%frame\n  %%done = icmp eq i32 %%tag, -1\n"
            "  br i1 %%done, label %%finished, label %%waits\nfinished:\n  ret i64 -1\n"
            "waits:\n  %%y = getelementptr i8, ptr %%frame, i64 %zu\n"
            "  %%id = load i64, ptr %%y\n  ret i64 %%id\n}\n",
            at + (turn->kind == T_UNION ? turn->payload_offset : 0));
    fprintf(f, "define internal void @rh_job_finish_%d(ptr %%frame, ptr %%out) {\nentry:\n",
            co->id);
    fprintf(f, "  %%turn = getelementptr i8, ptr %%frame, i64 %zu\n", at);
    if (final == turn) {
        copy_bytes(f, final, "%turn", "%out", &tmp);
    }
    else if (final->kind != T_UNION) {
        fprintf(f, "  %%held = getelementptr i8, ptr %%turn, i64 %zu\n", turn->payload_offset);
        copy_bytes(f, final, "%held", "%out", &tmp);
    }
    else {
        // Each alternative of the final type is retagged as that type numbers it.
        fprintf(f,
                "  %%tag = load i32, ptr %%turn\n"
                "  %%held = getelementptr i8, ptr %%turn, i64 %zu\n"
                "  %%part = getelementptr i8, ptr %%out, i64 %zu\n  br label %%arm0\n",
                turn->payload_offset, final->payload_offset);
        for (int k = 0; k < final->def->alternatives.n; k++) {
            Type *arm = final->def->alternatives.v[k];
            fprintf(f,
                    "arm%d:\n  %%is%d = icmp eq i32 %%tag, %d\n"
                    "  br i1 %%is%d, label %%take%d, label %%arm%d\ntake%d:\n"
                    "  store i32 %d, ptr %%out\n",
                    k, k, union_alternative(turn, arm), k, k, k + 1, k,
                    union_alternative(final, arm));
            copy_bytes(f, arm, "%held", "%part", &tmp);
            fputs("  ret void\n", f);
        }
        fprintf(f, "arm%d:\n", final->def->alternatives.n);
    }
    fputs("  ret void\n}\n", f);
    fprintf(f,
            "@rh_job_%d = private constant %%RhJobType { i64 %zu, i64 %zu, ptr @rh_job_step_%d, "
            "ptr @rh_job_finish_%d, i64 %zu, i64 %zu }\n",
            co->id, co->frame_size, co->frame_align, co->id, co->id, final->size,
            final->align ? final->align : 1);
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
          "declare i64 @rh_channel_clone(i64)\n"
          "declare void @rh_channel_close(i64)\n"
          "declare void @rh_channel_drop(i64)\n"
          "declare void @rh_channel_receive(i64, ptr, ptr, i64, i32, i32, i32, i64)\n"
          "declare void @rh_thread_unhandled() cold noreturn\n"
          "declare i1 @rh_channel_ready(i64)\n"
          "declare void @rh_channel_watch(i64, i64)\n"
          "%RhJobType = type { i64, i64, ptr, ptr, i64, i64 }\n"
          "declare i64 @rh_pool_submit_frame(i64, ptr, ptr)\n"
          "declare void @rh_task_take(i64, ptr, i32, i32, i64, i64)\n",
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
        if (kind == 6 && t->kind == T_STRUCT) {
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
        if (in->task_job && in->emitted) {
            emit_job(p, in, f);
        }
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
