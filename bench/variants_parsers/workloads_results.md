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
Old R^ executable-collision timings are withdrawn. Verbatim original data is retained in bench/invalid_runs/variants_parsers_partial.json.
All current rows use language-specific executable paths with SHA-256 verified before and after every execution.

## records

R^ ports records_fixed.goose with a native nominal Event union, 24-byte records and a 15-byte inline text capacity. Tag tests and checked payload extraction are generated from types; no manually synchronized kind/payload struct is used. Original sizes, RNG consumption, all four alternatives, actual per-Say word generation and four scans are preserved. This is the original fixed-mode variant; variable-size ADTs and separately owned string payloads are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ native nominal union / inline text (LLVM/Clang) | 113.05 | 15.27 | 1.000x |
| small / 500000 | C++ virtual objects (msvc) | 102.40 | 28.05 | 0.906x |
| small / 500000 | C++ variant / owned string (msvc) | 97.99 | 48.68 | 0.867x |
| small / 500000 | C++ variant / inline text (msvc) | 61.18 | 26.04 | 0.541x |
| small / 500000 | C++ virtual objects (clangcl) | 120.51 | 28.07 | 1.066x |
| small / 500000 | C++ variant / owned string (clangcl) | 107.58 | 48.70 | 0.952x |
| small / 500000 | C++ variant / inline text (clangcl) | 72.78 | 26.05 | 0.644x |
| small / 500000 | Rust records | 78.52 | 22.65 | 0.695x |
| medium / 4000000 | R^ native nominal union / inline text (LLVM/Clang) | 759.35 | 95.34 | 1.000x |
| medium / 4000000 | C++ virtual objects (msvc) | 706.09 | 187.05 | 0.930x |
| medium / 4000000 | C++ variant / owned string (msvc) | 560.90 | 330.36 | 0.739x |
| medium / 4000000 | C++ variant / inline text (msvc) | 274.64 | 166.88 | 0.362x |
| medium / 4000000 | C++ virtual objects (clangcl) | 677.70 | 187.06 | 0.892x |
| medium / 4000000 | C++ variant / owned string (clangcl) | 475.91 | 330.38 | 0.627x |
| medium / 4000000 | C++ variant / inline text (clangcl) | 290.87 | 166.88 | 0.383x |
| medium / 4000000 | Rust records | 431.18 | 150.71 | 0.568x |
| large / 16000000 | R^ native nominal union / inline text (LLVM/Clang) | 2957.28 | 370.02 | 1.000x |
| large / 16000000 | C++ virtual objects (msvc) | 2705.95 | 709.27 | 0.915x |
| large / 16000000 | C++ variant / owned string (msvc) | 2076.70 | 1100.81 | 0.702x |
| large / 16000000 | C++ variant / inline text (msvc) | 979.48 | 552.10 | 0.331x |
| large / 16000000 | C++ virtual objects (clangcl) | 2941.99 | 709.24 | 0.995x |
| large / 16000000 | C++ variant / owned string (clangcl) | 1858.41 | 1100.82 | 0.628x |
| large / 16000000 | C++ variant / inline text (clangcl) | 1009.24 | 552.11 | 0.341x |
| large / 16000000 | Rust records | 1578.38 | 590.04 | 0.534x |

## interp

R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 child indices, matching the C++ switch / Rust arena representation. The generated tag tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, postorder construction, seeds, depths and eight pass-dependent evaluations modulo 1000003 are preserved. Goose's variable-size nodes and relative references are not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ native nominal union / u32 arena (LLVM/Clang) | 28.50 | 4.21 | 1.000x |
| small / 16 | C++ virtual owning tree (msvc) | 22.32 | 4.88 | 0.783x |
| small / 16 | C++ variant arena (msvc) | 20.56 | 4.77 | 0.722x |
| small / 16 | C++ switch arena (msvc) | 20.57 | 4.77 | 0.722x |
| small / 16 | C++ virtual owning tree (clangcl) | 23.47 | 4.88 | 0.824x |
| small / 16 | C++ variant arena (clangcl) | 21.04 | 4.77 | 0.739x |
| small / 16 | C++ switch arena (clangcl) | 19.44 | 4.77 | 0.682x |
| small / 16 | Rust interp_box | 25.95 | 5.31 | 0.911x |
| small / 16 | Rust interp_arena | 21.35 | 5.58 | 0.749x |
| medium / 20 | R^ native nominal union / u32 arena (LLVM/Clang) | 104.63 | 7.21 | 1.000x |
| medium / 20 | C++ virtual owning tree (msvc) | 84.59 | 12.88 | 0.808x |
| medium / 20 | C++ variant arena (msvc) | 39.47 | 10.23 | 0.377x |
| medium / 20 | C++ switch arena (msvc) | 38.93 | 10.23 | 0.372x |
| medium / 20 | C++ virtual owning tree (clangcl) | 79.95 | 12.88 | 0.764x |
| medium / 20 | C++ variant arena (clangcl) | 37.81 | 10.23 | 0.361x |
| medium / 20 | C++ switch arena (clangcl) | 38.59 | 10.23 | 0.369x |
| medium / 20 | Rust interp_box | 88.17 | 13.30 | 0.843x |
| medium / 20 | Rust interp_arena | 38.35 | 11.68 | 0.367x |
| large / 24 | R^ native nominal union / u32 arena (LLVM/Clang) | 1000.40 | 39.34 | 1.000x |
| large / 24 | C++ virtual owning tree (msvc) | 658.72 | 99.16 | 0.658x |
| large / 24 | C++ variant arena (msvc) | 238.20 | 59.55 | 0.238x |
| large / 24 | C++ switch arena (msvc) | 218.15 | 59.55 | 0.218x |
| large / 24 | C++ virtual owning tree (clangcl) | 630.86 | 99.16 | 0.631x |
| large / 24 | C++ variant arena (clangcl) | 273.69 | 59.55 | 0.274x |
| large / 24 | C++ switch arena (clangcl) | 224.19 | 59.55 | 0.224x |
| large / 24 | Rust interp_box | 766.98 | 99.60 | 0.767x |
| large / 24 | Rust interp_arena | 255.77 | 53.69 | 0.256x |

