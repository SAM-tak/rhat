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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/variants_parsers_partial.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## records

R^ ports records_fixed.goose with a native nominal Event union, 24-byte records and a 15-byte inline text capacity. Tag tests and checked payload extraction are generated from types; no manually synchronized kind/payload struct is used. Original sizes, RNG consumption, all four alternatives, actual per-Say word generation and four scans are preserved. This is the original fixed-mode variant; variable-size ADTs and separately owned string payloads are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ native nominal union / inline text (LLVM/Clang) | 116.90 | 15.21 | 1.000x |
| small / 500000 | C++ virtual objects (msvc) | 97.55 | 28.04 | 0.834x |
| small / 500000 | C++ variant / owned string (msvc) | 83.29 | 48.64 | 0.713x |
| small / 500000 | C++ variant / inline text (msvc) | 49.72 | 26.00 | 0.425x |
| small / 500000 | C++ virtual objects (clangcl) | 92.08 | 28.05 | 0.788x |
| small / 500000 | C++ variant / owned string (clangcl) | 73.88 | 48.66 | 0.632x |
| small / 500000 | C++ variant / inline text (clangcl) | 49.98 | 26.01 | 0.428x |
| small / 500000 | Rust records | 65.17 | 22.66 | 0.557x |
| medium / 4000000 | R^ native nominal union / inline text (LLVM/Clang) | 808.54 | 95.32 | 1.000x |
| medium / 4000000 | C++ virtual objects (msvc) | 637.09 | 187.05 | 0.788x |
| medium / 4000000 | C++ variant / owned string (msvc) | 540.89 | 330.32 | 0.669x |
| medium / 4000000 | C++ variant / inline text (msvc) | 265.06 | 166.84 | 0.328x |
| medium / 4000000 | C++ virtual objects (clangcl) | 632.78 | 187.06 | 0.783x |
| medium / 4000000 | C++ variant / owned string (clangcl) | 454.81 | 330.34 | 0.563x |
| medium / 4000000 | C++ variant / inline text (clangcl) | 266.24 | 166.85 | 0.329x |
| medium / 4000000 | Rust records | 396.07 | 150.71 | 0.490x |
| large / 16000000 | R^ native nominal union / inline text (LLVM/Clang) | 3195.26 | 369.98 | 1.000x |
| large / 16000000 | C++ virtual objects (msvc) | 2581.27 | 709.26 | 0.808x |
| large / 16000000 | C++ variant / owned string (msvc) | 1998.75 | 1100.77 | 0.626x |
| large / 16000000 | C++ variant / inline text (msvc) | 976.49 | 552.07 | 0.306x |
| large / 16000000 | C++ virtual objects (clangcl) | 2490.12 | 709.27 | 0.779x |
| large / 16000000 | C++ variant / owned string (clangcl) | 1718.90 | 1100.79 | 0.538x |
| large / 16000000 | C++ variant / inline text (clangcl) | 985.94 | 552.07 | 0.309x |
| large / 16000000 | Rust records | 1531.97 | 589.98 | 0.479x |

## interp

R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 child indices, matching the C++ switch / Rust arena representation. The generated tag tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, postorder construction, seeds, depths and eight pass-dependent evaluations modulo 1000003 are preserved. Goose's variable-size nodes and relative references are not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ native nominal union / u32 arena (LLVM/Clang) | 26.44 | 4.19 | 1.000x |
| small / 16 | C++ virtual owning tree (msvc) | 20.59 | 4.88 | 0.779x |
| small / 16 | C++ variant arena (msvc) | 18.62 | 4.97 | 0.704x |
| small / 16 | C++ switch arena (msvc) | 18.10 | 4.97 | 0.684x |
| small / 16 | C++ virtual owning tree (clangcl) | 20.35 | 4.88 | 0.770x |
| small / 16 | C++ variant arena (clangcl) | 18.90 | 4.98 | 0.715x |
| small / 16 | C++ switch arena (clangcl) | 18.52 | 4.98 | 0.700x |
| small / 16 | Rust interp_box | 22.05 | 5.32 | 0.834x |
| small / 16 | Rust interp_arena | 19.11 | 5.34 | 0.723x |
| medium / 20 | R^ native nominal union / u32 arena (LLVM/Clang) | 104.57 | 7.15 | 1.000x |
| medium / 20 | C++ virtual owning tree (msvc) | 68.00 | 12.88 | 0.650x |
| medium / 20 | C++ variant arena (msvc) | 32.63 | 10.20 | 0.312x |
| medium / 20 | C++ switch arena (msvc) | 32.41 | 10.20 | 0.310x |
| medium / 20 | C++ virtual owning tree (clangcl) | 69.40 | 12.88 | 0.664x |
| medium / 20 | C++ variant arena (clangcl) | 34.70 | 10.20 | 0.332x |
| medium / 20 | C++ switch arena (clangcl) | 33.96 | 10.20 | 0.325x |
| medium / 20 | Rust interp_box | 76.90 | 13.31 | 0.735x |
| medium / 20 | Rust interp_arena | 36.33 | 11.66 | 0.347x |
| large / 24 | R^ native nominal union / u32 arena (LLVM/Clang) | 904.83 | 39.28 | 1.000x |
| large / 24 | C++ virtual owning tree (msvc) | 556.25 | 99.16 | 0.615x |
| large / 24 | C++ variant arena (msvc) | 201.00 | 59.52 | 0.222x |
| large / 24 | C++ switch arena (msvc) | 188.27 | 59.52 | 0.208x |
| large / 24 | C++ virtual owning tree (clangcl) | 561.75 | 99.16 | 0.621x |
| large / 24 | C++ variant arena (clangcl) | 238.21 | 59.52 | 0.263x |
| large / 24 | C++ switch arena (clangcl) | 203.43 | 59.52 | 0.225x |
| large / 24 | Rust interp_box | 679.85 | 99.58 | 0.751x |
| large / 24 | Rust interp_arena | 213.80 | 53.69 | 0.236x |

