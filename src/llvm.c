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
/* Non-owning values up to two machine words are LLVM values. Large and
 * owned results retain destination passing; no host C ABI is changed. */
static bool direct(Type *t) {
  return t->kind != T_VOID && !t->owned && t->size > 0 && t->size <= 16;
}
static char *stack_for(Output *o, int id) {
  int owner = ((Slot *)o->in->slots.v[id])->owner;
  if (o->in->result->owned && owner == o->in->result_slot)
    return "%result_stack";
  return fmt("%%s%d", owner);
}
/* A non-owning value cannot contain an owning array descriptor. Array
 * elements are non-owning; references cannot expose descriptor fields or
 * reinterpret storage. Keep aggregates containing owned fields unclassified
 * so their whole-value copies may alias either category. */
static const char *memory_tag(Type *t) {
  return t->kind == T_GROW ? ", !tbaa !5" : !t->owned ? ", !tbaa !4" : "";
}
static int load(Output *o, int n) {
  int t = ++o->tmp;
  Type *type = slot_type(o, n);
  if (type->kind == T_GROW) {
    /* SROA drops TBAA on aggregate loads. Scalar descriptor accesses retain
     * the distinction from element stores through optimizer decomposition. */
    int data = ++o->tmp, lenaddr = ++o->tmp, len = ++o->tmp;
    int stackaddr = ++o->tmp, stack = ++o->tmp;
    int first = ++o->tmp, second = ++o->tmp;
    const char *ll = llvm_type(type);
    fprintf(o->f, "  %%t%d = load ptr, ptr %%p%d, !tbaa !5\n", data, n);
    fprintf(o->f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 1\n",
            lenaddr, ll, n);
    fprintf(o->f, "  %%t%d = load i64, ptr %%t%d, !tbaa !5\n", len, lenaddr);
    fprintf(o->f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n",
            stackaddr, ll, n);
    fprintf(o->f, "  %%t%d = load ptr, ptr %%t%d, !tbaa !5\n", stack,
            stackaddr);
    fprintf(o->f, "  %%t%d = insertvalue %s undef, ptr %%t%d, 0\n", first, ll,
            data);
    fprintf(o->f, "  %%t%d = insertvalue %s %%t%d, i64 %%t%d, 1\n", second, ll,
            first, len);
    fprintf(o->f, "  %%t%d = insertvalue %s %%t%d, ptr %%t%d, 2\n", t, ll,
            second, stack);
    return t;
  }
  fprintf(o->f, "  %%t%d = load %s, ptr %%p%d%s\n", t, llvm_type(type), n,
          memory_tag(type));
  return t;
}
static void save(Output *o, int dst, Type *t, const char *operand) {
  fprintf(o->f, "  store %s %s, ptr %%p%d%s\n", llvm_type(t), operand, dst,
          memory_tag(t));
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
typedef struct {
  char *lo, *hi;
} Interval;
typedef struct {
  Output *o;
  Interval iv;
  char *valid;
} RangeOutput;
static char *range_binary(Output *o, const char *op, const char *a,
                          const char *b) {
  int id = ++o->tmp;
  fprintf(o->f, "  %%t%d = %s i128 %s, %s\n", id, op, a, b);
  return fmt("%%t%d", id);
}
static void range_require(RangeOutput *r, char *condition) {
  int id = ++r->o->tmp;
  fprintf(r->o->f, "  %%t%d = and i1 %s, %s\n", id, r->valid, condition);
  r->valid = fmt("%%t%d", id);
}
static void range_fit(RangeOutput *r, Interval v) {
  range_require(r,
                range_binary(r->o, "icmp sge", v.lo, "-9223372036854775808"));
  range_require(r, range_binary(r->o, "icmp sle", v.hi, "9223372036854775807"));
}
static char *range_select(Output *o, char *a, char *b, bool maximum) {
  char *condition = range_binary(o, maximum ? "icmp sgt" : "icmp slt", a, b);
  int id = ++o->tmp;
  fprintf(o->f, "  %%t%d = select i1 %s, i128 %s, i128 %s\n", id, condition, a,
          b);
  return fmt("%%t%d", id);
}
static Interval range_expression(RangeOutput *r, RangeExpr *e) {
  Output *o = r->o;
  FILE *f = o->f;
  if (e->kind == RANGE_IV)
    return r->iv;
  if (e->kind == RANGE_CONST)
    return (Interval){e->text, e->text};
  if (e->kind == RANGE_SLOT) {
    int value = load(o, e->slot), wide = ++o->tmp;
    fprintf(f, "  %%t%d = sext i64 %%t%d to i128\n", wide, value);
    return (Interval){fmt("%%t%d", wide), fmt("%%t%d", wide)};
  }
  if (e->kind == RANGE_LENGTH) {
    if (e->type->kind == T_ARRAY) {
      char *length = fmt("%d", e->type->count);
      return (Interval){length, length};
    }
    char *place = fmt("%%p%d", e->slot);
    if (e->indirect)
      place = fmt("%%t%d", load(o, e->slot));
    int ptr = ++o->tmp, len = ++o->tmp, wide = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr %s, ptr %s, i32 0, i32 1\n", ptr,
            llvm_type(e->type), place);
    fprintf(f, "  %%t%d = load i64, ptr %%t%d\n", len, ptr);
    fprintf(f, "  %%t%d = zext i64 %%t%d to i128\n", wide, len);
    return (Interval){fmt("%%t%d", wide), fmt("%%t%d", wide)};
  }
  Interval a = range_expression(r, e->a), b = range_expression(r, e->b), v;
  if (e->kind == RANGE_ADD) {
    v.lo = range_binary(o, "add", a.lo, b.lo);
    v.hi = range_binary(o, "add", a.hi, b.hi);
  } else if (e->kind == RANGE_SUB) {
    v.lo = range_binary(o, "sub", a.lo, b.hi);
    v.hi = range_binary(o, "sub", a.hi, b.lo);
  } else {
    char *values[] = {
        range_binary(o, "mul", a.lo, b.lo), range_binary(o, "mul", a.lo, b.hi),
        range_binary(o, "mul", a.hi, b.lo), range_binary(o, "mul", a.hi, b.hi)};
    v.lo = v.hi = values[0];
    for (int j = 1; j < 4; j++) {
      v.lo = range_select(o, v.lo, values[j], false);
      v.hi = range_select(o, v.hi, values[j], true);
    }
  }
  /* i128 arithmetic deliberately has no overflow flags. If a child did not
   * fit i64, its false validity bit survives all following computations. */
  range_fit(r, v);
  return v;
}
static void range_guard(Output *o, Instr *i) {
  RangePlan *plan = i->range;
  RangeOutput r = {o, {NULL, NULL}, "true"};
  Interval upper = range_expression(&r, plan->upper);
  int limit = ++o->tmp;
  fprintf(o->f, "  %%t%d = trunc i128 %s to i64\n", limit, upper.lo);
  save(o, plan->limit, slot_type(o, plan->limit), fmt("%%t%d", limit));
  int low = load(o, plan->induction), wide = ++o->tmp;
  fprintf(o->f, "  %%t%d = sext i64 %%t%d to i128\n", wide, low);
  r.iv.lo = fmt("%%t%d", wide);
  r.iv.hi = range_binary(o, "sub", upper.hi, "1");
  range_fit(&r, r.iv);
  range_require(&r, range_binary(o, "icmp sle", r.iv.lo, r.iv.hi));
  for (int j = 0; j < plan->indices.n; j++) {
    Interval index = range_expression(&r, plan->indices.v[j]);
    Interval length = range_expression(&r, plan->arrays.v[j]);
    range_require(&r, range_binary(o, "icmp sge", index.lo, "0"));
    range_require(&r, range_binary(o, "icmp slt", index.hi, length.lo));
  }
  fprintf(o->f, "  ; range.guard result=%%p%d\n", i->dst);
  save(o, i->dst, i->type, r.valid);
  /* The proof excludes container/reference mutation. Snapshot the data
   * pointers explicitly: byte stores otherwise obscure their invariance to
   * LLVM's alias analysis. Distinct arguments are still allowed to alias. */
  for (int j = 0; j < plan->arrays.n; j++) {
    RangeExpr *arr = plan->arrays.v[j];
    int slot = (int)(intptr_t)plan->data.v[j] - 1;
    char *place = fmt("%%p%d", arr->slot);
    if (arr->indirect)
      place = fmt("%%t%d", load(o, arr->slot));
    if (arr->type->kind != T_ARRAY) {
      int data = ++o->tmp;
      fprintf(o->f, "  %%t%d = load ptr, ptr %s\n", data, place);
      place = fmt("%%t%d", data);
    }
    save(o, slot, slot_type(o, slot), place);
  }
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
    {
      Type *index_type = slot_type(o, i->b);
      int bits = numeric_bits(index_type->kind);
      a = load(o, i->b);
      if (bits < 64) {
        b = ++o->tmp;
        fprintf(f, "  %%t%d = %s %s %%t%d to i64\n", b,
                kind_unsigned(index_type->kind) ? "zext" : "sext",
                llvm_type(index_type), a);
        a = b;
      }
      if (!i->index_proven) {
        char *length;
        if (t->kind == T_ARRAY)
          length = fmt("%d", t->count);
        else {
          b = ++o->tmp;
          r = ++o->tmp;
          fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 1\n", b,
                  llvm_type(t), i->a);
          fprintf(f, "  %%t%d = load i64, ptr %%t%d%s\n", r, b, memory_tag(t));
          length = fmt("%%t%d", r);
        }
        const char *compare_type = bits > 64 ? "i128" : "i64";
        if (bits > 64) {
          b = ++o->tmp;
          fprintf(f, "  %%t%d = zext i64 %s to i128\n", b, length);
          length = fmt("%%t%d", b);
        }
        int ok = ++o->tmp;
        /* Unsigned compare rejects a signed negative at its full width too.
         * Wide indices are truncated only after proving index < length. */
        fprintf(f, "  %%t%d = icmp ult %s %%t%d, %s\n", ok, compare_type, a,
                length);
        fprintf(
            f,
            "  br i1 %%t%d, label %%index_ok_%d, label %%index_bad_%d, !prof "
            "!0\nindex_bad_%d:\n",
            ok, ok, ok, ok);
        fprintf(
            f,
            "  call void @rh_index_fail(i32 %d)\n  unreachable\nindex_ok_%d:\n",
            i->loc.line, ok);
      }
      if (bits > 64) {
        b = ++o->tmp;
        fprintf(f, "  %%t%d = trunc i128 %%t%d to i64\n", b, a);
        a = b;
      }
      if (i->index_proven) {
        r = load(o, i->index_data);
        fprintf(f, "  %%p%d = getelementptr %s, ptr %%t%d, i64 %%t%d\n", i->dst,
                t->kind == T_STRING ? "i8" : llvm_type(t->elem), r, a);
      } else if (t->kind == T_ARRAY)
        fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i64 0, i64 %%t%d\n",
                i->dst, llvm_type(t), i->a, a);
      else {
        b = ++o->tmp;
        r = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n", b,
                llvm_type(t), i->a);
        fprintf(f, "  %%t%d = load ptr, ptr %%t%d%s\n", r, b, memory_tag(t));
        fprintf(f, "  %%p%d = getelementptr %s, ptr %%t%d, i64 %%t%d\n", i->dst,
                t->kind == T_STRING ? "i8" : llvm_type(t->elem), r, a);
      }
    }
    break;
  case I_RANGE_GUARD:
    range_guard(o, i);
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
    int bytes = ++o->tmp, off = ++o->tmp, remaining = ++o->tmp, ok = ++o->tmp;
    fprintf(f, "  %%t%d = sub i64 %%t%d, %%t%d\n", bytes, li, di);
    /* Every constructor/mutator maintains len * size <= available. Test
     * remaining bytes before adding a new element; no capacity division. */
    fprintf(f, "  %%t%d = mul i64 %%t%d, %zu\n", off, len, t->elem->size);
    fprintf(f, "  %%t%d = sub i64 %%t%d, %%t%d\n", remaining, bytes, off);
    fprintf(f, "  %%t%d = icmp uge i64 %%t%d, %zu\n", ok, remaining,
            t->elem->size);
    fprintf(f,
            "  br i1 %%t%d, label %%push_ok_%d, label %%push_bad_%d, !prof "
            "!0\npush_bad_%d:\n",
            ok, ok, ok, ok);
    fprintf(
        f,
        "  call void @rh_stack_overflow(i32 %d)\n  unreachable\npush_ok_%d:\n",
        i->loc.line, ok);
    int ptr = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr i8, ptr %%t%d, i64 %%t%d\n", ptr, data,
            off);
    a = load(o, i->b);
    fprintf(f, "  store %s %%t%d, ptr %%t%d\n", llvm_type(t->elem), a, ptr);
    int end = ++o->tmp, topaddr = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr i8, ptr %%t%d, i64 %zu\n", end, ptr,
            t->elem->size);
    fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %%t%d, i32 0, i32 1\n",
            topaddr, stack);
    if (i->push_at_top) {
      fprintf(f, "  store ptr %%t%d, ptr %%t%d\n", end, topaddr);
    } else {
      int top = ++o->tmp, greater = ++o->tmp, high = ++o->tmp;
      fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n", top, topaddr);
      fprintf(f, "  %%t%d = icmp ugt ptr %%t%d, %%t%d\n", greater, end, top);
      fprintf(f, "  %%t%d = select i1 %%t%d, ptr %%t%d, ptr %%t%d\n", high,
              greater, end, top);
      fprintf(f, "  store ptr %%t%d, ptr %%t%d\n", high, topaddr);
    }
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
  case I_POP: {
    int base = load(o, i->a), data = ++o->tmp, len = ++o->tmp;
    int valid = ++o->tmp, last = ++o->tmp, element = ++o->tmp;
    int value = ++o->tmp, lenaddr = ++o->tmp;
    fprintf(f, "  %%t%d = extractvalue %s %%t%d, 0\n", data, llvm_type(t),
            base);
    fprintf(f, "  %%t%d = extractvalue %s %%t%d, 1\n", len, llvm_type(t), base);
    fprintf(f, "  %%t%d = icmp ne i64 %%t%d, 0\n", valid, len);
    fprintf(f,
            "  br i1 %%t%d, label %%pop_ok_%d, label %%pop_bad_%d, !prof "
            "!0\npop_bad_%d:\n",
            valid, valid, valid, valid);
    fprintf(f,
            "  call void @rh_pop_fail(i32 %d)\n"
            "  unreachable\npop_ok_%d:\n",
            i->loc.line, valid);
    fprintf(f, "  %%t%d = sub i64 %%t%d, 1\n", last, len);
    fprintf(f, "  %%t%d = getelementptr %s, ptr %%t%d, i64 %%t%d\n", element,
            llvm_type(t->elem), data, last);
    fprintf(f, "  %%t%d = load %s, ptr %%t%d\n", value, llvm_type(t->elem),
            element);
    save(o, i->dst, t->elem, fmt("%%t%d", value));
    fprintf(f,
            "  %%t%d = getelementptr {ptr,i64,ptr}, ptr %%p%d, i32 0, i32 1\n",
            lenaddr, i->a);
    /* Shrinking changes only logical length. References keep the physical
     * storage alive, and a later push must retain its high-water mark. */
    fprintf(f, "  store i64 %%t%d, ptr %%t%d\n", last, lenaddr);
    break;
  }
  case I_CLEAR:
    a = ++o->tmp;
    fprintf(f,
            "  %%t%d = getelementptr {ptr,i64,ptr}, ptr %%p%d, i32 0, i32 1\n",
            a, i->a);
    fprintf(f, "  store i64 0, ptr %%t%d\n", a);
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
      int valid = ++o->tmp;
      fprintf(f, "  %%t%d = icmp eq i32 %%t%d, %d\n", valid, tag, i->label);
      fprintf(f,
              "  br i1 %%t%d, label %%tag_ok_%d, label %%tag_bad_%d, !prof "
              "!0\ntag_bad_%d:\n",
              valid, valid, valid, valid);
      fprintf(f,
              "  call void @rh_union_fail(i32 %d)\n  "
              "unreachable\ntag_ok_%d:\n",
              i->loc.line, valid);
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
  case I_TRUNCATE: {
    a = load(o, i->b);
    int lenaddr = ++o->tmp, len = ++o->tmp, nonnegative = ++o->tmp;
    int within = ++o->tmp, valid = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 1\n", lenaddr,
            llvm_type(t), i->a);
    fprintf(f, "  %%t%d = load i64, ptr %%t%d, !tbaa !5\n", len, lenaddr);
    fprintf(f, "  %%t%d = icmp sge i64 %%t%d, 0\n", nonnegative, a);
    fprintf(f, "  %%t%d = icmp ule i64 %%t%d, %%t%d\n", within, a, len);
    fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", valid, nonnegative, within);
    fprintf(f,
            "  br i1 %%t%d, label %%truncate_ok_%d, label %%truncate_bad_%d, "
            "!prof !0\ntruncate_bad_%d:\n",
            valid, valid, valid, valid);
    fprintf(f,
            "  call void @rh_truncate_fail(i32 %d)\n  "
            "unreachable\ntruncate_ok_%d:\n",
            i->loc.line, valid);
    fprintf(f, "  store i64 %%t%d, ptr %%t%d, !tbaa !5\n", a, lenaddr);
    break;
  }
  case I_APPEND:
    for (int k = 1; k < o->in->code.n; k++) {
      if (o->in->code.v[k] != i)
        continue;
      Instr *literal = o->in->code.v[k - 1];
      /* The immediately preceding instruction fixes the source value;
       * arbitrary views retain overlap-safe runtime memmove semantics. */
      if (literal->op == I_STRING && literal->dst == i->b &&
          t->elem->kind == T_U8) {
        fprintf(f,
                "  call void @rh_append_literal(ptr %%p%d, ptr @rh_str_%d, i64 "
                "%zu, i32 %d)\n",
                i->a, literal->label, strlen(literal->text), i->loc.line);
        return;
      }
      break;
    }
    a = load(o, i->b);
    b = ++o->tmp;
    r = ++o->tmp;
    fprintf(f, "  %%t%d = extractvalue {ptr,i64} %%t%d, 0\n", b, a);
    fprintf(f, "  %%t%d = extractvalue {ptr,i64} %%t%d, 1\n", r, a);
    fprintf(f,
            "  call void @rh_append_view(ptr %%p%d, ptr %%t%d, i64 %%t%d, i64 "
            "%zu, i32 %d)\n",
            i->a, b, r, t->elem->size, i->loc.line);
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
    int start_ok = ++o->tmp, count_ok = ++o->tmp, remaining = ++o->tmp;
    int start_positive = ++o->tmp, count_positive = ++o->tmp;
    int valid_start = ++o->tmp, valid_count = ++o->tmp, valid = ++o->tmp;
    fprintf(f, "  %%t%d = icmp ule i64 %%t%d, %s\n", start_ok, a, length);
    fprintf(f, "  %%t%d = sub i64 %s, %%t%d\n", remaining, length, a);
    fprintf(f, "  %%t%d = icmp ule i64 %%t%d, %%t%d\n", count_ok, b, remaining);
    fprintf(f, "  %%t%d = icmp sge i64 %%t%d, 0\n", start_positive, a);
    fprintf(f, "  %%t%d = icmp sge i64 %%t%d, 0\n", count_positive, b);
    fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", valid_start, start_ok,
            start_positive);
    fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", valid_count, count_ok,
            count_positive);
    fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", valid, valid_start,
            valid_count);
    fprintf(f,
            "  br i1 %%t%d, label %%slice_ok_%d, label %%slice_bad_%d, !prof "
            "!0\nslice_bad_%d:\n",
            valid, valid, valid, valid);
    fprintf(f,
            "  call void @rh_slice_fail(i32 %d)\n  unreachable\nslice_ok_%d:\n",
            i->loc.line, valid);
    int ptr = ++o->tmp, first = ++o->tmp, view = ++o->tmp;
    fprintf(f, "  %%t%d = getelementptr %s, ptr %s, i64 %%t%d\n", ptr,
            t->kind == T_STRING ? "i8" : llvm_type(t->elem), data, a);
    fprintf(f, "  %%t%d = insertvalue {ptr,i64} undef, ptr %%t%d, 0\n", first,
            ptr);
    fprintf(f, "  %%t%d = insertvalue {ptr,i64} %%t%d, i64 %%t%d, 1\n", view,
            first, b);
    save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", view));
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
              "load i64, ptr %%t%d%s\n",
              a, llvm_type(t), i->a, b, a, memory_tag(t));
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
      int nonzero = ++o->tmp;
      fprintf(f, "  %%t%d = icmp ne %s %%t%d, 0\n", nonzero, operand_type, b);
      int ok = nonzero;
      if (!uns) {
        int bits = numeric_bits(t->kind);
        const char *minimum =
            bits == 128 ? "-170141183460469231731687303715884105728"
            : bits == 64
                ? "-9223372036854775808"
                : fmt("-%llu", (unsigned long long)(UINT64_C(1) << (bits - 1)));
        int ismin = ++o->tmp, negone = ++o->tmp, bad = ++o->tmp,
            valid = ++o->tmp;
        fprintf(f, "  %%t%d = icmp eq %s %%t%d, %s\n", ismin, operand_type, a,
                minimum);
        fprintf(f, "  %%t%d = icmp eq %s %%t%d, -1\n", negone, operand_type, b);
        fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", bad, ismin, negone);
        fprintf(f, "  %%t%d = xor i1 %%t%d, true\n", valid, bad);
        ok = ++o->tmp;
        fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", ok, nonzero, valid);
      }
      fprintf(f,
              "  br i1 %%t%d, label %%division_ok_%d, label %%division_bad_%d, "
              "!prof !0\ndivision_bad_%d:\n",
              ok, ok, ok, ok);
      fprintf(f,
              "  call void @rh_division_fail(i32 %d, i32 %d)\n  "
              "unreachable\ndivision_ok_%d:\n",
              !strcmp(op, "%"), i->loc.line, ok);
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
        bool uns = kind_unsigned(t->kind);
        uint64_t upper = (uint64_t)(1023 + dst_bits - (uns ? 0 : 1)) << 52;
        uint64_t lower = uns ? 0 : upper | (UINT64_C(1) << 63);
        int low = ++o->tmp, high = ++o->tmp, valid = ++o->tmp;
        fprintf(f, "  %%t%d = fcmp oge double %%t%d, 0x%016llX\n", low, checked,
                (unsigned long long)lower);
        fprintf(f, "  %%t%d = fcmp olt double %%t%d, 0x%016llX\n", high,
                checked, (unsigned long long)upper);
        fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", valid, low, high);
        fprintf(f,
                "  br i1 %%t%d, label %%cast_ok_%d, label %%cast_bad_%d, !prof "
                "!0\ncast_bad_%d:\n",
                valid, valid, valid, valid);
        fprintf(f,
                "  call void @rh_float_cast_fail(i32 %d)\n  "
                "unreachable\ncast_ok_%d:\n",
                i->loc.line, valid);
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
  case I_CALL: {
    a = ++o->tmp;
    fprintf(f, "  %%t%d = add i64 %%sp, %d\n", a, i->stack_index);
    int *values = mem((size_t)i->args.n * sizeof(int));
    for (int j = 0; j < i->args.n; j++)
      if (direct(i->callee->params.v[j]))
        values[j] = load(o, (int)(intptr_t)i->args.v[j] - 1);
    r = ++o->tmp;
    if (direct(t))
      fprintf(f, "  %%t%d = call %s @%s(i64 %%t%d", r, llvm_type(t),
              i->callee->symbol, a);
    else
      fprintf(f, "  call void @%s(i64 %%t%d", i->callee->symbol, a);
    if (t->kind != T_VOID && !direct(t))
      fprintf(f, ", ptr %%p%d", i->dst);
    if (t->owned)
      fprintf(f, ", ptr %s", stack_for(o, i->dst));
    for (int j = 0; j < i->args.n; j++) {
      Type *param = i->callee->params.v[j];
      if (direct(param))
        fprintf(f, ", %s %%t%d", llvm_type(param), values[j]);
      else
        fprintf(f, ", ptr %%p%d", (int)(intptr_t)i->args.v[j] - 1);
    }
    fputs(")\n", f);
    if (direct(t))
      save(o, i->dst, t, fmt("%%t%d", r));
    break;
  }
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
  case I_ASSERT: {
    a = integer64(o, (int)(intptr_t)i->args.v[0] - 1);
    int valid = ++o->tmp;
    fprintf(f, "  %%t%d = icmp ne i64 %%t%d, 0\n", valid, a);
    fprintf(f,
            "  br i1 %%t%d, label %%assert_ok_%d, label %%assert_bad_%d, !prof "
            "!0\nassert_bad_%d:\n",
            valid, valid, valid, valid);
    fprintf(
        f,
        "  call void @rh_assert_fail(i32 %d)\n  unreachable\nassert_ok_%d:\n",
        i->loc.line, valid);
    break;
  }
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
    if (direct(o->in->result)) {
      r = load(o, o->in->result_slot);
      fprintf(f, "  ret %s %%t%d\n", llvm_type(o->in->result), r);
    } else
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
  emit_llvm_builtins(f);
  fputs("declare void @rh_runtime_init(ptr, i64)\n"
        "declare void @rh_runtime_shutdown()\n"
        "declare void @rh_print_f64(double)\n"
        "declare void @rh_print_bool(i64)\n"
        "declare void @rh_print_string(ptr, i64)\n"
        "declare void @rh_print_newline()\n"
        "declare void @rh_print_space()\n"
        "declare void @rh_print_integer(ptr, i32, i32)\n"
        "declare void @rh_division_fail(i32, i32) cold noreturn\n",
        f);
  const char *failures[] = {"stack_overflow", "index_fail",      "pop_fail",
                            "union_fail",     "float_cast_fail", "slice_fail",
                            "truncate_fail",  "assert_fail"};
  for (size_t j = 0; j < sizeof(failures) / sizeof(failures[0]); j++)
    fprintf(f, "declare void @rh_%s(i32) cold noreturn\n", failures[j]);
  for (int j = 0; j < p->instances.n; j++) {
    Instance *in = p->instances.v[j];
    fprintf(f, "\n; %s%s%s\ndefine internal %s @%s(",
            in->self ? in->self->name : "", in->self ? "." : "", in->fn->name,
            direct(in->result) ? llvm_type(in->result) : "void", in->symbol);
    bool result = in->result->kind != T_VOID;
    fputs("i64 %sp", f);
    if (result && !direct(in->result))
      fputs(", ptr %out", f);
    if (in->result->owned)
      fputs(", ptr %result_stack", f);
    for (int k = 0; k < in->params.n; k++) {
      Type *param = in->params.v[k];
      fprintf(f, ", %s %%arg%d", direct(param) ? llvm_type(param) : "ptr", k);
    }
    fputs(") {\nentry:\n", f);
    Output o = {f, in, 0};
    for (int k = 0; k < in->slots.n; k++) {
      Slot *s = in->slots.v[k];
      if (!s->alias || (result && direct(in->result) && k == in->result_slot))
        fprintf(f, "  %%p%d = alloca %s\n", k, llvm_type(s->type));
    }
    for (int k = 0; k < in->code.n; k++) {
      Instr *i = in->code.v[k];
      if (i->op == I_STACK_BEGIN)
        fprintf(f, "  %%mark%d = alloca ptr\n", i->dst);
    }
    if (result && !direct(in->result))
      fprintf(f, "  %%p%d = getelementptr i8, ptr %%out, i64 0\n",
              in->result_slot);
    for (int k = 0; k < in->params.n; k++) {
      Type *t = in->params.v[k];
      int id = k + (result ? 1 : 0);
      if (direct(t))
        save(&o, id, t, fmt("%%arg%d", k));
      else {
        int r = ++o.tmp;
        fprintf(f, "  %%t%d = load %s, ptr %%arg%d\n", r, llvm_type(t), k);
        save(&o, id, t, fmt("%%t%d", r));
      }
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
  fputs("\n!0 = !{!\"branch_weights\", i32 1048575, i32 1}\n", f);
  fputs("!1 = !{!\"Rhat storage\"}\n"
        "!2 = !{!\"non-owning value\", !1, i64 0}\n"
        "!3 = !{!\"owning array descriptor\", !1, i64 0}\n"
        "!4 = !{!2, !2, i64 0}\n"
        "!5 = !{!3, !3, i64 0}\n",
        f);
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
                         "stack.save",
                         "range.guard"};
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
      if (i->op == I_PUSH)
        fprintf(f, " top=%s", i->push_at_top ? "append" : "retained");
      if (i->op == I_INDEX && i->index_proven)
        fputs(" range=proven", f);
      if (i->op == I_RANGE_GUARD)
        fprintf(f, " induction=%%%d indices=%d fallback=checked",
                i->range->induction, i->range->indices.n);
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
