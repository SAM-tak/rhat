# Additional R^ workloads

Measured: 2026-10-23T19:26:43.252991+00:00 (UTC).

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
| commit | 555c77b2b33232521f985c722825a47145b8e30d |
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/results.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/upstream/PROVENANCE.md;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/llvm.c;  M src/rhat.h;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/lru/; ?? bench/rhat/lru.rh; ?? bench/rhat/scene.rh; ?? bench/upstream/cpp/lru.cpp; ?? bench/upstream/cpp/scene.cpp; ?? bench/upstream/rust/lru_hashmap.rs; ?? bench/upstream/rust/lru_open.rs; ?? bench/upstream/rust/scene_arena.rs; ?? bench/upstream/rust/scene_box.rs |
| compiler_sha256 | 9995d1e1d7249d226b8cc30f8c774d13f82cdafe247575ee1c361fd267027cd8 |
| runtime_sha256 | a21ec55ba155c8a0893afe6f2b48a701da3c431377c5739c90d9662764e66483 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## scene

R^ uses 120-byte nodes with inline local/world transforms, four u32 child indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and f32 matrix/translation arithmetic are preserved. Composition writes through references to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array storage; Goose's 117-byte limited-array/relative-reference layout is not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 12 | R^ inline u32-child arena (LLVM/Clang) | 131.28 | 8.43 | 1.000x |
| small / 12 | C++ owning-pointer tree (msvc) | 56.73 | 10.11 | 0.432x |
| small / 12 | C++ index arena (msvc) | 35.45 | 10.91 | 0.270x |
| small / 12 | C++ owning-pointer tree (clangcl) | 59.16 | 10.11 | 0.451x |
| small / 12 | C++ index arena (clangcl) | 32.45 | 10.91 | 0.247x |
| small / 12 | Rust scene_box | 49.59 | 10.82 | 0.378x |
| small / 12 | Rust scene_arena | 31.11 | 13.57 | 0.237x |
| medium / 14 | R^ inline u32-child arena (LLVM/Clang) | 746.22 | 32.89 | 1.000x |
| medium / 14 | C++ owning-pointer tree (msvc) | 479.59 | 41.99 | 0.643x |
| medium / 14 | C++ index arena (msvc) | 174.64 | 52.12 | 0.234x |
| medium / 14 | C++ owning-pointer tree (clangcl) | 443.19 | 42.00 | 0.594x |
| medium / 14 | C++ index arena (clangcl) | 166.25 | 52.12 | 0.223x |
| medium / 14 | Rust scene_box | 469.99 | 43.95 | 0.630x |
| medium / 14 | Rust scene_arena | 145.72 | 36.08 | 0.195x |
| large / 16 | R^ inline u32-child arena (LLVM/Clang) | 4662.76 | 185.89 | 1.000x |
| large / 16 | C++ owning-pointer tree (msvc) | 2921.17 | 242.62 | 0.626x |
| large / 16 | C++ index arena (msvc) | 1073.58 | 365.10 | 0.230x |
| large / 16 | C++ owning-pointer tree (clangcl) | 2807.23 | 242.62 | 0.602x |
| large / 16 | C++ index arena (clangcl) | 983.02 | 365.10 | 0.211x |
| large / 16 | Rust scene_box | 3049.29 | 252.75 | 0.654x |
| large / 16 | Rust scene_arena | 839.33 | 246.06 | 0.180x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
