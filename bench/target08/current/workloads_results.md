# Additional R^ workloads

Measured: 2026-10-24T08:44:09.980173+00:00 (UTC).

R^ replaces Goose. All comparison implementations were built and measured in this session.

Whole-process wall time, two warm-ups, best of 3 timed samples; peak resident working set.
R^ retains bounds/range checks and OS-backed, stable-address arrays. No affinity is set.

Comparison / R^ time: above 1 means R^ is faster. No suite-wide average is claimed.

Smoke sizes are correctness checks and are not performance evidence.

Storage: statically assigned data stacks, scope watermark rewinds, and Windows fault-driven commits (up to 1 MiB).
The LLVM push fast path makes no runtime call; array descriptors are 24 bytes.

Executable paths are unique per language; every run verifies its build-time SHA-256 before and after execution.

Index bounds and address calculations are emitted inline, including full-width 128-bit checks.
Push checks remaining bytes without division; proven append-only paths update the top directly.

Internal functions pass scalars/references directly; canonical i64 loops use checked range guards and a checked fallback.

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | 4ba4c51bea2ee601dc6e49ff1565325818743a16 |
| dirty | M bench/README.md;  M bench/coverage.md;  M bench/report_suite.py;  M bench/rhat/bintrees.rh;  M bench/rhat/graph.rh;  M bench/run_workloads.py;  M docs/design.md;  M src/arrays.c;  M tests/run.py; ?? bench/rhat/variants/; ?? bench/target08/ |
| compiler_sha256 | 33e18d7d5db330b389e2ae762303bd6cc211a5a8261e1b17cec26bb5f835374b |
| runtime_sha256 | fa716c2b975c1a319f24c7a08fdce07d0194298765efbf629955eab2ade9e034 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## graph

