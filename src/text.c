#include "sema.h"

// r.end (i64^ | nil^, nil^ when open) or r.len.
Value range_member(Context *c, Value r, const char *name, Loc loc)
{
    Type *range = r.type, *whole = type_builtin(c->p, T_I64);
    Value start = field_value(c, r, range->def->fields.v[0], loc);
    Value end = field_value(c, r, range->def->fields.v[1], loc);
    Value open = operate(c, "=", end, constant(c, T_I64, RANGE_OPEN, loc), loc);
    int yes = ++c->in->labels, no = ++c->in->labels, done = ++c->in->labels;
    if (!strcmp(name, "end")) {
        Type *t = type_union(c->p, whole, type_builtin(c->p, T_NIL), loc);
        Value out = value(c, t);
        nil_branch(c, open, yes, no, loc);
        nil_label(c, yes, loc);
        convert_value(c, nil_literal(c, loc), t, out.id, loc);
        nil_jump(c, done, loc);
        nil_label(c, no, loc);
        convert_value(c, end, t, out.id, loc);
        nil_jump(c, done, loc);
        nil_label(c, done, loc);
        return out;
    }
    if (strcmp(name, "len")) {
        fail(loc, "range^ has start, end, len and contains(i)");
    }
    const char *message = "an open range has no length";
    nil_branch(c, open, yes, no, loc);
    nil_label(c, yes, loc);
    Expr text = {.kind = E_STRING, .loc = loc, .text = (char *)message, .length = strlen(message)};
    terminate(c, expression(c, &text, type_builtin(c->p, T_STRING)), true, loc);
    c->terminated = false;
    nil_label(c, no, loc);
    Value out = value(c, whole), length = operate(c, "-", end, start, loc);
    copy_to(c, out, length, loc, true);
    int negative = ++c->in->labels;
    nil_branch(c, operate(c, "<", length, constant(c, T_I64, "0", loc), loc), negative, done, loc);
    nil_label(c, negative, loc);
    copy_to(c, out, constant(c, T_I64, "0", loc), loc, true);
    nil_jump(c, done, loc);
    nil_label(c, done, loc);
    return out;
}

// r.contains(i): start <= i and, unless r is open, i < end.
Value range_contains(Context *c, Value r, Value i, Loc loc)
{
    Value start = field_value(c, r, r.type->def->fields.v[0], loc);
    Value end = field_value(c, r, r.type->def->fields.v[1], loc);
    Value open = operate(c, "=", end, constant(c, T_I64, RANGE_OPEN, loc), loc);
    Value below = operate(c, "bitor^", open, operate(c, "<", i, end, loc), loc);
    return operate(c, "bitand^", operate(c, "<=", start, i, loc), below, loc);
}

// char^(x): the one character of string^ x, or integer x as a scalar
// value, else std.text.Error.Invalid.
Value char_cast(Context *c, Expr *e, int destination)
{
    Program *p = c->p;
    Type *t = char_cast_type(p, e->loc);
    if (e->args.n != 1) {
        fail(e->loc, "char^(...) casts one string^ or integer");
    }
    Value v = expression(c, e->args.v[0], NULL);
    if (dead(v)) {
        return v;
    }
    bool text = v.type->kind == T_STRING;
    if (!text && (!kind_integer(v.type->kind) || numeric_bits(v.type->kind) > 64)) {
        fail(e->loc, "char^(...) casts a string^ or an integer of at most 64 bits");
    }
    if (!text && v.type->kind != T_I64) {
        Value whole = value(c, type_builtin(p, T_I64));
        Instr *i = emit(c, I_CONVERT, e->loc);
        i->dst = whole.id;
        i->a = v.id;
        i->type = whole.type;
        v = whole;
    }
    Value code = value(c, type_builtin(p, T_I64));
    Instr *i = emit(c, I_CHAR, e->loc);
    i->dst = code.id;
    i->a = v.id;
    i->type = v.type;
    Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t};
    int good = ++c->in->labels, bad = ++c->in->labels, done = ++c->in->labels;
    nil_branch(c, operate(c, ">=", code, constant(c, T_I64, "0", e->loc), e->loc), good, bad,
               e->loc);
    nil_label(c, good, e->loc);
    Value scalar = value(c, type_builtin(p, T_CHAR));
    i = emit(c, I_CONVERT, e->loc);
    i->dst = scalar.id;
    i->a = code.id;
    i->type = scalar.type;
    convert_value(c, scalar, t, out.id, e->loc);
    nil_jump(c, done, e->loc);
    nil_label(c, bad, e->loc);
    Expr message = {
        .kind = E_STRING,
        .loc = e->loc,
        .text = text ? "not exactly one Unicode scalar value"
                     : "not a Unicode scalar value"
    };
    message.length = strlen(message.text);
    Expr invalid = {.kind = E_RECORD, .loc = e->loc, .text = "std.text.Error.Invalid"};
    add(&invalid.names, "message");
    add(&invalid.args, &message);
    construct(c, &invalid, t, out.id);
    nil_jump(c, done, e->loc);
    nil_label(c, done, e->loc);
    return out;
}

