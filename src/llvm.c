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
    return "{ ptr, i64 }";
  case T_REF:
    return "ptr";
  case T_ARRAY:
    return fmt("[%d x %s]", t->count, llvm_type(t->elem));
  case T_GROW:
    return "{ ptr, i64, i64 }";
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
    else {
      fprintf(f, "  call void @rh_bounds(i64 %%t%d, i64 %d, i32 %d)\n", a,
              t->count, i->loc.line);
      fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i64 0, i64 %%t%d\n",
              i->dst, llvm_type(t), i->a, a);
    }
    break;
  case I_REGION:
    fprintf(f, "  call void @rh_region_init(ptr %%p%d)\n", i->dst);
    break;
  case I_RELEASE:
    fprintf(f, "  call void @rh_region_release(ptr %%p%d)\n", i->a);
    break;
  case I_PUSH:
    r = ++o->tmp;
    fprintf(
        f,
        "  %%t%d = call ptr @rh_array_push(ptr %%p%d, ptr %%p%d, i64 %zu, i32 %d)\n",
        r, i->a, i->b, t->elem->size, i->loc.line);
    if (i->dst >= 0)
      save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", r));
    break;
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
                kind_unsigned(count->kind) ? "zext" : "sext", llvm_type(count), b);
      }
      int neg = ++o->tmp, magnitude = ++o->tmp, masked = ++o->tmp, amount = masked;
      int left = ++o->tmp, right = ++o->tmp, selected = ++o->tmp, large = ++o->tmp;
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
        fprintf(f, "  %%t%d = trunc i128 %%t%d to %s\n", amount, masked, llvm_type(t));
      }
      fprintf(f, "  %%t%d = shl %s %%t%d, %%t%d\n", left, llvm_type(t), a, amount);
      fprintf(f, "  %%t%d = lshr %s %%t%d, %%t%d\n", right, llvm_type(t), a, amount);
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
                src_fp ? "fpext" : kind_unsigned(source->kind) ? "uitofp" : "sitofp",
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
        op = src_bits > dst_bits ? "trunc" : kind_unsigned(source->kind) ? "zext" : "sext";
      else if (!src_fp)
        op = kind_unsigned(source->kind) ? "uitofp" : "sitofp";
      else if (!dst_fp) {
        int checked = a;
        if (src_bits != 64) {
          checked = ++o->tmp;
          fprintf(f, "  %%t%d = fpext %s %%t%d to double\n", checked, src_type, a);
        }
        fprintf(f, "  call void @rh_check_float_cast(double %%t%d, i32 %d, i32 %d, i32 %d)\n",
                checked, dst_bits, kind_unsigned(t->kind), i->loc.line);
        op = kind_unsigned(t->kind) ? "fptoui" : "fptosi";
      } else
        op = src_bits > dst_bits ? "fptrunc" : "fpext";
      if (src_fp && dst_fp && src_bits == dst_bits)
        r = a;
      else {
        r = ++o->tmp;
        fprintf(f, "  %%t%d = %s %s %%t%d to %s\n", r, op, src_type, a, llvm_type(t));
      }
    }
    save(o, i->dst, t, fmt("%%t%d", r));
    break;
  }
  case I_CALL:
    fprintf(f, "  call void @%s(", i->callee->symbol);
    if (t->kind != T_VOID)
      fprintf(f, "ptr %%p%d", i->dst);
    for (int j = 0; j < i->args.n; j++) {
      if (j || t->kind != T_VOID)
        fputs(", ", f);
      fprintf(f, "ptr %%p%d", (int)(intptr_t)i->args.v[j] - 1);
    }
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
  fputs("\ndeclare void @rh_region_init(ptr)\ndeclare void "
        "@rh_region_release(ptr)\ndeclare ptr @rh_array_push(ptr, ptr, i64, "
        "i32)\ndeclare ptr @rh_array_at(ptr, i64, i64, i32)\ndeclare void "
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
        "declare void @rh_check_float_cast(double, i32, i32, i32)\n", f);
  for (int j = 0; j < p->instances.n; j++) {
    Instance *in = p->instances.v[j];
    fprintf(f, "\n; %s%s%s\ndefine void @%s(", in->self ? in->self->name : "",
            in->self ? "." : "", in->fn->name, in->symbol);
    bool result = in->result->kind != T_VOID;
    if (result)
      fputs("ptr %out", f);
    for (int k = 0; k < in->params.n; k++)
      fprintf(f, "%sptr %%arg%d", k || result ? ", " : "", k);
    fputs(") {\nentry:\n", f);
    Output o = {f, in, 0};
    for (int k = 0; k < in->slots.n; k++) {
      Slot *s = in->slots.v[k];
      if (!s->alias)
        fprintf(f, "  %%p%d = alloca %s\n", k, llvm_type(s->type));
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
  fputs("\ndefine i32 @main() {\nentry:\n  call void @rh_fn_0()\n  ret i32 "
        "0\n}\n",
        f);
  if (fclose(f))
    fail((Loc){path, 1, 1}, "failed to write LLVM IR");
}
void dump_ir(Program *p, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    fail((Loc){path, 1, 1}, "cannot write R^ IR");
  const char *names[] = {"const",          "string",     "copy",
                         "zero",           "binary",     "unary",
                         "borrow",         "deref",      "field",
                         "index.checked",  "call",       "print",
                         "assert",         "label",      "branch",
                         "branch.if",      "return",     "region.construct",
                         "region.release", "array.push", "length", "numeric.convert"};
  for (int j = 0; j < p->defs.n; j++) {
    Def *d = p->defs.v[j];
    fprintf(f, "type %s size=%zu align=%zu%s\n", d->name, d->type->size,
            d->type->align, d->abstract ? " abstract" : "");
    size_t off = 0;
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
      if (i->type)
        fprintf(f, " type=%s", type_name(i->type));
      if (i->text)
        fprintf(f, " op=%s", i->op == I_STRING ? "<literal>" : i->text);
      if (i->callee)
        fprintf(f, " target=%s", i->callee->symbol);
      if (i->op == I_BR || i->op == I_CBR || i->op == I_LABEL)
        fprintf(f, " label=%d other=%d", i->label, i->other);
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
