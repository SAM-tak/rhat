#include "rhat.h"

// These are LLVM definitions, never C runtime entry points. Always-inline
// helpers keep the emitter small while exposing their work to the optimizer.
static void append_helper(FILE *f, int mode)
{
    bool literal = mode == 1, repeat = mode == 2;
    fprintf(f,
            "define internal void @rh_append_%s(ptr %%a, ptr %%src, i64 %%n%s, "
            "i32 %%line) alwaysinline {\nentry:\n",
            repeat    ? "repeat"
            : literal ? "literal"
                      : "view",
            literal ? "" : ", i64 %size");
    if (literal) {
        fputs("  %size = add i64 0, 1\n", f);
    }
    fputs("  %data = load ptr, ptr %a, !tbaa !5\n"
          "  %lenp = getelementptr {ptr,i64,ptr}, ptr %a, i32 0, i32 1\n"
          "  %len = load i64, ptr %lenp, !tbaa !5\n"
          "  %stackp = getelementptr {ptr,i64,ptr}, ptr %a, i32 0, i32 2\n"
          "  %stack = load ptr, ptr %stackp, !tbaa !5\n"
          "  %limitp = getelementptr %RhStack, ptr %stack, i32 0, i32 2\n"
          "  %limit = load ptr, ptr %limitp\n"
          "  %li = ptrtoint ptr %limit to i64\n"
          "  %di = ptrtoint ptr %data to i64\n"
          "  %bytes = sub i64 %li, %di\n"
          "  %capacity = udiv i64 %bytes, %size\n"
          "  %len_ok = icmp ule i64 %len, %capacity\n"
          "  %remaining = sub i64 %capacity, %len\n"
          "  %count_ok = icmp ule i64 %n, %remaining\n"
          "  %ok = and i1 %len_ok, %count_ok\n"
          "  br i1 %ok, label %checked, label %bad, !prof !0\n"
          "bad:\n  call void @rh_growth_fail(ptr %stack, i32 %line)\n  unreachable\n"
          "checked:\n"
          "  %off = mul i64 %len, %size\n"
          "  %copybytes = mul i64 %n, %size\n"
          "  %dst = getelementptr i8, ptr %data, i64 %off\n"
          "  %empty = icmp eq i64 %n, 0\n"
          "  br i1 %empty, label %done, label %copy\n"
          "copy:\n",
          f);
    if (repeat) {
        fputs(
            "  br label %fill\n"
            "fill:\n"
            "  %index = phi i64 [0, %copy], [%next, %fill]\n"
            "  %offset = mul i64 %index, %size\n"
            "  %element = getelementptr i8, ptr %dst, i64 %offset\n"
            "  call void @llvm.memcpy.p0.p0.i64(ptr %element, ptr %src, i64 %size, i1 false), !tbaa !4\n"
            "  %next = add i64 %index, 1\n"
            "  %more = icmp ult i64 %next, %n\n"
            "  br i1 %more, label %fill, label %done\n",
            f);
    }
    else {
        fprintf(f,
                "  call void @llvm.%s.p0.p0.i64(ptr %%dst, ptr %%src, i64 "
                "%%copybytes, i1 false), !tbaa !4\n",
                literal ? "memcpy" : "memmove");
        fputs("  br label %done\n", f);
    }
    fputs("done:\n"
          "  %newlen = add i64 %len, %n\n"
          "  %endbytes = add i64 %off, %copybytes\n"
          "  %end = getelementptr i8, ptr %data, i64 %endbytes\n"
          "  %topp = getelementptr %RhStack, ptr %stack, i32 0, i32 1\n"
          "  %top = load ptr, ptr %topp\n"
          "  %greater = icmp ugt ptr %end, %top\n"
          "  %high = select i1 %greater, ptr %end, ptr %top\n"
          "  store ptr %high, ptr %topp\n"
          "  store i64 %newlen, ptr %lenp, !tbaa !5\n"
          "  ret void\n}\n",
          f);
}

