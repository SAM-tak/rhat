# Goose-suite port coverage

Inspected revision: `94cc64daeabcae6f072dc1fe2b864acda5a001bc`.
The recorded benchmark run deliberately added no R^ language/runtime features.
The subsequent numeric-type work adds u64^, the other integer widths, f8^,
f16^ and f32^, and requires ^ on built-in type names. The table below is
updated for those types; the saved timings still describe the earlier
compiler/runtime recorded in results.json.

**1/16 workloads is ported.** `bintrees` is measured at all original sizes.
There is no suite-wide performance claim. Missing workloads are not assigned
estimated times or treated as failures of performance.

The other 15 workloads share an immediate blocker: their input generator is
the same xorshift64, using unsigned 64-bit wraparound, shifts and XOR. R^ now
has u64^ but still lacks the bitwise operators and numeric casts. Replacing the RNG,
moving generation outside timing, or calling a foreign C implementation
would change the comparison, so this port does none of those things.

| Workload | Original small / medium / large | Additional requirements / representation decisions |
|---|---|---|
| sum | N=1M / 16M / 128M | i64-to-i32 conversion; XOR in scan; the 512 MB large array exceeds current 64 MiB region limit |
| push | N=1M / 16M / 64M | Numeric conversions; to retain the reference interface, push must return a reference and grow arrays must accept rooted references; large storage exceeds region limit |
| strlist | N=200K / 2M / 8M | Owned variable-length inline strings in an array; split/append/slices; nested dynamic storage |
| records | N=500K / 4M / 16M | Tagged unions and dispatch, numeric casts, variable-size payloads, inline owned strings; fixed-union row could precede variable-size representation |
| tree | depth=16 / 20 / 24 | Numeric conversions and XOR; pool can initially use indices as bintrees does; original uses relative optional references; large pool exceeds limit |
| interp | depth=16 / 20 / 24 | Tagged unions and dispatch; original uses variable-size nodes, varint and relative references; indices/fixed nodes are a distinct representation |
| graph | V=100K / 500K / 2M | Numeric conversions; linked version needs null links/indices; CSR can use fixed-size scalar arrays once RNG/casts work; larger arrays may exceed region limit; check-free index types are a later optimization |
| words | N=500K / 4M / 16M | Byte slicing, string generation and comparison, hash-table implementation, bitwise operations and numeric casts; dictionary variant additionally needs library support |
| particles | N=100K / 1M / 4M | Numeric conversions; f32^ is now available; scalar-update version needs no vector arithmetic; large AoS storage exceeds region limit |
| sexp | N=20K / 100K / 300K | Byte text parsing, tagged unions, variable-size symbols, optional/relative links, error propagation (explicit errors could replace nonlocal return with a documented interface difference) |
| lru | N=2M / 8M / 32M | u32 conversions, reusable nodes and deletion; index-based variant can precede the relative-reference/pool version; do not replace eviction by unbounded append |
| scene | depth=12 / 14 / 16 | Numeric casts, fixed-capacity child lists, recursive pool mutation, numeric transforms; f32^ is now available; indices could replace child references |
| calc | N=100K / 400K / 1.6M | Byte parsing, shrinking/reusing input buffers, AST variants, error values/propagation, numeric conversions and unsigned evaluation |
| bintrees | depth=15 / 17 / 19 | **Ported:** 8-byte i32-index nodes, local OS region per tree. No compiler/runtime changes. Original nullable relative-reference interface is not claimed |
| respond | N=100K / 400K / 1.6M | Streaming row first: byte append/formatting, numeric casts, u64 FNV checksum; DTO row additionally needs nested owned strings/arrays |
| blur | W=1024 / 2048 / 8192 | Numeric widening/narrowing/shifts; flat byte-array variant can precede row slices/check-free contracts; W=8192 occupies exactly 64 MiB per buffer |

Priority after this phase: bitwise operations and explicit numeric
conversions unlock `sum`, then `push` with a documented reference-interface
choice, `particles` and `blur`. Increasing region
capacity and avoiding redundant page commits are separate runtime changes
that should be benchmarked as such, rather than silently folded into a port.