// dst = helper(args..., line), or no result when t is void.
Value helper(Context *c, const char *name, Value *args, int n, int k, Loc loc)
{
    Value out = k == T_VOID ? (Value){.id = -1, .type = type_builtin(c->p, T_VOID)}
                            : value(c, type_builtin(c->p, T_I64));
    Instr *i = emit(c, I_HELPER, loc);
    i->dst = out.id;
    i->text = (char *)name;
    for (int j = 0; j < n; j++) {
        add(&i->args, (void *)(intptr_t)(args[j].id + 1));
    }
    return out;
}

// string^(bytes): the bytes as a string^ when they are UTF-8, else
// std.text.Error.Invalid.
Value text_cast(Context *c, Expr *e, int destination)
{
    Program *p = c->p;
    Type *t = text_cast_type(p, e->loc), *text = type_builtin(p, T_STRING);
    if (e->args.n != 1) {
        fail(e->loc, "string^(...) casts one u8^[:]");
    }
    Value bytes = expression(c, e->args.v[0], NULL);
    if (dead(bytes)) {
        return bytes;
    }
    if (bytes.type->kind != T_SLICE || bytes.type->elem->kind != T_U8) {
        fail(e->loc, "string^(...) casts a u8^[:] of UTF-8");
    }
    Value out = {
        .id = destination >= 0 ? destination : slot(c, t, false), .type = t, .deps = bytes.deps};
    int good = ++c->in->labels, bad = ++c->in->labels, done = ++c->in->labels;
    Value valid = helper(c, "rh_text_valid", &bytes, 1, T_I64, e->loc);
    nil_branch(c, operate(c, "!=", valid, constant(c, T_I64, "0", e->loc), e->loc), good, bad,
               e->loc);
    nil_label(c, good, e->loc);
    Value view = {.id = slot(c, text, false), .type = text, .deps = bytes.deps};
    Instr *i = emit(c, I_COPY, e->loc);
    i->dst = view.id;
    i->a = bytes.id;
    i->type = text;
    convert_value(c, view, t, out.id, e->loc);
    nil_jump(c, done, e->loc);
    nil_label(c, bad, e->loc);
    Expr message = {.kind = E_STRING, .loc = e->loc, .text = "not valid UTF-8"};
    message.length = strlen(message.text);
    Expr invalid = {.kind = E_RECORD, .loc = e->loc, .text = "std.text.Error.Invalid"};
    add(&invalid.names, "message");
    add(&invalid.args, &message);
    construct(c, &invalid, t, out.id);
    nil_jump(c, done, e->loc);
    nil_label(c, done, e->loc);
    return out;
}

