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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/variants_parsers_partial.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## records

R^ ports records_fixed.goose with a native nominal Event union, 24-byte records and a 15-byte inline text capacity. Tag tests and checked payload extraction are generated from types; no manually synchronized kind/payload struct is used. Original sizes, RNG consumption, all four alternatives, actual per-Say word generation and four scans are preserved. This is the original fixed-mode variant; variable-size ADTs and separately owned string payloads are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ native nominal union / inline text (LLVM/Clang) | 41.02 | 15.28 | 1.000x |
| small / 500000 | C++ virtual objects (msvc) | 98.31 | 28.05 | 2.397x |
| small / 500000 | C++ variant / owned string (msvc) | 84.68 | 48.68 | 2.065x |
| small / 500000 | C++ variant / inline text (msvc) | 49.28 | 26.04 | 1.201x |
| small / 500000 | C++ virtual objects (clangcl) | 92.14 | 28.07 | 2.246x |
| small / 500000 | C++ variant / owned string (clangcl) | 75.78 | 48.70 | 1.847x |
| small / 500000 | C++ variant / inline text (clangcl) | 50.25 | 26.05 | 1.225x |
| small / 500000 | Rust records | 65.50 | 22.64 | 1.597x |
| medium / 4000000 | R^ native nominal union / inline text (LLVM/Clang) | 203.70 | 95.34 | 1.000x |
| medium / 4000000 | C++ virtual objects (msvc) | 668.75 | 187.05 | 3.283x |
| medium / 4000000 | C++ variant / owned string (msvc) | 542.70 | 330.36 | 2.664x |
| medium / 4000000 | C++ variant / inline text (msvc) | 273.22 | 166.88 | 1.341x |
| medium / 4000000 | C++ virtual objects (clangcl) | 644.66 | 187.06 | 3.165x |
| medium / 4000000 | C++ variant / owned string (clangcl) | 484.22 | 330.38 | 2.377x |
| medium / 4000000 | C++ variant / inline text (clangcl) | 270.43 | 166.88 | 1.328x |
| medium / 4000000 | Rust records | 408.73 | 150.68 | 2.007x |
| large / 16000000 | R^ native nominal union / inline text (LLVM/Clang) | 777.38 | 370.03 | 1.000x |
| large / 16000000 | C++ virtual objects (msvc) | 2755.61 | 709.24 | 3.545x |
| large / 16000000 | C++ variant / owned string (msvc) | 1976.83 | 1100.81 | 2.543x |
| large / 16000000 | C++ variant / inline text (msvc) | 1014.50 | 552.10 | 1.305x |
| large / 16000000 | C++ virtual objects (clangcl) | 2557.36 | 709.27 | 3.290x |
| large / 16000000 | C++ variant / owned string (clangcl) | 1728.82 | 1100.82 | 2.224x |
| large / 16000000 | C++ variant / inline text (clangcl) | 1008.58 | 552.11 | 1.297x |
| large / 16000000 | Rust records | 1584.83 | 589.99 | 2.039x |

## interp

R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 child indices, matching the C++ switch / Rust arena representation. The generated tag tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, postorder construction, seeds, depths and eight pass-dependent evaluations modulo 1000003 are preserved. Goose's variable-size nodes and relative references are not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ native nominal union / u32 arena (LLVM/Clang) | 18.66 | 4.22 | 1.000x |
| small / 16 | C++ virtual owning tree (msvc) | 20.37 | 4.88 | 1.092x |
| small / 16 | C++ variant arena (msvc) | 18.35 | 4.77 | 0.983x |
| small / 16 | C++ switch arena (msvc) | 17.79 | 4.77 | 0.954x |
| small / 16 | C++ virtual owning tree (clangcl) | 21.07 | 4.88 | 1.129x |
| small / 16 | C++ variant arena (clangcl) | 18.78 | 4.77 | 1.007x |
| small / 16 | C++ switch arena (clangcl) | 19.57 | 4.77 | 1.049x |
| small / 16 | Rust interp_box | 21.60 | 5.31 | 1.158x |
| small / 16 | Rust interp_arena | 21.56 | 5.39 | 1.156x |
| medium / 20 | R^ native nominal union / u32 arena (LLVM/Clang) | 32.87 | 7.22 | 1.000x |
| medium / 20 | C++ virtual owning tree (msvc) | 68.73 | 12.87 | 2.091x |
| medium / 20 | C++ variant arena (msvc) | 33.79 | 10.23 | 1.028x |
| medium / 20 | C++ switch arena (msvc) | 30.68 | 10.23 | 0.934x |
| medium / 20 | C++ virtual owning tree (clangcl) | 69.18 | 12.87 | 2.105x |
| medium / 20 | C++ variant arena (clangcl) | 35.71 | 10.24 | 1.086x |
| medium / 20 | C++ switch arena (clangcl) | 34.03 | 10.24 | 1.035x |
| medium / 20 | Rust interp_box | 83.80 | 13.31 | 2.550x |
| medium / 20 | Rust interp_arena | 33.40 | 11.63 | 1.016x |
| large / 24 | R^ native nominal union / u32 arena (LLVM/Clang) | 224.72 | 39.34 | 1.000x |
| large / 24 | C++ virtual owning tree (msvc) | 594.63 | 99.16 | 2.646x |
| large / 24 | C++ variant arena (msvc) | 204.11 | 59.55 | 0.908x |
| large / 24 | C++ switch arena (msvc) | 198.44 | 59.55 | 0.883x |
| large / 24 | C++ virtual owning tree (clangcl) | 589.03 | 99.16 | 2.621x |
| large / 24 | C++ variant arena (clangcl) | 244.25 | 59.55 | 1.087x |
| large / 24 | C++ switch arena (clangcl) | 212.78 | 59.55 | 0.947x |
| large / 24 | Rust interp_box | 653.98 | 99.60 | 2.910x |
| large / 24 | Rust interp_arena | 224.22 | 53.68 | 0.998x |