## sexp

R^ generates the original text, parses it into 24-byte nominal union nodes linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks and all five checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 20000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 715.98 | 52.21 | 1.000x |
| small / 20000 | C++ owning-pointer / owned symbols (msvc) | 485.06 | 129.99 | 0.677x |
| small / 20000 | C++ index arena / borrowed symbols (msvc) | 216.53 | 61.06 | 0.302x |
| small / 20000 | C++ owning-pointer / owned symbols (clangcl) | 403.67 | 130.01 | 0.564x |
| small / 20000 | C++ index arena / borrowed symbols (clangcl) | 181.05 | 61.03 | 0.253x |
| small / 20000 | Rust sexp_box | 392.59 | 96.23 | 0.548x |
| small / 20000 | Rust sexp_arena | 166.44 | 62.53 | 0.232x |
| medium / 100000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 3654.98 | 245.70 | 1.000x |
| medium / 100000 | C++ owning-pointer / owned symbols (msvc) | 2334.44 | 633.54 | 0.639x |
| medium / 100000 | C++ index arena / borrowed symbols (msvc) | 1074.93 | 293.52 | 0.294x |
| medium / 100000 | C++ owning-pointer / owned symbols (clangcl) | 1932.29 | 633.55 | 0.529x |
| medium / 100000 | C++ index arena / borrowed symbols (clangcl) | 828.21 | 293.54 | 0.227x |
| medium / 100000 | Rust sexp_box | 1914.57 | 463.24 | 0.524x |
| medium / 100000 | Rust sexp_arena | 761.39 | 243.05 | 0.208x |
| large / 300000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 11750.20 | 729.51 | 1.000x |
| large / 300000 | C++ owning-pointer / owned symbols (msvc) | 7516.67 | 1891.72 | 0.640x |
| large / 300000 | C++ index arena / borrowed symbols (msvc) | 3045.21 | 963.77 | 0.259x |
| large / 300000 | C++ owning-pointer / owned symbols (clangcl) | 6022.55 | 1891.68 | 0.513x |
| large / 300000 | C++ index arena / borrowed symbols (clangcl) | 2473.14 | 963.79 | 0.210x |
| large / 300000 | Rust sexp_box | 6053.83 | 1380.90 | 0.515x |
| large / 300000 | Rust sexp_arena | 2270.29 | 910.78 | 0.193x |

## calc

R^ generates and damages the original arithmetic inputs, parses every input with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and division-by-zero failures. All partially built nodes count toward the total. Text and node regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, RNG, corruption, parse/evaluation order and all eight checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 257.24 | 4.19 | 1.000x |
| small / 100000 | C++ owning AST / exceptions (msvc) | 312.24 | 4.23 | 1.214x |
| small / 100000 | C++ index arena / error codes (msvc) | 80.66 | 4.18 | 0.314x |
| small / 100000 | C++ owning AST / exceptions (clangcl) | 300.53 | 4.23 | 1.168x |
| small / 100000 | C++ index arena / error codes (clangcl) | 68.83 | 4.19 | 0.268x |
| small / 100000 | Rust calc_box | 155.15 | 4.69 | 0.603x |
| small / 100000 | Rust calc_arena | 68.83 | 4.69 | 0.268x |
| medium / 400000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 1003.17 | 4.19 | 1.000x |
| medium / 400000 | C++ owning AST / exceptions (msvc) | 1214.45 | 4.24 | 1.211x |
| medium / 400000 | C++ index arena / error codes (msvc) | 274.34 | 4.18 | 0.273x |
| medium / 400000 | C++ owning AST / exceptions (clangcl) | 1184.12 | 4.23 | 1.180x |
| medium / 400000 | C++ index arena / error codes (clangcl) | 227.61 | 4.19 | 0.227x |
| medium / 400000 | Rust calc_box | 567.36 | 4.66 | 0.566x |
| medium / 400000 | Rust calc_arena | 219.33 | 4.68 | 0.219x |
| large / 1600000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 4024.96 | 4.19 | 1.000x |
| large / 1600000 | C++ owning AST / exceptions (msvc) | 4975.17 | 4.24 | 1.236x |
| large / 1600000 | C++ index arena / error codes (msvc) | 1045.25 | 4.18 | 0.260x |
| large / 1600000 | C++ owning AST / exceptions (clangcl) | 4698.61 | 4.23 | 1.167x |
| large / 1600000 | C++ index arena / error codes (clangcl) | 848.04 | 4.19 | 0.211x |
| large / 1600000 | Rust calc_box | 2195.91 | 4.68 | 0.546x |
| large / 1600000 | Rust calc_arena | 823.24 | 4.68 | 0.205x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