// x.write_text(out) appends x to Builder out; x.tostring() builds a new
// Builder at the destination with it. x is a number, bool^, char^ or
// string^; the others are written into a local buffer first.
static void write_value(Context *c, Value x, Value into, Expr *format, Loc loc);
Value value_text(Context *c, Expr *e, int destination)
{
    Program *p = c->p;
    Expr *target = e->a;
    bool build = !strcmp(target->text, "tostring");
    Def *builder = lookup_def(p, "std.text.Builder");
    if (!builder) {
        fail(e->loc, "internal: %s needs std.text", target->text);
    }
    if (e->args.n != !build && e->args.n != !build + 1) {
        fail(e->loc,
             build ? "x.tostring() takes an optional format"
                   : "x.write_text(out) takes a ref^ std.text.Builder and an optional format");
    }
    Value x = expression(c, target->a, NULL);
    if (dead(x)) {
        return x;
    }
    Value out = {.id = -1, .type = type_builtin(p, T_VOID)}, into;
    if (build) {
        if (destination < 0) {
            fail(e->loc,
                 "x.tostring() builds a std.text.Builder; bind it, as in let^ s = x.tostring()");
        }
        Expr empty = {.kind = E_RECORD, .loc = e->loc, .text = builder->name};
        out = into = construct(c, &empty, builder->type, destination);
    }
    else {
        into = deref(c, expression(c, e->args.v[0], type_wrap(p, T_REF, builder->type, 0)), e->loc);
        if (into.type != builder->type) {
            fail(e->loc, "x.write_text(out) takes a ref^ std.text.Builder, found '%s'",
                 type_name(into.type));
        }
    }
    write_value(c, x, into, e->args.n > !build ? e->args.v[e->args.n - 1] : NULL, e->loc);
    return out;
}

// Append x, written with the C format format when there is one, to the
// Builder into.
static void write_value(Context *c, Value x, Value into, Expr *format, Loc loc)
{
    Program *p = c->p;
    Def *builder = into.type->def;
    bool number = kind_integer(x.type->kind) || kind_float(x.type->kind);
    if (format && !number) {
        fail(loc, "a format applies to a number, not '%s'", type_name(x.type));
    }
    Value text = x;
    if (x.type->kind != T_STRING) {
        if (x.type->kind == T_F8 || x.type->kind == T_F16) {
            Value wide = value(c, type_builtin(p, T_F64));
            Instr *i = emit(c, I_CONVERT, loc);
            i->dst = wide.id;
            i->a = x.id;
            i->type = wide.type;
            x = wide;
        }
        Type *buffer = type_wrap(p, T_ARRAY, type_builtin(p, T_U8), 256);
        Type *view = type_wrap(p, T_SLICE, type_builtin(p, T_U8), 0);
        int bytes = slot(c, buffer, false);
        Expr none = {.kind = E_STRING, .loc = loc, .text = ""};
        Value args[4] = {
            {.id = slot(c, view, false), .type = view},
            {.id = slot(c, type_wrap(p, T_REF, x.type, 0), false)},
            constant(c, T_I64,
                     fmt("%d", numeric_bits(x.type->kind) | (kind_unsigned(x.type->kind) ? 1
                                                             : kind_float(x.type->kind)  ? 2
                                                             : x.type->kind == T_BOOL    ? 3
                                                             : x.type->kind == T_CHAR    ? 4
                                                                                         : 0)
                                                                << 8),
                     loc),
            expression(c, format ? format : &none, type_builtin(p, T_STRING))};
        if (args[3].type->kind != T_STRING) {
            fail(loc, "a format is a string^, as in \"%%5.2f\"");
        }
        Instr *i = emit(c, I_SLICE, loc);
        i->dst = args[0].id;
        i->a = bytes;
        i->b = i->other = -1;
        i->type = buffer;
        i = emit(c, I_ADDR, loc);
        i->dst = args[1].id;
        i->a = x.id;
        i->type = ((Slot *)c->in->slots.v[args[1].id])->type;
        Value length = helper(c, "rh_text_format", args, 4, T_I64, loc);
        text = (Value){
            .id = slot(c, type_builtin(p, T_STRING), false),
            .type = type_builtin(p, T_STRING)
        };
        Value start = constant(c, T_I64, "0", loc);
        i = emit(c, I_SLICE, loc);
        i->dst = text.id;
        i->a = bytes;
        i->b = start.id;
        i->other = length.id;
        i->type = buffer;
    }
    Field *bytes = field(builder, "bytes");
    int place = slot(c, bytes->type, true);
    Instr *i = emit(c, I_FIELD, loc);
    i->dst = place;
    i->a = into.id;
    i->type = builder->type;
    i->label = bytes->index;
    i = emit(c, I_APPEND, loc);
    i->a = place;
    i->b = text.id;
    i->type = bytes->type;
}

