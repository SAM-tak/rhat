# Additional R^ workloads

Measured: 2026-10-24T03:33:36.565830+00:00 (UTC).

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

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | d904390f94ffbab6694b16ce6cae70d6aee653e7 |
| dirty | M CMakeLists.txt;  M README.md;  M bench/README.md;  M bench/current_bintrees/results.json;  M bench/current_bintrees/results.md;  M bench/report_suite.py;  M bench/run_bench.py;  M bench/run_workloads.py;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/llvm.c;  M src/main.c;  M src/rhat.h;  M tests/run.py; ?? bench/array_codegen_comparison/; ?? bench/pre_array_fastpaths/; ?? bench/pre_array_fastpaths_bintrees/; ?? src/arrays.c |
| compiler_sha256 | e5e63c7e119cc6354e917b2d2ea77f63dafc310fb04c45dae796fc922251f3a4 |
| runtime_sha256 | fa716c2b975c1a319f24c7a08fdce07d0194298765efbf629955eab2ade9e034 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

This matrix is a subset of the complete current session in bench/current/workloads_results.json.
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/push.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 23.50 | 11.60 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 31.85 | 14.53 | 1.355x |
| small / 1000000 | C++ reserved vector indices (msvc) | 22.93 | 11.51 | 0.976x |
| small / 1000000 | C++ deque pointers (msvc) | 67.26 | 23.50 | 2.862x |
| small / 1000000 | C++ vector indices (clangcl) | 33.86 | 14.52 | 1.441x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 27.47 | 11.51 | 1.169x |
| small / 1000000 | C++ deque pointers (clangcl) | 63.40 | 23.51 | 2.697x |
| small / 1000000 | Rust push | 24.15 | 12.04 | 1.028x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 103.14 | 127.84 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 242.42 | 189.38 | 2.350x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 112.10 | 129.52 | 1.087x |
| medium / 16000000 | C++ deque pointers (msvc) | 743.50 | 315.89 | 7.208x |
| medium / 16000000 | C++ vector indices (clangcl) | 240.00 | 189.38 | 2.327x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 167.51 | 129.49 | 1.624x |
| medium / 16000000 | C++ deque pointers (clangcl) | 740.46 | 315.88 | 7.179x |
| medium / 16000000 | Rust push | 100.86 | 129.20 | 0.978x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 348.55 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 943.13 | 936.46 | 2.706x |
| large / 64000000 | C++ reserved vector indices (msvc) | 364.51 | 501.43 | 1.046x |
| large / 64000000 | C++ deque pointers (msvc) | 3053.28 | 1251.09 | 8.760x |
| large / 64000000 | C++ vector indices (clangcl) | 988.25 | 936.46 | 2.835x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 635.52 | 501.46 | 1.823x |
| large / 64000000 | C++ deque pointers (clangcl) | 2998.11 | 1251.10 | 8.602x |
| large / 64000000 | Rust push | 341.10 | 501.13 | 0.979x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
