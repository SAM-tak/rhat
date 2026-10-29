#include "rhat.h"

char *llvm_type(Type *t)
{
    switch (t->kind) {
    case T_VOID:
        return "void";
    case T_BOOL:
        return "i1";
    case T_NIL:
    case T_FUNCTION:
    case T_INTERSECTION:
    case T_U8:
    case T_I8:
    case T_F8:
        return "i8";
    case T_U16:
    case T_I16:
        return "i16";
    case T_U32:
    case T_I32:
    case T_CHAR:
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
    case T_DYN:
        return "{ ptr, ptr }";
    case T_REF:
    case T_CFUNC:
        return "ptr";
    case T_ARRAY:
        return fmt("[%d x %s]", t->count, llvm_type(t->elem));
    case T_GROW:
        return "{ ptr, i64, ptr }";
    default:
        return t->symbol ? t->symbol : fmt("%%rh_type_%d", t->id);
    }
}

typedef struct {
    FILE *f;
    Instance *in;
    int tmp;
    Program *p;
} Output;

static Type *slot_type(Output *o, int n)
{
    return ((Slot *)o->in->slots.v[n])->type;
}

// Non-owning values up to two machine words are LLVM values. Large and
// owned results retain destination passing; no host C ABI is changed.
static bool direct(Type *t)
{
    return t->kind != T_VOID && !t->owned && t->size > 0 && t->size <= 16;
}

static char *stack_for(Output *o, int id)
{
    int owner = ((Slot *)o->in->slots.v[id])->owner;
    if (o->in->result->owned && owner == o->in->result_slot) {
        return "%result_stack";
    }
    return fmt("%%s%d", owner);
}

// A non-owning value cannot contain an owning array descriptor. Array
// elements are non-owning; references cannot expose descriptor fields or
// reinterpret storage. Keep aggregates containing owned fields unclassified
// so their whole-value copies may alias either category.
static const char *memory_tag(Type *t)
{
    return t->kind == T_GROW ? ", !tbaa !5" : !t->owned ? ", !tbaa !4" : "";
}