// Reserve n elements for a fresh T[max^] at its stack's top, preceded by the
// private 16-byte aligned RhStack its push and append check: limit ends the
// elements, so the shared growth paths enforce the capacity unchanged.
static void reserve_helper(FILE *f)
{
    fputs("define internal void @rh_reserve(ptr %a, i64 %n, i64 %size, i32 %line) "
          "alwaysinline {\nentry:\n"
          "  %top = load ptr, ptr %a, !tbaa !5\n"
          "  %stackp = getelementptr {ptr,i64,ptr}, ptr %a, i32 0, i32 2\n"
          "  %stack = load ptr, ptr %stackp, !tbaa !5\n"
          "  %limitp = getelementptr %RhStack, ptr %stack, i32 0, i32 2\n"
          "  %limit = load ptr, ptr %limitp\n"
          "  %ti = ptrtoint ptr %top to i64\n"
          "  %bumped = add i64 %ti, 15\n"
          "  %ri = and i64 %bumped, -16\n"
          "  %li = ptrtoint ptr %limit to i64\n"
          "  %bytes = sub i64 %li, %ri\n"
          "  %room_ok = icmp sge i64 %bytes, 32\n"
          "  %room = sub i64 %bytes, 32\n"
          "  %capacity = udiv i64 %room, %size\n"
          "  %count_ok = icmp ule i64 %n, %capacity\n"
          "  %ok = and i1 %room_ok, %count_ok\n"
          "  br i1 %ok, label %checked, label %bad, !prof !0\n"
          "bad:\n  call void @rh_stack_overflow(i32 %line)\n  unreachable\n"
          "checked:\n"
          "  %record = inttoptr i64 %ri to ptr\n"
          "  %data = getelementptr i8, ptr %record, i64 32\n"
          "  %databytes = mul i64 %n, %size\n"
          "  %end = getelementptr i8, ptr %data, i64 %databytes\n"
          "  store %RhStack { ptr null, ptr null, ptr null, ptr null }, ptr %record\n"
          "  %recordtop = getelementptr %RhStack, ptr %record, i32 0, i32 1\n"
          "  store ptr %data, ptr %recordtop\n"
          "  %recordlimit = getelementptr %RhStack, ptr %record, i32 0, i32 2\n"
          "  store ptr %end, ptr %recordlimit\n"
          "  %stacktop = getelementptr %RhStack, ptr %stack, i32 0, i32 1\n"
          "  store ptr %end, ptr %stacktop\n"
          "  store ptr %data, ptr %a, !tbaa !5\n"
          "  store ptr %record, ptr %stackp, !tbaa !5\n"
          "  ret void\n}\n",
          f);
}

// Growth past a stack's limit: a T[max^]'s private record has no base, so
// there the limit is its capacity rather than the data stack's end.
static void growth_fail(FILE *f)
{
    fputs("define internal void @rh_growth_fail(ptr %stack, i32 %line) cold noreturn {\n"
          "entry:\n"
          "  %base = load ptr, ptr %stack\n"
          "  %capped = icmp eq ptr %base, null\n"
          "  br i1 %capped, label %full, label %overflow\n"
          "full:\n  call void @rh_capacity_fail(i32 %line)\n  unreachable\n"
          "overflow:\n  call void @rh_stack_overflow(i32 %line)\n  unreachable\n}\n",
          f);
}

static void fp8_decode(FILE *f)
{
    fputs("define internal float @rh_f8_decode(i8 %value) alwaysinline nounwind "
          "memory(none) {\n"
          "entry:\n"
          "  %raw = zext i8 %value to i32\n"
          "  %signbit = and i32 %raw, 128\n"
          "  %sign = shl i32 %signbit, 24\n"
          "  %payload = and i32 %raw, 127\n"
          "  %es = lshr i32 %raw, 3\n"
          "  %exponent = and i32 %es, 15\n"
          "  %fraction = and i32 %raw, 7\n"
          "  %ne = add i32 %exponent, 120\n"
          "  %neb = shl i32 %ne, 23\n"
          "  %nfb = shl i32 %fraction, 20\n"
          "  %normal = or i32 %neb, %nfb\n"
          "  %ge4 = icmp uge i32 %fraction, 4\n"
          "  %ge2 = icmp uge i32 %fraction, 2\n"
          "  %lower = select i1 %ge2, i32 1, i32 0\n"
          "  %leading = select i1 %ge4, i32 2, i32 %lower\n"
          "  %one = shl i32 1, %leading\n"
          "  %sf = sub i32 %fraction, %one\n"
          "  %shift = sub i32 23, %leading\n"
          "  %sfb = shl i32 %sf, %shift\n"
          "  %se = add i32 %leading, 118\n"
          "  %seb = shl i32 %se, 23\n"
          "  %sb = or i32 %seb, %sfb\n"
          "  %zero = icmp eq i32 %fraction, 0\n"
          "  %subnormal = select i1 %zero, i32 0, i32 %sb\n"
          "  %sub = icmp eq i32 %exponent, 0\n"
          "  %finite = select i1 %sub, i32 %subnormal, i32 %normal\n"
          "  %nan = icmp eq i32 %payload, 127\n"
          "  %magnitude = select i1 %nan, i32 2143289344, i32 %finite\n"
          "  %bits = or i32 %sign, %magnitude\n"
          "  %result = bitcast i32 %bits to float\n"
          "  ret float %result\n}\n",
          f);
}

