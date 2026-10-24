# Additional R^ workloads

Measured: 2026-10-23T21:02:09.397008+00:00 (UTC).

R^ replaces Goose. All comparison implementations were built and measured in this session.

Whole-process wall time, two warm-ups, best of 3 timed samples; peak resident working set.
R^ retains bounds/range checks and OS-backed, stable-address arrays. No affinity is set.

Comparison / R^ time: above 1 means R^ is faster. No suite-wide average is claimed.

Smoke sizes are correctness checks and are not performance evidence.

Executable paths are unique per language; every run verifies its build-time SHA-256 before and after execution.

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | 555c77b2b33232521f985c722825a47145b8e30d |
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/push/workloads_results.json;  M bench/push/workloads_results.md;  M bench/results.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/upstream/PROVENANCE.md;  M bench/workloads_results.json;  M bench/workloads_results.md;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/front.c;  M src/llvm.c;  M src/rhat.h;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/artifacts.py; ?? bench/historical_bintrees_results.md; ?? bench/invalid_runs/; ?? bench/lru/; ?? bench/report_suite.py; ?? bench/rhat/calc.rh; ?? bench/rhat/interp.rh; ?? bench/rhat/lru.rh; ?? bench/rhat/records.rh; ?? bench/rhat/respond.rh; ?? bench/rhat/scene.rh; ?? bench/rhat/sexp.rh; ?? bench/rhat/strlist.rh; ?? bench/rhat/words.rh; ?? bench/scene/; ?? bench/text/; ?? bench/upstream/cpp/calc.cpp; ?? bench/upstream/cpp/interp.cpp; ?? bench/upstream/cpp/lru.cpp; ?? bench/upstream/cpp/records.cpp; ?? bench/upstream/cpp/respond.cpp; ?? bench/upstream/cpp/scene.cpp; ?? bench/upstream/cpp/sexp.cpp; ?? bench/upstream/cpp/strlist.cpp; ?? bench/upstream/cpp/words.cpp; ?? bench/upstream/rust/calc_arena.rs; ?? bench/upstream/rust/calc_box.rs; ?? bench/upstream/rust/interp_arena.rs; ?? bench/upstream/rust/interp_box.rs; ?? bench/upstream/rust/lru_hashmap.rs; ?? bench/upstream/rust/lru_open.rs; ?? bench/upstream/rust/records.rs; ?? bench/upstream/rust/respond_dto.rs; ?? bench/upstream/rust/respond_stream.rs; ?? bench/upstream/rust/scene_arena.rs; ?? bench/upstream/rust/scene_box.rs; ?? bench/upstream/rust/sexp_arena.rs; ?? bench/upstream/rust/sexp_box.rs; ?? bench/upstream/rust/strlist_borrowed.rs; ?? bench/upstream/rust/strlist_owned.rs; ?? bench/upstream/rust/words_hashmap.rs; ?? bench/upstream/rust/words_open.rs; ?? bench/variants_parsers/ |
| compiler_sha256 | 0d5be2d1de7705f476440990257e52d7dc67328af099a3ecc38c88355e5113af |
| runtime_sha256 | c0dfdca54088c8e0aec92cef0348f864651c75ba08a1c8391619bcc2088339e7 |
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
| small / 1000000 | R^ LLVM/Clang | 61.63 | 7.59 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 23.18 | 11.00 | 0.376x |
| small / 1000000 | C++ reserved vector (msvc) | 20.72 | 7.59 | 0.336x |
| small / 1000000 | C++ vector (clangcl) | 23.47 | 11.00 | 0.381x |
| small / 1000000 | C++ reserved vector (clangcl) | 21.29 | 7.59 | 0.345x |
| small / 1000000 | Rust sum | 21.81 | 8.02 | 0.354x |
| medium / 16000000 | R^ LLVM/Clang | 773.63 | 64.81 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 138.56 | 96.89 | 0.179x |
| medium / 16000000 | C++ reserved vector (msvc) | 100.66 | 64.81 | 0.130x |
| medium / 16000000 | C++ vector (clangcl) | 137.01 | 96.89 | 0.177x |
| medium / 16000000 | C++ reserved vector (clangcl) | 119.30 | 64.81 | 0.154x |
| medium / 16000000 | Rust sum | 95.63 | 65.25 | 0.124x |
| large / 128000000 | R^ LLVM/Clang | 6358.22 | 492.06 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 915.75 | 698.49 | 0.144x |
| large / 128000000 | C++ reserved vector (msvc) | 710.66 | 492.06 | 0.112x |
| large / 128000000 | C++ vector (clangcl) | 1066.96 | 698.49 | 0.168x |
| large / 128000000 | C++ reserved vector (clangcl) | 857.60 | 492.06 | 0.135x |
| large / 128000000 | Rust sum | 651.10 | 492.50 | 0.102x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 333.93 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 63.92 | 7.68 | 0.191x |
| small / 1024 | C++ row pointers (msvc) | 33.70 | 5.77 | 0.101x |
| small / 1024 | C++ flat (clangcl) | 61.17 | 7.68 | 0.183x |
| small / 1024 | C++ row pointers (clangcl) | 25.23 | 5.79 | 0.076x |
| small / 1024 | Rust blur_index | 26.98 | 7.41 | 0.081x |
| small / 1024 | Rust blur_windows | 26.14 | 7.41 | 0.078x |
| medium / 2048 | R^ LLVM/Clang | 1301.01 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 201.29 | 15.81 | 0.155x |
| medium / 2048 | C++ row pointers (msvc) | 83.86 | 11.77 | 0.064x |
| medium / 2048 | C++ flat (clangcl) | 216.96 | 15.80 | 0.167x |
| medium / 2048 | C++ row pointers (clangcl) | 53.70 | 11.79 | 0.041x |
| medium / 2048 | Rust blur_index | 57.81 | 13.41 | 0.044x |
| medium / 2048 | Rust blur_windows | 56.79 | 13.41 | 0.044x |
| large / 8192 | R^ LLVM/Clang | 21609.55 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3026.90 | 178.88 | 0.140x |
| large / 8192 | C++ row pointers (msvc) | 1079.90 | 131.77 | 0.050x |
| large / 8192 | C++ flat (clangcl) | 3440.59 | 178.87 | 0.159x |
| large / 8192 | C++ row pointers (clangcl) | 566.55 | 131.79 | 0.026x |
| large / 8192 | Rust blur_index | 668.70 | 133.41 | 0.031x |
| large / 8192 | Rust blur_windows | 671.00 | 133.41 | 0.031x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 95.49 | 6.07 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 54.96 | 6.07 | 0.576x |
| small / 100000 | C++ SoA (msvc) | 149.44 | 6.08 | 1.565x |
| small / 100000 | C++ vector AoS (msvc) | 60.62 | 6.07 | 0.635x |
| small / 100000 | C++ scalar AoS (clangcl) | 45.40 | 6.07 | 0.475x |
| small / 100000 | C++ SoA (clangcl) | 88.79 | 6.07 | 0.930x |
| small / 100000 | C++ vector AoS (clangcl) | 49.99 | 6.07 | 0.523x |
| small / 100000 | Rust particles_scalar | 46.13 | 6.51 | 0.483x |
| small / 100000 | Rust particles | 51.04 | 6.51 | 0.535x |
| medium / 1000000 | R^ LLVM/Clang | 925.38 | 26.67 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 641.64 | 26.67 | 0.693x |
| medium / 1000000 | C++ SoA (msvc) | 2618.60 | 26.67 | 2.830x |
| medium / 1000000 | C++ vector AoS (msvc) | 668.70 | 26.67 | 0.723x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 610.15 | 26.67 | 0.659x |
| medium / 1000000 | C++ SoA (clangcl) | 2597.73 | 26.67 | 2.807x |
| medium / 1000000 | C++ vector AoS (clangcl) | 625.13 | 26.67 | 0.676x |
| medium / 1000000 | Rust particles_scalar | 619.58 | 27.11 | 0.670x |
| medium / 1000000 | Rust particles | 635.44 | 27.11 | 0.687x |
| large / 4000000 | R^ LLVM/Clang | 3718.61 | 95.33 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2549.34 | 95.33 | 0.686x |
| large / 4000000 | C++ SoA (msvc) | 11416.97 | 95.34 | 3.070x |
| large / 4000000 | C++ vector AoS (msvc) | 2669.71 | 95.33 | 0.718x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2478.54 | 95.33 | 0.667x |
| large / 4000000 | C++ SoA (clangcl) | 10629.52 | 95.34 | 2.858x |
| large / 4000000 | C++ vector AoS (clangcl) | 2529.82 | 95.33 | 0.680x |
| large / 4000000 | Rust particles_scalar | 2464.58 | 95.77 | 0.663x |
| large / 4000000 | Rust particles | 2505.73 | 95.77 | 0.674x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](results.md).
