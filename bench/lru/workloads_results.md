# Additional R^ workloads

Measured: 2026-10-23T19:14:47.473015+00:00 (UTC).

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
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/upstream/PROVENANCE.md;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/llvm.c;  M src/rhat.h;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/rhat/lru.rh; ?? bench/upstream/cpp/lru.cpp; ?? bench/upstream/rust/lru_hashmap.rs; ?? bench/upstream/rust/lru_open.rs |
| compiler_sha256 | 9995d1e1d7249d226b8cc30f8c774d13f82cdafe247575ee1c361fd267027cd8 |
| runtime_sha256 | a21ec55ba155c8a0893afe6f2b48a701da3c431377c5739c90d9662764e66483 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays reserve stable 1 GiB regions and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 542.48 | 11.63 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 691.01 | 27.15 | 1.274x |
| small / 2000000 | C++ index arena / open table (msvc) | 170.81 | 15.66 | 0.315x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 627.52 | 27.06 | 1.157x |
| small / 2000000 | C++ index arena / open table (clangcl) | 120.49 | 16.17 | 0.222x |
| small / 2000000 | Rust lru_hashmap | 182.77 | 16.23 | 0.337x |
| small / 2000000 | Rust lru_open | 147.69 | 13.18 | 0.272x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 3020.69 | 35.08 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3197.79 | 96.71 | 1.059x |
| medium / 8000000 | C++ index arena / open table (msvc) | 952.19 | 41.13 | 0.315x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 3013.27 | 96.63 | 0.998x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 800.59 | 42.21 | 0.265x |
| medium / 8000000 | Rust lru_hashmap | 1328.88 | 46.91 | 0.440x |
| medium / 8000000 | Rust lru_open | 993.39 | 37.21 | 0.329x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 13080.34 | 128.86 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 13761.89 | 416.06 | 1.052x |
| large / 32000000 | C++ index arena / open table (msvc) | 4910.79 | 175.90 | 0.375x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 12931.52 | 416.25 | 0.989x |
| large / 32000000 | C++ index arena / open table (clangcl) | 4049.97 | 175.89 | 0.310x |
| large / 32000000 | Rust lru_hashmap | 7656.56 | 169.74 | 0.585x |
| large / 32000000 | Rust lru_open | 4753.54 | 133.21 | 0.363x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
