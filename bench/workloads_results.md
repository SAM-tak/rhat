# Additional R^ workloads

Measured: 2026-10-23T16:38:38.933332+00:00 (UTC).

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
| commit | 1e77abb9865c1bc1346068ead101cac467b730f5 |
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/upstream/PROVENANCE.md;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/front.c;  M src/llvm.c;  M src/rhat.h;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/rhat/blur.rh; ?? bench/rhat/particles.rh; ?? bench/rhat/sum.rh; ?? bench/run_workloads.py; ?? bench/upstream/cpp/blur.cpp; ?? bench/upstream/cpp/particles.cpp; ?? bench/upstream/cpp/sum.cpp; ?? bench/upstream/rust/blur_index.rs; ?? bench/upstream/rust/blur_windows.rs; ?? bench/upstream/rust/particles.rs; ?? bench/upstream/rust/particles_scalar.rs; ?? bench/upstream/rust/sum.rs |
| compiler_sha256 | 683d4d4b34bba0f40134a4496f47b05592760c73144cd9d81c4e1c1971313266 |
| runtime_sha256 | 912a91e011f8eb34438f683fef28ce77d9f3ac1dc95b954dada5ed5c163abf9e |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

MSVC version metadata was repaired after measurement: the original probe selected the help title. No timings were changed. The exact measured harness and full replacement version probe are archived in JSON.

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 22.97 | 8.02 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 24.01 | 11.00 | 1.045x |
| small / 1000000 | C++ reserved vector (msvc) | 22.93 | 7.59 | 0.998x |
| small / 1000000 | C++ vector (clangcl) | 23.78 | 11.00 | 1.035x |
| small / 1000000 | C++ reserved vector (clangcl) | 22.99 | 7.59 | 1.001x |
| small / 1000000 | Rust sum | 22.16 | 8.03 | 0.965x |
| medium / 16000000 | R^ LLVM/Clang | 99.30 | 65.25 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 142.54 | 96.89 | 1.436x |
| medium / 16000000 | C++ reserved vector (msvc) | 108.74 | 64.81 | 1.095x |
| medium / 16000000 | C++ vector (clangcl) | 147.68 | 96.89 | 1.487x |
| medium / 16000000 | C++ reserved vector (clangcl) | 123.27 | 64.82 | 1.241x |
| medium / 16000000 | Rust sum | 98.42 | 65.25 | 0.991x |
| large / 128000000 | R^ LLVM/Clang | 667.29 | 492.50 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 948.94 | 698.49 | 1.422x |
| large / 128000000 | C++ reserved vector (msvc) | 719.93 | 492.06 | 1.079x |
| large / 128000000 | C++ vector (clangcl) | 1114.90 | 698.49 | 1.671x |
| large / 128000000 | C++ reserved vector (clangcl) | 896.21 | 492.06 | 1.343x |
| large / 128000000 | Rust sum | 688.11 | 492.50 | 1.031x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 296.99 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 67.29 | 7.68 | 0.227x |
| small / 1024 | C++ row pointers (msvc) | 33.81 | 5.77 | 0.114x |
| small / 1024 | C++ flat (clangcl) | 67.51 | 7.68 | 0.227x |
| small / 1024 | C++ row pointers (clangcl) | 26.18 | 5.79 | 0.088x |
| small / 1024 | Rust blur_index | 29.37 | 7.41 | 0.099x |
| small / 1024 | Rust blur_windows | 29.08 | 7.41 | 0.098x |
| medium / 2048 | R^ LLVM/Clang | 1151.03 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 206.79 | 15.81 | 0.180x |
| medium / 2048 | C++ row pointers (msvc) | 91.40 | 11.77 | 0.079x |
| medium / 2048 | C++ flat (clangcl) | 221.46 | 15.80 | 0.192x |
| medium / 2048 | C++ row pointers (clangcl) | 54.85 | 11.79 | 0.048x |
| medium / 2048 | Rust blur_index | 59.65 | 13.41 | 0.052x |
| medium / 2048 | Rust blur_windows | 56.66 | 13.41 | 0.049x |
| large / 8192 | R^ LLVM/Clang | 21494.08 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3332.60 | 178.88 | 0.155x |
| large / 8192 | C++ row pointers (msvc) | 1263.20 | 131.77 | 0.059x |
| large / 8192 | C++ flat (clangcl) | 3719.38 | 178.87 | 0.173x |
| large / 8192 | C++ row pointers (clangcl) | 630.73 | 131.79 | 0.029x |
| large / 8192 | Rust blur_index | 692.23 | 133.41 | 0.032x |
| large / 8192 | Rust blur_windows | 696.10 | 133.41 | 0.032x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 57.07 | 6.51 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 57.08 | 6.07 | 1.000x |
| small / 100000 | C++ SoA (msvc) | 152.15 | 6.08 | 2.666x |
| small / 100000 | C++ vector AoS (msvc) | 65.85 | 6.07 | 1.154x |
| small / 100000 | C++ scalar AoS (clangcl) | 48.31 | 6.06 | 0.846x |
| small / 100000 | C++ SoA (clangcl) | 96.62 | 6.07 | 1.693x |
| small / 100000 | C++ vector AoS (clangcl) | 54.32 | 6.07 | 0.952x |
| small / 100000 | Rust particles_scalar | 51.14 | 6.51 | 0.896x |
| small / 100000 | Rust particles | 54.55 | 6.51 | 0.956x |
| medium / 1000000 | R^ LLVM/Clang | 692.80 | 27.11 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 720.70 | 26.67 | 1.040x |
| medium / 1000000 | C++ SoA (msvc) | 2983.70 | 26.67 | 4.307x |
| medium / 1000000 | C++ vector AoS (msvc) | 741.23 | 26.67 | 1.070x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 681.07 | 26.67 | 0.983x |
| medium / 1000000 | C++ SoA (clangcl) | 2787.93 | 26.67 | 4.024x |
| medium / 1000000 | C++ vector AoS (clangcl) | 691.54 | 26.67 | 0.998x |
| medium / 1000000 | Rust particles_scalar | 671.87 | 27.12 | 0.970x |
| medium / 1000000 | Rust particles | 725.78 | 27.11 | 1.048x |
| large / 4000000 | R^ LLVM/Clang | 2703.58 | 95.77 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2838.38 | 95.33 | 1.050x |
| large / 4000000 | C++ SoA (msvc) | 11330.41 | 95.34 | 4.191x |
| large / 4000000 | C++ vector AoS (msvc) | 2864.84 | 95.33 | 1.060x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2626.43 | 95.33 | 0.971x |
| large / 4000000 | C++ SoA (clangcl) | 10795.22 | 95.34 | 3.993x |
| large / 4000000 | C++ vector AoS (clangcl) | 2657.12 | 95.33 | 0.983x |
| large / 4000000 | Rust particles_scalar | 2613.74 | 95.78 | 0.967x |
| large / 4000000 | Rust particles | 2696.61 | 95.78 | 0.997x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in results.md.
