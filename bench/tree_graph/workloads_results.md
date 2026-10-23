# Additional R^ workloads

Measured: 2026-10-23T16:57:28.913087+00:00 (UTC).

R^ replaces Goose. All comparison implementations were built and measured in this session.

Whole-process wall time, two warm-ups, best of 3 timed samples; peak resident working set.
R^ retains bounds/range checks and OS-backed, stable-address arrays. No affinity is set.

Comparison / R^ time: above 1 means R^ is faster. No suite-wide average is claimed.

Smoke sizes are correctness checks and are not performance evidence.

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | 3d7517f124c88d67ec4796bfcad2f2921b4feac7 |
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/upstream/PROVENANCE.md;  M tests/run.py; ?? bench/rhat/graph.rh; ?? bench/rhat/tree.rh; ?? bench/upstream/cpp/graph.cpp; ?? bench/upstream/cpp/tree.cpp; ?? bench/upstream/rust/graph_arena.rs; ?? bench/upstream/rust/graph_csr.rs; ?? bench/upstream/rust/graph_nested.rs; ?? bench/upstream/rust/tree_arena.rs; ?? bench/upstream/rust/tree_box.rs |
| compiler_sha256 | 683d4d4b34bba0f40134a4496f47b05592760c73144cd9d81c4e1c1971313266 |
| runtime_sha256 | 912a91e011f8eb34438f683fef28ce77d9f3ac1dc95b954dada5ed5c163abf9e |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## tree

R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ u32-index node pool (LLVM/Clang) | 32.23 | 5.28 | 1.000x |
| small / 16 | C++ unique_ptr (msvc) | 32.87 | 7.82 | 1.020x |
| small / 16 | C++ new/delete (msvc) | 28.52 | 7.79 | 0.885x |
| small / 16 | C++ index arena (msvc) | 19.84 | 5.27 | 0.616x |
| small / 16 | C++ unique_ptr (clangcl) | 29.16 | 7.82 | 0.905x |
| small / 16 | C++ new/delete (clangcl) | 27.63 | 7.79 | 0.857x |
| small / 16 | C++ index arena (clangcl) | 20.62 | 5.28 | 0.640x |
| small / 16 | Rust tree_box | 29.71 | 8.25 | 0.922x |
| small / 16 | Rust tree_arena | 21.77 | 5.72 | 0.675x |
| medium / 20 | R^ u32-index node pool (LLVM/Clang) | 299.63 | 27.78 | 1.000x |
| medium / 20 | C++ unique_ptr (msvc) | 301.00 | 68.24 | 1.005x |
| medium / 20 | C++ new/delete (msvc) | 284.98 | 68.22 | 0.951x |
| medium / 20 | C++ index arena (msvc) | 115.09 | 27.77 | 0.384x |
| medium / 20 | C++ unique_ptr (clangcl) | 292.88 | 68.25 | 0.977x |
| medium / 20 | C++ new/delete (clangcl) | 277.93 | 68.22 | 0.928x |
| medium / 20 | C++ index arena (clangcl) | 114.23 | 27.78 | 0.381x |
| medium / 20 | Rust tree_box | 299.78 | 68.69 | 1.000x |
| medium / 20 | Rust tree_arena | 101.59 | 28.22 | 0.339x |
| large / 24 | R^ u32-index node pool (LLVM/Clang) | 4748.01 | 387.78 | 1.000x |
| large / 24 | C++ unique_ptr (msvc) | 4728.78 | 1034.96 | 0.996x |
| large / 24 | C++ new/delete (msvc) | 4556.83 | 1034.93 | 0.960x |
| large / 24 | C++ index arena (msvc) | 1585.30 | 387.77 | 0.334x |
| large / 24 | C++ unique_ptr (clangcl) | 4658.74 | 1034.96 | 0.981x |
| large / 24 | C++ new/delete (clangcl) | 4461.05 | 1034.92 | 0.940x |
| large / 24 | C++ index arena (clangcl) | 1549.91 | 387.78 | 0.326x |
| large / 24 | Rust tree_box | 4671.48 | 1035.38 | 0.984x |
| large / 24 | Rust tree_arena | 1317.56 | 388.22 | 0.277x |

## graph

R^ uses 8-byte edges (i32 target, u32 next index), including one list-head slot per vertex. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ linked u32-index pool (LLVM/Clang) | 277.76 | 11.42 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 135.19 | 13.57 | 0.487x |
| small / 100000 | C++ CSR (msvc) | 41.64 | 14.47 | 0.150x |
| small / 100000 | C++ linked index arena (msvc) | 91.16 | 11.40 | 0.328x |
| small / 100000 | C++ nested vectors (clangcl) | 130.36 | 13.57 | 0.469x |
| small / 100000 | C++ CSR (clangcl) | 43.56 | 14.48 | 0.157x |
| small / 100000 | C++ linked index arena (clangcl) | 98.53 | 11.41 | 0.355x |
| small / 100000 | Rust graph_nested | 98.24 | 15.60 | 0.354x |
| small / 100000 | Rust graph_csr | 41.54 | 14.93 | 0.150x |
| small / 100000 | Rust graph_arena | 115.28 | 11.87 | 0.415x |
| medium / 500000 | R^ linked u32-index pool (LLVM/Clang) | 2403.35 | 41.94 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1212.21 | 52.44 | 0.504x |
| medium / 500000 | C++ CSR (msvc) | 350.43 | 57.20 | 0.146x |
| medium / 500000 | C++ linked index arena (msvc) | 1392.26 | 41.93 | 0.579x |
| medium / 500000 | C++ nested vectors (clangcl) | 1194.50 | 52.52 | 0.497x |
| medium / 500000 | C++ CSR (clangcl) | 310.15 | 57.21 | 0.129x |
| medium / 500000 | C++ linked index arena (clangcl) | 1233.57 | 41.94 | 0.513x |
| medium / 500000 | Rust graph_nested | 738.06 | 60.92 | 0.307x |
| medium / 500000 | Rust graph_csr | 368.04 | 57.65 | 0.153x |
| medium / 500000 | Rust graph_arena | 1362.31 | 42.38 | 0.567x |
| large / 2000000 | R^ linked u32-index pool (LLVM/Clang) | 11831.71 | 156.39 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 5824.16 | 207.15 | 0.492x |
| large / 2000000 | C++ CSR (msvc) | 2212.80 | 217.42 | 0.187x |
| large / 2000000 | C++ linked index arena (msvc) | 7357.65 | 156.37 | 0.622x |
| large / 2000000 | C++ nested vectors (clangcl) | 5785.61 | 207.30 | 0.489x |
| large / 2000000 | C++ CSR (clangcl) | 2110.04 | 217.43 | 0.178x |
| large / 2000000 | C++ linked index arena (clangcl) | 6374.60 | 156.37 | 0.539x |
| large / 2000000 | Rust graph_nested | 3843.42 | 244.95 | 0.325x |
| large / 2000000 | Rust graph_csr | 2104.27 | 217.87 | 0.178x |
| large / 2000000 | Rust graph_arena | 6882.49 | 156.82 | 0.582x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
