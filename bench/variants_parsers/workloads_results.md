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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/variants_parsers_partial.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## records

R^ ports records_fixed.goose with a native nominal Event union, 24-byte records and a 15-byte inline text capacity. Tag tests and checked payload extraction are generated from types; no manually synchronized kind/payload struct is used. Original sizes, RNG consumption, all four alternatives, actual per-Say word generation and four scans are preserved. This is the original fixed-mode variant; variable-size ADTs and separately owned string payloads are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ native nominal union / inline text (LLVM/Clang) | 37.73 | 15.27 | 1.000x |
| small / 500000 | C++ virtual objects (msvc) | 97.33 | 28.04 | 2.580x |
| small / 500000 | C++ variant / owned string (msvc) | 84.62 | 48.64 | 2.243x |
| small / 500000 | C++ variant / inline text (msvc) | 49.13 | 26.00 | 1.302x |
| small / 500000 | C++ virtual objects (clangcl) | 92.12 | 28.05 | 2.442x |
| small / 500000 | C++ variant / owned string (clangcl) | 81.65 | 48.66 | 2.164x |
| small / 500000 | C++ variant / inline text (clangcl) | 50.47 | 26.01 | 1.338x |
| small / 500000 | Rust records | 64.26 | 22.64 | 1.703x |
| medium / 4000000 | R^ native nominal union / inline text (LLVM/Clang) | 184.22 | 95.34 | 1.000x |
| medium / 4000000 | C++ virtual objects (msvc) | 676.44 | 187.05 | 3.672x |
| medium / 4000000 | C++ variant / owned string (msvc) | 539.98 | 330.32 | 2.931x |
| medium / 4000000 | C++ variant / inline text (msvc) | 273.15 | 166.84 | 1.483x |
| medium / 4000000 | C++ virtual objects (clangcl) | 651.43 | 187.06 | 3.536x |
| medium / 4000000 | C++ variant / owned string (clangcl) | 490.61 | 330.34 | 2.663x |
| medium / 4000000 | C++ variant / inline text (clangcl) | 270.14 | 166.85 | 1.466x |
| medium / 4000000 | Rust records | 401.20 | 150.71 | 2.178x |
| large / 16000000 | R^ native nominal union / inline text (LLVM/Clang) | 710.40 | 370.02 | 1.000x |
| large / 16000000 | C++ virtual objects (msvc) | 2666.74 | 709.26 | 3.754x |
| large / 16000000 | C++ variant / owned string (msvc) | 2061.80 | 1100.77 | 2.902x |
| large / 16000000 | C++ variant / inline text (msvc) | 986.45 | 552.07 | 1.389x |
| large / 16000000 | C++ virtual objects (clangcl) | 2582.14 | 709.25 | 3.635x |
| large / 16000000 | C++ variant / owned string (clangcl) | 1733.62 | 1100.79 | 2.440x |
| large / 16000000 | C++ variant / inline text (clangcl) | 1054.58 | 552.07 | 1.484x |
| large / 16000000 | Rust records | 1571.77 | 589.99 | 2.213x |

## interp

R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 child indices, matching the C++ switch / Rust arena representation. The generated tag tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, postorder construction, seeds, depths and eight pass-dependent evaluations modulo 1000003 are preserved. Goose's variable-size nodes and relative references are not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ native nominal union / u32 arena (LLVM/Clang) | 19.49 | 4.21 | 1.000x |
| small / 16 | C++ virtual owning tree (msvc) | 20.43 | 4.88 | 1.049x |
| small / 16 | C++ variant arena (msvc) | 18.24 | 4.96 | 0.936x |
| small / 16 | C++ switch arena (msvc) | 18.29 | 4.97 | 0.938x |
| small / 16 | C++ virtual owning tree (clangcl) | 21.02 | 4.88 | 1.079x |
| small / 16 | C++ variant arena (clangcl) | 18.85 | 4.97 | 0.967x |
| small / 16 | C++ switch arena (clangcl) | 18.72 | 4.98 | 0.961x |
| small / 16 | Rust interp_box | 21.77 | 5.32 | 1.117x |
| small / 16 | Rust interp_arena | 19.49 | 5.62 | 1.000x |
| medium / 20 | R^ native nominal union / u32 arena (LLVM/Clang) | 37.88 | 7.21 | 1.000x |
| medium / 20 | C++ virtual owning tree (msvc) | 69.00 | 12.88 | 1.822x |
| medium / 20 | C++ variant arena (msvc) | 31.90 | 10.20 | 0.842x |
| medium / 20 | C++ switch arena (msvc) | 32.15 | 10.20 | 0.849x |
| medium / 20 | C++ virtual owning tree (clangcl) | 69.67 | 12.88 | 1.839x |
| medium / 20 | C++ variant arena (clangcl) | 34.00 | 10.20 | 0.898x |
| medium / 20 | C++ switch arena (clangcl) | 31.19 | 10.20 | 0.824x |
| medium / 20 | Rust interp_box | 78.10 | 13.30 | 2.062x |
| medium / 20 | Rust interp_arena | 33.39 | 11.70 | 0.881x |
| large / 24 | R^ native nominal union / u32 arena (LLVM/Clang) | 276.96 | 39.34 | 1.000x |
| large / 24 | C++ virtual owning tree (msvc) | 551.55 | 99.16 | 1.991x |
| large / 24 | C++ variant arena (msvc) | 207.26 | 59.52 | 0.748x |
| large / 24 | C++ switch arena (msvc) | 195.96 | 59.52 | 0.708x |
| large / 24 | C++ virtual owning tree (clangcl) | 570.38 | 99.16 | 2.059x |
| large / 24 | C++ variant arena (clangcl) | 247.51 | 59.52 | 0.894x |
| large / 24 | C++ switch arena (clangcl) | 213.65 | 59.52 | 0.771x |
| large / 24 | Rust interp_box | 688.68 | 99.59 | 2.487x |
| large / 24 | Rust interp_arena | 221.41 | 53.66 | 0.799x |