static int load(Output *o, int n)
{
    int t = ++o->tmp;
    Type *type = slot_type(o, n);
    if (type->kind == T_GROW) {
        // SROA drops TBAA on aggregate loads. Scalar descriptor accesses retain
        // the distinction from element stores through optimizer decomposition.
        int data = ++o->tmp, lenaddr = ++o->tmp, len = ++o->tmp;
        int stackaddr = ++o->tmp, stack = ++o->tmp;
        int first = ++o->tmp, second = ++o->tmp;
        const char *ll = llvm_type(type);
        fprintf(o->f, "  %%t%d = load ptr, ptr %%p%d, !tbaa !5\n", data, n);
        fprintf(o->f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 1\n", lenaddr, ll, n);
        fprintf(o->f, "  %%t%d = load i64, ptr %%t%d, !tbaa !5\n", len, lenaddr);
        fprintf(o->f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n", stackaddr, ll, n);
        fprintf(o->f, "  %%t%d = load ptr, ptr %%t%d, !tbaa !5\n", stack, stackaddr);
        fprintf(o->f, "  %%t%d = insertvalue %s undef, ptr %%t%d, 0\n", first, ll, data);
        fprintf(o->f, "  %%t%d = insertvalue %s %%t%d, i64 %%t%d, 1\n", second, ll, first, len);
        fprintf(o->f, "  %%t%d = insertvalue %s %%t%d, ptr %%t%d, 2\n", t, ll, second, stack);
        return t;
    }
    fprintf(o->f, "  %%t%d = load %s, ptr %%p%d%s\n", t, llvm_type(type), n, memory_tag(type));
    return t;
}

static void save(Output *o, int dst, Type *t, const char *operand)
{
    fprintf(o->f, "  store %s %s, ptr %%p%d%s\n", llvm_type(t), operand, dst, memory_tag(t));
}

static int integer64(Output *o, int id)
{
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

static void bytes(FILE *f, const char *s, size_t length)
{
    fputs("c\"", f);
    for (size_t j = 0; j < length; j++) {
        fprintf(f, "\\%02X", (unsigned char)s[j]);
    }
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

static char *range_binary(Output *o, const char *op, const char *a, const char *b)
{
    int id = ++o->tmp;
    fprintf(o->f, "  %%t%d = %s i128 %s, %s\n", id, op, a, b);
    return fmt("%%t%d", id);
}

static void range_require(RangeOutput *r, char *condition)
{
    int id = ++r->o->tmp;
    fprintf(r->o->f, "  %%t%d = and i1 %s, %s\n", id, r->valid, condition);
    r->valid = fmt("%%t%d", id);
}

static void range_fit(RangeOutput *r, Interval v)
{
    range_require(r, range_binary(r->o, "icmp sge", v.lo, "-9223372036854775808"));
    range_require(r, range_binary(r->o, "icmp sle", v.hi, "9223372036854775807"));
}

static char *range_select(Output *o, char *a, char *b, bool maximum)
{
    char *condition = range_binary(o, maximum ? "icmp sgt" : "icmp slt", a, b);
    int id = ++o->tmp;
    fprintf(o->f, "  %%t%d = select i1 %s, i128 %s, i128 %s\n", id, condition, a, b);
    return fmt("%%t%d", id);
}

static Interval range_expression(RangeOutput *r, RangeExpr *e)
{
    Output *o = r->o;
    FILE *f = o->f;
    if (e->kind == RANGE_IV) {
        return r->iv;
    }
    if (e->kind == RANGE_CONST) {
        return (Interval){e->text, e->text};
    }
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
        if (e->indirect) {
            place = fmt("%%t%d", load(o, e->slot));
        }
        int ptr = ++o->tmp, len = ++o->tmp, wide = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr %s, ptr %s, i32 0, i32 1\n", ptr, llvm_type(e->type),
                place);
        fprintf(f, "  %%t%d = load i64, ptr %%t%d\n", len, ptr);
        fprintf(f, "  %%t%d = zext i64 %%t%d to i128\n", wide, len);
        return (Interval){fmt("%%t%d", wide), fmt("%%t%d", wide)};
    }
    Interval a = range_expression(r, e->a), b = range_expression(r, e->b), v;
    if (e->kind == RANGE_ADD) {
        v.lo = range_binary(o, "add", a.lo, b.lo);
        v.hi = range_binary(o, "add", a.hi, b.hi);
    }
    else if (e->kind == RANGE_SUB) {
        v.lo = range_binary(o, "sub", a.lo, b.hi);
        v.hi = range_binary(o, "sub", a.hi, b.lo);
    }
    else {
        char *values[] = {range_binary(o, "mul", a.lo, b.lo), range_binary(o, "mul", a.lo, b.hi),
                          range_binary(o, "mul", a.hi, b.lo), range_binary(o, "mul", a.hi, b.hi)};
        v.lo = v.hi = values[0];
        for (int j = 1; j < 4; j++) {
            v.lo = range_select(o, v.lo, values[j], false);
            v.hi = range_select(o, v.hi, values[j], true);
        }
    }
    // i128 arithmetic deliberately has no overflow flags. If a child did not
    // fit i64, its false validity bit survives all following computations.
    range_fit(r, v);
    return v;
}

static void range_guard(Output *o, Instr *i)
{
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
    // The proof excludes container/reference mutation. Snapshot the data
    // pointers explicitly: byte stores otherwise obscure their invariance to
    // LLVM's alias analysis. Distinct arguments are still allowed to alias.
    for (int j = 0; j < plan->arrays.n; j++) {
        RangeExpr *arr = plan->arrays.v[j];
        int slot = (int)(intptr_t)plan->data.v[j] - 1;
        char *place = fmt("%%p%d", arr->slot);
        if (arr->indirect) {
            place = fmt("%%t%d", load(o, arr->slot));
        }
        if (arr->type->kind != T_ARRAY) {
            int data = ++o->tmp;
            fprintf(o->f, "  %%t%d = load ptr, ptr %s\n", data, place);
            place = fmt("%%t%d", data);
        }
        save(o, slot, slot_type(o, slot), place);
    }
}

// A method table has one address in the whole program so that fits^ can
// compare it; every IR module using it emits the same linkonce_odr copy.
static char *table_symbol(DynTable *t)
{
    return fmt("rh_dyn_%s", link_identity(fmt("%s:%s", t->concrete->name, t->iface->name)));
}

// Whether table t holds a type that the dyn^ test or cast toward type accepts
// on a value whose dyn^ def is from.
static bool table_matches(DynTable *t, Def *from, Type *type)
{
    return t->iface == from && (type->kind == T_DYN ? derives(t->concrete, type->elem->def)
                                                    : t->concrete == type->elem->def);
}

// Whether instruction i of in names table t.
static bool table_used(Program *p, Instance *in, Instr *i, DynTable *t)
{
    if (i->op == I_DYN) {
        return i->label == t->id;
    }
    if (i->op != I_DYNIS && i->op != I_DYNCAST) {
        return false;
    }
    Def *from = ((Slot *)in->slots.v[i->a])->type->elem->def;
    if (table_matches(t, from, i->type)) {
        return true;
    }
    for (int j = 0; i->op == I_DYNCAST && j < p->dyn_tables.n; j++) {
        DynTable *source = p->dyn_tables.v[j];
        if (table_matches(source, from, i->type) && source->concrete == t->concrete &&
            t->iface == i->type->elem->def) {
            return true;
        }
    }
    return false;
}

static DynTable *table_for(Program *p, Def *concrete, Def *iface)
{
    for (int j = 0; j < p->dyn_tables.n; j++) {
        DynTable *t = p->dyn_tables.v[j];
        if (t->concrete == concrete && t->iface == iface) {
            return t;
        }
    }
    return NULL;
}

// The C ABI spelling of an externC^ parameter or result type. A slice or
// string passes its data pointer only; small integers carry their extension.
static char *c_abi(Type *t, bool attributes)
{
    // A union here is ref^ T | nil^, a pointer that may be NULL.
    if (t->kind == T_REF || t->kind == T_SLICE || t->kind == T_STRING || t->kind == T_UNION) {
        return "ptr";
    }
    const char *extension = "";
    if (attributes && (t->kind == T_BOOL || t->kind == T_U8 || t->kind == T_U16)) {
        extension = "zeroext ";
    }
    else if (attributes && (t->kind == T_I8 || t->kind == T_I16)) {
        extension = "signext ";
    }
    return fmt("%s%s", extension, llvm_type(t));
}

// A parameter's attributes follow its type, unlike a result's.
static char *c_param(Type *t)
{
    bool zero = t->kind == T_BOOL || t->kind == T_U8 || t->kind == T_U16;
    bool sign = t->kind == T_I8 || t->kind == T_I16;
    return fmt("%s%s", c_abi(t, false), zero ? " zeroext" : sign ? " signext" : "");
}

// LLVM IR does not lower C's struct passing for the target, so a C function
// taking or returning a def^ by value is reached through a generated C shim
// that takes those values by pointer.
static bool needs_shim(Instance *in)
{
    // With includeC^, every call goes through the header's own prototype.
    bool any = in->result->kind == T_STRUCT || in->fn->module->c_headers.n;
    for (int j = 0; j < in->params.n; j++) {
        any |= ((Type *)in->params.v[j])->kind == T_STRUCT;
    }
    return !in->fn->intrinsic && any;
}

// The tag of the ref^ alternative of a nullable C pointer type.
static int pointer_tag(Type *t)
{
    return ((Type *)t->def->alternatives.v[0])->kind == T_NIL;
}

// A direct C call without the stack position or result destination.
static void external_invoke(Output *o, Instr *i)
{
    bool shim = needs_shim(i->callee);
    char *operands = "";
    if (shim && i->type->kind == T_STRUCT) {
        operands = fmt("ptr %%p%d", i->dst);
    }
    for (int j = 0; j < i->args.n; j++) {
        Type *param = i->callee->params.v[j];
        int id = (int)(intptr_t)i->args.v[j] - 1;
        char *sep = *operands ? ", " : "";
        if (param->kind == T_STRUCT) {
            operands = fmt("%s%sptr %%p%d", operands, sep, id);
            continue;
        }
        if (param->kind == T_UNION) {
            int tagp = ++o->tmp, tag = ++o->tmp, payload = ++o->tmp, ptr = ++o->tmp;
            int present = ++o->tmp, arg = ++o->tmp;
            fprintf(o->f,
                    "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n"
                    "  %%t%d = load i32, ptr %%t%d\n"
                    "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n"
                    "  %%t%d = load ptr, ptr %%t%d\n"
                    "  %%t%d = icmp eq i32 %%t%d, %d\n"
                    "  %%t%d = select i1 %%t%d, ptr %%t%d, ptr null\n",
                    tagp, llvm_type(param), id, tag, tagp, payload, llvm_type(param), id, ptr,
                    payload, present, tag, pointer_tag(param), arg, present, ptr);
            operands = fmt("%s%sptr %%t%d", operands, sep, arg);
            continue;
        }
        int v = load(o, id);
        if (param->kind == T_SLICE || param->kind == T_STRING) {
            int data = ++o->tmp;
            fprintf(o->f, "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n", data, v);
            v = data;
        }
        operands = fmt("%s%s%s %%t%d", operands, sep, c_abi(param, false), v);
    }
    const char *symbol = shim ? fmt("rh_shim_%s", i->callee->symbol) : i->callee->symbol;
    if (i->type->kind == T_VOID || (shim && i->type->kind == T_STRUCT)) {
        fprintf(o->f, "  call void @%s(%s)\n", symbol, operands);
        return;
    }
    int r = ++o->tmp;
    fprintf(o->f, "  %%t%d = call %s @%s(%s)\n", r, c_abi(i->type, false), symbol, operands);
    if (i->type->kind == T_UNION) {
        // A NULL result becomes nil^.
        int present = ++o->tmp, tag = ++o->tmp, tagp = ++o->tmp, payload = ++o->tmp;
        fprintf(o->f,
                "  %%t%d = icmp ne ptr %%t%d, null\n"
                "  %%t%d = select i1 %%t%d, i32 %d, i32 %d\n"
                "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n"
                "  store i32 %%t%d, ptr %%t%d\n"
                "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n"
                "  store ptr %%t%d, ptr %%t%d\n",
                present, r, tag, present, pointer_tag(i->type), !pointer_tag(i->type), tagp,
                llvm_type(i->type), i->dst, tag, tagp, payload, llvm_type(i->type), i->dst, r,
                payload);
        return;
    }
    save(o, i->dst, i->type, fmt("%%t%d", r));
}

// A C call may enter exportC^ functions, which use the data stacks above
// those this frame keeps live at the call.
static void external_call(Output *o, Instr *i)
{
    int saved = 0;
    if (o->p->callbacks && !i->callee->fn->intrinsic) {
        saved = ++o->tmp;
        int mark = ++o->tmp;
        fprintf(o->f,
                "  %%t%d = load i64, ptr @rh_callback_sp\n"
                "  %%t%d = add i64 %%sp, %d\n"
                "  store i64 %%t%d, ptr @rh_callback_sp\n",
                saved, mark, i->stack_index, mark);
    }
    external_invoke(o, i);
    if (saved) {
        fprintf(o->f, "  store i64 %%t%d, ptr @rh_callback_sp\n", saved);
    }
}

// An exportC^ function's entry from its C wrapper: by-value def^ values come
// by pointer, a nullable pointer as a plain one, and the data stacks start
// where the interrupted R^ call left them.
static void export_entry(Instance *in, FILE *f)
{
    Type *t = in->result;
    // A string (exportL^ only) comes as data and length, and goes back
    // through two out pointers.
    bool out = t->kind == T_STRUCT, text = t->kind == T_STRING;
    fprintf(f, "\ndefine %s @rh_entry_%s(%s",
            out || text || t->kind == T_VOID ? "void" : c_abi(t, true), in->symbol,
            out    ? "ptr %out"
            : text ? "ptr %text, ptr %length"
                   : "");
    for (int j = 0; j < in->params.n; j++) {
        Type *param = in->params.v[j];
        if (param->kind == T_STRING) {
            fprintf(f, "%sptr %%c%d, i64 %%l%d", j || out || text ? ", " : "", j, j);
            continue;
        }
        fprintf(f, "%s%s %%c%d", j || out || text ? ", " : "",
                param->kind == T_STRUCT ? "ptr" : c_param(param), j);
    }
    fprintf(f, ") {\nentry:\n  %%base = call i64 @rh_callback_enter(i64 %d)\n", in->callback_need);
    char *args = "";
    for (int j = 0; j < in->params.n; j++) {
        Type *param = in->params.v[j];
        char *ll = llvm_type(param);
        if (param->kind == T_UNION) {
            fprintf(
                f,
                "  %%u%d = alloca %s\n  store %s zeroinitializer, ptr %%u%d\n"
                "  %%n%d = icmp ne ptr %%c%d, null\n"
                "  %%g%d = select i1 %%n%d, i32 %d, i32 %d\n"
                "  %%h%d = getelementptr %s, ptr %%u%d, i32 0, i32 0\n  store i32 %%g%d, ptr %%h%d\n"
                "  %%q%d = getelementptr %s, ptr %%u%d, i32 0, i32 2\n  store ptr %%c%d, ptr %%q%d\n",
                j, ll, ll, j, j, j, j, j, pointer_tag(param), !pointer_tag(param), j, ll, j, j, j,
                j, ll, j, j, j);
            fprintf(f, "  %%v%d = load %s, ptr %%u%d\n", j, ll, j);
            args = fmt("%s, %s %%v%d", args, ll, j);
        }
        else if (param->kind == T_STRING) {
            fprintf(f,
                    "  %%s%d = insertvalue { ptr, i64 } undef, ptr %%c%d, 0\n"
                    "  %%v%d = insertvalue { ptr, i64 } %%s%d, i64 %%l%d, 1\n",
                    j, j, j, j, j);
            args = fmt("%s, { ptr, i64 } %%v%d", args, j);
        }
        else if (param->kind == T_STRUCT && direct(param)) {
            fprintf(f, "  %%v%d = load %s, ptr %%c%d\n", j, ll, j);
            args = fmt("%s, %s %%v%d", args, ll, j);
        }
        else {
            args = fmt("%s, %s %%c%d", args, param->kind == T_STRUCT ? "ptr" : ll, j);
        }
    }
    if (t->kind == T_VOID || (out && !direct(t))) {
        fprintf(f, "  call void @%s(i64 %%base%s%s)\n  ret void\n}\n", in->symbol,
                out ? ", ptr %out" : "", args);
        return;
    }
    fprintf(f, "  %%r = call %s @%s(i64 %%base%s)\n", llvm_type(t), in->symbol, args);
    if (out) {
        fprintf(f, "  store %s %%r, ptr %%out\n  ret void\n}\n", llvm_type(t));
    }
    else if (text) {
        fputs("  %rt = extractvalue { ptr, i64 } %r, 0\n  store ptr %rt, ptr %text\n"
              "  %rl = extractvalue { ptr, i64 } %r, 1\n  store i64 %rl, ptr %length\n"
              "  ret void\n}\n",
              f);
    }
    else if (t->kind == T_UNION) {
        fprintf(f,
                "  %%ru = alloca %s\n  store %s %%r, ptr %%ru\n"
                "  %%rh = getelementptr %s, ptr %%ru, i32 0, i32 0\n  %%rg = load i32, ptr %%rh\n"
                "  %%rq = getelementptr %s, ptr %%ru, i32 0, i32 2\n  %%rp = load ptr, ptr %%rq\n"
                "  %%rn = icmp eq i32 %%rg, %d\n  %%rv = select i1 %%rn, ptr %%rp, ptr null\n"
                "  ret ptr %%rv\n}\n",
                llvm_type(t), llvm_type(t), llvm_type(t), llvm_type(t), pointer_tag(t));
    }
    else {
        fprintf(f, "  ret %s %%r\n}\n", llvm_type(t));
    }
}

// Declare a C function once per IR module. An f^ declaration over scalars
// promises a result that depends only on its arguments.
static void external_declaration(Program *p, int index, FILE *f)
{
    Instance *in = p->instances.v[index];
    for (int j = 0; j < index; j++) {
        Instance *other = p->instances.v[j];
        if (other->emitted && other->fn->external && !strcmp(other->symbol, in->symbol)) {
            return;
        }
    }
    if (in->fn->intrinsic) {
        // LLVM supplies the attributes of its own intrinsics.
        fprintf(f, "\ndeclare %s @%s(", in->result->kind == T_VOID ? "void" : llvm_type(in->result),
                in->symbol);
        for (int j = 0; j < in->params.n; j++) {
            fprintf(f, "%s%s", j ? ", " : "", c_abi(in->params.v[j], false));
        }
        fputs(")\n", f);
        return;
    }
    if (needs_shim(in)) {
        bool out = in->result->kind == T_STRUCT;
        fprintf(f, "\ndeclare %s @rh_shim_%s(%s",
                out || in->result->kind == T_VOID ? "void" : c_abi(in->result, true), in->symbol,
                out ? "ptr" : "");
        for (int j = 0; j < in->params.n; j++) {
            Type *param = in->params.v[j];
            fprintf(f, "%s%s", j || out ? ", " : "",
                    param->kind == T_STRUCT ? "ptr" : c_param(param));
        }
        fputs(") nounwind\n", f);
        return;
    }
    bool pure = !in->fn->procedure;
    fprintf(f, "\ndeclare %s @%s(", in->result->kind == T_VOID ? "void" : c_abi(in->result, true),
            in->symbol);
    for (int j = 0; j < in->params.n; j++) {
        Type *param = in->params.v[j];
        pure &= param->kind != T_REF && param->kind != T_SLICE && param->kind != T_STRING;
        fprintf(f, "%s%s", j ? ", " : "", c_param(param));
    }
    fprintf(f, ") nounwind%s\n", pure ? " memory(none)" : "");
}

// Declare the C functions whose addresses the emitted code takes, once and
// unless a direct call already declared them.
static void address_declarations(Program *p, FILE *f)
{
    List done = {0};
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->emitted && in->fn->external && !in->fn->intrinsic && !needs_shim(in)) {
            add(&done, in->symbol);
        }
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        for (int k = 0; in->emitted && !in->fn->external && k < in->code.n; k++) {
            Instr *i = in->code.v[k];
            bool seen = !(i->op == I_CONST && i->type->kind == T_CFUNC);
            for (int m = 0; !seen && m < done.n; m++) {
                seen = !strcmp(done.v[m], i->text + 1);
            }
            if (!seen) {
                add(&done, i->text + 1);
                fprintf(f, "\ndeclare void %s()\n", i->text);
            }
        }
    }
}

// Lower the stack top of a shrunk flex^ array to its new length.
static void flex_release(Output *o, Instr *i, const char *length)
{
    if (!i->type->count) {
        return;
    }
    int desc = load(o, i->a), data = ++o->tmp, stack = ++o->tmp, bytes = ++o->tmp;
    fprintf(o->f,
            "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 0\n"
            "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 2\n"
            "  %%t%d = mul i64 %s, %zu\n"
            "  call void @rh_flex_release(ptr %%t%d, ptr %%t%d, i64 %%t%d)\n",
            data, desc, stack, desc, bytes, length, i->type->elem->size, stack, data, bytes);
}

