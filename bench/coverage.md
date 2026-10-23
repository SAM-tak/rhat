# Goose-suite port coverage

Inspected revision: `94cc64daeabcae6f072dc1fe2b864acda5a001bc`.
The original bintrees measurement deliberately added no R^ language/runtime features.
The subsequent numeric-type work adds u64^, the other integer widths, f8^,
f16^ and f32^, and requires ^ on built-in type names. The table below is
updated for those types. Original timings remain in results.json; results.md
uses the newer R^ timings from page_commit_results.json alongside the
original C++/Rust rows, with the separate measurement sessions identified.
Windows page commits have also been reduced to newly required pages, with
the page size cached once per process; see page_commit_results.md for the
separate controlled comparison.

**7/16 workloads are ported:** `bintrees`, `sum`, `blur`, `particles`, `tree`, `graph`, `push`.
The new ports preserve the original RNG, sizes, passes and checksums. Their
comparisons are in workloads_results.md, tree_graph/workloads_results.md
and push/workloads_results.md,
with separate measurement sessions identified; historical bintrees
measurements remain in results.md. No suite-wide performance claim is made.

Implemented shared prerequisites: all-width integer bitwise operations
(`bitand^`, `bitor^`, `bitxor^`, `bitshift^`, `bitnot^`), explicit numeric
conversions (`i32^(value)` etc.) and 1 GiB virtual reservations per array.
This retains stable addresses and commits physical pages only as needed.

| Workload | Original small / medium / large | Additional requirements / representation decisions |
|---|---|---|
| sum | N=1M / 16M / 128M | **Ported:** i32 array, xorshift64 input, eight scans; 512 MB large input fits the 1 GiB reservation |
| push | N=1M / 16M / 64M | **Ported:** 8-byte Items and real references returned by push; every 64th pointer retained across growth in a reference-element array; original four checksums |
| strlist | N=200K / 2M / 8M | Owned variable-length inline strings in an array; split/append/slices; nested dynamic storage |
| records | N=500K / 4M / 16M | Tagged unions and dispatch, variable-size payloads, inline owned strings; fixed-union row could precede variable-size representation |
| tree | depth=16 / 20 / 24 | **Ported:** 12-byte nodes with u32 child indices, zero sentinel excluded from count, eight full traversals; relative references are not claimed |
| interp | depth=16 / 20 / 24 | Tagged unions and dispatch; original uses variable-size nodes, varint and relative references; indices/fixed nodes are a distinct representation |
| graph | V=100K / 500K / 2M | **Ported:** 8-byte linked index pool, degree eight, four BFS sources; queue overwrites initialized storage with a separate active tail; no check-free vertex types or relative-reference guarantees |
| words | N=500K / 4M / 16M | Byte slicing, string generation and comparison, hash-table implementation; dictionary variant additionally needs library support |
| particles | N=100K / 1M / 4M | **Ported:** scalar AoS, six f32 fields (24 bytes), 200 updates; no vector arithmetic substitution |
| sexp | N=20K / 100K / 300K | Byte text parsing, tagged unions, variable-size symbols, optional/relative links, error propagation (explicit errors could replace nonlocal return with a documented interface difference) |
| lru | N=2M / 8M / 32M | Reusable nodes and deletion; index-based variant can precede the relative-reference/pool version; do not replace eviction by unbounded append |
| scene | depth=12 / 14 / 16 | Fixed-capacity child lists, recursive pool mutation, numeric transforms; indices could replace child references |
| calc | N=100K / 400K / 1.6M | Byte parsing, shrinking/reusing input buffers, AST variants, error values/propagation and unsigned evaluation |
| bintrees | depth=15 / 17 / 19 | **Ported:** 8-byte i32-index nodes, local OS region per tree. No compiler/runtime changes. Original nullable relative-reference interface is not claimed |
| respond | N=100K / 400K / 1.6M | Streaming row first: byte append/formatting, u64 FNV checksum; DTO row additionally needs nested owned strings/arrays |
| blur | W=1024 / 2048 / 8192 | **Ported:** flat u8 buffers, 16 passes, original border behavior; row-slice/check-free contracts remain future optimizations |

Next steps:

1. Add safe reusable node/buffer APIs for `lru` / `calc`. The ported graph
   currently reuses initialized queue storage with its own active-prefix tail;
   a language-level clear operation is not required by that port.
2. Add owned byte/string views and operations for `words`, `strlist`, `respond`.
3. Add nominal tagged variants and matching for `records`, `interp`, `sexp`,
   and `calc`; then address nested/variable-size storage and explicit errors.

Reusing regions/shared data stacks and removing redundant bounds checks are
separate performance work. Redundant page commits have been addressed and
measured separately. The 1 GiB reservation is still a fixed implementation
limit, not an unbounded container.

Reference-returning push and fixed-size reference-bearing array elements are now
implemented. Stores obey owner/scope checks; references read from arrays retain
the container lifetime bound. Loop reference histories reach a fixed point before
IR is finalized. Borrowed-storage reference writes remain conservatively restricted.
