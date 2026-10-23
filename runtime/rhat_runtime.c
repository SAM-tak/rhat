/* R^ runtime: OS-backed stable-address regions; no malloc/free or GC. */
#if !defined(_WIN32)
#define _DEFAULT_SOURCE
#endif
#include <inttypes.h>
#include <limits.h>
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
  uint64_t len, capacity;
} RhArray;
#define RH_REGION_BYTES (UINT64_C(64) * 1024 * 1024)
static _Noreturn void rh_panic(const char *why, int32_t line) {
  fprintf(stderr, "R^ runtime error at line %" PRId32 ": %s\n", line, why);
  exit(1);
}
void rh_region_init(RhArray *a) {
#if defined(_WIN32)
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
  a->len = a->capacity = 0;
}
void rh_bounds(int64_t index, uint64_t length, int32_t line) {
  if (index < 0 || (uint64_t)index >= length)
    rh_panic("array index out of bounds", line);
}
void *rh_array_at(RhArray *a, int64_t index, uint64_t size, int32_t line) {
  rh_bounds(index, a->len, line);
  return a->data + (uint64_t)index * size;
}
void rh_array_push(RhArray *a, const void *value, uint64_t size, int32_t line) {
  if (!size || size > a->capacity || a->len >= a->capacity / size)
    rh_panic("region capacity exceeded", line);
  uint64_t off = a->len * size;
#if defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  uint64_t page = info.dwPageSize;
  uint64_t begin = off / page * page,
           end = (off + size + page - 1) / page * page;
  if (!VirtualAlloc(a->data + begin, (SIZE_T)(end - begin), MEM_COMMIT,
                    PAGE_READWRITE))
    rh_panic("region commit failed", line);
#endif
  memcpy(a->data + off, value, (size_t)size);
  a->len++;
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
