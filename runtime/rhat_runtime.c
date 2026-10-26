/* R^ runtime: OS-backed stable-address regions; no malloc/free or GC. */
#if !defined(_WIN32)
#define _DEFAULT_SOURCE
#endif
#include <inttypes.h>
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

/* Shared stack ABI. Array/view operations and descriptors live in LLVM IR. */
typedef struct {
  unsigned char *base, *top, *limit, *committed;
} RhStack;
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
#if defined(_WIN32)
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif
typedef struct {
  const char *data;
  int64_t length;
} RhString;
typedef struct {
  RhString *items;
  int64_t count;
} RhStrings;
/* The command arguments after the program name, as UTF-8 R^ strings that live
 * until the process exits. */
void rh_runtime_args(int argc, char **argv, RhStrings *out)
{
#if defined(_WIN32)
  (void)argv;
  wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wide) {
    rh_panic("cannot read command arguments", 0);
  }
#endif
  int64_t count = argc > 1 ? argc - 1 : 0;
  RhString *items = malloc((size_t)(count ? count : 1) * sizeof(*items));
  if (!items) {
    rh_panic("cannot store command arguments", 0);
  }
  for (int64_t j = 0; j < count; j++) {
#if defined(_WIN32)
    int bytes =
        WideCharToMultiByte(CP_UTF8, 0, wide[j + 1], -1, NULL, 0, NULL, NULL);
    char *text = bytes > 0 ? malloc((size_t)bytes) : NULL;
    if (!text || !WideCharToMultiByte(CP_UTF8, 0, wide[j + 1], -1, text,
                                      bytes, NULL, NULL)) {
      rh_panic("cannot convert command arguments", 0);
    }
#else
    const char *text = argv[j + 1];
#endif
    items[j] = (RhString){text, (int64_t)strlen(text)};
  }
#if defined(_WIN32)
  LocalFree(wide);
#endif
  *out = (RhStrings){items, count};
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
_Noreturn void rh_division_fail(int32_t remainder, int32_t line) {
  rh_panic(remainder ? "invalid integer remainder" : "invalid integer division",
           line);
}
_Noreturn void rh_index_fail(int32_t line) {
  rh_panic("array index out of bounds", line);
}
_Noreturn void rh_pop_fail(int32_t line) {
  rh_panic("pop from empty array", line);
}
_Noreturn void rh_truncate_fail(int32_t line) {
  rh_panic("truncate length out of bounds", line);
}
_Noreturn void rh_slice_fail(int32_t line) {
  rh_panic("slice range out of bounds", line);
}
_Noreturn void rh_assert_fail(int32_t line) {
  rh_panic("assertion failed", line);
}
_Noreturn void rh_float_cast_fail(int32_t line) {
  rh_panic("floating value out of integer range", line);
}
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
