/* Count real Windows calls without adding instrumentation to the runtime. */
#define WIN32_LEAN_AND_MEAN
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static unsigned queries, reserves, commits, releases;
static void *last_commit;
static SIZE_T last_size;
static int fail_commit, fail_page_size;
static LPVOID WINAPI counted_alloc(LPVOID address, SIZE_T size, DWORD kind,
                                   DWORD protection);
static BOOL WINAPI counted_free(LPVOID address, SIZE_T size, DWORD kind);
static void WINAPI counted_system_info(LPSYSTEM_INFO info);
#define VirtualAlloc counted_alloc
#define VirtualFree counted_free
#define GetSystemInfo counted_system_info
#include "../runtime/rhat_runtime.c"
#undef VirtualAlloc
#undef VirtualFree
#undef GetSystemInfo

static LPVOID WINAPI counted_alloc(LPVOID address, SIZE_T size, DWORD kind,
                                   DWORD protection) {
  if (kind & MEM_RESERVE)
    reserves++;
  if (kind & MEM_COMMIT) {
    commits++;
    last_commit = address;
    last_size = size;
    if (fail_commit) {
      SetLastError(ERROR_COMMITMENT_LIMIT);
      return NULL;
    }
  }
  return VirtualAlloc(address, size, kind, protection);
}
static BOOL WINAPI counted_free(LPVOID address, SIZE_T size, DWORD kind) {
  releases++;
  return VirtualFree(address, size, kind);
}
static void WINAPI counted_system_info(LPSYSTEM_INFO info) {
  queries++;
  GetSystemInfo(info);
  if (fail_page_size)
    info->dwPageSize = 0;
}
static DWORD page_state(const void *address) {
  MEMORY_BASIC_INFORMATION info;
  assert(VirtualQuery(address, &info, sizeof(info)) == sizeof(info));
  return info.State;
}
static void push(RhArray *a, const void *v, uint64_t size) {
  rh_array_write(a, v, 1, size, 91);
}
int main(int argc, char **argv) {
  _Static_assert(sizeof(RhArray) == 24, "generated LLVM array ABI");
  _Static_assert(sizeof(RhStack) == 32, "generated LLVM stack ABI");
  _Static_assert(sizeof(RhSlice) == 16, "generated LLVM slice ABI");
  if (argc > 1 && !strcmp(argv[1], "page-failure"))
    fail_page_size = 1;
  RhStack stacks[2] = {0};
  rh_runtime_init(stacks, 2);
  assert(queries == 1 && reserves == 2 && commits == 0);
  RhStack *s = &stacks[0];
  RhArray a = {s->top, 0, s};
  unsigned char value = 37;
  assert(page_state(a.data) == MEM_RESERVE);
  if (argc > 1) {
    if (!strcmp(argv[1], "commit-failure"))
      fail_commit = 1;
    if (!strcmp(argv[1], "small-limit")) {
      *(volatile unsigned char *)(s->limit - 1) = value;
      assert(last_size == RH_STACK_BYTES && s->committed == s->limit);
      assert(page_state(s->limit) == MEM_RESERVE);
      rh_runtime_shutdown();
      puts("small reservation verified");
      return 0;
    }
    if (!strcmp(argv[1], "element-capacity")) {
      push(&a, &value, UINT64_MAX);
      return 2;
    }
    if (!strcmp(argv[1], "guard")) {
      *(volatile unsigned char *)s->limit = 1;
      return 2;
    }
    if (!strcmp(argv[1], "capacity"))
      a.len = RH_STACK_BYTES;
    if (!strcmp(argv[1], "truncate-growth")) {
      rh_array_truncate(&a, 1, 91);
      return 2;
    }
    if (!strcmp(argv[1], "append-capacity")) {
      RhSlice too_many = {&value, UINT64_MAX};
      rh_array_append(&a, &too_many, 1, 91);
      return 2;
    }
    push(&a, &value, 1);
    return 2;
  }
  /* The fault commits a chunk, not one page per push. */
  for (uint64_t i = 0; i < RH_COMMIT_BYTES; i++)
    push(&a, &value, 1);
  assert(commits == 1 && last_commit == a.data && last_size == RH_COMMIT_BYTES);
  assert(s->top == a.data + RH_COMMIT_BYTES);
  assert(page_state(a.data + RH_COMMIT_BYTES) == MEM_RESERVE);
  push(&a, &value, 1);
  assert(commits == 2);
  unsigned char *first = rh_array_at(&a, 0, 1, 1);
  unsigned char out;
  rh_array_pop(&a, &out, 1, 1);
  assert(out == value);
  unsigned char *high = s->top;
  rh_array_clear(&a);
  assert(a.len == 0 && s->top == high && *first == value);
  value = 42;
  push(&a, &value, 1);
  assert(*first == 42 && s->top == high);
  /* End/re-enter a scope: the owner descriptor is new; reservation and
   * committed pages are reused, and the other active stack is untouched. */
  unsigned saved = commits;
  for (unsigned repeat = 0; repeat < 1000; repeat++) {
    s->top = s->base;
    a = (RhArray){s->top, 0, s};
    push(&a, &value, 1);
    assert(a.data == first);
  }
  assert(reserves == 2 && releases == 0 && commits == saved);
  assert(stacks[1].top == stacks[1].base);
  /* Bulk overlap and zero-count append keep their previous semantics. */
  RhSlice view = {a.data, 1};
  rh_array_append(&a, &view, 1, 1);
  assert(a.len == 2 && a.data[1] == value);
  RhSlice empty = {NULL, 0};
  rh_array_append(&a, &empty, 1, 1);
  assert(commits == saved);
  static unsigned char bulk[2 * RH_COMMIT_BYTES + 3];
  RhArray second = {stacks[1].top, 0, &stacks[1]};
  RhSlice many = {bulk, sizeof(bulk)};
  rh_array_append(&second, &many, 1, 1);
  assert(second.len == sizeof(bulk));
  assert(commits == saved + 3);
  assert(stacks[1].top == second.data + sizeof(bulk));
  assert(second.data[sizeof(bulk) - 1] == 0);
  /* Unrelated faults, execute faults and faults on already committed pages
   * are never swallowed by our handler. */
  EXCEPTION_RECORD record = {0};
  CONTEXT context = {0};
  EXCEPTION_POINTERS ep = {&record, &context};
  record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
  record.NumberParameters = 2;
  record.ExceptionInformation[0] = 1;
  record.ExceptionInformation[1] = (ULONG_PTR)first;
  assert(rh_fault(&ep) == EXCEPTION_CONTINUE_SEARCH);
  record.ExceptionInformation[1] = 0;
  assert(rh_fault(&ep) == EXCEPTION_CONTINUE_SEARCH);
  record.ExceptionInformation[0] = 8;
  record.ExceptionInformation[1] = (ULONG_PTR)stacks[1].base;
  assert(rh_fault(&ep) == EXCEPTION_CONTINUE_SEARCH);
  rh_runtime_shutdown();
  assert(releases == 2);
  puts("data stacks verified");
  return 0;
}