static void instruction(Output *o, Instr *i)
{
    FILE *f = o->f;
    Type *t = i->type;
    int a, b, r;
    switch (i->op) {
    case I_CONST:
        save(o, i->dst, t, i->text);
        break;
    case I_STRING:
        save(o, i->dst, t, fmt("{ ptr @%s_str_%d, i64 %zu }", o->in->symbol, i->label, i->length));
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
        fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i32 0, i32 %d\n", i->dst, llvm_type(t),
                i->a, i->label);
        break;
    case I_INDEX:
        if (i->b < 0) {
            fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i64 0, i64 %d\n", i->dst,
                    llvm_type(t), i->a, i->label);
            break;
        }
        {
            Type *index_type = slot_type(o, i->b);
            int bits = numeric_bits(index_type->kind);
            a = load(o, i->b);
            if (bits < 64) {
                b = ++o->tmp;
                fprintf(f, "  %%t%d = %s %s %%t%d to i64\n", b,
                        kind_unsigned(index_type->kind) ? "zext" : "sext", llvm_type(index_type),
                        a);
                a = b;
            }
            if (!i->index_proven) {
                char *length;
                if (t->kind == T_ARRAY) {
                    length = fmt("%d", t->count);
                }
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
                // Unsigned compare rejects a signed negative at its full width too.
                // Wide indices are truncated only after proving index < length.
                fprintf(f, "  %%t%d = icmp ult %s %%t%d, %s\n", ok, compare_type, a, length);
                fprintf(f,
                        "  br i1 %%t%d, label %%index_ok_%d, label %%index_bad_%d, !prof "
                        "!0\nindex_bad_%d:\n",
                        ok, ok, ok, ok);
                fprintf(f, "  call void @rh_index_fail(i32 %d)\n  unreachable\nindex_ok_%d:\n",
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
            }
            else if (t->kind == T_ARRAY) {
                fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i64 0, i64 %%t%d\n", i->dst,
                        llvm_type(t), i->a, a);
            }
            else {
                b = ++o->tmp;
                r = ++o->tmp;
                fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n", b, llvm_type(t),
                        i->a);
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
        fprintf(f, "  %%s%d = getelementptr %%RhStack, ptr @rh_stacks, i64 %%t%d\n", i->dst, a);
        fprintf(f, "  %%markp%d = getelementptr %%RhStack, ptr %%s%d, i32 0, i32 1\n", i->dst,
                i->dst);
        b = ++o->tmp;
        fprintf(f, "  %%t%d = load ptr, ptr %%markp%d\n", b, i->dst);
        fprintf(f, "  store ptr %%t%d, ptr %%mark%d\n", b, i->dst);
        break;
    case I_REGION: {
        char *stack = stack_for(o, i->dst);
        a = ++o->tmp;
        b = ++o->tmp;
        r = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %s, i32 0, i32 1\n", a, stack);
        fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n", b, a);
        fprintf(f,
                "  %%t%d = insertvalue { ptr, i64, ptr } { ptr null, i64 0, ptr "
                "null }, ptr %%t%d, 0\n",
                r, b);
        a = ++o->tmp;
        fprintf(f, "  %%t%d = insertvalue { ptr, i64, ptr } %%t%d, ptr %s, 2\n", a, r, stack);
        save(o, i->dst, t, fmt("%%t%d", a));
        break;
    }
    case I_RELEASE:
        a = ++o->tmp;
        fprintf(f, "  %%t%d = load ptr, ptr %%mark%d\n", a, i->a);
        fprintf(f, "  store ptr %%t%d, ptr %%markp%d\n", a, i->a);
        break;
    case I_STACK_SAVE:
    case I_STACK_REWIND: {
        char *stack = stack_for(o, i->a);
        a = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %s, i32 0, i32 1\n", a, stack);
        b = ++o->tmp;
        if (i->op == I_STACK_SAVE) {
            fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n  store ptr %%t%d, ptr %%p%d\n", b, a, b,
                    i->dst);
        }
        else {
            fprintf(f, "  %%t%d = load ptr, ptr %%p%d\n  store ptr %%t%d, ptr %%t%d\n", b, i->dst,
                    b, a);
        }
        break;
    }
    case I_PUSH: {
        int desc = load(o, i->a);
        int data = ++o->tmp, len = ++o->tmp, stack = ++o->tmp;
        fprintf(f, "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 0\n", data, desc);
        fprintf(f, "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 1\n", len, desc);
        fprintf(f, "  %%t%d = extractvalue { ptr, i64, ptr } %%t%d, 2\n", stack, desc);
        int lp = ++o->tmp, limit = ++o->tmp, li = ++o->tmp, di = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %%t%d, i32 0, i32 2\n", lp, stack);
        fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n", limit, lp);
        fprintf(f, "  %%t%d = ptrtoint ptr %%t%d to i64\n", li, limit);
        fprintf(f, "  %%t%d = ptrtoint ptr %%t%d to i64\n", di, data);
        int bytes = ++o->tmp, off = ++o->tmp, remaining = ++o->tmp, ok = ++o->tmp;
        fprintf(f, "  %%t%d = sub i64 %%t%d, %%t%d\n", bytes, li, di);
        // Every constructor/mutator maintains len * size <= available. Test
        // remaining bytes before adding a new element; no capacity division.
        fprintf(f, "  %%t%d = mul i64 %%t%d, %zu\n", off, len, t->elem->size);
        fprintf(f, "  %%t%d = sub i64 %%t%d, %%t%d\n", remaining, bytes, off);
        fprintf(f, "  %%t%d = icmp uge i64 %%t%d, %zu\n", ok, remaining, t->elem->size);
        fprintf(f,
                "  br i1 %%t%d, label %%push_ok_%d, label %%push_bad_%d, !prof "
                "!0\npush_bad_%d:\n",
                ok, ok, ok, ok);
        fprintf(f, "  call void @rh_stack_overflow(i32 %d)\n  unreachable\npush_ok_%d:\n",
                i->loc.line, ok);
        int ptr = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr i8, ptr %%t%d, i64 %%t%d\n", ptr, data, off);
        a = load(o, i->b);
        fprintf(f, "  store %s %%t%d, ptr %%t%d\n", llvm_type(t->elem), a, ptr);
        int end = ++o->tmp, topaddr = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr i8, ptr %%t%d, i64 %zu\n", end, ptr, t->elem->size);
        fprintf(f, "  %%t%d = getelementptr %%RhStack, ptr %%t%d, i32 0, i32 1\n", topaddr, stack);
        if (i->push_at_top) {
            fprintf(f, "  store ptr %%t%d, ptr %%t%d\n", end, topaddr);
        }
        else {
            int top = ++o->tmp, greater = ++o->tmp, high = ++o->tmp;
            fprintf(f, "  %%t%d = load ptr, ptr %%t%d\n", top, topaddr);
            fprintf(f, "  %%t%d = icmp ugt ptr %%t%d, %%t%d\n", greater, end, top);
            fprintf(f, "  %%t%d = select i1 %%t%d, ptr %%t%d, ptr %%t%d\n", high, greater, end,
                    top);
            fprintf(f, "  store ptr %%t%d, ptr %%t%d\n", high, topaddr);
        }
        int newlen = ++o->tmp, lenaddr = ++o->tmp;
        fprintf(f, "  %%t%d = add i64 %%t%d, 1\n", newlen, len);
        fprintf(f, "  %%t%d = getelementptr { ptr, i64, ptr }, ptr %%p%d, i32 0, i32 1\n", lenaddr,
                i->a);
        fprintf(f, "  store i64 %%t%d, ptr %%t%d\n", newlen, lenaddr);
        if (i->dst >= 0) {
            save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", ptr));
        }
        break;
    }
    case I_POP: {
        int base = load(o, i->a), data = ++o->tmp, len = ++o->tmp;
        int valid = ++o->tmp, last = ++o->tmp, element = ++o->tmp;
        int value = ++o->tmp, lenaddr = ++o->tmp;
        fprintf(f, "  %%t%d = extractvalue %s %%t%d, 0\n", data, llvm_type(t), base);
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
        fprintf(f, "  %%t%d = load %s, ptr %%t%d\n", value, llvm_type(t->elem), element);
        save(o, i->dst, t->elem, fmt("%%t%d", value));
        fprintf(f, "  %%t%d = getelementptr {ptr,i64,ptr}, ptr %%p%d, i32 0, i32 1\n", lenaddr,
                i->a);
        // A grow^ shrink changes only logical length. References keep the
        // physical storage alive, and a later push must retain its high-water
        // mark. A flex^ shrink also returns the tail (sema proved it unused).
        fprintf(f, "  store i64 %%t%d, ptr %%t%d\n", last, lenaddr);
        flex_release(o, i, fmt("%%t%d", last));
        break;
    }
    case I_CLEAR:
        a = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr {ptr,i64,ptr}, ptr %%p%d, i32 0, i32 1\n", a, i->a);
        fprintf(f, "  store i64 0, ptr %%t%d\n", a);
        flex_release(o, i, "0");
        break;
    case I_UNION: {
        Type *arm = slot_type(o, i->a);
        save(o, i->dst, t, "zeroinitializer");
        int tag = ++o->tmp, payload = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n", tag, llvm_type(t),
                i->dst);
        fprintf(f, "  store i32 %d, ptr %%t%d\n", i->label, tag);
        fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n", payload, llvm_type(t),
                i->dst);
        a = load(o, i->a);
        fprintf(f, "  store %s %%t%d, ptr %%t%d\n", llvm_type(arm), a, payload);
        break;
    }
    case I_CHAR:
        a = load(o, i->a);
        if (t->kind == T_STRING) {
            fprintf(f,
                    "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n"
                    "  %%t%d = extractvalue { ptr, i64 } %%t%d, 1\n"
                    "  %%t%d = call i64 @rh_char_of_text(ptr %%t%d, i64 %%t%d)\n",
                    o->tmp + 1, a, o->tmp + 2, a, o->tmp + 3, o->tmp + 1, o->tmp + 2);
            o->tmp += 3;
        }
        else {
            fprintf(f, "  %%t%d = call i64 @rh_char_of_whole(i64 %%t%d)\n", ++o->tmp, a);
        }
        save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", o->tmp));
        break;
    case I_HELPER: {
        char *args = "";
        for (int k = 0; k < i->args.n; k++) {
            int id = (int)(intptr_t)i->args.v[k] - 1;
            Type *at = slot_type(o, id);
            a = load(o, id);
            if (at->kind == T_STRING || at->kind == T_SLICE) {
                fprintf(f, "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n", ++o->tmp, a);
                fprintf(f, "  %%t%d = extractvalue { ptr, i64 } %%t%d, 1\n", ++o->tmp, a);
                args = fmt("%sptr %%t%d, i64 %%t%d, ", args, o->tmp - 1, o->tmp);
            }
            else {
                args = fmt("%s%s %%t%d, ", args, llvm_type(at), a);
            }
        }
        if (i->dst >= 0) {
            fprintf(f, "  %%t%d = call i64 @%s(%si32 %d)\n", ++o->tmp, i->text, args, i->loc.line);
            save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", o->tmp));
        }
        else {
            fprintf(f, "  call void @%s(%si32 %d)\n", i->text, args, i->loc.line);
        }
        break;
    }
    case I_SITE:
        fprintf(f, "  store ptr @%s_site_%d, ptr %%p%d\n", o->in->symbol, i->label, i->dst);
        break;
    case I_CASE_AT:
        if (i->b) {
            a = ++o->tmp;
            fprintf(
                f,
                "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n  store i32 %d, ptr %%t%d\n",
                a, llvm_type(t), i->a, i->label, a);
        }
        fprintf(f, "  %%p%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n", i->dst, llvm_type(t),
                i->a);
        break;
    case I_CASE_IS:
    case I_CASE_GET: {
        if (i->op == I_CASE_IS) {
            int tagptr = ++o->tmp, tag = ++o->tmp;
            fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 0\n", tagptr,
                    llvm_type(t), i->a);
            fprintf(f, "  %%t%d = load i32, ptr %%t%d\n", tag, tagptr);
            r = ++o->tmp;
            fprintf(f, "  %%t%d = icmp eq i32 %%t%d, %d\n", r, tag, i->label);
            save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", r));
        }
        else {
            int payload = ++o->tmp;
            Type *arm = slot_type(o, i->dst);
            fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 2\n", payload,
                    llvm_type(t), i->a);
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
        fprintf(f, "  %%t%d = getelementptr %s, ptr %%p%d, i32 0, i32 1\n", lenaddr, llvm_type(t),
                i->a);
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
        flex_release(o, i, fmt("%%t%d", a));
        break;
    }
    case I_APPEND:
        for (int k = 1; k < o->in->code.n; k++) {
            if (o->in->code.v[k] != i) {
                continue;
            }
            Instr *literal = o->in->code.v[k - 1];
            // The immediately preceding instruction fixes the source value;
            // arbitrary views retain overlap-safe runtime memmove semantics.
            if (literal->op == I_STRING && literal->dst == i->b && t->elem->kind == T_U8) {
                fprintf(f,
                        "  call void @rh_append_literal(ptr %%p%d, ptr @%s_str_%d, i64 "
                        "%zu, i32 %d)\n",
                        i->a, o->in->symbol, literal->label, literal->length, i->loc.line);
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
        }
        else {
            int base = load(o, i->a);
            int ptr = ++o->tmp, len = ++o->tmp;
            fprintf(f, "  %%t%d = extractvalue %s %%t%d, 0\n", ptr, llvm_type(t), base);
            fprintf(f, "  %%t%d = extractvalue %s %%t%d, 1\n", len, llvm_type(t), base);
            data = fmt("%%t%d", ptr);
            length = fmt("%%t%d", len);
        }
        // Omitted bounds are 0 and the length; an inclusive end adds one, and a
        // wrapped end fails the ordering check.
        char *lo = i->b >= 0 ? fmt("%%t%d", load(o, i->b)) : "0";
        char *hi = i->other >= 0 ? fmt("%%t%d", load(o, i->other)) : length;
        if (i->text && (!strcmp(i->text, "open") || !strcmp(i->text, "text"))) {
            // A range^ value's open end, the minimum i64, closes at the length.
            int open = ++o->tmp;
            fprintf(f, "  %%t%d = icmp eq i64 %s, -9223372036854775808\n", open, hi);
            fprintf(f, "  %%t%d = select i1 %%t%d, i64 %s, i64 %s\n", ++o->tmp, open, length, hi);
            hi = fmt("%%t%d", o->tmp);
        }
        if (i->label) {
            fprintf(f, "  %%t%d = add i64 %s, 1\n", ++o->tmp, hi);
            hi = fmt("%%t%d", o->tmp);
        }
        int lo_ok = ++o->tmp, ordered = ++o->tmp, hi_ok = ++o->tmp;
        int lower = ++o->tmp, valid = ++o->tmp;
        b = ++o->tmp;
        fprintf(f, "  %%t%d = icmp sge i64 %s, 0\n", lo_ok, lo);
        fprintf(f, "  %%t%d = icmp sge i64 %s, %s\n", ordered, hi, lo);
        fprintf(f, "  %%t%d = icmp ule i64 %s, %s\n", hi_ok, hi, length);
        fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", lower, lo_ok, ordered);
        fprintf(f, "  %%t%d = and i1 %%t%d, %%t%d\n", valid, lower, hi_ok);
        fprintf(f, "  %%t%d = sub i64 %s, %s\n", b, hi, lo);
        fprintf(f,
                "  br i1 %%t%d, label %%slice_ok_%d, label %%slice_bad_%d, !prof "
                "!0\nslice_bad_%d:\n",
                valid, valid, valid, valid);
        fprintf(f, "  call void @rh_slice_fail(i32 %d)\n  unreachable\nslice_ok_%d:\n", i->loc.line,
                valid);
        if (i->text && !strcmp(i->text, "text")) {
            // A string^ is cut only between characters.
            fprintf(f, "  call void @rh_text_boundary(ptr %s, i64 %s, i64 %s, i64 %s, i32 %d)\n",
                    data, length, lo, hi, i->loc.line);
        }
        int ptr = ++o->tmp, first = ++o->tmp, view = ++o->tmp;
        fprintf(f, "  %%t%d = getelementptr %s, ptr %s, i64 %s\n", ptr,
                t->kind == T_STRING ? "i8" : llvm_type(t->elem), data, lo);
        fprintf(f, "  %%t%d = insertvalue {ptr,i64} undef, ptr %%t%d, 0\n", first, ptr);
        fprintf(f, "  %%t%d = insertvalue {ptr,i64} %%t%d, i64 %%t%d, 1\n", view, first, b);
        save(o, i->dst, slot_type(o, i->dst), fmt("%%t%d", view));
        break;
    }
    case I_LENGTH:
        if (t->kind == T_ARRAY) {
            save(o, i->dst, slot_type(o, i->dst), fmt("%d", t->count));
        }
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
                        kind_unsigned(count->kind) ? "zext" : "sext", llvm_type(count), b);
            }
            int neg = ++o->tmp, magnitude = ++o->tmp, masked = ++o->tmp, amount = masked;
            int left = ++o->tmp, right = ++o->tmp, selected = ++o->tmp, large = ++o->tmp;
            fprintf(f, "  %%t%d = icmp slt i128 %%t%d, 0\n", neg, wide);
            if (kind_unsigned(count->kind)) {
                fprintf(f, "  %%t%d = add i128 %%t%d, 0\n", magnitude, wide);
            }
            else {
                int inverse = ++o->tmp;
                fprintf(f, "  %%t%d = sub i128 0, %%t%d\n", inverse, wide);
                fprintf(f, "  %%t%d = select i1 %%t%d, i128 %%t%d, i128 %%t%d\n", magnitude, neg,
                        inverse, wide);
            }
            // Mask before shifting, even on the unselected path: LLVM shifts must
            // never receive a count >= the operand width.
            fprintf(f, "  %%t%d = and i128 %%t%d, %d\n", masked, magnitude,
                    numeric_bits(t->kind) - 1);
            if (numeric_bits(t->kind) != 128) {
                amount = ++o->tmp;
                fprintf(f, "  %%t%d = trunc i128 %%t%d to %s\n", amount, masked, llvm_type(t));
            }
            fprintf(f, "  %%t%d = shl %s %%t%d, %%t%d\n", left, llvm_type(t), a, amount);
            fprintf(f, "  %%t%d = lshr %s %%t%d, %%t%d\n", right, llvm_type(t), a, amount);
            fprintf(f, "  %%t%d = select i1 %s, %s %%t%d, %s %%t%d\n", selected,
                    kind_unsigned(count->kind) ? "false" : fmt("%%t%d", neg), llvm_type(t), right,
                    llvm_type(t), left);
            fprintf(f, "  %%t%d = icmp uge i128 %%t%d, %d\n", large, magnitude,
                    numeric_bits(t->kind));
            fprintf(f, "  %%t%d = select i1 %%t%d, %s 0, %s %%t%d\n", r, large, llvm_type(t),
                    llvm_type(t), selected);
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
        if (!strcmp(op, "+")) {
            ins = fp ? "fadd" : "add";
        }
        else if (!strcmp(op, "-")) {
            ins = fp ? "fsub" : "sub";
        }
        else if (!strcmp(op, "*")) {
            ins = fp ? "fmul" : "mul";
        }
        else if (!strcmp(op, "/") && fp) {
            ins = "fdiv";
        }
        else if (!strcmp(op, "bitand^")) {
            ins = "and";
        }
        else if (!strcmp(op, "bitor^")) {
            ins = "or";
        }
        else if (!strcmp(op, "bitxor^")) {
            ins = "xor";
        }
        if (ins) {
            fprintf(f, "  %%t%d = %s %s %%t%d, %%t%d\n", r, ins, operand_type, a, b);
        }
        else if (!strcmp(op, "/") || !strcmp(op, "%")) {
            int nonzero = ++o->tmp;
            fprintf(f, "  %%t%d = icmp ne %s %%t%d, 0\n", nonzero, operand_type, b);
            int ok = nonzero;
            if (!uns) {
                int bits = numeric_bits(t->kind);
                const char *minimum =
                    bits == 128  ? "-170141183460469231731687303715884105728"
                    : bits == 64 ? "-9223372036854775808"
                                 : fmt("-%llu", (unsigned long long)(UINT64_C(1) << (bits - 1)));
                int ismin = ++o->tmp, negone = ++o->tmp, bad = ++o->tmp, valid = ++o->tmp;
                fprintf(f, "  %%t%d = icmp eq %s %%t%d, %s\n", ismin, operand_type, a, minimum);
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
        }
        else {
            const char *pred = NULL;
            if (!strcmp(op, "=") || !strcmp(op, "==")) {
                pred = fp ? "oeq" : "eq";
            }
            else if (!strcmp(op, "!=")) {
                pred = fp ? "une" : "ne";
            }
            else if (!strcmp(op, "<")) {
                pred = fp ? "olt" : uns ? "ult" : "slt";
            }
            else if (!strcmp(op, ">")) {
                pred = fp ? "ogt" : uns ? "ugt" : "sgt";
            }
            else if (!strcmp(op, "<=")) {
                pred = fp ? "ole" : uns ? "ule" : "sle";
            }
            else if (!strcmp(op, ">=")) {
                pred = fp ? "oge" : uns ? "uge" : "sge";
            }
            if (!pred) {
                fail(i->loc, "internal: unknown binary operator");
            }
            fprintf(f, "  %%t%d = %s %s %s %%t%d, %%t%d\n", r, fp ? "fcmp" : "icmp", pred,
                    operand_type, a, b);
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
        if (!strcmp(i->text, "!")) {
            fprintf(f, "  %%t%d = xor i1 %%t%d, true\n", r, a);
        }
        else if (!strcmp(i->text, "bitnot^")) {
            fprintf(f, "  %%t%d = xor %s %%t%d, -1\n", r, llvm_type(t), a);
        }
        else if (t->kind == T_F8) {
            fprintf(f, "  %%t%d = xor i8 %%t%d, 128\n", r, a);
        }
        else if (kind_float(t->kind)) {
            fprintf(f, "  %%t%d = fneg %s %%t%d\n", r, llvm_type(t), a);
        }
        else {
            fprintf(f, "  %%t%d = sub %s 0, %%t%d\n", r, llvm_type(t), a);
        }
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
        }
        else if (!src_fp && !dst_fp && src_bits == dst_bits) {
            r = a;
        }
        else {
            const char *op;
            if (!src_fp && !dst_fp) {
                op = src_bits > dst_bits ? "trunc" : kind_unsigned(source->kind) ? "zext" : "sext";
            }
            else if (!src_fp) {
                op = kind_unsigned(source->kind) ? "uitofp" : "sitofp";
            }
            else if (!dst_fp) {
                int checked = a;
                if (src_bits != 64) {
                    checked = ++o->tmp;
                    fprintf(f, "  %%t%d = fpext %s %%t%d to double\n", checked, src_type, a);
                }
                bool uns = kind_unsigned(t->kind);
                uint64_t upper = (uint64_t)(1023 + dst_bits - (uns ? 0 : 1)) << 52;
                uint64_t lower = uns ? 0 : upper | (UINT64_C(1) << 63);
                int low = ++o->tmp, high = ++o->tmp, valid = ++o->tmp;
                fprintf(f, "  %%t%d = fcmp oge double %%t%d, 0x%016llX\n", low, checked,
                        (unsigned long long)lower);
                fprintf(f, "  %%t%d = fcmp olt double %%t%d, 0x%016llX\n", high, checked,
                        (unsigned long long)upper);
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
            }
            else {
                op = src_bits > dst_bits ? "fptrunc" : "fpext";
            }
            if (src_fp && dst_fp && src_bits == dst_bits) {
                r = a;
            }
            else {
                r = ++o->tmp;
                fprintf(f, "  %%t%d = %s %s %%t%d to %s\n", r, op, src_type, a, llvm_type(t));
            }
        }
        save(o, i->dst, t, fmt("%%t%d", r));
        break;
    }
    case I_CALL: {
        if (i->callee->fn->external) {
            external_call(o, i);
            break;
        }
        a = ++o->tmp;
        fprintf(f, "  %%t%d = add i64 %%sp, %d\n", a, i->stack_index);
        int *values = mem((size_t)i->args.n * sizeof(int));
        for (int j = 0; j < i->args.n; j++) {
            if (direct(i->callee->params.v[j])) {
                values[j] = load(o, (int)(intptr_t)i->args.v[j] - 1);
            }
        }
        r = ++o->tmp;
        if (direct(t)) {
            fprintf(f, "  %%t%d = call %s @%s(i64 %%t%d", r, llvm_type(t), i->callee->symbol, a);
        }
        else {
            fprintf(f, "  call void @%s(i64 %%t%d", i->callee->symbol, a);
        }
        if (t->kind != T_VOID && !direct(t)) {
            fprintf(f, ", ptr %%p%d", i->dst);
        }
        if (t->owned) {
            fprintf(f, ", ptr %s", stack_for(o, i->dst));
        }
        for (int j = 0; j < i->args.n; j++) {
            Type *param = i->callee->params.v[j];
            if (direct(param)) {
                fprintf(f, ", %s %%t%d", llvm_type(param), values[j]);
            }
            else {
                fprintf(f, ", ptr %%p%d", (int)(intptr_t)i->args.v[j] - 1);
            }
        }
        fputs(")\n", f);
        if (direct(t)) {
            save(o, i->dst, t, fmt("%%t%d", r));
        }
        break;
    }
    case I_DYN: {
        a = load(o, i->a);
        b = ++o->tmp;
        r = ++o->tmp;
        fprintf(f,
                "  %%t%d = insertvalue { ptr, ptr } undef, ptr %%t%d, 0\n"
                "  %%t%d = insertvalue { ptr, ptr } %%t%d, ptr @%s, 1\n",
                b, a, r, b, table_symbol(o->p->dyn_tables.v[i->label]));
        save(o, i->dst, t, fmt("%%t%d", r));
        break;
    }
    case I_DYNIS:
    case I_DYNCAST: {
        // Compare the table against every table of an accepted concrete type.
        a = load(o, i->a);
        int data = ++o->tmp, table = ++o->tmp;
        fprintf(
            f,
            "  %%t%d = extractvalue { ptr, ptr } %%t%d, 0\n  %%t%d = extractvalue { ptr, ptr } %%t%d, 1\n",
            data, a, table, a);
        Def *from = slot_type(o, i->a)->elem->def;
        char *result = i->op == I_DYNIS ? "false" : "null";
        for (int j = 0; j < o->p->dyn_tables.n; j++) {
            DynTable *candidate = o->p->dyn_tables.v[j];
            if (!table_matches(candidate, from, i->type)) {
                continue;
            }
            int hit = ++o->tmp, next = ++o->tmp;
            fprintf(f, "  %%t%d = icmp eq ptr %%t%d, @%s\n", hit, table, table_symbol(candidate));
            if (i->op == I_DYNIS) {
                fprintf(f, "  %%t%d = or i1 %s, %%t%d\n", next, result, hit);
            }
            else {
                DynTable *to = table_for(o->p, candidate->concrete, i->type->elem->def);
                fprintf(f, "  %%t%d = select i1 %%t%d, ptr @%s, ptr %s\n", next, hit,
                        table_symbol(to), result);
            }
            result = fmt("%%t%d", next);
        }
        if (i->op == I_DYNCAST) {
            b = ++o->tmp;
            r = ++o->tmp;
            fprintf(f,
                    "  %%t%d = insertvalue { ptr, ptr } undef, ptr %%t%d, 0\n"
                    "  %%t%d = insertvalue { ptr, ptr } %%t%d, ptr %s, 1\n",
                    b, data, r, b, result);
            result = fmt("%%t%d", r);
        }
        save(o, i->dst, slot_type(o, i->dst), result);
        break;
    }
    case I_DYNDATA:
        a = load(o, i->a);
        r = ++o->tmp;
        fprintf(f, "  %%t%d = extractvalue { ptr, ptr } %%t%d, 0\n", r, a);
        save(o, i->dst, t, fmt("%%t%d", r));
        break;
    case I_DYNCALL: {
        // Every table entry shares the declared signature; self is the data.
        int receiver = load(o, (int)(intptr_t)i->args.v[0] - 1);
        int data = ++o->tmp, table = ++o->tmp, entry = ++o->tmp, target = ++o->tmp;
        fprintf(f,
                "  %%t%d = extractvalue { ptr, ptr } %%t%d, 0\n"
                "  %%t%d = extractvalue { ptr, ptr } %%t%d, 1\n"
                "  %%t%d = getelementptr ptr, ptr %%t%d, i64 %d\n"
                "  %%t%d = load ptr, ptr %%t%d\n",
                data, receiver, table, receiver, entry, table, i->label, target, entry);
        a = ++o->tmp;
        fprintf(f, "  %%t%d = add i64 %%sp, %d\n", a, i->stack_index);
        int *values = mem((size_t)i->args.n * sizeof(int));
        for (int j = 1; j < i->args.n; j++) {
            if (direct(slot_type(o, (int)(intptr_t)i->args.v[j] - 1))) {
                values[j] = load(o, (int)(intptr_t)i->args.v[j] - 1);
            }
        }
        r = ++o->tmp;
        if (direct(t)) {
            fprintf(f, "  %%t%d = call %s %%t%d(i64 %%t%d", r, llvm_type(t), target, a);
        }
        else {
            fprintf(f, "  call void %%t%d(i64 %%t%d", target, a);
        }
        if (t->kind != T_VOID && !direct(t)) {
            fprintf(f, ", ptr %%p%d", i->dst);
        }
        fprintf(f, ", ptr %%t%d", data);
        for (int j = 1; j < i->args.n; j++) {
            Type *param = slot_type(o, (int)(intptr_t)i->args.v[j] - 1);
            if (direct(param)) {
                fprintf(f, ", %s %%t%d", llvm_type(param), values[j]);
            }
            else {
                fprintf(f, ", ptr %%p%d", (int)(intptr_t)i->args.v[j] - 1);
            }
        }
        fputs(")\n", f);
        if (direct(t)) {
            save(o, i->dst, t, fmt("%%t%d", r));
        }
        break;
    }
    case I_PRINT:
        for (int j = 0; j < i->args.n; j++) {
            int id = (int)(intptr_t)i->args.v[j] - 1;
            Type *ty = slot_type(o, id);
            if (j) {
                fputs("  call void @rh_print_space()\n", f);
            }
            if (ty->kind == T_STRING) {
                a = load(o, id);
                b = ++o->tmp;
                r = ++o->tmp;
                fprintf(f,
                        "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n  %%t%d = "
                        "extractvalue { ptr, i64 } %%t%d, 1\n  call void "
                        "@rh_print_string(ptr %%t%d, i64 %%t%d)\n",
                        b, a, r, a, b, r);
            }
            else if (kind_float(ty->kind)) {
                a = load(o, id);
                if (ty->kind == T_F8) {
                    int decoded = ++o->tmp;
                    fprintf(f, "  %%t%d = call float @rh_f8_decode(i8 %%t%d)\n", decoded, a);
                    a = decoded;
                }
                if (ty->kind != T_F64) {
                    b = ++o->tmp;
                    fprintf(f, "  %%t%d = fpext %s %%t%d to double\n", b,
                            ty->kind == T_F16 ? "half" : "float", a);
                    a = b;
                }
                fprintf(f, "  call void @rh_print_f64(double %%t%d)\n", a);
            }
            else if (ty->kind == T_BOOL) {
                a = integer64(o, id);
                fprintf(f, "  call void @rh_print_bool(i64 %%t%d)\n", a);
            }
            else if (ty->kind == T_CHAR) {
                a = load(o, id);
                fprintf(f, "  call void @rh_print_char(i32 %%t%d)\n", a);
            }
            else {
                fprintf(f, "  call void @rh_print_integer(ptr %%p%d, i32 %d, i32 %d)\n", id,
                        numeric_bits(ty->kind), kind_unsigned(ty->kind));
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
        fprintf(f, "  call void @rh_assert_fail(i32 %d)\n  unreachable\nassert_ok_%d:\n",
                i->loc.line, valid);
        break;
    }
    case I_TERMINATE: {
        if (i->other) {
            // An unhandled error with its message() and the site it was built at.
            int name = load(o, i->a), message = load(o, i->b), site = load(o, i->dst);
            int n0 = ++o->tmp, n1 = ++o->tmp, m0 = ++o->tmp, m1 = ++o->tmp;
            fprintf(
                f,
                "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n"
                "  %%t%d = extractvalue { ptr, i64 } %%t%d, 1\n"
                "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n"
                "  %%t%d = extractvalue { ptr, i64 } %%t%d, 1\n"
                "  call void @rh_unhandled(ptr %%t%d, i64 %%t%d, ptr %%t%d, i64 %%t%d, ptr %%t%d, "
                "i32 %d, i32 %d)\n  unreachable\n",
                n0, name, n1, name, m0, message, m1, message, n0, n1, m0, m1, site, i->loc.line,
                i->label);
            break;
        }
        a = load(o, i->a);
        b = ++o->tmp;
        r = ++o->tmp;
        fprintf(f,
                "  %%t%d = extractvalue { ptr, i64 } %%t%d, 0\n"
                "  %%t%d = extractvalue { ptr, i64 } %%t%d, 1\n"
                "  call void @rh_terminate(ptr %%t%d, i64 %%t%d, i32 %d, i32 %d)\n"
                "  unreachable\n",
                b, a, r, a, b, r, i->loc.line, i->label);
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
        fprintf(f, "  br i1 %%t%d, label %%block%d, label %%block%d\n", a, i->label, i->other);
        break;
    case I_RETURN:
        if (direct(o->in->result)) {
            r = load(o, o->in->result_slot);
            fprintf(f, "  ret %s %%t%d\n", llvm_type(o->in->result), r);
        }
        else {
            fputs("  ret void\n", f);
        }
        break;
    default:
        fail(i->loc, "internal: unknown IR operation");
    }
}