## sexp

R^ generates the original text, parses it into 24-byte nominal union nodes linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks and all five checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 20000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 174.69 | 52.34 | 1.000x |
| small / 20000 | C++ owning-pointer / owned symbols (msvc) | 476.11 | 130.00 | 2.725x |
| small / 20000 | C++ index arena / borrowed symbols (msvc) | 220.49 | 61.04 | 1.262x |
| small / 20000 | C++ owning-pointer / owned symbols (clangcl) | 416.39 | 130.00 | 2.384x |
| small / 20000 | C++ index arena / borrowed symbols (clangcl) | 183.95 | 61.15 | 1.053x |
| small / 20000 | Rust sexp_box | 402.39 | 96.24 | 2.303x |
| small / 20000 | Rust sexp_arena | 169.89 | 62.58 | 0.973x |
| medium / 100000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 806.47 | 245.84 | 1.000x |
| medium / 100000 | C++ owning-pointer / owned symbols (msvc) | 2439.98 | 633.55 | 3.026x |
| medium / 100000 | C++ index arena / borrowed symbols (msvc) | 1031.73 | 293.78 | 1.279x |
| medium / 100000 | C++ owning-pointer / owned symbols (clangcl) | 2129.76 | 633.53 | 2.641x |
| medium / 100000 | C++ index arena / borrowed symbols (clangcl) | 825.15 | 293.76 | 1.023x |
| medium / 100000 | Rust sexp_box | 1962.50 | 463.21 | 2.433x |
| medium / 100000 | Rust sexp_arena | 759.43 | 242.17 | 0.942x |
| large / 300000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 2409.51 | 729.60 | 1.000x |
| large / 300000 | C++ owning-pointer / owned symbols (msvc) | 7187.80 | 1891.73 | 2.983x |
| large / 300000 | C++ index arena / borrowed symbols (msvc) | 3232.35 | 963.75 | 1.341x |
| large / 300000 | C++ owning-pointer / owned symbols (clangcl) | 5946.39 | 1891.71 | 2.468x |
| large / 300000 | C++ index arena / borrowed symbols (clangcl) | 2481.38 | 964.05 | 1.030x |
| large / 300000 | Rust sexp_box | 6085.85 | 1380.86 | 2.526x |
| large / 300000 | Rust sexp_arena | 2332.73 | 911.07 | 0.968x |

## calc

R^ generates and damages the original arithmetic inputs, parses every input with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and division-by-zero failures. All partially built nodes count toward the total. Text and node regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, RNG, corruption, parse/evaluation order and all eight checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 78.84 | 4.20 | 1.000x |
| small / 100000 | C++ owning AST / exceptions (msvc) | 316.47 | 4.24 | 4.014x |
| small / 100000 | C++ index arena / error codes (msvc) | 83.93 | 4.17 | 1.065x |
| small / 100000 | C++ owning AST / exceptions (clangcl) | 305.26 | 4.23 | 3.872x |
| small / 100000 | C++ index arena / error codes (clangcl) | 68.14 | 4.18 | 0.864x |
| small / 100000 | Rust calc_box | 151.60 | 4.66 | 1.923x |
| small / 100000 | Rust calc_arena | 68.48 | 4.69 | 0.869x |
| medium / 400000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 266.14 | 4.20 | 1.000x |
| medium / 400000 | C++ owning AST / exceptions (msvc) | 1263.66 | 4.23 | 4.748x |
| medium / 400000 | C++ index arena / error codes (msvc) | 268.86 | 4.17 | 1.010x |
| medium / 400000 | C++ owning AST / exceptions (clangcl) | 1164.05 | 4.23 | 4.374x |
| medium / 400000 | C++ index arena / error codes (clangcl) | 237.92 | 4.18 | 0.894x |
| medium / 400000 | Rust calc_box | 569.77 | 4.66 | 2.141x |
| medium / 400000 | Rust calc_arena | 217.39 | 4.69 | 0.817x |
| large / 1600000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 1022.61 | 4.19 | 1.000x |
| large / 1600000 | C++ owning AST / exceptions (msvc) | 5017.32 | 4.24 | 4.906x |
| large / 1600000 | C++ index arena / error codes (msvc) | 1087.44 | 4.17 | 1.063x |
| large / 1600000 | C++ owning AST / exceptions (clangcl) | 4666.28 | 4.23 | 4.563x |
| large / 1600000 | C++ index arena / error codes (clangcl) | 861.40 | 4.18 | 0.842x |
| large / 1600000 | Rust calc_box | 2275.70 | 4.66 | 2.225x |
| large / 1600000 | Rust calc_arena | 837.96 | 4.69 | 0.819x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
