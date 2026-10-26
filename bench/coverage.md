# Goose-suite port coverage

Inspected revision: `94cc64daeabcae6f072dc1fe2b864acda5a001bc`.
The original bintrees measurement deliberately added no R^ language/runtime features.
The subsequent numeric-type work adds u64^, the other integer widths, f8^,
f16^ and f32^, and requires ^ on built-in type names. Original bintrees timings
remain in results.json, and the separate page-commit study in page_commit_results.json.
Current results.md summarizes the complete current suite from
current/workloads_results.json and current_bintrees/results.json, with matching
compiler/runtime fingerprints.
Windows page commits have also been reduced to newly required pages, with
the page size cached once per process; see page_commit_results.md for the
separate controlled comparison.

**16/16 workloads are ported:** `bintrees`, `sum`, `blur`, `particles`, `tree`, `graph`, `push`, `lru`, `scene`, `strlist`, `words`, `respond`, `records`, `interp`, `sexp`, `calc`.
The ports preserve the original RNG, sizes, passes and checksums for the selected
original variants. Current comparisons for all 16 are in current/workloads_results.md
and current_bintrees/results.md; results.md summarizes all 48 original sizes.
Previous per-stage sessions remain historical. Invalid pre-fix executable-collision
rows are archived and withdrawn. No suite-wide average is claimed.

Implemented shared prerequisites: all-width integer bitwise operations
(`bitand^`, `bitor^`, `bitxor^`, `bitshift^`, `bitnot^`), explicit numeric
conversions (`i32^(value)` etc.) and 1 GiB virtual reservations per array.
This retains stable addresses and commits physical pages only as needed.

| Workload | Original small / medium / large | Additional requirements / representation decisions |
|---|---|---|
| sum | N=1M / 16M / 128M | **Ported:** i32 array, xorshift64 input, eight scans; 512 MB large input fits the 1 GiB reservation |
| push | N=1M / 16M / 64M | **Ported:** 8-byte Items and real references returned by push; every 64th pointer retained across growth in a reference-element array; original four checksums |
| strlist | N=200K / 2M / 8M | **Ported:** copied packed owned words with one-byte varint lengths (original 3..20-byte words), four scans; implemented as a WordList library struct and owned byte region, not a general nested-owned array type |
| records | N=500K / 4M / 16M | **Ported (fixed mode):** native nominal Event union, 24-byte records, 15-byte inline text; four alternatives and four scans as records_fixed.goose; variable-mode ADT remains future work |
| tree | depth=16 / 20 / 24 | **Ported:** 12-byte nodes with u32 child indices, zero sentinel excluded from count, eight full traversals; relative references are not claimed |
| interp | depth=16 / 20 / 24 | **Ported:** native nominal Expr union, 12-byte u32-index arena, original tree/RNG and eight pass-dependent evaluations; variable-size nodes and relative references are not claimed |
| graph | V=100K / 500K / 2M | **Ported:** two-pass CSR matching C++ VARIANT 1 / Rust CSR, including source/destination arrays, counts, prefix sums and fill; degree eight, four BFS sources and queue reuse unchanged. Previous 8-byte linked pool retained in rhat/variants/graph_linked.rh |
| words | N=500K / 4M / 16M | **Ported:** 21-byte vocabulary words, copied packed word list, FNV open table with 24-byte borrowed-slice key/count slots; original text/skew/RNG and all checksums; built-in dictionary remains future work |
| particles | N=100K / 1M / 4M | **Ported:** scalar AoS, six f32 fields (24 bytes), 200 updates; no vector arithmetic substitution |
| sexp | N=20K / 100K / 300K | **Ported:** 24-byte nominal union nodes with u32 links, separate owned copied symbol-byte region and offset/length fields; real EOF/unclosed error propagation via a ParseResult union; original parse and four walks; explicit results replace nonlocal return |
| lru | N=2M / 8M / 32M | **Ported:** 16-byte nodes, bounded index arena/free list, open-addressed table with backward-shift deletion; original eviction, explicit removal and six checksums; no pool-bound relative-reference guarantees |
| scene | depth=12 / 14 / 16 | **Ported:** 120-byte nodes with four inline u32 child indices and u8 count, matching C++/Rust arena layout; original preorder generation, f32 transforms and 16 frames; no 117-byte limited-array/relative-reference layout |
| calc | N=100K / 400K / 1.6M | **Ported:** 24-byte nominal AST nodes with i64 numbers and u32 links; explicit ParseResult/EvalResult errors, clear/truncate reuse, original damaged inputs / precedence / wrapping u64 evaluation and eight checksum lines |
| bintrees | depth=15 / 17 / 19 | **Ported:** 8-byte i32-index nodes, shared data-stack rewind per local tree, no per-tree OS release; node count comes from pool length. Previous duplicate-counter port retained in rhat/variants/bintrees_counted.rh. Original nullable relative-reference interface is not claimed |
| respond | N=100K / 400K / 1.6M | **Ported (streaming):** original respond_stream workload with identical escaped JSON, integer formatting, reusable output region and FNV checksum; DTO-building variant / general nested ownership remain future work |
| blur | W=1024 / 2048 / 8192 | **Ported:** flat u8 buffers, 16 passes, original border behavior; canonical inner loops use range-checked fast paths with the original checked fallback |

All 16 original workload names now have R^ sources and independent smoke oracles.
Records selects the original fixed variant; respond selects the original streaming
variant. This is workload coverage, not a claim that every Goose variant or every
Goose storage/language feature is implemented. General variable-mode ADTs, nested
owned strings/arrays, pool-bound relative links and nonlocal return remain future
language work. Representation differences are recorded per workload.

Reusing regions/shared data stacks and removing redundant bounds checks are
separate performance work. Redundant page commits have been addressed and
measured separately. The 1 GiB reservation is still a fixed implementation
limit, not an unbounded container.

Reference-returning push and fixed-size reference-bearing array elements are now
implemented. Stores obey owner/scope checks; references read from arrays retain
the container lifetime bound. Loop reference histories reach a fixed point before
IR is finalized. Borrowed-storage reference writes remain conservatively restricted.

Array pop/clear preserve initialized slots and monotonically retained reference
histories. The current array descriptor is 24 bytes (data, length, stack pointer).
The compiler assigns reusable data stacks, reserves them at startup, and rewinds
scope watermarks without decommitting pages. Recursive cycles cannot retain
local owned regions across cycle calls; borrowed external pools remain supported.
Historical reports retain their original runtimes and descriptor sizes.

Read-only `T[:]` views, checked `a[lo :: hi]` slices, byte access on string literals
and bulk append are implemented. Strlist/words copy the split strings into owned
packed storage; these are not borrowed-word substitutions. Text measurements are
in their own session and do not refresh the historical runtime measurements.

Nominal sum types now use def^ Name = Alternative1 | Alternative2. LLVM stores an
inline tag/max-payload, with checked .is/.get operations and ordinary owner checks.
Both parsers retain their real error paths. Calc returns explicit nominal results
and counts partially built ASTs before discarding them. Truncate retains initialized
slots/committed pages and never grows an array.

Historical executable-name collisions invalidated R^ timing rows for sum,
particles, push and records. See invalid_runs/README.md for archived evidence.
Current reference reports use unique executable paths and per-run SHA-256 checks;
corrected measurements replace the affected performance claims.

The complete current reference uses current/workloads_results.md and
current_bintrees/results.md; the root results.md summarizes all 16 with matching
compiler/runtime fingerprints. Older per-stage sessions remain historical.
