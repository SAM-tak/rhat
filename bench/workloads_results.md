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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/numeric.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 21.62 | 7.65 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 23.46 | 11.03 | 1.085x |
| small / 1000000 | C++ reserved vector (msvc) | 20.24 | 7.59 | 0.936x |
| small / 1000000 | C++ vector (clangcl) | 22.80 | 11.03 | 1.055x |
| small / 1000000 | C++ reserved vector (clangcl) | 20.17 | 7.59 | 0.933x |
| small / 1000000 | Rust sum | 21.19 | 8.02 | 0.980x |
| medium / 16000000 | R^ LLVM/Clang | 98.45 | 64.84 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 135.86 | 96.89 | 1.380x |
| medium / 16000000 | C++ reserved vector (msvc) | 103.67 | 64.81 | 1.053x |
| medium / 16000000 | C++ vector (clangcl) | 156.69 | 96.93 | 1.592x |
| medium / 16000000 | C++ reserved vector (clangcl) | 120.72 | 64.82 | 1.226x |
| medium / 16000000 | Rust sum | 96.08 | 65.25 | 0.976x |
| large / 128000000 | R^ LLVM/Clang | 701.36 | 492.09 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 985.27 | 698.52 | 1.405x |
| large / 128000000 | C++ reserved vector (msvc) | 766.29 | 492.06 | 1.093x |
| large / 128000000 | C++ vector (clangcl) | 1087.00 | 698.52 | 1.550x |
| large / 128000000 | C++ reserved vector (clangcl) | 861.62 | 492.06 | 1.229x |
| large / 128000000 | Rust sum | 689.48 | 492.50 | 0.983x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 21.84 | 5.79 | 1.000x |
| small / 1024 | C++ flat (msvc) | 64.01 | 7.68 | 2.930x |
| small / 1024 | C++ row pointers (msvc) | 31.76 | 5.77 | 1.454x |
| small / 1024 | C++ flat (clangcl) | 67.75 | 6.44 | 3.102x |
| small / 1024 | C++ row pointers (clangcl) | 23.47 | 5.79 | 1.075x |
| small / 1024 | Rust blur_index | 27.10 | 7.41 | 1.241x |
| small / 1024 | Rust blur_windows | 25.61 | 7.40 | 1.172x |
| medium / 2048 | R^ LLVM/Clang | 44.05 | 11.79 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 217.86 | 14.56 | 4.946x |
| medium / 2048 | C++ row pointers (msvc) | 87.22 | 11.77 | 1.980x |
| medium / 2048 | C++ flat (clangcl) | 225.25 | 14.56 | 5.114x |
| medium / 2048 | C++ row pointers (clangcl) | 50.19 | 11.79 | 1.139x |
| medium / 2048 | Rust blur_index | 56.97 | 13.41 | 1.293x |
| medium / 2048 | Rust blur_windows | 58.02 | 13.41 | 1.317x |
| large / 8192 | R^ LLVM/Clang | 493.17 | 131.79 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3191.96 | 178.88 | 6.472x |
| large / 8192 | C++ row pointers (msvc) | 1117.49 | 131.77 | 2.266x |
| large / 8192 | C++ flat (clangcl) | 3517.10 | 177.62 | 7.132x |
| large / 8192 | C++ row pointers (clangcl) | 594.34 | 131.79 | 1.205x |
| large / 8192 | Rust blur_index | 720.55 | 133.40 | 1.461x |
| large / 8192 | Rust blur_windows | 680.97 | 133.39 | 1.381x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 43.22 | 6.09 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 50.02 | 6.06 | 1.157x |
| small / 100000 | C++ SoA (msvc) | 146.00 | 6.08 | 3.378x |
| small / 100000 | C++ vector AoS (msvc) | 59.79 | 6.07 | 1.383x |
| small / 100000 | C++ scalar AoS (clangcl) | 47.85 | 6.07 | 1.107x |
| small / 100000 | C++ SoA (clangcl) | 78.96 | 6.07 | 1.827x |
| small / 100000 | C++ vector AoS (clangcl) | 51.75 | 6.07 | 1.197x |
| small / 100000 | Rust particles_scalar | 45.62 | 6.51 | 1.055x |
| small / 100000 | Rust particles | 49.21 | 6.51 | 1.139x |
| medium / 1000000 | R^ LLVM/Clang | 657.23 | 26.72 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 737.86 | 26.66 | 1.123x |
| medium / 1000000 | C++ SoA (msvc) | 2775.94 | 26.67 | 4.224x |
| medium / 1000000 | C++ vector AoS (msvc) | 702.04 | 26.67 | 1.068x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 697.34 | 26.67 | 1.061x |
| medium / 1000000 | C++ SoA (clangcl) | 2705.78 | 26.66 | 4.117x |
| medium / 1000000 | C++ vector AoS (clangcl) | 677.94 | 26.67 | 1.031x |
| medium / 1000000 | Rust particles_scalar | 707.79 | 27.11 | 1.077x |
| medium / 1000000 | Rust particles | 705.82 | 27.11 | 1.074x |
| large / 4000000 | R^ LLVM/Clang | 2666.65 | 95.34 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2761.28 | 95.33 | 1.035x |
| large / 4000000 | C++ SoA (msvc) | 11891.10 | 95.34 | 4.459x |
| large / 4000000 | C++ vector AoS (msvc) | 2820.62 | 95.33 | 1.058x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2638.19 | 95.33 | 0.989x |
| large / 4000000 | C++ SoA (clangcl) | 10920.52 | 95.34 | 4.095x |
| large / 4000000 | C++ vector AoS (clangcl) | 2752.57 | 95.33 | 1.032x |
| large / 4000000 | Rust particles_scalar | 2714.97 | 95.78 | 1.018x |
| large / 4000000 | Rust particles | 2882.20 | 95.77 | 1.081x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](results.md).
