/* R^ runtime: OS-backed stable-address regions; no malloc/free or GC. */
#if !defined(_WIN32)
#define _DEFAULT_SOURCE
#endif
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

typedef struct {
  unsigned char *data;
  uint64_t len, capacity, high_water;
} RhArray;
typedef struct {
  const unsigned char *data;
  uint64_t len;
} RhSlice;
#define RH_REGION_BYTES (UINT64_C(1024) * 1024 * 1024)
static _Noreturn void rh_panic(const char *why, int32_t line) {
  fprintf(stderr, "R^ runtime error at line %" PRId32 ": %s\n", line, why);
  exit(1);
}
#if defined(_WIN32)
static INIT_ONCE rh_page_once = INIT_ONCE_STATIC_INIT;
static uint64_t rh_page_bytes;
static BOOL CALLBACK rh_init_page_size(PINIT_ONCE once, PVOID parameter,
                                       PVOID *context) {
  (void)once;
  (void)parameter;
  (void)context;
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  rh_page_bytes = info.dwPageSize;
  return rh_page_bytes != 0;
}
#endif
void rh_region_init(RhArray *a) {
#if defined(_WIN32)
  if (!InitOnceExecuteOnce(&rh_page_once, rh_init_page_size, NULL, NULL))
    rh_panic("page size query failed", 0);
  a->data =
      VirtualAlloc(NULL, (SIZE_T)RH_REGION_BYTES, MEM_RESERVE, PAGE_READWRITE);
  if (!a->data)
    rh_panic("virtual address reservation failed", 0);
#else
  void *p = mmap(NULL, (size_t)RH_REGION_BYTES, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    rh_panic("virtual address reservation failed", 0);
  a->data = p;
#endif
  a->len = 0;
  a->capacity = RH_REGION_BYTES;
  a->high_water = 0;
}
void rh_region_release(RhArray *a) {
#if defined(_WIN32)
  if (a->data && !VirtualFree(a->data, 0, MEM_RELEASE))
    rh_panic("region release failed", 0);
#else
  if (a->data && munmap(a->data, (size_t)a->capacity))
    rh_panic("region release failed", 0);
#endif
  a->data = NULL;
  a->len = a->capacity = a->high_water = 0;
}
void rh_bounds(int64_t index, uint64_t length, int32_t line) {
  if (index < 0 || (uint64_t)index >= length)
    rh_panic("array index out of bounds", line);
}
void *rh_array_at(RhArray *a, int64_t index, uint64_t size, int32_t line) {
  rh_bounds(index, a->len, line);
  return a->data + (uint64_t)index * size;
}
static void *rh_array_write(RhArray *a, const void *value, uint64_t count,
                            uint64_t size, int32_t line) {
  if (!size || size > a->capacity || a->len > a->capacity / size ||
      count > a->capacity / size - a->len)
    rh_panic("region capacity exceeded", line);
  uint64_t off = a->len * size, end_bytes = off + count * size;
  if (!count)
    return a->data + off;
#if defined(_WIN32)
  uint64_t page = rh_page_bytes;
  /* Pop/clear retain initialized storage and committed pages. */
  uint64_t begin = (a->high_water + page - 1) / page * page,
           end = (end_bytes + page - 1) / page * page;
  if (end > begin && !VirtualAlloc(a->data + begin, (SIZE_T)(end - begin),
                                   MEM_COMMIT, PAGE_READWRITE))
    rh_panic("region commit failed", line);
#endif
  memmove(a->data + off, value, (size_t)(count * size));
  a->len += count;
  if (end_bytes > a->high_water)
    a->high_water = end_bytes;
  return a->data + off;
}
void *rh_array_push(RhArray *a, const void *value, uint64_t size, int32_t line) {
  return rh_array_write(a, value, 1, size, line);
}
void rh_array_append(RhArray *a, const RhSlice *values, uint64_t size, int32_t line) {
  rh_array_write(a, values->data, values->len, size, line);
}
void rh_array_pop(RhArray *a, void *out, uint64_t size, int32_t line) {
  if (!a->len)
    rh_panic("pop from empty array", line);
  memmove(out, a->data + (a->len - 1) * size, (size_t)size);
  a->len--;
}
void rh_array_clear(RhArray *a) {
  /* Slots and their values remain alive until region release. Existing
   * references can observe a later push overwriting the same slot. */
  a->len = 0;
}
void rh_array_truncate(RhArray *a, int64_t length, int32_t line) {
  if (length < 0 || (uint64_t)length > a->len)
    rh_panic("truncate length out of bounds", line);
  a->len = (uint64_t)length;
}
void rh_make_slice(RhSlice *out, const void *data, uint64_t len,
                   int64_t start, int64_t count, uint64_t size, int32_t line) {
  if (start < 0 || count < 0 || (uint64_t)start > len || (uint64_t)count > len - (uint64_t)start)
    rh_panic("slice range out of bounds", line);
  out->data = (const unsigned char *)data + (uint64_t)start * size;
  out->len = (uint64_t)count;
}
const void *rh_view_at(const RhSlice *view, int64_t index, uint64_t size, int32_t line) {
  rh_bounds(index, view->len, line);
  return view->data + (uint64_t)index * size;
}
void rh_union_tag(int32_t actual, int32_t wanted, int32_t line) {
  if (actual != wanted)
    rh_panic("union alternative mismatch", line);
}
void rh_assert(int64_t condition, int32_t line) {
  if (!condition)
    rh_panic("assertion failed", line);
}
void rh_print_i64(int64_t n) { printf("%" PRId64, n); }
void rh_print_f64(double n) { printf("%.17g", n); }
void rh_print_bool(int64_t b) { fputs(b ? "true" : "false", stdout); }
void rh_print_string(const char *s, uint64_t n) {
  fwrite(s, 1, (size_t)n, stdout);
}
void rh_print_space(void) { fputc(' ', stdout); }
void rh_print_newline(void) { fputc('\n', stdout); }
/* Byte-oriented integer helpers keep the C/LLVM ABI identical for i128 and
 * narrow integers, and do not require a C compiler-specific 128-bit ABI. */
void rh_print_integer(const void *value, int32_t bits, int32_t uns) {
  unsigned char bytes[16] = {0};
  int n = bits / 8;
  memcpy(bytes, value, (size_t)n);
  if (!uns && (bytes[n - 1] & 128)) {
    fputc('-', stdout);
    unsigned carry = 1;
    for (int j = 0; j < n; j++) {
      unsigned v = (unsigned)(bytes[j] ^ 255u) + carry;
      bytes[j] = (unsigned char)v;
      carry = v >> 8;
    }
  }
  char digits[40];
  int count = 0;
  bool nonzero;
  do {
    unsigned remainder = 0;
    nonzero = false;
    for (int j = n - 1; j >= 0; j--) {
      unsigned v = remainder * 256 + bytes[j];
      bytes[j] = (unsigned char)(v / 10);
      remainder = v % 10;
      nonzero = nonzero || bytes[j] != 0;
    }
    digits[count++] = (char)('0' + remainder);
  } while (nonzero);
  while (count)
    fputc(digits[--count], stdout);
}
void rh_check_division(const void *lhs, const void *rhs, int32_t bits,
                       int32_t uns, int32_t remainder, int32_t line) {
  const unsigned char *a = lhs, *b = rhs;
  bool zero = true, minus_one = true, minimum = true;
  int n = bits / 8;
  for (int j = 0; j < n; j++) {
    zero = zero && b[j] == 0;
    minus_one = minus_one && b[j] == 255;
    minimum = minimum && a[j] == (j == n - 1 ? 128 : 0);
  }
  if (zero || (!uns && minimum && minus_one))
    rh_panic(remainder ? "invalid integer remainder"
                       : "invalid integer division",
             line);
}
int64_t rh_index_integer(const void *value, int32_t bits, int32_t uns,
                         int32_t line) {
  const unsigned char *bytes = value;
  int n = bits / 8;
  if (!uns && (bytes[n - 1] & 128))
    rh_panic("array index out of bounds", line);
  uint64_t result = 0;
  for (int j = 0; j < n; j++) {
    if (j >= 8) {
      if (bytes[j])
        rh_panic("array index out of bounds", line);
    } else
      result |= (uint64_t)bytes[j] << (j * 8);
  }
  if (result > INT64_MAX)
    rh_panic("array index out of bounds", line);
  return (int64_t)result;
}
float rh_f8_decode(uint8_t value) {
  uint32_t sign = (uint32_t)(value & 128u) << 24;
  unsigned exponent = (value >> 3) & 15u, fraction = value & 7u;
  uint32_t bits = sign;
  if (exponent == 15 && fraction == 7)
    bits |= UINT32_C(0x7fc00000);
  else if (exponent)
    bits |= ((uint32_t)(exponent + 120) << 23) | (fraction << 20);
  else if (fraction) {
    unsigned leading = fraction >= 4 ? 2u : fraction >= 2 ? 1u : 0u;
    bits |= ((leading + 118) << 23) |
            ((fraction - (1u << leading)) << (23 - leading));
  }
  float result;
  memcpy(&result, &bits, sizeof(result));
  return result;
}
uint8_t rh_f8_encode(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  unsigned sign = (bits >> 24) & 128u, exponent = (bits >> 23) & 255u;
  if (exponent == 255)
    return (uint8_t)(sign | 127u);
  if (!exponent)
    return (uint8_t)sign;
  int e = (int)exponent - 127;
  unsigned shift = e < -6 ? (unsigned)(14 - e) : 20u;
  if (shift >= 32)
    return (uint8_t)sign;
  uint32_t significand = (bits & UINT32_C(0x7fffff)) | UINT32_C(0x800000);
  unsigned q = significand >> shift;
  unsigned rest = significand & ((UINT32_C(1) << shift) - 1);
  unsigned halfway = UINT32_C(1) << (shift - 1);
  q += rest > halfway || (rest == halfway && (q & 1));
  unsigned encoded;
  if (e < -6)
    encoded = q;
  else {
    if (q == 16) {
      q = 8;
      e++;
    }
    if (e > 8)
      return (uint8_t)(sign | 127u);
    encoded = ((unsigned)(e + 7) << 3) | (q - 8);
  }
  return (uint8_t)(sign | (encoded > 126 ? 127u : encoded));
}
/* Convert binary64 directly: passing through float would double-round values
 * just to either side of an E4M3FN midpoint. */
uint8_t rh_f8_encode_f64(double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  unsigned sign = (unsigned)((bits >> 56) & 128u);
  unsigned exponent = (unsigned)((bits >> 52) & 2047u);
  if (exponent == 2047)
    return (uint8_t)(sign | 127u);
  if (!exponent)
    return (uint8_t)sign;
  int e = (int)exponent - 1023;
  if (e > 8)
    return (uint8_t)(sign | 127u);
  if (e < -10)
    return (uint8_t)sign;
  unsigned shift = e < -6 ? (unsigned)(43 - e) : 49u;
  uint64_t significand = (bits & UINT64_C(0xfffffffffffff)) | (UINT64_C(1) << 52);
  unsigned q = (unsigned)(significand >> shift);
  uint64_t rest = significand & ((UINT64_C(1) << shift) - 1);
  uint64_t halfway = UINT64_C(1) << (shift - 1);
  q += rest > halfway || (rest == halfway && (q & 1));
  unsigned encoded;
  if (e < -6)
    encoded = q;
  else {
    if (q == 16) {
      q = 8;
      e++;
    }
    encoded = e > 8 ? 127u : ((unsigned)(e + 7) << 3) | (q - 8);
  }
  return (uint8_t)(sign | (encoded > 126 ? 127u : encoded));
}
void rh_check_float_cast(double value, int32_t bits, int32_t uns, int32_t line) {
  double upper = 1.0;
  for (int32_t j = 0; j < bits - (uns ? 0 : 1); j++)
    upper *= 2.0;
  double lower = uns ? 0.0 : -upper;
  /* Ordered comparisons also reject NaNs and infinities before LLVM's
   * non-saturating conversion could produce poison. */
  if (!(value >= lower && value < upper))
    rh_panic("floating value out of integer range", line);
}
#define RH_DIVISION(T, NAME, MINIMUM)                                          \
  T rh_div_##NAME(T a, T b, int32_t line) {                                    \
    if (!b || (a == (MINIMUM) && b == (T) - 1))                                \
      rh_panic("invalid integer division", line);                              \
    return (T)(a / b);                                                         \
  }                                                                            \
  T rh_rem_##NAME(T a, T b, int32_t line) {                                    \
    if (!b || (a == (MINIMUM) && b == (T) - 1))                                \
      rh_panic("invalid integer remainder", line);                             \
    return (T)(a % b);                                                         \
  }
RH_DIVISION(int32_t, i32, INT32_MIN)
RH_DIVISION(int64_t, i64, INT64_MIN)
uint8_t rh_div_u8(uint8_t a, uint8_t b, int32_t line) {
  if (!b)
    rh_panic("division by zero", line);
  return (uint8_t)(a / b);
}
uint8_t rh_rem_u8(uint8_t a, uint8_t b, int32_t line) {
  if (!b)
    rh_panic("remainder by zero", line);
  return (uint8_t)(a % b);
}
