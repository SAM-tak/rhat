#include "rhat.h"

/* These are LLVM definitions, never C runtime entry points. Always-inline
 * helpers keep the emitter small while exposing their work to the optimizer. */
static void append_helper(FILE *f, bool literal) {
  fprintf(f,
          "define internal void @rh_append_%s(ptr %%a, ptr %%src, i64 %%n%s, "
          "i32 %%line) alwaysinline {\nentry:\n",
          literal ? "literal" : "view", literal ? "" : ", i64 %size");
  if (literal)
    fputs("  %size = add i64 0, 1\n", f);
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
        "bad:\n  call void @rh_stack_overflow(i32 %line)\n  unreachable\n"
        "checked:\n"
        "  %off = mul i64 %len, %size\n"
        "  %copybytes = mul i64 %n, %size\n"
        "  %dst = getelementptr i8, ptr %data, i64 %off\n"
        "  %empty = icmp eq i64 %n, 0\n"
        "  br i1 %empty, label %done, label %copy\n"
        "copy:\n",
        f);
  fprintf(f,
          "  call void @llvm.%s.p0.p0.i64(ptr %%dst, ptr %%src, i64 "
          "%%copybytes, i1 false), !tbaa !4\n",
          literal ? "memcpy" : "memmove");
  fputs("  br label %done\n"
        "done:\n"
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

static void fp8_decode(FILE *f) {
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

/* Round binary32 or binary64 directly to E4M3FN, ties to even. All shifts
 * execute only for -10 <= exponent <= 8; no oversized LLVM shift/poison.
 * The binary64 path never goes through binary32 (no double rounding). */
static void fp8_encode(FILE *f, bool wide) {
  int precision = wide ? 52 : 23;
  fprintf(f,
          "define internal i8 @rh_f8_encode%s(%s %%value) alwaysinline "
          "nounwind memory(none) {\nentry:\n",
          wide ? "_f64" : "", wide ? "double" : "float");
  if (wide)
    fputs("  %raw = bitcast double %value to i64\n", f);
  else
    fputs("  %raw32 = bitcast float %value to i32\n"
          "  %raw = zext i32 %raw32 to i64\n",
          f);
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
          precision - 9, precision - 3,
          (unsigned long long)((UINT64_C(1) << precision) - 1),
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

/* After a flex^ shrink, lower the stack top to the new end. The committed
 * field keeps the highest end seen; once more than 4 MiB lies above the end,
 * the runtime returns those pages to the OS. */
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

void emit_llvm_builtins(FILE *f) {
  fputs("declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)\n"
        "declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)\n",
        f);
  flex_release(f);
  append_helper(f, true);
  append_helper(f, false);
  fp8_decode(f);
  fp8_encode(f, false);
  fp8_encode(f, true);
}
