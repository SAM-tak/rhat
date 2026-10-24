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

/* Shared with emitted LLVM: four pointers, followed by a three-word array. */
typedef struct {
  unsigned char *base, *top, *limit, *committed;
} RhStack;
typedef struct {
  unsigned char *data;
  uint64_t len;
  RhStack *stack;
} RhArray;
typedef struct {
  const unsigned char *data;
  uint64_t len;
} RhSlice;
#ifndef RH_STACK_BYTES
#define RH_STACK_BYTES (UINT64_C(1024) * 1024 * 1024)
#endif
#define RH_GUARD_BYTES (UINT64_C(1024) * 1024)
#define RH_COMMIT_BYTES (UINT64_C(1024) * 1024)
static RhStack *rh_stacks;
static uint64_t rh_stack_count;
static _Noreturn void rh_panic(const char *why, int32_t line) {
  fprintf(stderr, "R^ runtime error at line %" PRId32 ": %s\n", line, why);
  exit(1);
}
#if defined(_WIN32)
static PVOID rh_fault_handler;
static uint64_t rh_page_bytes;
static _Noreturn void rh_fault_die(const char *message, DWORD length) {
  DWORD written;
  WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, length, &written, NULL);
  ExitProcess(1);
}
static LONG WINAPI rh_fault(EXCEPTION_POINTERS *ep) {
  EXCEPTION_RECORD *e = ep->ExceptionRecord;
  if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      e->NumberParameters < 2 ||
      (e->ExceptionInformation[0] != 0 && e->ExceptionInformation[0] != 1))
    return EXCEPTION_CONTINUE_SEARCH;
  uintptr_t hit = (uintptr_t)e->ExceptionInformation[1];
  for (uint64_t j = 0; j < rh_stack_count; j++) {
    RhStack *s = &rh_stacks[j];
    if (!s->base)
      continue;
    uintptr_t base = (uintptr_t)s->base;
    if (hit < base || hit - base >= RH_STACK_BYTES + RH_GUARD_BYTES)
      continue;
    if (hit - base >= RH_STACK_BYTES) {
      static const char msg[] = "R^ runtime error: data stack overflow\n";
      rh_fault_die(msg, sizeof(msg) - 1);
    }
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery((void *)hit, &info, sizeof(info)) ||
        info.State != MEM_RESERVE)
      return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t offset = (hit - base) / RH_COMMIT_BYTES * RH_COMMIT_BYTES;
    uint64_t bytes = RH_COMMIT_BYTES;
    if (bytes > RH_STACK_BYTES - offset)
      bytes = RH_STACK_BYTES - offset;
    unsigned char *page = s->base + offset;
    if (!VirtualAlloc(page, (SIZE_T)bytes, MEM_COMMIT, PAGE_READWRITE)) {
      static const char msg[] = "R^ runtime error: data stack commit failed\n";
      rh_fault_die(msg, sizeof(msg) - 1);
    }
    if ((uintptr_t)s->committed < (uintptr_t)page + bytes)
      s->committed = page + bytes;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
void rh_runtime_init(RhStack *stacks, uint64_t count) {
  rh_stacks = stacks;
  rh_stack_count = count;
#if defined(_WIN32)
  if (count) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    rh_page_bytes = info.dwPageSize;
    if (!rh_page_bytes)
      rh_panic("page size query failed", 0);
    rh_fault_handler = AddVectoredExceptionHandler(1, rh_fault);
    if (!rh_fault_handler)
      rh_panic("cannot install data stack fault handler", 0);
  }
#endif
  for (uint64_t j = 0; j < count; j++) {
    RhStack *s = &stacks[j];
#if defined(_WIN32)
    s->base = VirtualAlloc(NULL, (SIZE_T)(RH_STACK_BYTES + RH_GUARD_BYTES),
                           MEM_RESERVE, PAGE_READWRITE);
    if (!s->base)
      rh_panic("data stack reservation failed", 0);
#else
    void *p = mmap(NULL, (size_t)(RH_STACK_BYTES + RH_GUARD_BYTES), PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
      rh_panic("data stack reservation failed", 0);
    s->base = p;
    if (mprotect(p, (size_t)RH_STACK_BYTES, PROT_READ | PROT_WRITE))
      rh_panic("cannot enable data stack pages", 0);
#endif
    s->top = s->committed = s->base;
    s->limit = s->base + RH_STACK_BYTES;
  }
}
void rh_runtime_shutdown(void) {
  for (uint64_t j = 0; j < rh_stack_count; j++) {
#if defined(_WIN32)
    if (!VirtualFree(rh_stacks[j].base, 0, MEM_RELEASE))
      rh_panic("data stack release failed", 0);
#else
    if (munmap(rh_stacks[j].base, (size_t)(RH_STACK_BYTES + RH_GUARD_BYTES)))
      rh_panic("data stack release failed", 0);
#endif
    rh_stacks[j].base = NULL;
  }
  rh_stack_count = 0;
#if defined(_WIN32)
  if (rh_fault_handler)
    RemoveVectoredExceptionHandler(rh_fault_handler);
  rh_fault_handler = NULL;
#endif
  rh_stacks = NULL;
}
_Noreturn void rh_stack_overflow(int32_t line) {
  rh_panic("data stack overflow", line);
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
  uint64_t available = (uint64_t)(a->stack->limit - a->data);
  if (!size || a->len > available / size || count > available / size - a->len)
    rh_stack_overflow(line);
  uint64_t off = a->len * size, end_bytes = off + count * size;
  if (count)
    memmove(a->data + off, value, (size_t)(count * size));
  a->len += count;
  if (a->data + end_bytes > a->stack->top)
    a->stack->top = a->data + end_bytes;
  return a->data + off;
}
void rh_array_append(RhArray *a, const RhSlice *values, uint64_t size,
                     int32_t line) {
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
void rh_make_slice(RhSlice *out, const void *data, uint64_t len, int64_t start,
                   int64_t count, uint64_t size, int32_t line) {
  if (start < 0 || count < 0 || (uint64_t)start > len ||
      (uint64_t)count > len - (uint64_t)start)
    rh_panic("slice range out of bounds", line);
  out->data = (const unsigned char *)data + (uint64_t)start * size;
  out->len = (uint64_t)count;
}
const void *rh_view_at(const RhSlice *view, int64_t index, uint64_t size,
                       int32_t line) {
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
  uint64_t significand =
      (bits & UINT64_C(0xfffffffffffff)) | (UINT64_C(1) << 52);
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
void rh_check_float_cast(double value, int32_t bits, int32_t uns,
                         int32_t line) {
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