## sexp

R^ generates the original text, parses it into 24-byte nominal union nodes linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks and all five checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 20000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 594.00 | 52.35 | 1.000x |
| small / 20000 | C++ owning-pointer / owned symbols (msvc) | 480.47 | 130.02 | 0.809x |
| small / 20000 | C++ index arena / borrowed symbols (msvc) | 218.20 | 61.14 | 0.367x |
| small / 20000 | C++ owning-pointer / owned symbols (clangcl) | 417.44 | 130.00 | 0.703x |
| small / 20000 | C++ index arena / borrowed symbols (clangcl) | 189.15 | 61.13 | 0.318x |
| small / 20000 | Rust sexp_box | 393.13 | 96.24 | 0.662x |
| small / 20000 | Rust sexp_arena | 165.88 | 62.54 | 0.279x |
| medium / 100000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 3009.82 | 245.84 | 1.000x |
| medium / 100000 | C++ owning-pointer / owned symbols (msvc) | 2399.20 | 633.55 | 0.797x |
| medium / 100000 | C++ index arena / borrowed symbols (msvc) | 1079.57 | 293.77 | 0.359x |
| medium / 100000 | C++ owning-pointer / owned symbols (clangcl) | 2011.68 | 633.53 | 0.668x |
| medium / 100000 | C++ index arena / borrowed symbols (clangcl) | 830.09 | 293.79 | 0.276x |
| medium / 100000 | Rust sexp_box | 1964.10 | 463.20 | 0.653x |
| medium / 100000 | Rust sexp_arena | 750.69 | 243.16 | 0.249x |
| large / 300000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 9007.11 | 729.60 | 1.000x |
| large / 300000 | C++ owning-pointer / owned symbols (msvc) | 7264.29 | 1891.73 | 0.807x |
| large / 300000 | C++ index arena / borrowed symbols (msvc) | 3157.67 | 964.01 | 0.351x |
| large / 300000 | C++ owning-pointer / owned symbols (clangcl) | 5982.87 | 1891.68 | 0.664x |
| large / 300000 | C++ index arena / borrowed symbols (clangcl) | 2495.70 | 964.04 | 0.277x |
| large / 300000 | Rust sexp_box | 6147.11 | 1380.82 | 0.682x |
| large / 300000 | Rust sexp_arena | 2303.98 | 911.06 | 0.256x |

## calc

R^ generates and damages the original arithmetic inputs, parses every input with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and division-by-zero failures. All partially built nodes count toward the total. Text and node regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, RNG, corruption, parse/evaluation order and all eight checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 207.42 | 4.20 | 1.000x |
| small / 100000 | C++ owning AST / exceptions (msvc) | 323.70 | 4.24 | 1.561x |
| small / 100000 | C++ index arena / error codes (msvc) | 80.45 | 4.18 | 0.388x |
| small / 100000 | C++ owning AST / exceptions (clangcl) | 296.71 | 4.23 | 1.430x |
| small / 100000 | C++ index arena / error codes (clangcl) | 68.19 | 4.19 | 0.329x |
| small / 100000 | Rust calc_box | 152.60 | 4.70 | 0.736x |
| small / 100000 | Rust calc_arena | 69.41 | 4.69 | 0.335x |
| medium / 400000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 792.81 | 4.20 | 1.000x |
| medium / 400000 | C++ owning AST / exceptions (msvc) | 1253.78 | 4.24 | 1.581x |
| medium / 400000 | C++ index arena / error codes (msvc) | 277.88 | 4.18 | 0.350x |
| medium / 400000 | C++ owning AST / exceptions (clangcl) | 1154.65 | 4.23 | 1.456x |
| medium / 400000 | C++ index arena / error codes (clangcl) | 229.67 | 4.19 | 0.290x |
| medium / 400000 | Rust calc_box | 572.08 | 4.70 | 0.722x |
| medium / 400000 | Rust calc_arena | 222.86 | 4.68 | 0.281x |
| large / 1600000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 3220.66 | 4.20 | 1.000x |
| large / 1600000 | C++ owning AST / exceptions (msvc) | 4979.85 | 4.24 | 1.546x |
| large / 1600000 | C++ index arena / error codes (msvc) | 1083.79 | 4.18 | 0.337x |
| large / 1600000 | C++ owning AST / exceptions (clangcl) | 4646.70 | 4.23 | 1.443x |
| large / 1600000 | C++ index arena / error codes (clangcl) | 897.36 | 4.19 | 0.279x |
| large / 1600000 | Rust calc_box | 2282.36 | 4.66 | 0.709x |
| large / 1600000 | Rust calc_arena | 841.50 | 4.69 | 0.261x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