R^ uses the original two-pass CSR count/prefix/fill representation, matching C++ VARIANT 1 and Rust CSR. Source and destination arrays, start offsets, fill cursors and contiguous edges retain all construction cost. The previous linked index implementation is preserved in rhat/variants/graph_linked.rh. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 43.45 | 14.72 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 137.40 | 13.56 | 3.162x |
| small / 100000 | C++ CSR (msvc) | 42.22 | 14.47 | 0.972x |
| small / 100000 | C++ linked index arena (msvc) | 95.71 | 11.41 | 2.203x |
| small / 100000 | C++ nested vectors (clangcl) | 128.93 | 13.57 | 2.967x |
| small / 100000 | C++ CSR (clangcl) | 42.05 | 14.48 | 0.968x |
| small / 100000 | C++ linked index arena (clangcl) | 91.52 | 11.41 | 2.106x |
| small / 100000 | Rust graph_nested | 97.34 | 15.61 | 2.240x |
| small / 100000 | Rust graph_csr | 42.09 | 14.93 | 0.969x |
| small / 100000 | Rust graph_arena | 142.20 | 11.86 | 3.272x |
| medium / 500000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 318.73 | 57.47 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1204.31 | 52.44 | 3.778x |
| medium / 500000 | C++ CSR (msvc) | 305.90 | 57.20 | 0.960x |
| medium / 500000 | C++ linked index arena (msvc) | 1429.26 | 41.93 | 4.484x |
| medium / 500000 | C++ nested vectors (clangcl) | 1282.89 | 52.45 | 4.025x |
| medium / 500000 | C++ CSR (clangcl) | 325.00 | 57.21 | 1.020x |
| medium / 500000 | C++ linked index arena (clangcl) | 1198.81 | 41.94 | 3.761x |
| medium / 500000 | Rust graph_nested | 847.48 | 60.92 | 2.659x |
| medium / 500000 | Rust graph_csr | 330.71 | 57.65 | 1.038x |
| medium / 500000 | Rust graph_arena | 1350.48 | 42.38 | 4.237x |
| large / 2000000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 1949.15 | 217.72 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 5967.17 | 207.30 | 3.061x |
| large / 2000000 | C++ CSR (msvc) | 2126.79 | 217.42 | 1.091x |
| large / 2000000 | C++ linked index arena (msvc) | 7480.20 | 156.36 | 3.838x |
| large / 2000000 | C++ nested vectors (clangcl) | 5837.74 | 207.16 | 2.995x |
| large / 2000000 | C++ CSR (clangcl) | 2116.31 | 217.43 | 1.086x |
| large / 2000000 | C++ linked index arena (clangcl) | 6551.52 | 156.38 | 3.361x |
| large / 2000000 | Rust graph_nested | 3812.97 | 244.94 | 1.956x |
| large / 2000000 | Rust graph_csr | 2164.12 | 217.86 | 1.110x |
| large / 2000000 | Rust graph_arena | 6964.13 | 156.82 | 3.573x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 23.91 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 63.95 | 6.44 | 2.675x |
| small / 1024 | C++ row pointers (msvc) | 33.11 | 5.77 | 1.385x |
| small / 1024 | C++ flat (clangcl) | 63.51 | 7.68 | 2.656x |
| small / 1024 | C++ row pointers (clangcl) | 25.25 | 5.79 | 1.056x |
| small / 1024 | Rust blur_index | 28.50 | 7.40 | 1.192x |
| small / 1024 | Rust blur_windows | 27.08 | 7.41 | 1.133x |
| medium / 2048 | R^ LLVM/Clang | 44.75 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 199.32 | 14.56 | 4.454x |
| medium / 2048 | C++ row pointers (msvc) | 83.97 | 11.77 | 1.876x |
| medium / 2048 | C++ flat (clangcl) | 219.49 | 15.80 | 4.904x |
| medium / 2048 | C++ row pointers (clangcl) | 52.11 | 11.79 | 1.164x |
| medium / 2048 | Rust blur_index | 59.86 | 13.39 | 1.337x |
| medium / 2048 | Rust blur_windows | 60.33 | 13.41 | 1.348x |
| large / 8192 | R^ LLVM/Clang | 486.27 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3059.27 | 177.63 | 6.291x |
| large / 8192 | C++ row pointers (msvc) | 1114.83 | 131.77 | 2.293x |
| large / 8192 | C++ flat (clangcl) | 3469.57 | 178.87 | 7.135x |
| large / 8192 | C++ row pointers (clangcl) | 595.44 | 131.79 | 1.225x |
| large / 8192 | Rust blur_index | 685.12 | 133.41 | 1.409x |
| large / 8192 | Rust blur_windows | 667.65 | 133.41 | 1.373x |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays use statically assigned stable 1 GiB data stacks and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 99.97 | 11.66 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 659.01 | 27.05 | 6.592x |
| small / 2000000 | C++ index arena / open table (msvc) | 129.05 | 15.65 | 1.291x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 590.59 | 27.05 | 5.908x |
| small / 2000000 | C++ index arena / open table (clangcl) | 118.69 | 16.16 | 1.187x |
| small / 2000000 | Rust lru_hashmap | 176.92 | 16.20 | 1.770x |
| small / 2000000 | Rust lru_open | 113.85 | 13.17 | 1.139x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 738.23 | 35.10 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3315.99 | 96.62 | 4.492x |
| medium / 8000000 | C++ index arena / open table (msvc) | 954.56 | 41.11 | 1.293x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 3061.05 | 96.62 | 4.146x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 810.26 | 43.17 | 1.098x |
| medium / 8000000 | Rust lru_hashmap | 1425.31 | 46.36 | 1.931x |
| medium / 8000000 | Rust lru_open | 1030.69 | 37.20 | 1.396x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 3914.45 | 128.85 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 13805.34 | 416.16 | 3.527x |
| large / 32000000 | C++ index arena / open table (msvc) | 4873.59 | 175.88 | 1.245x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 13138.01 | 416.23 | 3.356x |
| large / 32000000 | C++ index arena / open table (clangcl) | 4228.68 | 175.88 | 1.080x |
| large / 32000000 | Rust lru_hashmap | 7827.55 | 169.71 | 2.000x |
| large / 32000000 | Rust lru_open | 4713.71 | 133.20 | 1.204x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../../results.md).
