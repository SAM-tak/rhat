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
