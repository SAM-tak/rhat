# Additional R^ workloads

Measured: 2026-10-23T20:37:40.834571+00:00 (UTC).

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
| commit | 555c77b2b33232521f985c722825a47145b8e30d |
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/results.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/upstream/PROVENANCE.md;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/front.c;  M src/llvm.c;  M src/rhat.h;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/lru/; ?? bench/rhat/calc.rh; ?? bench/rhat/interp.rh; ?? bench/rhat/lru.rh; ?? bench/rhat/records.rh; ?? bench/rhat/respond.rh; ?? bench/rhat/scene.rh; ?? bench/rhat/sexp.rh; ?? bench/rhat/strlist.rh; ?? bench/rhat/words.rh; ?? bench/scene/; ?? bench/text/; ?? bench/upstream/cpp/calc.cpp; ?? bench/upstream/cpp/interp.cpp; ?? bench/upstream/cpp/lru.cpp; ?? bench/upstream/cpp/records.cpp; ?? bench/upstream/cpp/respond.cpp; ?? bench/upstream/cpp/scene.cpp; ?? bench/upstream/cpp/sexp.cpp; ?? bench/upstream/cpp/strlist.cpp; ?? bench/upstream/cpp/words.cpp; ?? bench/upstream/rust/calc_arena.rs; ?? bench/upstream/rust/calc_box.rs; ?? bench/upstream/rust/interp_arena.rs; ?? bench/upstream/rust/interp_box.rs; ?? bench/upstream/rust/lru_hashmap.rs; ?? bench/upstream/rust/lru_open.rs; ?? bench/upstream/rust/records.rs; ?? bench/upstream/rust/respond_dto.rs; ?? bench/upstream/rust/respond_stream.rs; ?? bench/upstream/rust/scene_arena.rs; ?? bench/upstream/rust/scene_box.rs; ?? bench/upstream/rust/sexp_arena.rs; ?? bench/upstream/rust/sexp_box.rs; ?? bench/upstream/rust/strlist_borrowed.rs; ?? bench/upstream/rust/strlist_owned.rs; ?? bench/upstream/rust/words_hashmap.rs; ?? bench/upstream/rust/words_open.rs |
| compiler_sha256 | 0d5be2d1de7705f476440990257e52d7dc67328af099a3ecc38c88355e5113af |
| runtime_sha256 | c0dfdca54088c8e0aec92cef0348f864651c75ba08a1c8391619bcc2088339e7 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## records

R^ ports records_fixed.goose with a native nominal Event union, 24-byte records and a 15-byte inline text capacity. Tag tests and checked payload extraction are generated from types; no manually synchronized kind/payload struct is used. Original sizes, RNG consumption, all four alternatives, actual per-Say word generation and four scans are preserved. This is the original fixed-mode variant; variable-size ADTs and separately owned string payloads are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ native nominal union / inline text (LLVM/Clang) | 65.68 | 22.65 | 1.000x |
| small / 500000 | C++ virtual objects (msvc) | 94.76 | 28.04 | 1.443x |
| small / 500000 | C++ variant / owned string (msvc) | 83.12 | 48.64 | 1.266x |
| small / 500000 | C++ variant / inline text (msvc) | 56.34 | 26.00 | 0.858x |
| small / 500000 | C++ virtual objects (clangcl) | 109.86 | 28.05 | 1.673x |
| small / 500000 | C++ variant / owned string (clangcl) | 75.74 | 48.66 | 1.153x |
| small / 500000 | C++ variant / inline text (clangcl) | 49.40 | 26.01 | 0.752x |
| small / 500000 | Rust records | 65.03 | 22.64 | 0.990x |
| medium / 4000000 | R^ native nominal union / inline text (LLVM/Clang) | 393.77 | 150.68 | 1.000x |
| medium / 4000000 | C++ virtual objects (msvc) | 649.02 | 187.04 | 1.648x |
| medium / 4000000 | C++ variant / owned string (msvc) | 534.30 | 330.36 | 1.357x |
| medium / 4000000 | C++ variant / inline text (msvc) | 260.11 | 166.87 | 0.661x |
| medium / 4000000 | C++ virtual objects (clangcl) | 617.28 | 187.06 | 1.568x |
| medium / 4000000 | C++ variant / owned string (clangcl) | 473.01 | 330.38 | 1.201x |
| medium / 4000000 | C++ variant / inline text (clangcl) | 269.57 | 166.88 | 0.685x |
| medium / 4000000 | Rust records | 396.69 | 150.72 | 1.007x |
| large / 16000000 | R^ native nominal union / inline text (LLVM/Clang) | 1536.73 | 590.04 | 1.000x |
| large / 16000000 | C++ virtual objects (msvc) | 2573.74 | 709.25 | 1.675x |
| large / 16000000 | C++ variant / owned string (msvc) | 1984.28 | 1100.77 | 1.291x |
| large / 16000000 | C++ variant / inline text (msvc) | 1005.44 | 552.07 | 0.654x |
| large / 16000000 | C++ virtual objects (clangcl) | 2467.39 | 709.23 | 1.606x |
| large / 16000000 | C++ variant / owned string (clangcl) | 1674.90 | 1100.79 | 1.090x |
| large / 16000000 | C++ variant / inline text (clangcl) | 999.42 | 552.07 | 0.650x |
| large / 16000000 | Rust records | 1544.01 | 590.05 | 1.005x |

