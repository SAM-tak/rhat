#include "rhat.h"
#include "tooling.h"
#include <math.h>
#include <stdint.h>

const char *builtin_name(int k) {
  static const char *names[] = {"void^", "bool^",   "u8^", "u16^", "u32^",
                                "u64^",  "u128^",   "i8^", "i16^", "i32^",
                                "i64^",  "i128^",   "f8^", "f16^", "f32^",
                                "f64^",  "string^", "nil^",  "char^"};
  return k >= T_VOID && k < T_STRUCT ? names[k] : NULL;
}
bool kind_integer(int k) { return k >= T_U8 && k <= T_I128; }
bool kind_unsigned(int k) { return k >= T_U8 && k <= T_U128; }
bool kind_float(int k) { return k >= T_F8 && k <= T_F64; }
int numeric_bits(int k) {
  static const int widths[] = {0,  1,  8,  16,  32, 64, 128, 8,
                               16, 32, 64, 128, 8,  16, 32,  64};
  return k == T_CHAR ? 32 : k >= T_VOID && k <= T_F64 ? widths[k] : 0;
}
bool integer_fits(const char *text, int kind) {
  static const char *umax[] = {"255", "65535", "4294967295",
                               "18446744073709551615",
                               "340282366920938463463374607431768211455"};
  static const char *imax[] = {"127", "32767", "2147483647",
                               "9223372036854775807",
                               "170141183460469231731687303715884105727"};
  static const char *imin[] = {"128", "32768", "2147483648",
                               "9223372036854775808",
                               "170141183460469231731687303715884105728"};
  bool negative = *text == '-';
  if (negative)
    text++;
  if (negative && kind_unsigned(kind))
    return false;
  for (const char *p = text; *p; p++)
    if (*p < '0' || *p > '9')
      return false;
  if (!*text)
    return false;
  while (*text == '0' && text[1])
    text++;
  int index = kind_unsigned(kind) ? kind - T_U8 : kind - T_I8;
  const char *limit = kind_unsigned(kind) ? umax[index]
                      : negative          ? imin[index]
                                          : imax[index];
  size_t n = strlen(text), m = strlen(limit);
  return n < m || (n == m && strcmp(text, limit) <= 0);
}

/* Exact comparison of nonnegative decimal literals. Narrow-float rounding
 * boundaries are dyadic rationals with at most 150 decimal fraction digits.
 * Comparing digits avoids double rounding for literals just beside a tie. */
typedef struct {
  char *digits;
  long exponent;
  size_t length;
} Decimal;
static Decimal decimal(const char *text) {
  Decimal d = {0};
  d.digits = mem(strlen(text) + 1);
  long fractional = 0;
  bool dot = false;
  const char *p = text;
  for (; *p && *p != 'e' && *p != 'E'; p++) {
    if (*p == '.')
      dot = true;
    else {
      d.digits[d.length++] = *p;
      if (dot)
        fractional++;
    }
  }
  long exponent = 0;
  if (*p) {
    p++;
    bool neg = *p == '-';
    if (*p == '+' || *p == '-')
      p++;
    for (; *p; p++) {
      if (exponent < 100000000)
        exponent = exponent * 10 + (*p - '0');
    }
    if (neg)
      exponent = -exponent;
  }
  d.exponent = exponent - fractional;
  size_t first = 0;
  while (first < d.length && d.digits[first] == '0')
    first++;
  memmove(d.digits, d.digits + first, d.length - first);
  d.length -= first;
  while (d.length && d.digits[d.length - 1] == '0') {
    d.length--;
    d.exponent++;
  }
  return d;
}
static int decimal_compare(Decimal a, double value) {
  char text[256];
  snprintf(text, sizeof(text), "%.160f", value);
  Decimal b = decimal(text);
  int cmp = 0;
  if (!a.length || !b.length)
    cmp = a.length ? 1 : b.length ? -1 : 0;
  else if ((long)a.length + a.exponent != (long)b.length + b.exponent)
    cmp = (long)a.length + a.exponent < (long)b.length + b.exponent ? -1 : 1;
  else {
    size_t n = a.length > b.length ? a.length : b.length;
    for (size_t j = 0; j < n && !cmp; j++) {
      int x = j < a.length ? a.digits[j] : '0';
      int y = j < b.length ? b.digits[j] : '0';
      cmp = x < y ? -1 : x > y ? 1 : 0;
    }
  }
  mem_free(b.digits);
  return cmp;
}
static double narrow_value(unsigned bits, int kind) {
  int mantissa = kind == T_F8 ? 3 : kind == T_F16 ? 10 : 23;
  unsigned mask = (1u << mantissa) - 1;
  int exponent = (int)(bits >> mantissa);
  int bias = kind == T_F8 ? 7 : kind == T_F16 ? 15 : 127;
  return exponent ? ldexp((double)((1u << mantissa) + (bits & mask)),
                          exponent - bias - mantissa)
                  : ldexp((double)(bits & mask), 1 - bias - mantissa);
}
char *float_constant(const char *text, int kind, Loc loc) {
  if (kind == T_F8 || kind == T_F16 || kind == T_F32) {
    bool negative = *text == '-';
    if (negative)
      text++;
    Decimal d = decimal(text);
    unsigned low = 0, high = kind == T_F8    ? 126
                             : kind == T_F16 ? 31743
                                             : 2139095039;
    unsigned maximum = high;
    /* First find the greatest representable value <= the exact literal. */
    while (low < high) {
      unsigned mid = (low + high + 1) / 2;
      if (decimal_compare(d, narrow_value(mid, kind)) >= 0)
        low = mid;
      else
        high = mid - 1;
    }
    double a = narrow_value(low, kind);
    double b = narrow_value(low + 1, kind);
    int cmp = decimal_compare(d, (a + b) * 0.5);
    unsigned bits = low + (cmp > 0 || (cmp == 0 && (low & 1)));
    mem_free(d.digits);
    if (bits > maximum)
      fail(loc, "floating literal does not fit '%s'", builtin_name(kind));
    if (negative)
      bits |= kind == T_F8    ? 128u
              : kind == T_F16 ? 32768u
                              : UINT32_C(0x80000000);
    if (kind == T_F32) {
      float single;
      memcpy(&single, &bits, sizeof(single));
      double value = single;
      uint64_t representation;
      memcpy(&representation, &value, sizeof(representation));
      return fmt("0x%016llX", (unsigned long long)representation);
    }
    return kind == T_F8 ? fmt("%u", bits) : fmt("0xH%04X", bits);
  }
  double value = strtod(text, NULL);
  if (!isfinite(value))
    fail(loc, "floating literal does not fit '%s'", builtin_name(kind));
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return fmt("0x%016llX", (unsigned long long)bits);
}
