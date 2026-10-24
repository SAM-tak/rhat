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
int main(int argc, char **argv) {
  _Static_assert(sizeof(RhArray) == 32, "generated LLVM array ABI");
  _Static_assert(sizeof(RhSlice) == 16, "generated LLVM slice ABI");
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  SIZE_T page = info.dwPageSize;
  RhArray a, triples, large, empty;
  if (argc > 1 && !strcmp(argv[1], "page-failure"))
    fail_page_size = 1;
  rh_region_init(&a);
  assert(queries == 1 && reserves == 1 && commits == 0);
  assert(page_state(a.data) == MEM_RESERVE);
  unsigned char value = 37;
  if (argc > 1) {
    if (!strcmp(argv[1], "commit-failure"))
      fail_commit = 1;
    if (!strcmp(argv[1], "capacity"))
      a.len = a.capacity;
    if (!strcmp(argv[1], "truncate-growth")) {
      rh_array_truncate(&a, 1, 91);
      return 2;
    }
    if (!strcmp(argv[1], "append-capacity")) {
      RhSlice too_many = {&value, UINT64_MAX};
      rh_array_append(&a, &too_many, 1, 91);
      return 2;
    }
    rh_array_push(&a, &value, 1, 91);
    return 2; /* Every selected failure must terminate inside the runtime. */
  }
  /* A page of byte pushes commits exactly once, and the next page stays
   * reserved until its first byte is actually needed. */
  for (SIZE_T i = 0; i < page; i++)
    assert(rh_array_push(&a, &value, 1, 1) == a.data + i);
  assert(commits == 1 && queries == 1);
  assert(last_commit == a.data && last_size == page);
  assert(page_state(a.data) == MEM_COMMIT);
  assert(page_state(a.data + page) == MEM_RESERVE);
  unsigned char *first = rh_array_at(&a, 0, 1, 1);
  rh_array_push(&a, &value, 1, 1);
  assert(commits == 2 && last_commit == a.data + page && last_size == page);
  assert(first == a.data && *first == 37 && a.data[page] == 37);
  /* Logical shrinking keeps pages and initialized slots available for reuse. */
  unsigned char popped = 0;
  uint64_t water = a.high_water;
  unsigned saved_commits = commits;
  rh_array_pop(&a, &popped, 1, 1);
  assert(popped == 37 && a.len == page && a.high_water == water);
  assert(rh_array_push(&a, &value, 1, 1) == a.data + page);
  rh_array_clear(&a);
  assert(a.len == 0 && a.high_water == water && *first == 37);
  value = 99;
  assert(rh_array_push(&a, &value, 1, 1) == first && *first == 99);
  for (uint64_t j = 1; j < water; j++)
    rh_array_push(&a, &value, 1, 1);
  assert(commits == saved_commits);
  for (uint64_t j = water; j < page * 2 + 1; j++)
    rh_array_push(&a, &value, 1, 1);
  assert(commits == saved_commits + 1 && a.high_water == page * 2 + 1);

  /* Bulk copies retain stable slots, allow overlap, and commit a multi-page
   * extension in one call. An empty append does not touch memory or the OS. */
  RhSlice kept = {a.data, a.len}, zero = {a.data, 0};
  water = a.high_water;
  saved_commits = commits;
  rh_array_clear(&a);
  rh_array_append(&a, &zero, 1, 1);
  assert(a.len == 0 && commits == saved_commits);
  rh_array_append(&a, &kept, 1, 1);
  assert(a.len == kept.len && a.high_water == water && commits == saved_commits);
  rh_array_append(&a, &kept, 1, 1);
  assert(a.len == kept.len * 2 && a.high_water == water * 2);
  assert(commits == saved_commits + 1 && last_size == page * 2);
  assert(a.data[0] == 99 && a.data[a.len - 1] == 99);

  RhSlice rest = {a.data + 1, a.len - 1};
  water = a.high_water;
  saved_commits = commits;
  rh_array_truncate(&a, 1, 1);
  assert(a.len == 1 && a.high_water == water && *first == 99);
  rh_array_append(&a, &rest, 1, 1);
  assert(a.len == rest.len + 1 && a.high_water == water && commits == saved_commits);

  /* Three-byte elements cross a page partway through an element. */
  rh_region_init(&triples);
  unsigned before = commits;
  unsigned char triple[] = {11, 22, 33};
  assert(page % 3 != 0);
  for (SIZE_T i = 0; i < page / 3 + 1; i++)
    rh_array_push(&triples, triple, sizeof(triple), 2);
  assert(commits == before + 2);
  assert(last_commit == triples.data + page && last_size == page);
  assert(triples.data[0] == 11 && triples.data[triples.len * 3 - 1] == 33);

  /* A single element can need several pages; overlapping old pages must
   * not be committed again when the next large element is appended. */
  SIZE_T size = page * 2 + 3;
  unsigned char *source =
      VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  assert(source);
  memset(source, 0x5a, size);
  rh_region_init(&large);
  before = commits;
  rh_array_push(&large, source, size, 3);
  assert(commits == before + 1 && last_size == page * 3);
  rh_array_push(&large, source, size, 3);
  assert(commits == before + 2);
  assert(last_commit == large.data + page * 3 && last_size == page * 2);
  assert(large.data[0] == 0x5a && large.data[size * 2 - 1] == 0x5a);
  assert(VirtualFree(source, 0, MEM_RELEASE));

  rh_region_init(&empty);
  assert(queries == 1 && reserves == 4);
  rh_region_release(&empty);
  rh_region_release(&large);
  rh_region_release(&triples);
  rh_region_release(&a);
  assert(releases == 4 && a.data == NULL && a.len == 0 && a.capacity == 0 &&
         a.high_water == 0);
  assert(queries == 1);
  /* Cross the old 64 MiB limit through real pushes, retaining stable storage. */
  RhArray wide;
  unsigned char chunk[4096] = {42};
  rh_region_init(&wide);
  assert(wide.capacity == UINT64_C(1024) * 1024 * 1024);
  for (unsigned j = 0; j < 16385; j++)
    rh_array_push(&wide, chunk, sizeof(chunk), 4);
  assert(wide.len * sizeof(chunk) > UINT64_C(64) * 1024 * 1024);
  assert(wide.data[0] == 42 && wide.data[UINT64_C(64) * 1024 * 1024] == 42);
  assert(queries == 1);
  rh_region_release(&wide);
  puts("page commits verified");
  return 0;
}