void emit_llvm(Program *p, const char *path)
{
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        in->emitted = !p->emitting || in->fn->module == p->emitting;
    }
    bool changed;
    do {
        changed = false;
        for (int j = 0; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            if (!in->emitted) {
                continue;
            }
            for (int k = 0; k < in->code.n; k++) {
                Instr *i = in->code.v[k];
                if (i->op == I_CALL && !i->callee->emitted) {
                    i->callee->emitted = true;
                    changed = true;
                }
                for (int t = 0; t < p->dyn_tables.n; t++) {
                    DynTable *table = p->dyn_tables.v[t];
                    for (int n = 0; table_used(p, in, i, table) && n < table->entries.n; n++) {
                        Instance *entry = table->entries.v[n];
                        if (entry && !entry->emitted) {
                            entry->emitted = true;
                            changed = true;
                        }
                    }
                }
            }
        }
    } while (changed);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fail((Loc){path, 1, 1}, "cannot write LLVM IR");
    }
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
        if (t->kind != T_STRUCT) {
            continue;
        }
        fprintf(f, "%s = type { ", llvm_type(t));
        if (!t->def->fields.n) {
            fputs("i8", f);
        }
        for (int k = 0; k < t->def->fields.n; k++) {
            Field *a = t->def->fields.v[k];
            fprintf(f, "%s%s", k ? ", " : "", llvm_type(a->type));
        }
        fputs(" }\n", f);
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (!in->emitted) {
            continue;
        }
        int count = 0;
        for (int k = 0; k < in->code.n; k++) {
            Instr *i = in->code.v[k];
            if (i->op == I_STRING) {
                i->label = count++;
                fprintf(f, "@%s_str_%d = private unnamed_addr constant [%zu x i8] ", in->symbol,
                        i->label, i->length + 1);
                bytes(f, i->text, i->length);
                fputc('\n', f);
            }
            if (i->op == I_SITE) {
                // text holds the source, a NUL, then the message.
                const char *message = i->text + i->length + 1;
                i->label = count;
                count += 2;
                fprintf(f, "@%s_str_%d = private unnamed_addr constant [%zu x i8] ", in->symbol,
                        i->label, i->length + 1);
                bytes(f, i->text, i->length);
                fprintf(f, "\n@%s_str_%d = private unnamed_addr constant [%zu x i8] ", in->symbol,
                        i->label + 1, strlen(message) + 1);
                bytes(f, message, strlen(message));
                fprintf(
                    f,
                    "\n@%s_site_%d = private unnamed_addr constant { { ptr, i64 }, i32, i32, { ptr, i64 } } "
                    "{ { ptr, i64 } { ptr @%s_str_%d, i64 %zu }, i32 %d, i32 %d, "
                    "{ ptr, i64 } { ptr @%s_str_%d, i64 %zu } }\n",
                    in->symbol, i->label, in->symbol, i->label, i->length, i->loc.line, i->loc.col,
                    in->symbol, i->label + 1, strlen(message));
            }
        }
    }
    fprintf(f,
            "\n%%RhStack = type { ptr, ptr, ptr, ptr }\n@rh_stacks = %sglobal "
            "[%d x %%RhStack]%s\n",
            !p->emitting        ? "internal "
            : p->emitting->root ? ""
                                : "external ",
            p->stack_count, !p->emitting || p->emitting->root ? " zeroinitializer" : "");
    emit_llvm_builtins(f);
    unsigned char *tables = mem((size_t)p->dyn_tables.n + 1);
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        for (int k = 0; in->emitted && k < in->code.n; k++) {
            for (int t = 0; t < p->dyn_tables.n; t++) {
                tables[t] |= table_used(p, in, in->code.v[k], p->dyn_tables.v[t]);
            }
        }
    }
    for (int j = 0; j < p->dyn_tables.n; j++) {
        DynTable *table = p->dyn_tables.v[j];
        if (!tables[j]) {
            continue;
        }
        char *symbol = table_symbol(table);
#if defined(__APPLE__)
        fprintf(f, "@%s = linkonce_odr constant [%d x ptr] [", symbol, table->entries.n);
#else
        fprintf(f, "$%s = comdat any\n@%s = linkonce_odr constant [%d x ptr] [", symbol, symbol,
                table->entries.n);
#endif
        for (int k = 0; k < table->entries.n; k++) {
            Instance *entry = table->entries.v[k];
            fprintf(f, "%sptr %s%s", k ? ", " : "", entry ? "@" : "null",
                    entry ? entry->symbol : "");
        }
#if defined(__APPLE__)
        fputs("]\n", f);
#else
        fputs("], comdat\n", f);
#endif
    }
    fputs("declare void @rh_runtime_init(ptr, i64)\n"
          "declare void @rh_runtime_args(i32, ptr, ptr)\n"
          "declare void @rh_terminate(ptr, i64, i32, i32) cold noreturn\n"
          "declare void @rh_unhandled(ptr, i64, ptr, i64, ptr, i32, i32) cold noreturn\n"
          "declare void @rh_print_char(i32)\n"
          "declare i64 @rh_text_valid(ptr, i64, i32) readonly\n"
          "declare i64 @rh_text_format(ptr, i64, ptr, i64, ptr, i64, i32)\n"
          "declare i64 @rh_text_parse(ptr, i64, ptr, i64, i32)\n"
          "declare i64 @rh_text_count(ptr, i64, i32) readonly\n"
          "declare i64 @rh_text_find(ptr, i64, ptr, i64, i32) readonly\n"
          "declare i64 @rh_text_starts(ptr, i64, ptr, i64, i32) readonly\n"
          "declare i64 @rh_text_ends(ptr, i64, ptr, i64, i32) readonly\n"
          "declare i64 @rh_text_compare(ptr, i64, ptr, i64, i32) readonly\n"
          "declare i64 @rh_text_decode(ptr, i64, i64, i32)\n"
          "declare i64 @rh_text_next(ptr, i64, i64, i32)\n"
          "declare void @rh_text_boundary(ptr, i64, i64, i64, i32)\n"
          "declare i64 @rh_char_of_text(ptr, i64) readonly\n"
          "declare i64 @rh_char_of_whole(i64) readnone\n"
          "declare void @rh_runtime_shutdown()\n"
          "declare void @rh_print_f64(double)\n"
          "declare void @rh_print_bool(i64)\n"
          "declare void @rh_print_string(ptr, i64)\n"
          "declare void @rh_print_newline()\n"
          "declare void @rh_print_space()\n"
          "declare void @rh_print_integer(ptr, i32, i32)\n"
          "declare void @rh_division_fail(i32, i32) cold noreturn\n",
          f);
    const char *failures[] = {"stack_overflow", "index_fail",    "pop_fail",   "float_cast_fail",
                              "slice_fail",     "truncate_fail", "assert_fail"};
    for (size_t j = 0; j < sizeof(failures) / sizeof(failures[0]); j++) {
        fprintf(f, "declare void @rh_%s(i32) cold noreturn\n", failures[j]);
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (!in->emitted) {
            continue;
        }
        if (in->fn->external) {
            external_declaration(p, j, f);
            continue;
        }
        const char *linkage = !p->emitting                    ? "internal"
                              : in->fn->module != p->emitting ? "available_externally"
                              : in->cross_module              ? "dso_local"
                                                              : "internal";
        fprintf(f, "\n; %s%s%s\ndefine %s %s @%s(", in->self ? in->self->name : "",
                in->self ? "." : "", in->fn->name, linkage,
                direct(in->result) ? llvm_type(in->result) : "void", in->symbol);
        bool result = in->result->kind != T_VOID;
        fputs("i64 %sp", f);
        if (result && !direct(in->result)) {
            fputs(", ptr %out", f);
        }
        if (in->result->owned) {
            fputs(", ptr %result_stack", f);
        }
        for (int k = 0; k < in->params.n; k++) {
            Type *param = in->params.v[k];
            fprintf(f, ", %s %%arg%d", direct(param) ? llvm_type(param) : "ptr", k);
        }
        fputs(") {\nentry:\n", f);
        Output o = {f, in, 0, p};
        for (int k = 0; k < in->slots.n; k++) {
            Slot *s = in->slots.v[k];
            if (!s->alias || (result && direct(in->result) && k == in->result_slot)) {
                fprintf(f, "  %%p%d = alloca %s\n", k, llvm_type(s->type));
            }
        }
        for (int k = 0; k < in->code.n; k++) {
            Instr *i = in->code.v[k];
            if (i->op == I_STACK_BEGIN) {
                fprintf(f, "  %%mark%d = alloca ptr\n", i->dst);
            }
        }
        if (result && !direct(in->result)) {
            fprintf(f, "  %%p%d = getelementptr i8, ptr %%out, i64 0\n", in->result_slot);
        }
        for (int k = 0; k < in->params.n; k++) {
            Type *t = in->params.v[k];
            int id = k + (result ? 1 : 0);
            if (direct(t)) {
                save(&o, id, t, fmt("%%arg%d", k));
            }
            else {
                int r = ++o.tmp;
                fprintf(f, "  %%t%d = load %s, ptr %%arg%d\n", r, llvm_type(t), k);
                save(&o, id, t, fmt("%%t%d", r));
            }
        }
        for (int k = 0; k < in->code.n; k++) {
            instruction(&o, in->code.v[k]);
        }
        fputs("}\n", f);
    }
    address_declarations(p, f);
    if (p->callbacks) {
        fputs("\n@rh_callback_sp = external global i64\ndeclare i64 @rh_callback_enter(i64)\n", f);
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if ((in->fn->exported || in->fn->lhat_export) &&
            (!p->emitting || in->fn->module == p->emitting)) {
            export_entry(in, f);
        }
    }
    if (!p->emitting || p->emitting->root) {
        // The entry receives the command arguments as its ... string slice.
        fprintf(f,
                "\ndefine i32 @main(i32 %%argc, ptr %%argv) {\nentry:\n"
                "  %%args = alloca { ptr, i64 }\n"
                "  call void @rh_runtime_init(ptr @rh_stacks, i64 %d)\n"
                "  call void @rh_runtime_args(i32 %%argc, ptr %%argv, ptr %%args)\n"
                "  %%strings = load { ptr, i64 }, ptr %%args\n"
                "  call void @rh_fn_0(i64 0, { ptr, i64 } %%strings)\n"
                "  call void @rh_runtime_shutdown()\n  ret i32 0\n}\n",
                p->stack_count);
    }
    fputs("\n!0 = !{!\"branch_weights\", i32 1048575, i32 1}\n", f);
    fputs("!1 = !{!\"Rhat storage\"}\n"
          "!2 = !{!\"non-owning value\", !1, i64 0}\n"
          "!3 = !{!\"owning array descriptor\", !1, i64 0}\n"
          "!4 = !{!2, !2, i64 0}\n"
          "!5 = !{!3, !3, i64 0}\n",
          f);
    if (fclose(f)) {
        fail((Loc){path, 1, 1}, "failed to write LLVM IR");
    }
}