// Append the text and holes of $"..." e to the Builder into.
void interp_into(Context *c, Expr *e, Value into)
{
    for (int j = 0; j < e->args.n && !c->terminated; j++) {
        Expr *part = e->args.v[j];
        char *format = e->names.v[j];
        if (part->kind == E_INTERP && !*format) {
            // A nested $"..." goes on in the same Builder.
            interp_into(c, part, into);
            continue;
        }
        Value x = expression(c, part, NULL);
        if (dead(x)) {
            return;
        }
        if (x.type == into.type && !*format) {
            // A Builder in a hole gives its text.
            Function *text = NULL;
            for (int k = 0; k < into.type->def->methods.n; k++) {
                Function *m = into.type->def->methods.v[k];
                text = !strcmp(m->name, "text") ? m : text;
            }
            x = invoke_operator(c, text, x, x, part);
        }
        if (!textual(x.type)) {
            fail(part->loc,
                 "an interpolation hole takes a number, bool^, char^ or string^, found '%s'",
                 type_name(x.type));
        }
        Expr spec = {
            .kind = E_STRING,
            .loc = part->loc,
            .text = format,
            .length = strlen(format)
        };
        write_value(c, x, into, *format ? &spec : NULL, part->loc);
    }
}

// $"..." for print^: built in a Builder living until the print, which
// shows its text.
Value interp_print(Context *c, Expr *e, List *temps)
{
    Def *builder = lookup_def(c->p, "std.text.Builder");
    int temp = slot(c, builder->type, false);
    Instr *i = emit(c, I_STACK_BEGIN, e->loc);
    i->dst = temp;
    i->type = builder->type;
    i->text = "$\"...\"";
    add(&c->pending_owners, (void *)(intptr_t)(temp + 1));
    add(temps, (void *)(intptr_t)temp);
    Value out = construct(c, e, builder->type, temp);
    Function *text = NULL;
    for (int j = 0; j < builder->methods.n; j++) {
        Function *m = builder->methods.v[j];
        text = !strcmp(m->name, "text") ? m : text;
    }
    return dead(out) ? out : invoke_operator(c, text, out, out, e);
}

