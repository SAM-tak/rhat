# Additional R^ workloads

Measured: 2026-10-23T18:18:05.499631+00:00 (UTC).

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
| commit | c201142fdc473fe2811908d0bc35d276253b97ea |
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/upstream/PROVENANCE.md;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/front.c;  M src/llvm.c;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/push/; ?? bench/rhat/push.rh; ?? bench/upstream/cpp/push.cpp; ?? bench/upstream/rust/push.rs |
| compiler_sha256 | ce14d38a4a9b26183800a783c3f3b7d1466cdd007cf0f5dfc9620629aba9ebf2 |
| runtime_sha256 | 625eb1600d4895b6761f41cc63563875d2ba7ba7230e610a085baaad04eac542 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 22.21 | 12.05 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 30.19 | 14.53 | 1.359x |
| small / 1000000 | C++ reserved vector indices (msvc) | 21.78 | 11.51 | 0.981x |
| small / 1000000 | C++ deque pointers (msvc) | 58.29 | 23.50 | 2.625x |
| small / 1000000 | C++ vector indices (clangcl) | 30.27 | 14.65 | 1.363x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 26.08 | 11.74 | 1.175x |
| small / 1000000 | C++ deque pointers (clangcl) | 56.51 | 23.56 | 2.545x |
| small / 1000000 | Rust push | 21.88 | 12.04 | 0.985x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 96.23 | 129.18 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 231.00 | 189.41 | 2.400x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 101.48 | 129.54 | 1.054x |
| medium / 16000000 | C++ deque pointers (msvc) | 758.66 | 315.92 | 7.883x |
| medium / 16000000 | C++ vector indices (clangcl) | 236.80 | 189.18 | 2.461x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 166.24 | 129.56 | 1.727x |
| medium / 16000000 | C++ deque pointers (clangcl) | 722.91 | 315.93 | 7.512x |
| medium / 16000000 | Rust push | 96.20 | 129.18 | 1.000x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 360.96 | 501.13 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 967.31 | 936.48 | 2.680x |
| large / 64000000 | C++ reserved vector indices (msvc) | 366.31 | 501.46 | 1.015x |
| large / 64000000 | C++ deque pointers (msvc) | 2972.57 | 1251.09 | 8.235x |
| large / 64000000 | C++ vector indices (clangcl) | 981.02 | 936.28 | 2.718x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 629.18 | 501.48 | 1.743x |
| large / 64000000 | C++ deque pointers (clangcl) | 2866.42 | 1251.09 | 7.941x |
| large / 64000000 | Rust push | 355.88 | 501.14 | 0.986x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