void dump_ir(Program *p, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fail((Loc){path, 1, 1}, "cannot write R^ IR");
    }
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
                           "union.project",
                           "array.truncate",
                           "stack.save",
                           "result.checkpoint",
                           "result.rewind",
                           "range.guard",
                           "terminate",
                           "dyn.make",
                           "dyn.call",
                           "dyn.is",
                           "dyn.cast",
                           "dyn.data",
                           "union.place",
                           "char.check",
                           "runtime.helper",
                           "error.site"};
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        fprintf(f, "type %s size=%zu align=%zu%s\n", d->name, d->type->size, d->type->align,
                d->abstract ? " abstract" : "");
        size_t off = 0;
        if (d->type->kind == T_UNION) {
            fprintf(f, "  tag +0 : u32^ payload=+%zu bytes=%zu\n", d->type->payload_offset,
                    d->type->payload_bytes);
            for (int k = 0; k < d->alternatives.n; k++) {
                fprintf(f, "  case %d : %s\n", k, type_name(d->alternatives.v[k]));
            }
        }
        for (int k = 0; k < d->fields.n; k++) {
            Field *a = d->fields.v[k];
            off = (off + a->type->align - 1) / a->type->align * a->type->align;
            fprintf(f, "  +%zu %s.%s : %s%s\n", off, a->owner->name, a->name, type_name(a->type),
                    a->abstract ? " abstract" : "");
            off += a->type->size;
        }
    }
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        fprintf(f, "\nfunction %s %s%s%s -> %s destination=%%%d\n", in->symbol,
                in->self ? in->self->name : "", in->self ? "." : "", in->fn->name,
                type_name(in->result), in->result_slot);
        fprintf(f, "  stacks own=%d need=%d\n", in->stack_own, in->stack_need);
        for (int k = 0; k < in->params.n; k++) {
            fprintf(f, "  parameter %d : %s\n", k, type_name(in->params.v[k]));
        }
        for (int k = 0; k < in->slots.n; k++) {
            Slot *s = in->slots.v[k];
            fprintf(f, "  %%%d : %s %s\n", k, type_name(s->type), s->alias ? "place" : "storage");
        }
        for (int k = 0; k < in->code.n; k++) {
            Instr *i = in->code.v[k];
            fprintf(f, "  %-18s dst=%%%d a=%%%d b=%%%d", names[i->op], i->dst, i->a, i->b);
            if (i->op == I_STACK_BEGIN || i->op == I_CALL) {
                fprintf(f, " stack-offset=%d", i->stack_index);
            }
            if (i->op == I_REGION || (i->op == I_CALL && i->type->owned)) {
                fprintf(f, " stack-owner=%%%d", ((Slot *)in->slots.v[i->dst])->owner);
            }
            if (i->op == I_PUSH) {
                fprintf(f, " top=%s", i->push_at_top ? "append" : "retained");
            }
            if (i->op == I_INDEX && i->index_proven) {
                fputs(" range=proven", f);
            }
            if (i->op == I_RANGE_GUARD) {
                fprintf(f, " induction=%%%d indices=%d fallback=checked", i->range->induction,
                        i->range->indices.n);
            }
            if (i->type) {
                fprintf(f, " type=%s", type_name(i->type));
            }
            if (i->text) {
                fprintf(f, " op=%s", i->op == I_STRING ? "<literal>" : i->text);
            }
            if (i->callee) {
                fprintf(f, " target=%s", i->callee->symbol);
            }
            if (i->op == I_BR || i->op == I_CBR || i->op == I_LABEL) {
                fprintf(f, " label=%d other=%d", i->label, i->other);
            }
            if (i->op == I_UNION || i->op == I_CASE_IS || i->op == I_CASE_GET ||
                i->op == I_CASE_AT) {
                fprintf(f, " tag=%d", i->label);
            }
            if (i->op == I_CASE_GET) {
                fputs(" proof=flow", f);
            }
            if (i->op == I_SLICE) {
                fprintf(f, " end=%%%d%s", i->other, i->label ? " inclusive" : "");
            }
            if (i->op == I_FIELD) {
                fprintf(f, " field=%d", i->label);
            }
            if (i->args.n) {
                fputs(" args=", f);
                for (int a = 0; a < i->args.n; a++) {
                    fprintf(f, "%%%d%s", (int)(intptr_t)i->args.v[a] - 1,
                            a + 1 < i->args.n ? "," : "");
                }
            }
            fprintf(f, " at=%d:%d\n", i->loc.line, i->loc.col);
        }
    }
    if (fclose(f)) {
        fail((Loc){path, 1, 1}, "failed to write R^ IR");
    }
}