// Round binary32 or binary64 directly to E4M3FN, ties to even. All shifts
// execute only for -10 <= exponent <= 8; no oversized LLVM shift/poison.
// The binary64 path never goes through binary32 (no double rounding).
static void fp8_encode(FILE *f, bool wide)
{
    int precision = wide ? 52 : 23;
    fprintf(f,
            "define internal i8 @rh_f8_encode%s(%s %%value) alwaysinline "
            "nounwind memory(none) {\nentry:\n",
            wide ? "_f64" : "", wide ? "double" : "float");
    if (wide) {
        fputs("  %raw = bitcast double %value to i64\n", f);
    }
    else {
        fputs("  %raw32 = bitcast float %value to i32\n"
              "  %raw = zext i32 %raw32 to i64\n",
              f);
    }
    fprintf(f,
            "  %%ss = lshr i64 %%raw, %d\n"
            "  %%sign = and i64 %%ss, 128\n"
            "  %%sign8 = trunc i64 %%sign to i8\n"
            "  %%es = lshr i64 %%raw, %d\n"
            "  %%exponent = and i64 %%es, %d\n"
            "  %%e = sub i64 %%exponent, %d\n",
            wide ? 56 : 24, precision, wide ? 2047 : 255, wide ? 1023 : 127);
    fputs("  %large = icmp sgt i64 %e, 8\n"
          "  br i1 %large, label %nan, label %small_check\n"
          "nan:\n"
          "  %nanvalue = or i8 %sign8, 127\n"
          "  ret i8 %nanvalue\n"
          "small_check:\n"
          "  %small = icmp slt i64 %e, -10\n"
          "  br i1 %small, label %zero, label %round\n"
          "zero:\n  ret i8 %sign8\n"
          "round:\n"
          "  %sub = icmp slt i64 %e, -6\n",
          f);
    fprintf(f,
            "  %%subshift = sub i64 %d, %%e\n"
            "  %%shift = select i1 %%sub, i64 %%subshift, i64 %d\n"
            "  %%fraction = and i64 %%raw, %llu\n"
            "  %%significand = or i64 %%fraction, %llu\n",
            precision - 9, precision - 3, (unsigned long long)((UINT64_C(1) << precision) - 1),
            (unsigned long long)(UINT64_C(1) << precision));
    fputs("  %q = lshr i64 %significand, %shift\n"
          "  %unit = shl i64 1, %shift\n"
          "  %mask = sub i64 %unit, 1\n"
          "  %rest = and i64 %significand, %mask\n"
          "  %half = lshr i64 %unit, 1\n"
          "  %greater = icmp ugt i64 %rest, %half\n"
          "  %tie = icmp eq i64 %rest, %half\n"
          "  %lsb = and i64 %q, 1\n"
          "  %odd = icmp ne i64 %lsb, 0\n"
          "  %tieup = and i1 %tie, %odd\n"
          "  %up = or i1 %greater, %tieup\n"
          "  %increment = zext i1 %up to i64\n"
          "  %rounded = add i64 %q, %increment\n"
          "  %carry = icmp eq i64 %rounded, 16\n"
          "  %ce = zext i1 %carry to i64\n"
          "  %next_e = add i64 %e, %ce\n"
          "  %next_q = select i1 %carry, i64 8, i64 %rounded\n"
          "  %biased = add i64 %next_e, 7\n"
          "  %expbits = shl i64 %biased, 3\n"
          "  %fracbits = sub i64 %next_q, 8\n"
          "  %normal = or i64 %expbits, %fracbits\n"
          "  %encoded = select i1 %sub, i64 %rounded, i64 %normal\n"
          "  %overflow = icmp ugt i64 %encoded, 126\n"
          "  %magnitude = select i1 %overflow, i64 127, i64 %encoded\n"
          "  %bits = or i64 %sign, %magnitude\n"
          "  %result = trunc i64 %bits to i8\n"
          "  ret i8 %result\n}\n",
          f);
}