## interp

R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 child indices, matching the C++ switch / Rust arena representation. The generated tag tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, postorder construction, seeds, depths and eight pass-dependent evaluations modulo 1000003 are preserved. Goose's variable-size nodes and relative references are not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ native nominal union / u32 arena (LLVM/Clang) | 28.01 | 4.19 | 1.000x |
| small / 16 | C++ virtual owning tree (msvc) | 20.60 | 4.88 | 0.735x |
| small / 16 | C++ variant arena (msvc) | 19.44 | 4.96 | 0.694x |
| small / 16 | C++ switch arena (msvc) | 19.74 | 4.96 | 0.705x |
| small / 16 | C++ virtual owning tree (clangcl) | 25.85 | 4.88 | 0.923x |
| small / 16 | C++ variant arena (clangcl) | 21.29 | 4.74 | 0.760x |
| small / 16 | C++ switch arena (clangcl) | 20.78 | 4.74 | 0.742x |
| small / 16 | Rust interp_box | 27.60 | 5.31 | 0.985x |
| small / 16 | Rust interp_arena | 23.10 | 5.38 | 0.825x |
| medium / 20 | R^ native nominal union / u32 arena (LLVM/Clang) | 99.59 | 7.15 | 1.000x |
| medium / 20 | C++ virtual owning tree (msvc) | 65.73 | 12.87 | 0.660x |
| medium / 20 | C++ variant arena (msvc) | 29.92 | 10.19 | 0.300x |
| medium / 20 | C++ switch arena (msvc) | 30.28 | 10.20 | 0.304x |
| medium / 20 | C++ virtual owning tree (clangcl) | 67.91 | 12.88 | 0.682x |
| medium / 20 | C++ variant arena (clangcl) | 34.69 | 10.20 | 0.348x |
| medium / 20 | C++ switch arena (clangcl) | 30.84 | 10.20 | 0.310x |
| medium / 20 | Rust interp_box | 75.94 | 13.31 | 0.763x |
| medium / 20 | Rust interp_arena | 32.32 | 11.66 | 0.325x |
| large / 24 | R^ native nominal union / u32 arena (LLVM/Clang) | 915.05 | 39.28 | 1.000x |
| large / 24 | C++ virtual owning tree (msvc) | 526.31 | 99.15 | 0.575x |
| large / 24 | C++ variant arena (msvc) | 209.85 | 59.51 | 0.229x |
| large / 24 | C++ switch arena (msvc) | 188.76 | 59.51 | 0.206x |
| large / 24 | C++ virtual owning tree (clangcl) | 564.15 | 99.16 | 0.617x |
| large / 24 | C++ variant arena (clangcl) | 240.50 | 59.52 | 0.263x |
| large / 24 | C++ switch arena (clangcl) | 206.09 | 59.52 | 0.225x |
| large / 24 | Rust interp_box | 661.64 | 99.58 | 0.723x |
| large / 24 | Rust interp_arena | 215.43 | 53.66 | 0.235x |

## sexp

R^ generates the original text, parses it into 24-byte nominal union nodes linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks and all five checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 20000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 721.49 | 52.21 | 1.000x |
| small / 20000 | C++ owning-pointer / owned symbols (msvc) | 464.77 | 130.00 | 0.644x |
| small / 20000 | C++ index arena / borrowed symbols (msvc) | 211.87 | 61.05 | 0.294x |
| small / 20000 | C++ owning-pointer / owned symbols (clangcl) | 382.05 | 129.99 | 0.530x |
| small / 20000 | C++ index arena / borrowed symbols (clangcl) | 178.72 | 61.04 | 0.248x |
| small / 20000 | Rust sexp_box | 392.23 | 96.24 | 0.544x |
| small / 20000 | Rust sexp_arena | 167.96 | 62.50 | 0.233x |
| medium / 100000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 3635.73 | 245.69 | 1.000x |
| medium / 100000 | C++ owning-pointer / owned symbols (msvc) | 2296.48 | 633.55 | 0.632x |
| medium / 100000 | C++ index arena / borrowed symbols (msvc) | 1030.54 | 293.52 | 0.283x |
| medium / 100000 | C++ owning-pointer / owned symbols (clangcl) | 1883.87 | 633.54 | 0.518x |
| medium / 100000 | C++ index arena / borrowed symbols (clangcl) | 818.63 | 293.78 | 0.225x |
| medium / 100000 | Rust sexp_box | 1910.26 | 463.25 | 0.525x |
| medium / 100000 | Rust sexp_arena | 752.13 | 243.09 | 0.207x |

## calc

R^ generates and damages the original arithmetic inputs, parses every input with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and division-by-zero failures. All partially built nodes count toward the total. Text and node regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, RNG, corruption, parse/evaluation order and all eight checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