## sexp

R^ generates the original text, parses it into 24-byte nominal union nodes linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks and all five checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 20000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 254.21 | 52.36 | 1.000x |
| small / 20000 | C++ owning-pointer / owned symbols (msvc) | 480.21 | 130.00 | 1.889x |
| small / 20000 | C++ index arena / borrowed symbols (msvc) | 220.70 | 61.04 | 0.868x |
| small / 20000 | C++ owning-pointer / owned symbols (clangcl) | 408.72 | 129.98 | 1.608x |
| small / 20000 | C++ index arena / borrowed symbols (clangcl) | 181.75 | 61.04 | 0.715x |
| small / 20000 | Rust sexp_box | 410.76 | 96.24 | 1.616x |
| small / 20000 | Rust sexp_arena | 168.49 | 61.44 | 0.663x |
| medium / 100000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 1225.75 | 245.86 | 1.000x |
| medium / 100000 | C++ owning-pointer / owned symbols (msvc) | 2372.36 | 633.51 | 1.935x |
| medium / 100000 | C++ index arena / borrowed symbols (msvc) | 1041.30 | 293.53 | 0.850x |
| medium / 100000 | C++ owning-pointer / owned symbols (clangcl) | 2050.11 | 633.54 | 1.673x |
| medium / 100000 | C++ index arena / borrowed symbols (clangcl) | 840.35 | 293.53 | 0.686x |
| medium / 100000 | Rust sexp_box | 1983.27 | 463.24 | 1.618x |
| medium / 100000 | Rust sexp_arena | 795.64 | 242.19 | 0.649x |
| large / 300000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 3799.99 | 729.61 | 1.000x |
| large / 300000 | C++ owning-pointer / owned symbols (msvc) | 7471.44 | 1891.68 | 1.966x |
| large / 300000 | C++ index arena / borrowed symbols (msvc) | 3128.04 | 963.79 | 0.823x |
| large / 300000 | C++ owning-pointer / owned symbols (clangcl) | 6015.48 | 1891.69 | 1.583x |
| large / 300000 | C++ index arena / borrowed symbols (clangcl) | 2511.50 | 963.80 | 0.661x |
| large / 300000 | Rust sexp_box | 6088.70 | 1380.86 | 1.602x |
| large / 300000 | Rust sexp_arena | 2320.62 | 911.03 | 0.611x |

## calc

R^ generates and damages the original arithmetic inputs, parses every input with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and division-by-zero failures. All partially built nodes count toward the total. Text and node regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, RNG, corruption, parse/evaluation order and all eight checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 92.77 | 4.19 | 1.000x |
| small / 100000 | C++ owning AST / exceptions (msvc) | 319.62 | 4.23 | 3.445x |
| small / 100000 | C++ index arena / error codes (msvc) | 81.11 | 4.18 | 0.874x |
| small / 100000 | C++ owning AST / exceptions (clangcl) | 303.10 | 4.23 | 3.267x |
| small / 100000 | C++ index arena / error codes (clangcl) | 71.34 | 4.19 | 0.769x |
| small / 100000 | Rust calc_box | 156.93 | 4.70 | 1.692x |
| small / 100000 | Rust calc_arena | 70.96 | 4.69 | 0.765x |
| medium / 400000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 322.02 | 4.19 | 1.000x |
| medium / 400000 | C++ owning AST / exceptions (msvc) | 1211.69 | 4.24 | 3.763x |
| medium / 400000 | C++ index arena / error codes (msvc) | 279.17 | 4.18 | 0.867x |
| medium / 400000 | C++ owning AST / exceptions (clangcl) | 1137.89 | 4.23 | 3.534x |
| medium / 400000 | C++ index arena / error codes (clangcl) | 225.02 | 4.19 | 0.699x |
| medium / 400000 | Rust calc_box | 576.50 | 4.66 | 1.790x |
| medium / 400000 | Rust calc_arena | 222.82 | 4.69 | 0.692x |
| large / 1600000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 1272.34 | 4.18 | 1.000x |
| large / 1600000 | C++ owning AST / exceptions (msvc) | 4944.57 | 4.23 | 3.886x |
| large / 1600000 | C++ index arena / error codes (msvc) | 1073.85 | 4.18 | 0.844x |
| large / 1600000 | C++ owning AST / exceptions (clangcl) | 4607.22 | 4.23 | 3.621x |
| large / 1600000 | C++ index arena / error codes (clangcl) | 861.59 | 4.19 | 0.677x |
| large / 1600000 | Rust calc_box | 2213.89 | 4.66 | 1.740x |
| large / 1600000 | Rust calc_arena | 832.43 | 4.69 | 0.654x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