// The C spelling of a shim value. Pointers are untyped: C converts void *
// to the pointer types a prototype names, so only the convention matters.
static char *c_name(Type *t)
{
    switch (t->kind) {
    case T_BOOL:
        return "bool";
    case T_U8:
    case T_U16:
    case T_U32:
    case T_U64:
    case T_CHAR:
        return fmt("uint%d_t", numeric_bits(t->kind));
    case T_I8:
    case T_I16:
    case T_I32:
    case T_I64:
        return fmt("int%d_t", numeric_bits(t->kind));
    case T_F32:
        return "float";
    case T_F64:
        return "double";
    case T_STRUCT:
        return t->def->c_name ? t->def->c_name : fmt("rh_c_%d", t->id);
    default:
        return "void *";
    }
}

// Define t's C struct after those of its fields, unless a header defines
// it, and let the C compiler prove that it lays the struct out as R^ does.
// An externC^ def^ checks its own field names against the C type.
static void c_struct(FILE *f, Type *t, List *done, bool headers)
{
    while (t->kind == T_ARRAY) {
        t = t->elem;
    }
    if (t->kind != T_STRUCT || t->def->opaque) {
        return;
    }
    for (int j = 0; j < done->n; j++) {
        if (done->v[j] == t) {
            return;
        }
    }
    add(done, t);
    for (int j = 0; j < t->def->fields.n; j++) {
        c_struct(f, ((Field *)t->def->fields.v[j])->type, done, headers);
    }
    char *name = c_name(t);
    List names = {0};
    for (int j = 0; j < t->def->fields.n; j++) {
        Field *field = t->def->fields.v[j];
        add(&names, t->def->c_name ? field->name : fmt("f%d", j));
    }
    // A header defines only the C-named types.
    if (!headers || !t->def->c_name) {
        fputs("typedef struct {", f);
        for (int j = 0; j < t->def->fields.n; j++) {
            Type *base = ((Field *)t->def->fields.v[j])->type;
            char *dims = "";
            for (; base->kind == T_ARRAY; base = base->elem) {
                dims = fmt("%s[%d]", dims, base->count);
            }
            fprintf(f, " %s %s%s;",
                    base->kind == T_STRING ? "struct { const char *text; int64_t length; }"
                                           : c_name(base),
                    (char *)names.v[j], dims);
        }
        fprintf(f, " } %s;\n", name);
    }
    fprintf(f,
            "_Static_assert(sizeof(%s) == %zu && _Alignof(%s) == %zu, "
            "\"R^ layout of %s differs from C\");\n",
            name, t->size, name, t->align, t->def->name);
    size_t offset = 0;
    for (int j = 0; j < t->def->fields.n; j++) {
        Type *field = ((Field *)t->def->fields.v[j])->type;
        offset = (offset + field->align - 1) / field->align * field->align;
        fprintf(f,
                "_Static_assert(offsetof(%s, %s) == %zu && sizeof(((%s *)0)->%s) == %zu, "
                "\"R^ layout of %s.%s differs from C\");\n",
                name, (char *)names.v[j], offset, name, (char *)names.v[j], field->size,
                t->def->name, (char *)names.v[j]);
        offset += field->size;
    }
}

