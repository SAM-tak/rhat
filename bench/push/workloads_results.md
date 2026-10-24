# Additional R^ workloads

Measured: 2026-10-24T13:36:23.528961+00:00 (UTC).

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

Non-owning values up to 16 bytes pass directly; union/float guards and checked literal appends are inline.

Pop uses a checked inline load; typed storage metadata distinguishes non-owning values from array descriptors.

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | 1446808902970f403561443df576203cb20b0750 |
| dirty | M bench/all08/README.md;  M bench/all08/current_bintrees/results.json;  M bench/all08/current_bintrees/results.md;  M bench/all08/report.py;  M bench/push/workloads_results.json;  M bench/push/workloads_results.md;  M bench/report_suite.py;  M bench/results.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/variants_parsers/workloads_results.json;  M bench/variants_parsers/workloads_results.md;  M bench/workloads_results.json;  M bench/workloads_results.md;  M docs/design.md;  M src/llvm.c;  M tests/run.py; ?? bench/all08/aggregate_tags/; ?? bench/all08/check_codegen.py; ?? bench/all08/current/; ?? bench/all08/pre_pop/; ?? bench/all08/results.md; ?? bench/all08/verification.json |
| compiler_sha256 | 14306253a5809f5a5a4426f4677d268b45eb8a9defcda3e91ab83d1a7948c33d |
| runtime_sha256 | fa716c2b975c1a319f24c7a08fdce07d0194298765efbf629955eab2ade9e034 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

This matrix is a subset of the complete current session in bench/all08/current/workloads_results.json.
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/push.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 19.71 | 11.60 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 28.62 | 14.52 | 1.452x |
| small / 1000000 | C++ reserved vector indices (msvc) | 20.40 | 11.51 | 1.035x |
| small / 1000000 | C++ deque pointers (msvc) | 59.43 | 23.50 | 3.015x |
| small / 1000000 | C++ vector indices (clangcl) | 29.50 | 14.65 | 1.497x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 24.27 | 11.74 | 1.231x |
| small / 1000000 | C++ deque pointers (clangcl) | 56.48 | 23.56 | 2.865x |
| small / 1000000 | Rust push | 21.86 | 12.04 | 1.109x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 102.44 | 127.85 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 256.96 | 189.17 | 2.508x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 100.88 | 129.54 | 0.985x |
| medium / 16000000 | C++ deque pointers (msvc) | 943.52 | 315.91 | 9.210x |
| medium / 16000000 | C++ vector indices (clangcl) | 252.49 | 189.19 | 2.465x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 167.54 | 129.54 | 1.635x |
| medium / 16000000 | C++ deque pointers (clangcl) | 746.72 | 315.95 | 7.289x |
| medium / 16000000 | Rust push | 109.87 | 129.20 | 1.073x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 330.98 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 965.29 | 936.27 | 2.917x |
| large / 64000000 | C++ reserved vector indices (msvc) | 375.57 | 501.43 | 1.135x |
| large / 64000000 | C++ deque pointers (msvc) | 3141.41 | 1251.09 | 9.491x |
| large / 64000000 | C++ vector indices (clangcl) | 1001.10 | 936.29 | 3.025x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 633.00 | 501.48 | 1.913x |
| large / 64000000 | C++ deque pointers (clangcl) | 2994.11 | 1251.09 | 9.046x |
| large / 64000000 | Rust push | 352.98 | 501.13 | 1.066x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
