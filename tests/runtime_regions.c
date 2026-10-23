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
  _Static_assert(sizeof(RhArray) == 24, "generated LLVM array ABI");
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
    rh_array_push(&a, &value, 1, 91);
    return 2; /* Every selected failure must terminate inside the runtime. */
  }
  /* A page of byte pushes commits exactly once, and the next page stays
   * reserved until its first byte is actually needed. */
  for (SIZE_T i = 0; i < page; i++)
    rh_array_push(&a, &value, 1, 1);
  assert(commits == 1 && queries == 1);
  assert(last_commit == a.data && last_size == page);
  assert(page_state(a.data) == MEM_COMMIT);
  assert(page_state(a.data + page) == MEM_RESERVE);
  unsigned char *first = rh_array_at(&a, 0, 1, 1);
  rh_array_push(&a, &value, 1, 1);
  assert(commits == 2 && last_commit == a.data + page && last_size == page);
  assert(first == a.data && *first == 37 && a.data[page] == 37);

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
  assert(releases == 4 && a.data == NULL && a.len == 0 && a.capacity == 0);
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