// The L^ spelling of a value an exportL^ function takes or answers; a host
// type is qualified by the module, which the registration receives.
static const char *l_name(Type *t)
{
    Def *data = lhat_data_def(t);
    if (t->kind == T_STRUCT && t->def->tuple) {
        char *names = "";
        for (int j = 0; j < t->def->fields.n; j++) {
            names =
                fmt("%s%s%s", names, j ? ", " : "", l_name(((Field *)t->def->fields.v[j])->type));
        }
        return names;
    }
    return data                  ? fmt("%%s.%s%s", data->name, t->kind == T_UNION ? "|nil^" : "")
           : t->kind == T_STRUCT ? fmt("%%s.%s", t->def->name)
           : t->kind == T_STRING ? "string^"
           : t->kind == T_BOOL   ? "bool^"
                                 : "number^";
}

// The LhatHostValueFieldKind L^ reads a scalar field with, or NULL.
static const char *l_field(Type *t)
{
    switch (t->kind) {
    case T_F32:
    case T_F64:
        return fmt("LHAT_HVFIELD_F%d", numeric_bits(t->kind));
    case T_I8:
    case T_I16:
    case T_I32:
    case T_I64:
        return fmt("LHAT_HVFIELD_I%d", numeric_bits(t->kind));
    case T_U8:
    case T_U16:
    case T_U32:
        return fmt("LHAT_HVFIELD_U%d", numeric_bits(t->kind));
    default:
        return NULL;
    }
}

// A registration whose signature names host value types: the module goes
// in once for each.
static char *l_register(const char *call, const char *signature)
{
    char *modules = "";
    for (const char *s = strstr(signature, "%s"); s; s = strstr(s + 2, "%s")) {
        modules = fmt("%s, module", modules);
    }
    return fmt("  snprintf(s, sizeof s, \"%s;\"%s);\n  if (!%s) {\n    return false;\n  }\n",
               signature, modules, call);
}

// Whether L^ releases d's host data through a dispose method, so that the
// glue keeps one L^ value per pointer while L^ holds it.
static bool l_held(Def *d)
{
    for (int j = 0; d && j < d->own_methods.n; j++) {
        Function *f = d->own_methods.v[j];
        if (!f->priv && !strcmp(c_function_symbol(f), "dispose")) {
            return true;
        }
    }
    return false;
}

// What makes the L^ value of d's host data.
static const char *l_make(Def *d)
{
    return l_held(d) ? "rh_lhat_hold" : "lhat_machine_make_hostdata";
}

// The C statement answering the R^ value v of type t as answers[k], nil if
// L^ cannot hold it; a string's length is v.length.
static char *l_answer(Type *t, const char *v, int k)
{
    Def *data = lhat_data_def(t);
    char *nil = fmt(")) {\n    answers[%d] = lhat_nil();\n  }\n", k);
    if (t->kind == T_STRING) {
        return fmt(
            "  if (!lhat_machine_make_string(machine, %s.text, (size_t)%s.length, &answers[%d]%s",
            v, v, k, nil);
    }
    if (data) {
        return fmt("  if (!%s || !%s(machine, rh_lhat_data_%d, %s, &answers[%d]%s", v, l_make(data),
                   data->type->id, v, k, nil);
    }
    if (t->kind == T_STRUCT) {
        return fmt("  if (!lhat_make_hostvalue(machine, rh_lhat_tag_%d, &%s, &answers[%d]%s", t->id,
                   v, k, nil);
    }
    return fmt("  answers[%d] = %s(%s);\n", k,
               t->kind == T_BOOL     ? "lhat_bool"
               : kind_float(t->kind) ? "lhat_real"
                                     : "lhat_integer",
               v);
}

// LhatHostFn wrappers for exportL^ functions and methods, and the function
// that registers them, with the exportL^ def^ host value types and their
// scalar fields and the exportL^ ref^ def^ host data types, under an L^
// module path with signatures R^'s types imply. Each wrapper converts the
// L^ arguments, calls the R^ entry, and answers its result; a string
// argument is borrowed for the call, a string result copied, a host value
// copied either way, and host data passed as its pointer. For each host
// data type, rhat_lhat_wrap_<Name> makes the L^ value of an R^ reference.
static void lhat_glue(Program *p, FILE *f, List *hosts)
{
    fputs(
        "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
        "void rh_terminate(const char *, int64_t, int32_t, int32_t);\n"
        "static void *rh_lhat_value(LhatValue v, const LhatHostValueTag *tag)\n{\n"
        "  void *bytes = lhat_hostvalue_data(v, tag);\n  if (!bytes) {\n"
        "    static const char m[] = \"exportL^ received another host value type\";\n"
        "    rh_terminate(m, sizeof m - 1, 0, 1);\n  }\n  return bytes;\n}\n"
        "static void *rh_lhat_data(LhatValue v, const LhatHostDataTag *tag, bool nullable)\n{\n"
        "  void *pointer = nullable && v.tag == LHAT_VALUE_NIL ? NULL : lhat_hostdata_pointer(v, tag);\n"
        "  if (!pointer && !nullable) {\n"
        "    static const char m[] = \"exportL^ received another host data type\";\n"
        "    rh_terminate(m, sizeof m - 1, 0, 1);\n  }\n  return pointer;\n}\n",
        f);
    bool table = false;
    for (int j = 0; j < p->defs.n; j++) {
        table |= ((Def *)p->defs.v[j])->lhat_data && l_held(p->defs.v[j]);
    }
    if (table) {
        fputs(
            "/* The L^ values of host data L^ releases, one per machine, type and pointer\n"
            " * while L^ holds it, so that a pointer answered again is not given a second\n"
            " * wrapper and released twice. An open-addressing table the collector does\n"
            " * not see; release drops the entry. Each thread keeps its own, as a\n"
            " * machine is made, run and disposed on one thread and shares nothing. */\n"
            "typedef struct { LhatMachine *machine; const LhatHostDataTag *tag; void *pointer; LhatValue value; } RhLhatHeld;\n"
            "static _Thread_local RhLhatHeld *rh_lhat_held;\n"
            "static _Thread_local size_t rh_lhat_held_size, rh_lhat_held_count;\n"
            "static size_t rh_lhat_slot(LhatMachine *machine, const LhatHostDataTag *tag, void *pointer)\n"
            "{\n"
            "  size_t mask = rh_lhat_held_size - 1;\n"
            "  size_t at = (size_t)(((uintptr_t)pointer >> 4) * UINT64_C(11400714819323198485)) & mask;\n"
            "  for (RhLhatHeld *e = &rh_lhat_held[at];\n"
            "       e->pointer && (e->pointer != pointer || e->tag != tag || e->machine != machine);\n"
            "       e = &rh_lhat_held[at]) {\n"
            "    at = (at + 1) & mask;\n"
            "  }\n"
            "  return at;\n"
            "}\n"
            "static bool rh_lhat_hold(LhatMachine *machine, const LhatHostDataTag *tag, void *pointer, LhatValue *out)\n"
            "{\n"
            "  RhLhatHeld *e = rh_lhat_held_size ? &rh_lhat_held[rh_lhat_slot(machine, tag, pointer)] : NULL;\n"
            "  if (e && e->pointer) {\n"
            "    *out = e->value;\n"
            "    return true;\n"
            "  }\n"
            "  if (!lhat_machine_make_hostdata(machine, tag, pointer, out)) {\n"
            "    return false;\n"
            "  }\n"
            "  if (2 * (rh_lhat_held_count + 1) > rh_lhat_held_size) {\n"
            "    RhLhatHeld *old = rh_lhat_held;\n"
            "    size_t size = rh_lhat_held_size;\n"
            "    rh_lhat_held_size = size ? 2 * size : 64;\n"
            "    rh_lhat_held = calloc(rh_lhat_held_size, sizeof *rh_lhat_held);\n"
            "    if (!rh_lhat_held) {\n"
            "      static const char m[] = \"out of memory for exportL^ host data\";\n"
            "      rh_terminate(m, sizeof m - 1, 0, 1);\n"
            "    }\n"
            "    for (size_t j = 0; j < size; j++) {\n"
            "      if (old[j].pointer) {\n"
            "        rh_lhat_held[rh_lhat_slot(old[j].machine, old[j].tag, old[j].pointer)] = old[j];\n"
            "      }\n"
            "    }\n"
            "    free(old);\n"
            "  }\n"
            "  rh_lhat_held[rh_lhat_slot(machine, tag, pointer)] = (RhLhatHeld){machine, tag, pointer, *out};\n"
            "  rh_lhat_held_count++;\n"
            "  return true;\n"
            "}\n"
            "static void rh_lhat_drop(LhatMachine *machine, const LhatHostDataTag *tag, void *pointer)\n"
            "{\n"
            "  size_t at = rh_lhat_held_size ? rh_lhat_slot(machine, tag, pointer) : 0;\n"
            "  if (!rh_lhat_held_size || !rh_lhat_held[at].pointer) {\n"
            "    return;\n"
            "  }\n"
            "  rh_lhat_held[at].pointer = NULL;\n"
            "  rh_lhat_held_count--;\n"
            "  /* Reinsert the rest of the run so that no lookup stops at the hole. */\n"
            "  for (size_t j = (at + 1) & (rh_lhat_held_size - 1); rh_lhat_held[j].pointer;\n"
            "       j = (j + 1) & (rh_lhat_held_size - 1)) {\n"
            "    RhLhatHeld e = rh_lhat_held[j];\n"
            "    rh_lhat_held[j].pointer = NULL;\n"
            "    rh_lhat_held[rh_lhat_slot(e.machine, e.tag, e.pointer)] = e;\n"
            "  }\n"
            "}\n"
            "",
            f);
    }
    char *registrations = "";
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        if (d->lhat_data) {
            fprintf(
                f,
                "static const LhatHostDataTag *rh_lhat_data_%d;\n"
                "bool rhat_lhat_wrap_%s(LhatMachine *machine, void *pointer, LhatValue *out)\n{\n"
                "  return %s(machine, rh_lhat_data_%d, pointer, out);\n}\n",
                d->type->id, d->name, l_make(d), d->type->id);
            registrations =
                fmt("%s  rh_lhat_data_%d = lhat_register_hostdata_type(program, module, \"%s\");\n"
                    "  if (!rh_lhat_data_%d) {\n    return false;\n  }\n",
                    registrations, d->type->id, d->name, d->type->id);
        }
        if (!d->lhat_value) {
            continue;
        }
        char *name = c_name(d->type);
        fprintf(f, "static const LhatHostValueTag *rh_lhat_tag_%d;\n", d->type->id);
        registrations =
            fmt("%s  rh_lhat_tag_%d = lhat_register_hostvalue_type(program, module, \"%s\", "
                "sizeof(%s));\n  if (!rh_lhat_tag_%d) {\n    return false;\n  }\n",
                registrations, d->type->id, d->name, name, d->type->id);
        for (int k = 0; k < d->fields.n; k++) {
            Field *field = d->fields.v[k];
            if (l_field(field->type) && !field->priv) {
                registrations =
                    fmt("%s  if (!lhat_register_hostvalue_field(program, module, \"%s\", \"%s\", "
                        "offsetof(%s, %s), %s)) {\n    return false;\n  }\n",
                        registrations, d->name, field->name, name,
                        d->c_name ? field->name : fmt("f%d", k), l_field(field->type));
            }
        }
    }
    for (int j = 0; j < hosts->n; j++) {
        Instance *in = hosts->v[j];
        const char *name = c_function_symbol(in->fn), *symbol = in->symbol;
        Type *t = in->result;
        bool text = t->kind == T_STRING, none = t->kind == T_VOID, value = t->kind == T_STRUCT;
        Def *data = lhat_data_def(t), *owner = in->fn->owner;
        char *entry = text ? "const char **, int64_t *" : value ? fmt("%s *", c_name(t)) : "";
        char *call = text ? "&r.text, &r.length" : value ? "&r" : "";
        char *signature = fmt("%s", in->fn->procedure ? "p^" : "f^"), *body = "";
        for (int k = 0; k < in->params.n; k++) {
            Type *param = in->params.v[k];
            char *sep = *entry ? ", " : "", *comma = *call ? ", " : "";
            // A method's receiver comes as ref^ to the host value or data.
            bool self = owner && !k;
            Def *pointed = lhat_data_def(param);
            Type *held = self && !pointed ? param->elem : param;
            signature = fmt("%s%s%s", signature, k ? ", " : "", self ? "self^" : l_name(param));
            if (pointed) {
                entry = fmt("%s%svoid *", entry, sep);
                body =
                    fmt("%s  void *v%d = rh_lhat_data(arguments[%d], rh_lhat_data_%d, %s);\n", body,
                        k, k, pointed->type->id, param->kind == T_UNION ? "true" : "false");
                call = fmt("%s%sv%d", call, comma, k);
                continue;
            }
            if (held->kind == T_STRUCT) {
                entry = fmt("%s%s%s *", entry, sep, c_name(held));
                body =
                    fmt("%s  %s v%d;\n  memcpy(&v%d, rh_lhat_value(arguments[%d], rh_lhat_tag_%d), "
                        "sizeof v%d);\n",
                        body, c_name(held), k, k, k, held->id, k);
                call = fmt("%s%s&v%d", call, comma, k);
                continue;
            }
            if (param->kind == T_STRING) {
                entry = fmt("%s%sconst char *, int64_t", entry, sep);
                body = fmt(
                    "%s  const LhatString *s%d = (const LhatString *)lhat_as_object(arguments[%d]);\n",
                    body, k, k);
                call = fmt("%s%ss%d->text, (int64_t)s%d->length", call, comma, k, k);
                continue;
            }
            entry = fmt("%s%s%s", entry, sep, c_name(param));
            if (param->kind == T_BOOL) {
                call = fmt("%s%sarguments[%d].as.boolean", call, comma, k);
            }
            else if (kind_float(param->kind)) {
                call = fmt("%s%s(%s)lhat_number_as_real(arguments[%d])", call, comma, c_name(param),
                           k);
            }
            else {
                body = fmt(
                    "%s  LhatInteger w%d;\n  if (!lhat_number_as_whole(arguments[%d], &w%d)) {\n"
                    "    static const char m[] = \"exportL^ %s needs a whole number\";\n"
                    "    rh_terminate(m, sizeof m - 1, 0, 1);\n  }\n",
                    body, k, k, k, name);
                call = fmt("%s%s(%s)w%d", call, comma, c_name(param), k);
            }
        }
        if (!none) {
            signature = fmt("%s -> %s", signature, l_name(t));
        }
        if (owner && owner->lhat_data && l_held(owner) && !strcmp(name, "dispose")) {
            body = fmt("%s  rh_lhat_drop(machine, rh_lhat_data_%d, v0);\n", body, owner->type->id);
        }
        fprintf(f, "%s rh_entry_%s(%s);\n", none || text || value ? "void" : c_name(t), symbol,
                *entry ? entry : "void");
        fprintf(f,
                "static void rh_lhat_%s(LhatMachine *machine, void *context, const LhatValue "
                "*arguments, size_t count, LhatValue *answers, int *answer_count)\n{\n"
                "  (void)machine; (void)context; (void)count;\n%s",
                symbol, body);
        if (none) {
            // L^ releases host data through dispose with no room for answers.
            fprintf(f,
                    "  rh_entry_%s(%s);\n  if (answer_count) {\n    *answer_count = 0;\n  }\n}\n",
                    symbol, call);
        }
        else {
            // Multiple results answer each part in order.
            bool tuple = value && t->def->tuple;
            char *answers = tuple ? "" : l_answer(t, "r", 0);
            for (int k = 0; tuple && k < t->def->fields.n; k++) {
                answers = fmt("%s%s", answers,
                              l_answer(((Field *)t->def->fields.v[k])->type, fmt("r.f%d", k), k));
            }
            if (text || value) {
                fprintf(f, "  %s r;\n  rh_entry_%s(%s);\n",
                        text ? "struct { const char *text; int64_t length; }" : c_name(t), symbol,
                        call);
            }
            else {
                fprintf(f, "  %s r = rh_entry_%s(%s);\n", data ? "void *" : c_name(t), symbol,
                        call);
            }
            fprintf(f, "%s  *answer_count = %d;\n}\n", answers, tuple ? t->def->fields.n : 1);
        }
        registrations = fmt(
            "%s%s", registrations,
            l_register(owner ? fmt("lhat_register_%smember(program, module, "
                                   "\"%s\", \"%s\", s, rh_lhat_%s, NULL)",
                                   owner->lhat_data ? "" : "hostvalue_", owner->name, name, symbol)
                             : fmt("lhat_register_func(program, module, \"%s\", s, "
                                   "rh_lhat_%s, NULL)",
                                   name, symbol),
                       signature));
    }
    fprintf(f,
            "bool rhat_lhat_register(LhatProgram *program, const char *module)\n{\n"
            "  char s[1024];\n  (void)s;\n%s  return true;\n}\n",
            registrations);
}

