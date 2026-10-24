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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/numeric.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 21.63 | 7.65 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 23.53 | 11.00 | 1.088x |
| small / 1000000 | C++ reserved vector (msvc) | 21.32 | 7.59 | 0.986x |
| small / 1000000 | C++ vector (clangcl) | 24.05 | 11.00 | 1.112x |
| small / 1000000 | C++ reserved vector (clangcl) | 22.27 | 7.59 | 1.030x |
| small / 1000000 | Rust sum | 22.00 | 8.02 | 1.017x |
| medium / 16000000 | R^ LLVM/Clang | 100.91 | 64.84 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 133.77 | 96.89 | 1.326x |
| medium / 16000000 | C++ reserved vector (msvc) | 102.52 | 64.81 | 1.016x |
| medium / 16000000 | C++ vector (clangcl) | 149.18 | 96.89 | 1.478x |
| medium / 16000000 | C++ reserved vector (clangcl) | 127.50 | 64.82 | 1.263x |
| medium / 16000000 | Rust sum | 100.14 | 65.25 | 0.992x |
| large / 128000000 | R^ LLVM/Clang | 690.77 | 492.09 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 949.20 | 698.49 | 1.374x |
| large / 128000000 | C++ reserved vector (msvc) | 742.86 | 492.06 | 1.075x |
| large / 128000000 | C++ vector (clangcl) | 1117.13 | 698.49 | 1.617x |
| large / 128000000 | C++ reserved vector (clangcl) | 879.14 | 492.06 | 1.273x |
| large / 128000000 | Rust sum | 666.86 | 492.50 | 0.965x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 86.07 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 61.97 | 7.68 | 0.720x |
| small / 1024 | C++ row pointers (msvc) | 31.51 | 5.77 | 0.366x |
| small / 1024 | C++ flat (clangcl) | 64.32 | 7.68 | 0.747x |
| small / 1024 | C++ row pointers (clangcl) | 23.59 | 5.79 | 0.274x |
| small / 1024 | Rust blur_index | 26.19 | 7.41 | 0.304x |
| small / 1024 | Rust blur_windows | 27.24 | 7.41 | 0.316x |
| medium / 2048 | R^ LLVM/Clang | 315.37 | 11.77 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 204.88 | 15.81 | 0.650x |
| medium / 2048 | C++ row pointers (msvc) | 82.52 | 11.77 | 0.262x |
| medium / 2048 | C++ flat (clangcl) | 217.12 | 15.80 | 0.688x |
| medium / 2048 | C++ row pointers (clangcl) | 50.66 | 11.79 | 0.161x |
| medium / 2048 | Rust blur_index | 58.14 | 13.41 | 0.184x |
| medium / 2048 | Rust blur_windows | 57.00 | 13.41 | 0.181x |
| large / 8192 | R^ LLVM/Clang | 4939.43 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3212.73 | 178.88 | 0.650x |
| large / 8192 | C++ row pointers (msvc) | 1176.05 | 131.77 | 0.238x |
| large / 8192 | C++ flat (clangcl) | 3605.70 | 178.87 | 0.730x |
| large / 8192 | C++ row pointers (clangcl) | 584.01 | 131.79 | 0.118x |
| large / 8192 | Rust blur_index | 729.42 | 133.41 | 0.148x |
| large / 8192 | Rust blur_windows | 714.85 | 133.41 | 0.145x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 54.71 | 6.09 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 50.87 | 6.07 | 0.930x |
| small / 100000 | C++ SoA (msvc) | 146.02 | 6.08 | 2.669x |
| small / 100000 | C++ vector AoS (msvc) | 61.54 | 6.07 | 1.125x |
| small / 100000 | C++ scalar AoS (clangcl) | 45.07 | 6.07 | 0.824x |
| small / 100000 | C++ SoA (clangcl) | 83.77 | 6.07 | 1.531x |
| small / 100000 | C++ vector AoS (clangcl) | 50.31 | 6.07 | 0.920x |
| small / 100000 | Rust particles_scalar | 44.77 | 6.51 | 0.818x |
| small / 100000 | Rust particles | 49.02 | 6.51 | 0.896x |
| medium / 1000000 | R^ LLVM/Clang | 753.84 | 26.72 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 711.19 | 26.67 | 0.943x |
| medium / 1000000 | C++ SoA (msvc) | 2927.30 | 26.67 | 3.883x |
| medium / 1000000 | C++ vector AoS (msvc) | 705.48 | 26.67 | 0.936x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 654.09 | 26.67 | 0.868x |
| medium / 1000000 | C++ SoA (clangcl) | 2709.69 | 26.67 | 3.595x |
| medium / 1000000 | C++ vector AoS (clangcl) | 663.29 | 26.67 | 0.880x |
| medium / 1000000 | Rust particles_scalar | 648.64 | 27.11 | 0.860x |
| medium / 1000000 | Rust particles | 701.00 | 27.11 | 0.930x |
| large / 4000000 | R^ LLVM/Clang | 2980.13 | 95.34 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2666.64 | 95.33 | 0.895x |
| large / 4000000 | C++ SoA (msvc) | 11171.35 | 95.34 | 3.749x |
| large / 4000000 | C++ vector AoS (msvc) | 2866.97 | 95.33 | 0.962x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2606.77 | 95.33 | 0.875x |
| large / 4000000 | C++ SoA (clangcl) | 10721.43 | 95.34 | 3.598x |
| large / 4000000 | C++ vector AoS (clangcl) | 2688.32 | 95.33 | 0.902x |
| large / 4000000 | Rust particles_scalar | 2614.59 | 95.77 | 0.877x |
| large / 4000000 | Rust particles | 2662.62 | 95.77 | 0.893x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](results.md).