// After a flex^ shrink, lower the stack top to the new end. The committed
// field keeps the highest end seen; once more than 4 MiB lies above the end,
// the runtime returns those pages to the OS.
static void flex_release(FILE *f)
{
    fputs("define internal void @rh_flex_release(ptr %stack, ptr %data, i64 %bytes) "
          "alwaysinline {\nentry:\n"
          "  %end = getelementptr i8, ptr %data, i64 %bytes\n"
          "  %topp = getelementptr %RhStack, ptr %stack, i32 0, i32 1\n"
          "  %old = load ptr, ptr %topp\n"
          "  store ptr %end, ptr %topp\n"
          "  %highp = getelementptr %RhStack, ptr %stack, i32 0, i32 3\n"
          "  %committed = load ptr, ptr %highp\n"
          "  %above = icmp ugt ptr %old, %committed\n"
          "  %high = select i1 %above, ptr %old, ptr %committed\n"
          "  store ptr %high, ptr %highp\n"
          "  %hi = ptrtoint ptr %high to i64\n"
          "  %ei = ptrtoint ptr %end to i64\n"
          "  %spare = sub i64 %hi, %ei\n"
          "  %large = icmp ugt i64 %spare, 4194304\n"
          "  br i1 %large, label %trim, label %done\n"
          "trim:\n"
          "  call void @rh_flex_trim(ptr %stack, ptr %end)\n"
          "  br label %done\n"
          "done:\n"
          "  ret void\n}\n"
          "declare void @rh_flex_trim(ptr, ptr)\n",
          f);
}

// Copy one element into each of n slots of a fixed array.
static void fill_helper(FILE *f)
{
    fputs("define internal void @rh_fill_repeat(ptr %dst, ptr %elem, i64 %n, i64 %size) {\n"
          "entry:\n  br label %loop\n"
          "loop:\n"
          "  %i = phi i64 [0, %entry], [%next, %body]\n"
          "  %more = icmp ult i64 %i, %n\n"
          "  br i1 %more, label %body, label %done\n"
          "body:\n"
          "  %offset = mul i64 %i, %size\n"
          "  %to = getelementptr i8, ptr %dst, i64 %offset\n"
          "  call void @llvm.memcpy.p0.p0.i64(ptr %to, ptr %elem, i64 %size, i1 false)\n"
          "  %next = add i64 %i, 1\n"
          "  br label %loop\n"
          "done:\n  ret void\n}\n",
          f);
}

// Stack groups retain one checkpoint per independently growing row.
static void stack_group_helper(FILE *f, bool restore)
{
    fprintf(
        f,
        "define internal void @rh_stack_group_%s(ptr %%stacks, ptr %%marks, i64 %%n) alwaysinline {\nentry:\n  br label %%loop\nloop:\n"
        "  %%i = phi i64 [0, %%entry], [%%next, %%loop]\n"
        "  %%stack = getelementptr %%RhStack, ptr %%stacks, i64 %%i\n"
        "  %%top = getelementptr %%RhStack, ptr %%stack, i32 0, i32 1\n"
        "  %%mark = getelementptr ptr, ptr %%marks, i64 %%i\n",
        restore ? "restore" : "save");
    fputs(restore ? "  %value = load ptr, ptr %mark\n  store ptr %value, ptr %top\n"
                  : "  %value = load ptr, ptr %top\n  store ptr %value, ptr %mark\n",
          f);
    fputs("  %next = add i64 %i, 1\n  %more = icmp ult i64 %next, %n\n"
          "  br i1 %more, label %loop, label %done\ndone:\n  ret void\n}\n",
          f);
}

void emit_llvm_builtins(FILE *f)
{
    fputs("declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)\n"
          "declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)\n",
          f);
    stack_group_helper(f, false);
    stack_group_helper(f, true);
    flex_release(f);
    append_helper(f, true);
    append_helper(f, false);
    append_helper(f, 2);
    reserve_helper(f);
    fill_helper(f);
    growth_fail(f);
    fp8_decode(f);
    fp8_encode(f, false);
    fp8_encode(f, true);
}
