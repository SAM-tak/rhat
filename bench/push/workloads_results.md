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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/push.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 74.33 | 11.54 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 29.62 | 14.53 | 0.399x |
| small / 1000000 | C++ reserved vector indices (msvc) | 22.78 | 11.51 | 0.306x |
| small / 1000000 | C++ deque pointers (msvc) | 58.61 | 23.50 | 0.789x |
| small / 1000000 | C++ vector indices (clangcl) | 30.41 | 14.52 | 0.409x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 26.05 | 11.51 | 0.350x |
| small / 1000000 | C++ deque pointers (clangcl) | 57.40 | 23.51 | 0.772x |
| small / 1000000 | Rust push | 23.26 | 12.04 | 0.313x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 1013.88 | 127.76 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 230.55 | 189.38 | 0.227x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 104.36 | 129.52 | 0.103x |
| medium / 16000000 | C++ deque pointers (msvc) | 757.03 | 315.89 | 0.747x |
| medium / 16000000 | C++ vector indices (clangcl) | 235.12 | 189.38 | 0.232x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 165.13 | 129.50 | 0.163x |
| medium / 16000000 | C++ deque pointers (clangcl) | 700.29 | 315.91 | 0.691x |
| medium / 16000000 | Rust push | 95.74 | 129.21 | 0.094x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 4118.67 | 499.69 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 929.35 | 936.50 | 0.226x |
| large / 64000000 | C++ reserved vector indices (msvc) | 354.81 | 501.45 | 0.086x |
| large / 64000000 | C++ deque pointers (msvc) | 2876.34 | 1251.09 | 0.698x |
| large / 64000000 | C++ vector indices (clangcl) | 939.51 | 936.48 | 0.228x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 618.59 | 501.42 | 0.150x |
| large / 64000000 | C++ deque pointers (clangcl) | 2749.76 | 1251.10 | 0.668x |
| large / 64000000 | Rust push | 335.68 | 501.11 | 0.082x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
