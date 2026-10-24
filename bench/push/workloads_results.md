# Additional R^ workloads

Measured: 2026-10-24T01:49:57.138083+00:00 (UTC).

R^ replaces Goose. All comparison implementations were built and measured in this session.

Whole-process wall time, two warm-ups, best of 3 timed samples; peak resident working set.
R^ retains bounds/range checks and OS-backed, stable-address arrays. No affinity is set.

Comparison / R^ time: above 1 means R^ is faster. No suite-wide average is claimed.

Smoke sizes are correctness checks and are not performance evidence.

Storage: statically assigned data stacks, scope watermark rewinds, and Windows fault-driven commits (up to 1 MiB).
The LLVM push fast path makes no runtime call; array descriptors are 24 bytes.

Executable paths are unique per language; every run verifies its build-time SHA-256 before and after execution.

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | b958652e5b73f704689abff75b74bd574bc91120 |
| dirty | M CMakeLists.txt;  M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/current_bintrees/results.json;  M bench/current_bintrees/results.md;  M bench/report_suite.py;  M bench/run_bench.py;  M bench/run_workloads.py;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/llvm.c;  M src/main.c;  M src/rhat.h;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/pre_data_stacks/; ?? bench/pre_data_stacks_bintrees/; ?? src/stacks.c |
| compiler_sha256 | b009c93c77498b146d5d3838515e81a157d7c7fb4e23920c9ac243276cd421c7 |
| runtime_sha256 | c607b74ad004fb719288b594f60e0a25913763517803c2d7490f484319961073 |
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
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 61.39 | 11.59 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 28.89 | 14.66 | 0.471x |
| small / 1000000 | C++ reserved vector indices (msvc) | 21.87 | 11.74 | 0.356x |
| small / 1000000 | C++ deque pointers (msvc) | 61.71 | 23.55 | 1.005x |
| small / 1000000 | C++ vector indices (clangcl) | 29.48 | 14.65 | 0.480x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 25.47 | 11.74 | 0.415x |
| small / 1000000 | C++ deque pointers (clangcl) | 56.67 | 23.56 | 0.923x |
| small / 1000000 | Rust push | 21.60 | 12.04 | 0.352x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 810.96 | 127.84 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 237.91 | 189.19 | 0.293x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 105.46 | 129.55 | 0.130x |
| medium / 16000000 | C++ deque pointers (msvc) | 824.89 | 315.93 | 1.017x |
| medium / 16000000 | C++ vector indices (clangcl) | 245.75 | 189.19 | 0.303x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 172.25 | 129.52 | 0.212x |
| medium / 16000000 | C++ deque pointers (clangcl) | 745.03 | 315.93 | 0.919x |
| medium / 16000000 | Rust push | 115.12 | 129.21 | 0.142x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 3413.73 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 1030.51 | 936.29 | 0.302x |
| large / 64000000 | C++ reserved vector indices (msvc) | 402.74 | 501.45 | 0.118x |
| large / 64000000 | C++ deque pointers (msvc) | 3429.72 | 1251.09 | 1.005x |
| large / 64000000 | C++ vector indices (clangcl) | 1071.73 | 936.27 | 0.314x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 670.32 | 501.45 | 0.196x |
| large / 64000000 | C++ deque pointers (clangcl) | 3266.61 | 1251.07 | 0.957x |
| large / 64000000 | Rust push | 380.73 | 501.11 | 0.112x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
