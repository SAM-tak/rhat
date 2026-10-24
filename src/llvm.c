#include "rhat.h"

char *llvm_type(Type *t) {
  switch (t->kind) {
  case T_VOID:
    return "void";
  case T_BOOL:
    return "i1";
  case T_U8:
  case T_I8:
  case T_F8:
    return "i8";
  case T_U16:
  case T_I16:
    return "i16";
  case T_U32:
  case T_I32:
    return "i32";
  case T_U64:
  case T_I64:
    return "i64";
  case T_U128:
  case T_I128:
    return "i128";
  case T_F16:
    return "half";
  case T_F32:
    return "float";
  case T_F64:
    return "double";
  case T_STRING:
  case T_SLICE:
    return "{ ptr, i64 }";
  case T_REF:
    return "ptr";
  case T_ARRAY:
    return fmt("[%d x %s]", t->count, llvm_type(t->elem));
  case T_GROW:
    return "{ ptr, i64, ptr }";
  default:
    return fmt("%%rh_type_%d", t->id);
  }
}
typedef struct {
  FILE *f;
  Instance *in;
  int tmp;
} Output;
static Type *slot_type(Output *o, int n) {
  return ((Slot *)o->in->slots.v[n])->type;
}
static char *stack_for(Output *o, int id) {
  int owner = ((Slot *)o->in->slots.v[id])->owner;
  if (o->in->result->owned && owner == o->in->result_slot)
    return "%result_stack";
  return fmt("%%s%d", owner);
}
static int load(Output *o, int n) {
  int t = ++o->tmp;
  fprintf(o->f, "  %%t%d = load %s, ptr %%p%d\n", t, llvm_type(slot_type(o, n)),
          n);
  return t;
}
static void save(Output *o, int dst, Type *t, const char *operand) {
  fprintf(o->f, "  store %s %s, ptr %%p%d\n", llvm_type(t), operand, dst);
}
static int integer64(Output *o, int id) {
  Type *t = slot_type(o, id);
  int v = load(o, id);
  if (numeric_bits(t->kind) != 64) {
    int out = ++o->tmp;
    fprintf(o->f, "  %%t%d = %s %s %%t%d to i64\n", out,
            numeric_bits(t->kind) > 64                    ? "trunc"
            : kind_unsigned(t->kind) || t->kind == T_BOOL ? "zext"
                                                          : "sext",
            llvm_type(t), v);
    return out;
  }
  return v;
}
static void bytes(FILE *f, const char *s) {
  fputs("c\"", f);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    fprintf(f, "\\%02X", *p);
  fputs("\\00\"", f);
}
static void instruction(Output *o, Instr *i) {
  FILE *f = o->f;
  Type *t = i->type;
  int a, b, r;
  switch (i->op) {
  case I_CONST:
    save(o, i->dst, t, i->text);
    break;
  case I_STRING:
    save(o, i->dst, t,
         fmt("{ ptr @rh_str_%d, i64 %zu }", i->label, strlen(i->text)));
    break;
  case I_ZERO:
    save(o, i->dst, t, "zeroinitializer");
    break;
  case I_COPY:
    a = load(o, i->a);
    save(o, i->dst, t, fmt("%%t%d", a));
    break;
  case I_ADDR:
    save(o, i->dst, t, fmt("%%p%d", i->a));
    break;
  case I_DEREF:
    fprintf(f, "  %%p%d = load ptr, ptr %%p%d\n", i->dst, i->a);
    break;
  case I_FIELD:
    fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i32 0, i32 %d\n", i->dst,
            llvm_type(t), i->a, i->label);
    break;
  case I_INDEX:
    if (i->b < 0) {
      fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i64 0, i64 %d\n",
              i->dst, llvm_type(t), i->a, i->label);
      break;
    }
    if (kind_unsigned(slot_type(o, i->b)->kind) ||
        numeric_bits(slot_type(o, i->b)->kind) > 64) {
      a = ++o->tmp;
      Type *index_type = slot_type(o, i->b);
      fprintf(f,
              "  %%t%d = call i64 @rh_index_integer(ptr %%p%d, i32 %d, i32 %d, "
              "i32 %d)\n",
              a, i->b, numeric_bits(index_type->kind),
              kind_unsigned(index_type->kind), i->loc.line);
    } else
      a = integer64(o, i->b);
    if (t->kind == T_GROW)
      fprintf(f,
              "  %%p%d = call ptr @rh_array_at(ptr %%p%d, i64 %%t%d, i64 %zu, "
              "i32 %d)\n",
              i->dst, i->a, a, t->elem->size, i->loc.line);
    else if (t->kind == T_SLICE || t->kind == T_STRING)
      fprintf(f,
              "  %%p%d = call ptr @rh_view_at(ptr %%p%d, i64 %%t%d, i64 %zu, "
              "i32 %d)\n",
              i->dst, i->a, a, t->kind == T_STRING ? (size_t)1 : t->elem->size,
              i->loc.line);
    else {
      fprintf(f, "  call void @rh_bounds(i64 %%t%d, i64 %d, i32 %d)\n", a,
              t->count, i->loc.line);
      fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i64 0, i64 %%t%d\n",
              i->dst, llvm_type(t), i->a, a);
    }
    break;
  case I_STACK_BEGIN:
    a = ++o->tmp;
    fprintf(f, "  %%t%d = add i64 %%sp, %d\n", a, i->stack_index);
    fprintf(f, "  %%s%d = getelementptr %%RhStack, ptr @rh_stacks, i64 %%t%d\n",
            i->dst, a);
    fprintf(f,
            "  %%markp%d = getelementptr %%RhStack, ptr %%s%d, i32 0, i32 1\n",
            i->dst, i->dst);
    b = ++o->tmp;
    fprintf(f, "  %%t%d = load ptr, ptr %%markp%d\n", b, i->dst);
    fprintf(f, "  store ptr %%t%d, ptr %%mark%d\n", b, i->dst);
    break;
  case I_REGION: {
    char *stack = stack_for(o, i->dst);
    a = ++o->tmp;
    b = ++o->tmp;
    r = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %s, i32 0, i32 1\n", a,
            stack);
    fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n", b, a);
    fprintf(f,
            "  %%t%d = insertvalue { ptr, i64, ptr } { ptr null, i64 0, ptr "
            "null }, ptr %%t%d, 0\n",
            r, b);
    a = ++o->tmp;
    fprintf(f, "  %%t%d = insertvalue { ptr, i64, ptr } %%t%d, ptr %s, 2\n", a,
            r, stack);
    save(o, i->dst, t, fmt("%%t%d", a));
    break;
  }
  case I_RELEASE:
    a = ++o->tmp;
    fprintf(f, "  %%t%d = load ptr, ptr %%mark%d\n", a, i->a);
    fprintf(f, "  store ptr %%t%d, ptr %%markp%d\n", a, i->a);
    break;
  case I_PUSH: {
    int desc = load(o, i->a);
    int data = ++o->tmp, len = ++o->tmp, stack = ++o->tmp;
    fprintf(f, "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 0\n", data,
            desc);
    fprintf(f, "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 1\n", len,
            desc);
    fprintf(f, "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 2\n", stack,
            desc);
    int lp = ++o->tmp, limit = ++o->tmp, li = ++o->tmp, di = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %%t%d, i32 0, i32 2\n",
            lp, stack);
    fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n", limit, lp);
    fprintf(f, "  %%t%d = ptrtoint ptr %%t%d to i64\n", li, limit);
    fprintf(f, "  %%t%d = ptrtoint ptr %%t%d to i64\n", di, data);
    int bytes = ++o->tmp, cap = ++o->tmp, ok = ++o->tmp;
    fprintf(f, "  %%t%d = sub i64 %%t%d, %%t%d\n", bytes, li, di);
    fprintf(f, "  %%t%d = udiv i64 %%t%d, %zu\n", cap, bytes, t->elem->size);
    fprintf(f, "  %%t%d = icmp ult i64 %%t%d, %%t%d\n", ok, len, cap);
    fprintf(f,
            "  br i1 %%t%d, label %%push_ok_%d, label "
            "%%push_bad_%d\npush_bad_%d:\n",
            ok, ok, ok, ok);
    fprintf(
        f,
        "  call void @rh_stack_overflow(i32 %d)\n  unreachable\npush_ok_%d:\n",
        i->loc.line, ok);
    int off = ++o->tmp, ptr = ++o->tmp;
    fprintf(f, "  %%t%d = mul i64 %%t%d, %zu\n", off, len, t->elem->size);
    fprintf(f, "  %%t%d = getelementptr i8, ptr %%t%d, i64 %%t%d\n", ptr, data,
            off);
    a = load(o, i->b);
    fprintf(f, "  store %s %%t%d, ptr %%t%d\n", llvm_type(t->elem), a, ptr);
    int end = ++o->tmp, topaddr = ++o->tmp, top = ++o->tmp, greater = ++o->tmp,
        high = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr i8, ptr %%t%d, i64 %zu\n", end, ptr,
            t->elem->size);
    fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %%t%d, i32 0, i32 1\n",
            topaddr, stack);
    fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n", top, topaddr);
    fprintf(f, "  %%t%d = icmp ugt ptr %%t%d, %%t%d\n", greater, end, top);
    fprintf(f, "  %%t%d = select i1 %%t%d, ptr %%t%d, ptr %%t%d\n", high,
            greater, end, top);
    fprintf(f, "  store ptr %%t%d, ptr %%t%d\n", high, topaddr);
    int newlen = ++o->tmp, lenaddr = ++o->tmp;
    fprintf(f, "  %%t%d = add i64 %%t%d, 1\n", newlen, len);
    fprintf(
        f,
        "  %%t%d = getelementptr { ptr, i64, ptr }, ptr %%p%d, i32 0, i32 1\n",
        lenaddr, i->a);
    fprintf(f, "  store i64 %%t%d, ptr %%t%d\n", newlen, lenaddr);
    if (i->dst >= 0)
      save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", ptr));
    break;
  }
  case I_POP:
    fprintf(
        f, "  call void @rh_array_pop(ptr %%p%d, ptr %%p%d, i64 %zu, i32 %d)\n",
        i->a, i->dst, t->elem->size, i->loc.line);
    break;
  case I_CLEAR:
    fprintf(f, "  call void @rh_array_clear(ptr %%p%d)\n", i->a);
    break;
  case I_UNION: {
    Type *arm = slot_type(o, i->a);
    save(o, i->dst, t, "zeroinitializer");
    int tag = ++o->tmp, payload = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n", tag,
            llvm_type(t), i->dst);
    fprintf(f, "  store i32 %d, ptr %%t%d\n", i->label, tag);
    fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n", payload,
            llvm_type(t), i->dst);
    a = load(o, i->a);
    fprintf(f, "  store %s %%t%d, ptr %%t%d\n", llvm_type(arm), a, payload);
    break;
  }
  case I_CASE_IS:
  case I_CASE_GET: {
    int tagptr = ++o->tmp, tag = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n", tagptr,
            llvm_type(t), i->a);
    fprintf(f, "  %%t%d = load i32, ptr %%t%d\n", tag, tagptr);
    if (i->op == I_CASE_IS) {
      r = ++o->tmp;
      fprintf(f, "  %%t%d = icmp eq i32 %%t%d, %d\n", r, tag, i->label);
      save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", r));
    } else {
      fprintf(f, "  call void @rh_union_tag(i32 %%t%d, i32 %d, i32 %d)\n", tag,
              i->label, i->loc.line);
      int payload = ++o->tmp;
      Type *arm = slot_type(o, i->dst);
      fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n",
              payload, llvm_type(t), i->a);
      r = ++o->tmp;
      fprintf(f, "  %%t%d = load %s, ptr %%t%d\n", r, llvm_type(arm), payload);
      save(o, i->dst, arm, fmt("%%t%d", r));
    }
    break;
  }
  case I_TRUNCATE:
    a = load(o, i->b);
    fprintf(f, "  call void @rh_array_truncate(ptr %%p%d, i64 %%t%d, i32 %d)\n",
            i->a, a, i->loc.line);
    break;
  case I_APPEND:
    fprintf(
        f,
        "  call void @rh_array_append(ptr %%p%d, ptr %%p%d, i64 %zu, i32 %d)\n",
        i->a, i->b, t->elem->size, i->loc.line);
    break;
  case I_SLICE: {
    char *data, *length;
    if (t->kind == T_ARRAY) {
      data = fmt("%%p%d", i->a);
      length = fmt("%d", t->count);
    } else {
      int base = load(o, i->a);
      int ptr = ++o->tmp, len = ++o->tmp;
      fprintf(f, "  %%t%d = extractvalue %s %%t%d, 0\n", ptr, llvm_type(t),
              base);
      fprintf(f, "  %%t%d = extractvalue %s %%t%d, 1\n", len, llvm_type(t),
              base);
      data = fmt("%%t%d", ptr);
      length = fmt("%%t%d", len);
    }
    a = load(o, i->b);
    b = load(o, i->other);
    fprintf(f,
            "  call void @rh_make_slice(ptr %%p%d, ptr %s, i64 %s, i64 %%t%d, "
            "i64 %%t%d, i64 %zu, i32 %d)\n",
            i->dst, data, length, a, b,
            t->kind == T_STRING ? (size_t)1 : t->elem->size, i->loc.line);
    break;
  }
  case I_LENGTH:
    if (t->kind == T_ARRAY)
      save(o, i->dst, slot_type(o, i->dst), fmt("%d", t->count));
    else {
      a = ++o->tmp;
      b = ++o->tmp;
      fprintf(f,
              "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 1\n  %%t%d = "
              "load i64, ptr %%t%d\n",
              a, llvm_type(t), i->a, b, a);
      save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", b));
    }
    break;
  case I_BIN: {
    a = load(o, i->a);
    b = load(o, i->b);
    r = ++o->tmp;
    if (!strcmp(i->text, "bitshift^")) {
      Type *count = slot_type(o, i->b);
      int wide = b;
      if (numeric_bits(count->kind) != 128) {
        wide = ++o->tmp;
        fprintf(f, "  %%t%d = %s %s %%t%d to i128\n", wide,
                kind_unsigned(count->kind) ? "zext" : "sext", llvm_type(count),
                b);
      }
      int neg = ++o->tmp, magnitude = ++o->tmp, masked = ++o->tmp,
          amount = masked;
      int left = ++o->tmp, right = ++o->tmp, selected = ++o->tmp,
          large = ++o->tmp;
      fprintf(f, "  %%t%d = icmp slt i128 %%t%d, 0\n", neg, wide);
      if (kind_unsigned(count->kind))
        fprintf(f, "  %%t%d = add i128 %%t%d, 0\n", magnitude, wide);
      else {
        int inverse = ++o->tmp;
        fprintf(f, "  %%t%d = sub i128 0, %%t%d\n", inverse, wide);
        fprintf(f, "  %%t%d = select i1 %%t%d, i128 %%t%d, i128 %%t%d\n",
                magnitude, neg, inverse, wide);
      }
      /* Mask before shifting, even on the unselected path: LLVM shifts must
         never receive a count >= the operand width. */
      fprintf(f, "  %%t%d = and i128 %%t%d, %d\n", masked, magnitude,
              numeric_bits(t->kind) - 1);
      if (numeric_bits(t->kind) != 128) {
        amount = ++o->tmp;
        fprintf(f, "  %%t%d = trunc i128 %%t%d to %s\n", amount, masked,
                llvm_type(t));
      }
      fprintf(f, "  %%t%d = shl %s %%t%d, %%t%d\n", left, llvm_type(t), a,
              amount);
      fprintf(f, "  %%t%d = lshr %s %%t%d, %%t%d\n", right, llvm_type(t), a,
              amount);
      fprintf(f, "  %%t%d = select i1 %s, %s %%t%d, %s %%t%d\n", selected,
              kind_unsigned(count->kind) ? "false" : fmt("%%t%d", neg),
              llvm_type(t), right, llvm_type(t), left);
      fprintf(f, "  %%t%d = icmp uge i128 %%t%d, %d\n", large, magnitude,
              numeric_bits(t->kind));
      fprintf(f, "  %%t%d = select i1 %%t%d, %s 0, %s %%t%d\n", r, large,
              llvm_type(t), llvm_type(t), selected);
      save(o, i->dst, t, fmt("%%t%d", r));
      break;
    }
    bool fp = kind_float(t->kind), uns = kind_unsigned(t->kind);
    const char *operand_type = llvm_type(t);
    if (t->kind == T_F8) {
      int x = ++o->tmp, y = ++o->tmp;
      fprintf(f, "  %%t%d = call float @rh_f8_decode(i8 %%t%d)\n", x, a);
      fprintf(f, "  %%t%d = call float @rh_f8_decode(i8 %%t%d)\n", y, b);
      a = x;
      b = y;
      operand_type = "float";
    }
    const char *op = i->text, *ins = NULL;
    if (!strcmp(op, "+"))
      ins = fp ? "fadd" : "add";
    else if (!strcmp(op, "-"))
      ins = fp ? "fsub" : "sub";
    else if (!strcmp(op, "*"))
      ins = fp ? "fmul" : "mul";
    else if (!strcmp(op, "/") && fp)
      ins = "fdiv";
    else if (!strcmp(op, "bitand^"))
      ins = "and";
    else if (!strcmp(op, "bitor^"))
      ins = "or";
    else if (!strcmp(op, "bitxor^"))
      ins = "xor";
    if (ins)
      fprintf(f, "  %%t%d = %s %s %%t%d, %%t%d\n", r, ins, operand_type, a, b);
    else if (!strcmp(op, "/") || !strcmp(op, "%")) {
      fprintf(f,
              "  call void @rh_check_division(ptr %%p%d, ptr %%p%d, i32 %d, "
              "i32 %d, i32 %d, i32 %d)\n",
              i->a, i->b, numeric_bits(t->kind), uns, !strcmp(op, "%"),
              i->loc.line);
      fprintf(f, "  %%t%d = %s%s %s %%t%d, %%t%d\n", r, uns ? "u" : "s",
              !strcmp(op, "/") ? "div" : "rem", operand_type, a, b);
    } else {
      const char *pred = NULL;
      if (!strcmp(op, "=") || !strcmp(op, "=="))
        pred = fp ? "oeq" : "eq";
      else if (!strcmp(op, "!="))
        pred = fp ? "une" : "ne";
      else if (!strcmp(op, "<"))
        pred = fp ? "olt" : uns ? "ult" : "slt";
      else if (!strcmp(op, ">"))
        pred = fp ? "ogt" : uns ? "ugt" : "sgt";
      else if (!strcmp(op, "<="))
        pred = fp ? "ole" : uns ? "ule" : "sle";
      else if (!strcmp(op, ">="))
        pred = fp ? "oge" : uns ? "uge" : "sge";
      if (!pred)
        fail(i->loc, "internal: unknown binary operator");
      fprintf(f, "  %%t%d = %s %s %s %%t%d, %%t%d\n", r, fp ? "fcmp" : "icmp",
              pred, operand_type, a, b);
    }
    if (t->kind == T_F8 && slot_type(o, i->dst)->kind != T_BOOL) {
      int encoded = ++o->tmp;
      fprintf(f, "  %%t%d = call i8 @rh_f8_encode(float %%t%d)\n", encoded, r);
      r = encoded;
    }
    save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", r));
    break;
  }
  case I_UNARY:
    a = load(o, i->a);
    r = ++o->tmp;
    if (!strcmp(i->text, "!"))
      fprintf(f, "  %%t%d = xor i1 %%t%d, true\n", r, a);
    else if (!strcmp(i->text, "bitnot^"))
      fprintf(f, "  %%t%d = xor %s %%t%d, -1\n", r, llvm_type(t), a);
    else if (t->kind == T_F8)
      fprintf(f, "  %%t%d = xor i8 %%t%d, 128\n", r, a);
    else if (kind_float(t->kind))
      fprintf(f, "  %%t%d = fneg %s %%t%d\n", r, llvm_type(t), a);
    else
      fprintf(f, "  %%t%d = sub %s 0, %%t%d\n", r, llvm_type(t), a);
    save(o, i->dst, t, fmt("%%t%d", r));
    break;
  case I_CONVERT: {
    Type *source = slot_type(o, i->a);
    bool src_fp = kind_float(source->kind), dst_fp = kind_float(t->kind);
    a = load(o, i->a);
    const char *src_type = llvm_type(source);
    int src_bits = numeric_bits(source->kind), dst_bits = numeric_bits(t->kind);
    if (source == t) {
      save(o, i->dst, t, fmt("%%t%d", a));
      break;
    }
    if (source->kind == T_F8) {
      b = ++o->tmp;
      fprintf(f, "  %%t%d = call float @rh_f8_decode(i8 %%t%d)\n", b, a);
      a = b;
      src_type = "float";
      src_bits = 32;
    }
    if (t->kind == T_F8) {
      if (src_bits != 64 || !src_fp) {
        b = ++o->tmp;
        fprintf(f, "  %%t%d = %s %s %%t%d to double\n", b,
                src_fp                        ? "fpext"
                : kind_unsigned(source->kind) ? "uitofp"
                                              : "sitofp",
                src_type, a);
        a = b;
      }
      r = ++o->tmp;
      fprintf(f, "  %%t%d = call i8 @rh_f8_encode_f64(double %%t%d)\n", r, a);
    } else if (!src_fp && !dst_fp && src_bits == dst_bits) {
      r = a;
    } else {
      const char *op;
      if (!src_fp && !dst_fp)
        op = src_bits > dst_bits           ? "trunc"
             : kind_unsigned(source->kind) ? "zext"
                                           : "sext";
      else if (!src_fp)
        op = kind_unsigned(source->kind) ? "uitofp" : "sitofp";
      else if (!dst_fp) {
        int checked = a;
        if (src_bits != 64) {
          checked = ++o->tmp;
          fprintf(f, "  %%t%d = fpext %s %%t%d to double\n", checked, src_type,
                  a);
        }
        fprintf(f,
                "  call void @rh_check_float_cast(double %%t%d, i32 %d, i32 "
                "%d, i32 %d)\n",
                checked, dst_bits, kind_unsigned(t->kind), i->loc.line);
        op = kind_unsigned(t->kind) ? "fptoui" : "fptosi";
      } else
        op = src_bits > dst_bits ? "fptrunc" : "fpext";
      if (src_fp && dst_fp && src_bits == dst_bits)
        r = a;
      else {
        r = ++o->tmp;
        fprintf(f, "  %%t%d = %s %s %%t%d to %s\n", r, op, src_type, a,
                llvm_type(t));
      }
    }
    save(o, i->dst, t, fmt("%%t%d", r));
    break;
  }
  case I_CALL:
    a = ++o->tmp;
    fprintf(f, "  %%t%d = add i64 %%sp, %d\n", a, i->stack_index);
    fprintf(f, "  call void @%s(i64 %%t%d", i->callee->symbol, a);
    if (t->kind != T_VOID)
      fprintf(f, ", ptr %%p%d", i->dst);
    if (t->owned)
      fprintf(f, ", ptr %s", stack_for(o, i->dst));
    for (int j = 0; j < i->args.n; j++)
      fprintf(f, ", ptr %%p%d", (int)(intptr_t)i->args.v[j] - 1);
    fputs(")\n", f);
    break;
  case I_PRINT:
    for (int j = 0; j < i->args.n; j++) {
      int id = (int)(intptr_t)i->args.v[j] - 1;
      Type *ty = slot_type(o, id);
      if (j)
        fputs("  call void @rh_print_space()\n", f);
      if (ty->kind == T_STRING) {
        a = load(o, id);
        b = ++o->tmp;
        r = ++o->tmp;
        fprintf(f,
                "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n  %%t%d = "
                "extractvalue { ptr, i64 } %%t%d, 1\n  call void "
                "@rh_print_string(ptr %%t%d, i64 %%t%d)\n",
                b, a, r, a, b, r);
      } else if (kind_float(ty->kind)) {
        a = load(o, id);
        if (ty->kind == T_F8) {
          int decoded = ++o->tmp;
          fprintf(f, "  %%t%d = call float @rh_f8_decode(i8 %%t%d)\n", decoded,
                  a);
          a = decoded;
        }
        if (ty->kind != T_F64) {
          b = ++o->tmp;
          fprintf(f, "  %%t%d = fpext %s %%t%d to double\n", b,
                  ty->kind == T_F16 ? "half" : "float", a);
          a = b;
        }
        fprintf(f, "  call void @rh_print_f64(double %%t%d)\n", a);
      } else if (ty->kind == T_BOOL) {
        a = integer64(o, id);
        fprintf(f, "  call void @rh_print_bool(i64 %%t%d)\n", a);
      } else {
        fprintf(f, "  call void @rh_print_integer(ptr %%p%d, i32 %d, i32 %d)\n",
                id, numeric_bits(ty->kind), kind_unsigned(ty->kind));
      }
    }
    fputs("  call void @rh_print_newline()\n", f);
    break;
  case I_ASSERT:
    a = integer64(o, (int)(intptr_t)i->args.v[0] - 1);
    fprintf(f, "  call void @rh_assert(i64 %%t%d, i32 %d)\n", a, i->loc.line);
    break;
  case I_LABEL:
    fprintf(f, "block%d:\n", i->label);
    break;
  case I_BR:
    fprintf(f, "  br label %%block%d\n", i->label);
    break;
  case I_CBR:
    a = load(o, i->a);
    fprintf(f, "  br i1 %%t%d, label %%block%d, label %%block%d\n", a, i->label,
            i->other);
    break;
  case I_RETURN:
    fputs("  ret void\n", f);
    break;
  default:
    fail(i->loc, "internal: unknown IR operation");
  }
}
void emit_llvm(Program *p, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    fail((Loc){path, 1, 1}, "cannot write LLVM IR");
  fputs("; R^ typed, destination-passing LLVM IR.\n", f);
  for (int j = 0; j < p->types.n; j++) {
    Type *t = p->types.v[j];
    if (t->kind == T_UNION) {
      fprintf(f, "%s = type { i32, [%zu x i8], [%zu x i%zu] }\n", llvm_type(t),
              t->payload_offset - 4,
              (t->payload_bytes + t->payload_align - 1) / t->payload_align,
              t->payload_align * 8);
      continue;
    }
    if (t->kind != T_STRUCT)
      continue;
    fprintf(f, "%s = type { ", llvm_type(t));
    if (!t->def->fields.n)
      fputs("i8", f);
    for (int k = 0; k < t->def->fields.n; k++) {
      Field *a = t->def->fields.v[k];
      fprintf(f, "%s%s", k ? ", " : "", llvm_type(a->type));
    }
    fputs(" }\n", f);
  }
  int count = 0;
  for (int j = 0; j < p->instances.n; j++) {
    Instance *in = p->instances.v[j];
    for (int k = 0; k < in->code.n; k++) {
      Instr *i = in->code.v[k];
      if (i->op == I_STRING) {
        i->label = count++;
        fprintf(f, "@rh_str_%d = private unnamed_addr constant [%zu x i8] ",
                i->label, strlen(i->text) + 1);
        bytes(f, i->text);
        fputc('\n', f);
      }
    }
  }
  fprintf(f,
          "\n%%RhStack = type { ptr, ptr, ptr, ptr }\n@rh_stacks = internal "
          "global [%d x %%RhStack] zeroinitializer\n",
          p->stack_count);
  fputs("declare void @rh_runtime_init(ptr, i64)\ndeclare void "
        "@rh_runtime_shutdown()\n"
        "declare void @rh_stack_overflow(i32) noreturn\n"
        "declare ptr @rh_array_at(ptr, i64, i64, i32)\ndeclare void "
        "@rh_bounds(i64, i64, i32)\ndeclare void @rh_assert(i64, i32)\ndeclare "
        "void @rh_print_i64(i64)\ndeclare void @rh_print_f64(double)\ndeclare "
        "void @rh_print_bool(i64)\ndeclare void @rh_print_string(ptr, "
        "i64)\ndeclare void @rh_print_newline()\ndeclare void "
        "@rh_print_space()\n",
        f);
  fputs("declare void @rh_print_integer(ptr, i32, i32)\n"
        "declare void @rh_check_division(ptr, ptr, i32, i32, i32, i32)\n"
        "declare i64 @rh_index_integer(ptr, i32, i32, i32)\n"
        "declare float @rh_f8_decode(i8)\ndeclare i8 @rh_f8_encode(float)\n",
        f);
  fputs("declare i8 @rh_f8_encode_f64(double)\n"
        "declare void @rh_check_float_cast(double, i32, i32, i32)\n",
        f);
  fputs("declare void @rh_array_truncate(ptr, i64, i32)\n"
        "declare void @rh_union_tag(i32, i32, i32)\n"
        "declare void @rh_array_append(ptr, ptr, i64, i32)\n"
        "declare void @rh_make_slice(ptr, ptr, i64, i64, i64, i64, i32)\n"
        "declare ptr @rh_view_at(ptr, i64, i64, i32)\n"
        "declare void @rh_array_pop(ptr, ptr, i64, i32)\n"
        "declare void @rh_array_clear(ptr)\n",
        f);
  for (int j = 0; j < p->instances.n; j++) {
    Instance *in = p->instances.v[j];
    fprintf(f, "\n; %s%s%s\ndefine void @%s(", in->self ? in->self->name : "",
            in->self ? "." : "", in->fn->name, in->symbol);
    bool result = in->result->kind != T_VOID;
    fputs("i64 %sp", f);
    if (result)
      fputs(", ptr %out", f);
    if (in->result->owned)
      fputs(", ptr %result_stack", f);
    for (int k = 0; k < in->params.n; k++)
      fprintf(f, ", ptr %%arg%d", k);
    fputs(") {\nentry:\n", f);
    Output o = {f, in, 0};
    for (int k = 0; k < in->slots.n; k++) {
      Slot *s = in->slots.v[k];
      if (!s->alias)
        fprintf(f, "  %%p%d = alloca %s\n", k, llvm_type(s->type));
    }
    for (int k = 0; k < in->code.n; k++) {
      Instr *i = in->code.v[k];
      if (i->op == I_STACK_BEGIN)
        fprintf(f, "  %%mark%d = alloca ptr\n", i->dst);
    }
    if (result)
      fprintf(f, "  %%p%d = getelementptr i8, ptr %%out, i64 0\n",
              in->result_slot);
    for (int k = 0; k < in->params.n; k++) {
      Type *t = in->params.v[k];
      int r = ++o.tmp;
      int id = k + (result ? 1 : 0);
      fprintf(f, "  %%t%d = load %s, ptr %%arg%d\n", r, llvm_type(t), k);
      save(&o, id, t, fmt("%%t%d", r));
    }
    for (int k = 0; k < in->code.n; k++)
      instruction(&o, in->code.v[k]);
    fputs("}\n", f);
  }
  fprintf(f,
          "\ndefine i32 @main() {\nentry:\n  call void @rh_runtime_init(ptr "
          "@rh_stacks, i64 %d)\n  call void @rh_fn_0(i64 0)\n  call void "
          "@rh_runtime_shutdown()\n  ret i32 0\n}\n",
          p->stack_count);
  if (fclose(f))
    fail((Loc){path, 1, 1}, "failed to write LLVM IR");
}
void dump_ir(Program *p, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    fail((Loc){path, 1, 1}, "cannot write R^ IR");
  const char *names[] = {"const",
                         "string",
                         "copy",
                         "zero",
                         "binary",
                         "unary",
                         "borrow",
                         "deref",
                         "field",
                         "index.checked",
                         "call",
                         "print",
                         "assert",
                         "label",
                         "branch",
                         "branch.if",
                         "return",
                         "region.construct",
                         "stack.restore",
                         "array.push",
                         "length",
                         "numeric.convert",
                         "array.pop",
                         "array.clear",
                         "slice.checked",
                         "array.append",
                         "union.construct",
                         "union.is",
                         "union.get.checked",
                         "array.truncate",
                         "stack.save"};
  for (int j = 0; j < p->defs.n; j++) {
    Def *d = p->defs.v[j];
    fprintf(f, "type %s size=%zu align=%zu%s\n", d->name, d->type->size,
            d->type->align, d->abstract ? " abstract" : "");
    size_t off = 0;
    if (d->type->kind == T_UNION) {
      fprintf(f, "  tag +0 : u32^ payload=+%zu bytes=%zu\n",
              d->type->payload_offset, d->type->payload_bytes);
      for (int k = 0; k < d->alternatives.n; k++)
        fprintf(f, "  case %d : %s\n", k, type_name(d->alternatives.v[k]));
    }
    for (int k = 0; k < d->fields.n; k++) {
      Field *a = d->fields.v[k];
      off = (off + a->type->align - 1) / a->type->align * a->type->align;
      fprintf(f, "  +%zu %s.%s : %s%s\n", off, a->owner->name, a->name,
              type_name(a->type), a->abstract ? " abstract" : "");
      off += a->type->size;
    }
  }
  for (int j = 0; j < p->instances.n; j++) {
    Instance *in = p->instances.v[j];
    fprintf(f, "\nfunction %s %s%s%s -> %s destination=%%%d\n", in->symbol,
            in->self ? in->self->name : "", in->self ? "." : "", in->fn->name,
            type_name(in->result), in->result_slot);
    fprintf(f, "  stacks own=%d need=%d\n", in->stack_own, in->stack_need);
    for (int k = 0; k < in->params.n; k++)
      fprintf(f, "  parameter %d : %s\n", k, type_name(in->params.v[k]));
    for (int k = 0; k < in->slots.n; k++) {
      Slot *s = in->slots.v[k];
      fprintf(f, "  %%%d : %s %s\n", k, type_name(s->type),
              s->alias ? "place" : "storage");
    }
    for (int k = 0; k < in->code.n; k++) {
      Instr *i = in->code.v[k];
      fprintf(f, "  %-18s dst=%%%d a=%%%d b=%%%d", names[i->op], i->dst, i->a,
              i->b);
      if (i->op == I_STACK_BEGIN || i->op == I_CALL)
        fprintf(f, " stack-offset=%d", i->stack_index);
      if (i->op == I_REGION || (i->op == I_CALL && i->type->owned))
        fprintf(f, " stack-owner=%%%d", ((Slot *)in->slots.v[i->dst])->owner);
      if (i->type)
        fprintf(f, " type=%s", type_name(i->type));
      if (i->text)
        fprintf(f, " op=%s", i->op == I_STRING ? "<literal>" : i->text);
      if (i->callee)
        fprintf(f, " target=%s", i->callee->symbol);
      if (i->op == I_BR || i->op == I_CBR || i->op == I_LABEL)
        fprintf(f, " label=%d other=%d", i->label, i->other);
      if (i->op == I_UNION || i->op == I_CASE_IS || i->op == I_CASE_GET)
        fprintf(f, " tag=%d", i->label);
      if (i->op == I_SLICE)
        fprintf(f, " count=%%%d", i->other);
      if (i->op == I_FIELD)
        fprintf(f, " field=%d", i->label);
      if (i->args.n) {
        fputs(" args=", f);
        for (int a = 0; a < i->args.n; a++)
          fprintf(f, "%%%d%s", (int)(intptr_t)i->args.v[a] - 1,
                  a + 1 < i->args.n ? "," : "");
      }
      fprintf(f, " at=%d:%d\n", i->loc.line, i->loc.col);
    }
  }
  if (fclose(f))
    fail((Loc){path, 1, 1}, "failed to write R^ IR");
}
