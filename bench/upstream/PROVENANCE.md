# Upstream benchmark sources

Source: https://github.com/aardappel/goose

Pinned commit: `94cc64daeabcae6f072dc1fe2b864acda5a001bc`.

`cpp/bintrees.cpp`, `cpp/bench.h`, `rust/bintrees_box.rs`,
`rust/bintrees_arena.rs`, and `rust/bench.rs` are unmodified copies of files
under upstream `bench/`. The upstream Apache-2.0 license is in `LICENSE`.
The repository had no root NOTICE file at this revision.

Also copied unmodified from this same revision: `cpp/sum.cpp`, `cpp/blur.cpp`,
`cpp/particles.cpp`, `rust/sum.rs`, `rust/blur_index.rs`, `rust/blur_windows.rs`,
`rust/particles_scalar.rs`, and `rust/particles.rs`.

`../rhat/sum.rh`, `../rhat/blur.rh`, and `../rhat/particles.rh` are modified
translations of the corresponding upstream `bench/goose/` workloads.
Changes: R^ syntax, explicit numeric conversions and pool/width arguments,
index loops, flat byte buffers for blur and scalar AoS operations for particles.
RNG sequence, sizes, passes, border behavior and checksums are preserved.
R^ uses stable OS regions rather than vector reallocations or exact reserve.

Also copied unmodified from the pinned revision: `cpp/tree.cpp`, `cpp/graph.cpp`,
`rust/tree_box.rs`, `rust/tree_arena.rs`, `rust/graph_nested.rs`,
`rust/graph_csr.rs`, and `rust/graph_arena.rs`.

`../rhat/tree.rh` and `../rhat/graph.rh` are modified translations of upstream
`bench/goose/tree.goose` and `bench/goose/graph.goose`, using the fixed-size
index-pool representation also present in the C++/Rust arena controls.
The tree adds an index-zero null sentinel, excluded from its printed count.
The graph retains one head slot per vertex, degree eight and four BFS sources;
its queue overwrites an initialized prefix and tracks a separate active tail
instead of clear/push. It does not accumulate an unbounded queue between sources.
Links, nominal arguments, numeric conversions and loops use R^ syntax.
No original sizes, RNG outputs, tree shapes, distances or checksums are changed.

`cpp/push.cpp` and `rust/push.rs` are also unmodified copies from the pinned
revision. `../rhat/push.rh` is a modified translation of upstream
`bench/goose/push.goose`: R^ syntax, explicit numeric conversions, index loops
for final scans and `(&Item)[>..]` for the marks. The marks are actual Item
references returned by push and remain live across later growth; they are
not replaced by indices. Original sizes, every-64th marking, RNG outputs
and all four checksum lines are unchanged.

`../rhat/bintrees.rh` is a modified translation of upstream
`bench/goose/bintrees.goose`. Changes: R^ syntax, explicit pool arguments,
i32 indices instead of nullable relative references, an explicit index
counter, and repeated doubling instead of a shift for iteration counts.
No workload sizes, tree shapes, traversal counts or checksums are changed.

Upstream measurement policy: two warm-ups, best of three wall-clock times,
including the entire process lifecycle, and peak working set. The R^ runner
follows that policy and checks every run against an independent node-count
oracle. It does not run Goose or reuse published timing or memory data.

`cpp/lru.cpp`, `rust/lru_hashmap.rs` and `rust/lru_open.rs` are unmodified
copies from the same pinned revision. `../rhat/lru.rh` translates the workload
using the original C++ expert / Rust open-table index-arena representation:
16-byte nodes, 8-byte slots, backward-shift deletion and a pop-based free list.
The pool is bounded by capacity + two sentinels, with no unbounded append per
operation. Goose's pool-bound relative references are represented by u32 indices
without generation checks. Sizes, RNG, hash, recency, removals, eviction and all
six output lines are unchanged. A Python OrderedDict oracle independently checks
smoke runs and small-capacity integration cases.

`cpp/scene.cpp`, `rust/scene_box.rs` and `rust/scene_arena.rs` are unmodified
copies from the pinned revision. `../rhat/scene.rh` uses the original C++/Rust
120-byte arena node shape (fixed u32 child array plus u8 count). R^ syntax,
explicit pool parameters, index loops and explicit numeric conversions replace
the Goose interfaces. Build order, ids, RNG, original depths, f32 operations,
16 frames and both checksums are unchanged. A Python integer-affine oracle
independently checks smoke and boundary-depth cases. No Goose relative-reference
or 117-byte limited-array layout is claimed.

Unmodified text controls from the pinned revision: `cpp/strlist.cpp`,
`cpp/words.cpp`, `cpp/respond.cpp`, `rust/strlist_owned.rs`,
`rust/strlist_borrowed.rs`, `rust/words_hashmap.rs`, `rust/words_open.rs`,
`rust/respond_dto.rs` and `rust/respond_stream.rs`.

`../rhat/strlist.rh` and `../rhat/words.rh` translate the corresponding Goose
workloads using copied packed words (the original generated lengths fit a
one-byte varint). A WordList library struct owns a byte region. The vocabulary
uses 20 inline bytes and a u8 count; the word map uses borrowed slice keys and
FNV/linear probing. This does not implement general variable-size language array
elements. `../rhat/respond.rh` translates **respond_stream.goose**, preserving
its JSON bytes and RNG while reusing the output region like C++/Rust streaming.
It does not implement the separate DTO variant. All original sizes and checksum
lines are preserved. Python oracles use bytes.split, Counter and json.dumps.

Unmodified variant/parser controls from the pinned revision: `cpp/records.cpp`,
`cpp/interp.cpp`, `cpp/sexp.cpp`, `cpp/calc.cpp`, `rust/records.rs`,
`rust/interp_box.rs`, `rust/interp_arena.rs`, `rust/sexp_box.rs`,
`rust/sexp_arena.rs`, `rust/calc_box.rs` and `rust/calc_arena.rs`.

`../rhat/records.rh` translates **records_fixed.goose**, using a native nominal
union and fixed inline text. `../rhat/interp.rh` translates **interp.goose**
using native fixed-size nominal unions/u32 indices. `../rhat/sexp.rh` translates
**sexp.goose** with native union nodes, u32 child/sibling indices and copied
owned symbol bytes in a separate region, referenced by u32 offset/length.
`../rhat/calc.rh` translates **calc.goose** with native AST/result unions,
u32 child indices and retained regions cleared/truncated per input. Both parsers
explicitly return nominal errors instead of nonlocal return. All original RNG,
inputs, grammar/control flow, passes, sizes and output lines are preserved.
Both parsers retain i64 number fields. Relative-reference, variable-size ADT and nested
owned-string language interfaces are not claimed. Independent Python oracles use
semantic s-expression trees and a Pratt parser for arithmetic.
