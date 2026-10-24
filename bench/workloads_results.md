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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/numeric.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 46.21 | 7.66 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 23.16 | 11.03 | 0.501x |
| small / 1000000 | C++ reserved vector (msvc) | 21.56 | 7.59 | 0.467x |
| small / 1000000 | C++ vector (clangcl) | 23.17 | 11.03 | 0.501x |
| small / 1000000 | C++ reserved vector (clangcl) | 20.95 | 7.59 | 0.453x |
| small / 1000000 | Rust sum | 21.78 | 8.03 | 0.471x |
| medium / 16000000 | R^ LLVM/Clang | 534.12 | 64.84 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 143.57 | 96.93 | 0.269x |
| medium / 16000000 | C++ reserved vector (msvc) | 110.92 | 64.81 | 0.208x |
| medium / 16000000 | C++ vector (clangcl) | 155.42 | 96.93 | 0.291x |
| medium / 16000000 | C++ reserved vector (clangcl) | 120.80 | 64.82 | 0.226x |
| medium / 16000000 | Rust sum | 103.25 | 65.25 | 0.193x |
| large / 128000000 | R^ LLVM/Clang | 4209.05 | 492.09 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 1045.84 | 698.52 | 0.248x |
| large / 128000000 | C++ reserved vector (msvc) | 793.23 | 492.06 | 0.188x |
| large / 128000000 | C++ vector (clangcl) | 1167.91 | 698.52 | 0.277x |
| large / 128000000 | C++ reserved vector (clangcl) | 893.21 | 492.06 | 0.212x |
| large / 128000000 | Rust sum | 747.62 | 492.50 | 0.178x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 295.93 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 68.92 | 6.44 | 0.233x |
| small / 1024 | C++ row pointers (msvc) | 33.58 | 5.77 | 0.113x |
| small / 1024 | C++ flat (clangcl) | 67.63 | 6.44 | 0.229x |
| small / 1024 | C++ row pointers (clangcl) | 26.14 | 5.79 | 0.088x |
| small / 1024 | Rust blur_index | 31.28 | 7.39 | 0.106x |
| small / 1024 | Rust blur_windows | 28.69 | 7.41 | 0.097x |
| medium / 2048 | R^ LLVM/Clang | 1091.49 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 239.31 | 14.56 | 0.219x |
| medium / 2048 | C++ row pointers (msvc) | 93.63 | 11.77 | 0.086x |
| medium / 2048 | C++ flat (clangcl) | 237.42 | 14.56 | 0.218x |
| medium / 2048 | C++ row pointers (clangcl) | 60.14 | 11.79 | 0.055x |
| medium / 2048 | Rust blur_index | 62.41 | 13.41 | 0.057x |
| medium / 2048 | Rust blur_windows | 61.73 | 13.41 | 0.057x |
| large / 8192 | R^ LLVM/Clang | 18483.12 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3304.60 | 177.63 | 0.179x |
| large / 8192 | C++ row pointers (msvc) | 1244.67 | 131.77 | 0.067x |
| large / 8192 | C++ flat (clangcl) | 3746.41 | 177.62 | 0.203x |
| large / 8192 | C++ row pointers (clangcl) | 610.75 | 131.79 | 0.033x |
| large / 8192 | Rust blur_index | 714.49 | 133.41 | 0.039x |
| large / 8192 | Rust blur_windows | 703.77 | 133.41 | 0.038x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 95.27 | 6.09 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 53.00 | 6.07 | 0.556x |
| small / 100000 | C++ SoA (msvc) | 156.18 | 6.07 | 1.639x |
| small / 100000 | C++ vector AoS (msvc) | 67.23 | 6.07 | 0.706x |
| small / 100000 | C++ scalar AoS (clangcl) | 49.30 | 6.07 | 0.517x |
| small / 100000 | C++ SoA (clangcl) | 98.13 | 6.07 | 1.030x |
| small / 100000 | C++ vector AoS (clangcl) | 56.64 | 6.07 | 0.594x |
| small / 100000 | Rust particles_scalar | 47.07 | 6.51 | 0.494x |
| small / 100000 | Rust particles | 55.29 | 6.51 | 0.580x |
| medium / 1000000 | R^ LLVM/Clang | 918.65 | 26.72 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 686.29 | 26.67 | 0.747x |
| medium / 1000000 | C++ SoA (msvc) | 2878.18 | 26.67 | 3.133x |
| medium / 1000000 | C++ vector AoS (msvc) | 715.48 | 26.67 | 0.779x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 672.20 | 26.67 | 0.732x |
| medium / 1000000 | C++ SoA (clangcl) | 2706.16 | 26.67 | 2.946x |
| medium / 1000000 | C++ vector AoS (clangcl) | 647.69 | 26.67 | 0.705x |
| medium / 1000000 | Rust particles_scalar | 671.95 | 27.11 | 0.731x |
| medium / 1000000 | Rust particles | 706.80 | 27.11 | 0.769x |
| large / 4000000 | R^ LLVM/Clang | 3840.48 | 95.34 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2760.99 | 95.33 | 0.719x |
| large / 4000000 | C++ SoA (msvc) | 12526.58 | 95.34 | 3.262x |
| large / 4000000 | C++ vector AoS (msvc) | 2772.55 | 95.33 | 0.722x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2598.98 | 95.33 | 0.677x |
| large / 4000000 | C++ SoA (clangcl) | 11255.81 | 95.34 | 2.931x |
| large / 4000000 | C++ vector AoS (clangcl) | 2676.79 | 95.33 | 0.697x |
| large / 4000000 | Rust particles_scalar | 2572.18 | 95.78 | 0.670x |
| large / 4000000 | Rust particles | 2643.59 | 95.78 | 0.688x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](results.md).