bool emit_c_shims(Program *p, const char *path, bool headers)
{
    List shims = {0};
    for (int j = 0; j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        bool seen = false;
        for (int k = 0; k < shims.n; k++) {
            seen |= !strcmp(((Instance *)shims.v[k])->symbol, in->symbol);
        }
        if (in->fn->external && needs_shim(in) && !seen &&
            (in->fn->module->c_headers.n > 0) == headers) {
            add(&shims, in);
        }
    }
    // exportC^ wrappers name only R^'s own types, so they need no header.
    List exports = {0};
    for (int j = 0; !headers && j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->fn->exported) {
            add(&exports, in);
        }
    }
    // exportL^ glue names L^'s types, so it goes with the headers.
    List hosts = {0};
    for (int j = 0; headers && j < p->instances.n; j++) {
        Instance *in = p->instances.v[j];
        if (in->fn->lhat_export) {
            add(&hosts, in);
        }
    }
    bool lhat = hosts.n > 0;
    for (int j = 0; headers && j < p->defs.n; j++) {
        lhat |= ((Def *)p->defs.v[j])->lhat_value || ((Def *)p->defs.v[j])->lhat_data;
    }
    if (!shims.n && !exports.n && !lhat) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        fail((Loc){path, 1, 1}, "cannot write C shims");
    }
    fputs("/* Generated by rhat: C shims for externC^ functions. */\n"
          "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n",
          f);
    // Every includeC^ header of the program, once, in declaration order.
    List included = {0};
    for (int j = 0; headers && j < p->modules.n; j++) {
        Module *m = p->modules.v[j];
        for (int k = 0; k < m->c_headers.n; k++) {
            bool seen = false;
            for (int n = 0; n < included.n; n++) {
                seen |= !strcmp(included.v[n], m->c_headers.v[k]);
            }
            if (!seen) {
                add(&included, m->c_headers.v[k]);
                fprintf(f, "#include \"%s\"\n", (char *)m->c_headers.v[k]);
            }
        }
    }
    List done = {0}, typed = shims;
    for (int j = 0; j < exports.n; j++) {
        add(&typed, exports.v[j]);
    }
    for (int j = 0; j < typed.n; j++) {
        Instance *in = typed.v[j];
        c_struct(f, in->result, &done, headers);
        for (int k = 0; k < in->params.n; k++) {
            c_struct(f, in->params.v[k], &done, headers);
        }
    }
    for (int j = 0; j < exports.n; j++) {
        Instance *in = exports.v[j];
        const char *name = c_function_symbol(in->fn);
        bool out = in->result->kind == T_STRUCT, none = in->result->kind == T_VOID;
        char *entry = out ? fmt("%s *out", c_name(in->result)) : "", *wrapper = "";
        char *call = out ? "&r" : "";
        for (int k = 0; k < in->params.n; k++) {
            Type *param = in->params.v[k];
            bool value = param->kind == T_STRUCT;
            entry = fmt("%s%s%s%s", entry, *entry ? ", " : "", c_name(param), value ? " *" : "");
            wrapper = fmt("%s%s%s a%d", wrapper, k ? ", " : "", c_name(param), k);
            call = fmt("%s%s%sa%d", call, *call ? ", " : "", value ? "&" : "", k);
        }
        char *result = none || out ? "void" : c_name(in->result);
        fprintf(f, "%s rh_entry_%s(%s);\n", result, in->symbol, *entry ? entry : "void");
        fprintf(f, "%s %s(%s) { ", none ? "void" : c_name(in->result), name,
                *wrapper ? wrapper : "void");
        if (out) {
            fprintf(f, "%s r; rh_entry_%s(%s); return r; }\n", c_name(in->result), in->symbol,
                    call);
        }
        else {
            fprintf(f, "%srh_entry_%s(%s); }\n", none ? "" : "return ", in->symbol, call);
        }
    }
    if (lhat) {
        for (int j = 0; j < p->defs.n; j++) {
            Def *d = p->defs.v[j];
            if (d->lhat_value) {
                c_struct(f, d->type, &done, headers);
            }
        }
        for (int j = 0; j < hosts.n; j++) {
            c_struct(f, ((Instance *)hosts.v[j])->result, &done, headers);
        }
        lhat_glue(p, f, &hosts);
    }
    for (int j = 0; j < shims.n; j++) {
        Instance *in = shims.v[j];
        bool out = in->result->kind == T_STRUCT, none = in->result->kind == T_VOID;
        char *declared = "", *shim = out ? fmt("%s *out", c_name(in->result)) : "", *call = "";
        for (int k = 0; k < in->params.n; k++) {
            Type *param = in->params.v[k];
            bool value = param->kind == T_STRUCT;
            declared = fmt("%s%s%s", declared, k ? ", " : "", c_name(param));
            shim = fmt("%s%s%s%s a%d", shim, *shim ? ", " : "", value ? "const " : "",
                       value ? fmt("%s *", c_name(param)) : c_name(param), k);
            call = fmt("%s%s%sa%d", call, k ? ", " : "", value ? "*" : "", k);
        }
        // A header declares its own functions; otherwise R^'s signature does.
        if (!headers) {
            fprintf(f, "%s %s(%s);\n", none ? "void" : c_name(in->result), in->symbol,
                    *declared ? declared : "void");
        }
        // A header's pointer result may be const; R^ keeps it as void *.
        bool pointer = !out && !none && !strcmp(c_name(in->result), "void *");
        fprintf(f, "%s rh_shim_%s(%s) { %s%s%s(%s); }\n", out || none ? "void" : c_name(in->result),
                in->symbol, *shim ? shim : "void",
                out    ? "*out = "
                : none ? ""
                       : "return ",
                pointer ? "(void *)" : "", in->symbol, call);
    }
    if (fclose(f)) {
        fail((Loc){path, 1, 1}, "cannot write C shims");
    }
    return true;
}