// The built-in methods of string^, which address it by byte offsets.
Value text_method(Context *c, Expr *e, int destination)
{
    Program *p = c->p;
    Expr *target = e->a;
    const char *name = target->text;
    Type *t = text_method_type(p, name, e->loc), *whole = type_builtin(p, T_I64);
    Value s = deref(c, expression(c, target->a, NULL), e->loc);
    if (dead(s)) {
        return s;
    }
    bool unary = !strcmp(name, "count_chars") || !strcmp(name, "chars");
    if (e->args.n != !unary) {
        fail(e->loc, "s.%s takes %s", name, unary ? "no arguments" : "one argument");
    }
    Type *want = !strcmp(name, "slice")  ? type_range(p)
               : !strcmp(name, "decode") ? whole
                                         : type_builtin(p, T_STRING);
    Value arg = unary ? s : expression(c, e->args.v[0], want);
    if (dead(arg)) {
        return arg;
    }
    if (!unary && arg.type != want) {
        fail(e->loc, "s.%s takes '%s', found '%s'", name, type_name(want), type_name(arg.type));
    }
    Value both[2] = {s, arg};
    if (!strcmp(name, "count_chars")) {
        return helper(c, "rh_text_count", both, 1, T_I64, e->loc);
    }
    if (!strcmp(name, "starts_with") || !strcmp(name, "ends_with")) {
        Value r =
            helper(c, name[0] == 's' ? "rh_text_starts" : "rh_text_ends", both, 2, T_I64, e->loc);
        return operate(c, "!=", r, constant(c, T_I64, "0", e->loc), e->loc);
    }
    if (!strcmp(name, "find")) {
        Value at = helper(c, "rh_text_find", both, 2, T_I64, e->loc);
        Value out = {.id = destination >= 0 ? destination : slot(c, t, false), .type = t};
        int none = ++c->in->labels, found = ++c->in->labels, done = ++c->in->labels;
        nil_branch(c, operate(c, "<", at, constant(c, T_I64, "0", e->loc), e->loc), none, found,
                   e->loc);
        nil_label(c, none, e->loc);
        convert_value(c, nil_literal(c, e->loc), t, out.id, e->loc);
        nil_jump(c, done, e->loc);
        nil_label(c, found, e->loc);
        convert_value(c, at, t, out.id, e->loc);
        nil_jump(c, done, e->loc);
        nil_label(c, done, e->loc);
        return out;
    }
    if (!strcmp(name, "slice")) {
        // A byte range of s whose ends fall between characters.
        Value out = {
            .id = destination >= 0 ? destination : slot(c, t, false),
            .type = t,
            .deps = s.deps
        };
        int lo = field_value(c, arg, arg.type->def->fields.v[0], e->loc).id;
        int hi = field_value(c, arg, arg.type->def->fields.v[1], e->loc).id;
        Instr *i = emit(c, I_SLICE, e->loc);
        i->dst = out.id;
        i->a = s.id;
        i->b = lo;
        i->other = hi;
        i->text = "text";
        i->type = s.type;
        return out;
    }
    if (!strcmp(name, "decode")) {
        Value out = {
            .id = destination >= 0 ? destination : slot(c, t, false),
            .type = t
        };
        Value code = helper(c, "rh_text_decode", both, 2, T_I64, e->loc);
        Value scalar = value(c, type_builtin(p, T_CHAR));
        Instr *i = emit(c, I_CONVERT, e->loc);
        i->dst = scalar.id;
        i->a = code.id;
        i->type = scalar.type;
        copy_to(c, field_value(c, out, t->def->fields.v[0], e->loc), scalar, e->loc, true);
        copy_to(c, field_value(c, out, t->def->fields.v[1], e->loc),
                helper(c, "rh_text_next", both, 2, T_I64, e->loc), e->loc, true);
        return out;
    }
    // chars(): decode every character, pushed onto an array built at the destination.
    if (destination < 0) {
        fail(e->loc, "s.chars() builds an owned array; bind it, as in let^ cs = s.chars()");
    }
    Expr empty = {.kind = E_ARRAY, .loc = e->loc};
    Value out = construct(c, &empty, t, destination);
    Value at = value(c, whole), length = value(c, whole);
    copy_to(c, at, constant(c, T_I64, "0", e->loc), e->loc, true);
    Instr *i = emit(c, I_LENGTH, e->loc);
    i->dst = length.id;
    i->a = s.id;
    i->type = s.type;
    int head = ++c->in->labels, body = ++c->in->labels, done = ++c->in->labels;
    nil_jump(c, head, e->loc);
    nil_label(c, head, e->loc);
    nil_branch(c, operate(c, "<", at, length, e->loc), body, done, e->loc);
    nil_label(c, body, e->loc);
    Value from[2] = {s, at};
    Value code = helper(c, "rh_text_decode", from, 2, T_I64, e->loc);
    Value scalar = value(c, type_builtin(p, T_CHAR));
    i = emit(c, I_CONVERT, e->loc);
    i->dst = scalar.id;
    i->a = code.id;
    i->type = scalar.type;
    Value pushed = value(c, type_wrap(p, T_REF, scalar.type, 0));
    i = emit(c, I_PUSH, e->loc);
    i->dst = pushed.id;
    i->a = out.id;
    i->b = scalar.id;
    i->type = out.type;
    copy_to(c, at, helper(c, "rh_text_next", from, 2, T_I64, e->loc), e->loc, true);
    nil_jump(c, head, e->loc);
    nil_label(c, done, e->loc);
    return out;
}
