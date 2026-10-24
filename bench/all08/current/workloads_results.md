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

## respond

R^ ports the original respond_stream.goose variant, generating the same requests and JSON bytes, including real quote/backslash escaping. The output byte region is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, item counts, quantities, prices and all three checksums are preserved. The DTO-building Goose variant and general nested owned strings/arrays are not implemented by this port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 76.01 | 4.18 | 1.000x |
| small / 100000 | C++ response DTO (msvc) | 182.47 | 4.26 | 2.401x |
| small / 100000 | C++ streaming JSON (msvc) | 82.99 | 4.17 | 1.092x |
| small / 100000 | C++ response DTO (clangcl) | 155.97 | 4.27 | 2.052x |
| small / 100000 | C++ streaming JSON (clangcl) | 68.80 | 4.19 | 0.905x |
| small / 100000 | Rust respond_dto | 215.60 | 4.73 | 2.837x |
| small / 100000 | Rust respond_stream | 98.05 | 4.69 | 1.290x |
| medium / 400000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 256.41 | 4.18 | 1.000x |
| medium / 400000 | C++ response DTO (msvc) | 677.83 | 4.27 | 2.644x |
| medium / 400000 | C++ streaming JSON (msvc) | 272.52 | 4.17 | 1.063x |
| medium / 400000 | C++ response DTO (clangcl) | 581.12 | 4.27 | 2.266x |
| medium / 400000 | C++ streaming JSON (clangcl) | 213.13 | 4.19 | 0.831x |
| medium / 400000 | Rust respond_dto | 808.17 | 4.73 | 3.152x |
| medium / 400000 | Rust respond_stream | 337.85 | 4.70 | 1.318x |
| large / 1600000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 982.93 | 4.18 | 1.000x |
| large / 1600000 | C++ response DTO (msvc) | 2736.03 | 4.27 | 2.784x |
| large / 1600000 | C++ streaming JSON (msvc) | 1046.20 | 4.17 | 1.064x |
| large / 1600000 | C++ response DTO (clangcl) | 2257.47 | 4.27 | 2.297x |
| large / 1600000 | C++ streaming JSON (clangcl) | 844.41 | 4.19 | 0.859x |
| large / 1600000 | Rust respond_dto | 3189.25 | 4.73 | 3.245x |
| large / 1600000 | Rust respond_stream | 1308.74 | 4.68 | 1.331x |

## words

R^ stores vocabulary entries as 21-byte fixed-capacity words and copies split words into one owned packed byte region (one-byte varint lengths). The open-addressed table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, skewed word selection, RNG sequence, text construction, split and all three checksums are preserved. Packed strings are a library representation; general variable-size array elements and a built-in dictionary are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 82.07 | 19.97 | 1.000x |
| small / 500000 | C++ owned-key unordered_map (msvc) | 86.03 | 25.01 | 1.048x |
| small / 500000 | C++ borrowed-key unordered_map (msvc) | 85.00 | 25.02 | 1.036x |
| small / 500000 | C++ open table (msvc) | 80.00 | 24.99 | 0.975x |
| small / 500000 | C++ owned-key unordered_map (clangcl) | 76.93 | 25.02 | 0.937x |
| small / 500000 | C++ borrowed-key unordered_map (clangcl) | 81.92 | 25.00 | 0.998x |
| small / 500000 | C++ open table (clangcl) | 76.02 | 25.02 | 0.926x |
| small / 500000 | Rust words_hashmap | 89.33 | 21.62 | 1.088x |
| small / 500000 | Rust words_open | 73.96 | 25.18 | 0.901x |
| medium / 4000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 865.42 | 132.47 | 1.000x |
| medium / 4000000 | C++ owned-key unordered_map (msvc) | 1309.41 | 161.32 | 1.513x |
| medium / 4000000 | C++ borrowed-key unordered_map (msvc) | 1352.64 | 161.32 | 1.563x |
| medium / 4000000 | C++ open table (msvc) | 949.25 | 162.29 | 1.097x |
| medium / 4000000 | C++ owned-key unordered_map (clangcl) | 1090.18 | 161.33 | 1.260x |
| medium / 4000000 | C++ borrowed-key unordered_map (clangcl) | 1385.74 | 161.34 | 1.601x |
| medium / 4000000 | C++ open table (clangcl) | 806.25 | 162.30 | 0.932x |
| medium / 4000000 | Rust words_hashmap | 1173.76 | 142.18 | 1.356x |
| medium / 4000000 | Rust words_open | 878.86 | 172.08 | 1.016x |
| large / 16000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 4332.80 | 517.83 | 1.000x |
| large / 16000000 | C++ owned-key unordered_map (msvc) | 6293.59 | 563.45 | 1.453x |
| large / 16000000 | C++ borrowed-key unordered_map (msvc) | 6342.55 | 563.45 | 1.464x |
| large / 16000000 | C++ open table (msvc) | 4174.37 | 634.73 | 0.963x |
| large / 16000000 | C++ owned-key unordered_map (clangcl) | 5225.90 | 563.46 | 1.206x |
| large / 16000000 | C++ borrowed-key unordered_map (clangcl) | 6015.30 | 563.46 | 1.388x |
| large / 16000000 | C++ open table (clangcl) | 3861.24 | 634.72 | 0.891x |
| large / 16000000 | Rust words_hashmap | 6705.19 | 555.79 | 1.548x |
| large / 16000000 | Rust words_open | 4331.23 | 672.61 | 1.000x |

## strlist

R^ copies each split word into a single owned byte region, prefixed by its one-byte varint length (original generated lengths are 3..20). The original text and copied word list coexist; scans decode the packed lengths four times. A WordList library struct holds the count and owned data region. This does not claim general variable-size array elements or nested owned strings in the language. No word is replaced by a borrowed view or offset pair. All original sizes, seed sequence, text splitting, copies, passes and two checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 200000 | R^ owned packed word list (LLVM/Clang) | 31.95 | 8.66 | 1.000x |
| small / 200000 | C++ owned strings (msvc) | 51.51 | 16.47 | 1.612x |
| small / 200000 | C++ borrowed string views (msvc) | 35.37 | 11.28 | 1.107x |
| small / 200000 | C++ flat offsets (msvc) | 34.60 | 9.34 | 1.083x |
| small / 200000 | C++ owned strings (clangcl) | 45.65 | 16.41 | 1.429x |
| small / 200000 | C++ borrowed string views (clangcl) | 32.07 | 11.31 | 1.004x |
| small / 200000 | C++ flat offsets (clangcl) | 31.09 | 9.35 | 0.973x |
| small / 200000 | Rust strlist_owned | 51.69 | 16.45 | 1.618x |
| small / 200000 | Rust strlist_borrowed | 33.22 | 11.34 | 1.040x |
| medium / 2000000 | R^ owned packed word list (LLVM/Clang) | 165.11 | 51.54 | 1.000x |
| medium / 2000000 | C++ owned strings (msvc) | 380.66 | 144.02 | 2.305x |
| medium / 2000000 | C++ borrowed string views (msvc) | 204.51 | 76.60 | 1.239x |
| medium / 2000000 | C++ flat offsets (msvc) | 195.68 | 55.46 | 1.185x |
| medium / 2000000 | C++ owned strings (clangcl) | 327.41 | 144.02 | 1.983x |
| medium / 2000000 | C++ borrowed string views (clangcl) | 171.82 | 76.61 | 1.041x |
| medium / 2000000 | C++ flat offsets (clangcl) | 165.49 | 55.46 | 1.002x |
| medium / 2000000 | Rust strlist_owned | 363.66 | 125.23 | 2.203x |
| medium / 2000000 | Rust strlist_borrowed | 173.15 | 60.81 | 1.049x |
| large / 8000000 | R^ owned packed word list (LLVM/Clang) | 622.12 | 194.54 | 1.000x |
| large / 8000000 | C++ owned strings (msvc) | 1586.87 | 687.75 | 2.551x |
| large / 8000000 | C++ borrowed string views (msvc) | 786.38 | 343.33 | 1.264x |
| large / 8000000 | C++ flat offsets (msvc) | 714.11 | 191.14 | 1.148x |
| large / 8000000 | C++ owned strings (clangcl) | 1341.44 | 687.72 | 2.156x |
| large / 8000000 | C++ borrowed string views (clangcl) | 634.11 | 343.34 | 1.019x |
| large / 8000000 | C++ flat offsets (clangcl) | 559.01 | 191.16 | 0.899x |
| large / 8000000 | Rust strlist_owned | 1400.96 | 487.77 | 2.252x |
| large / 8000000 | Rust strlist_borrowed | 640.54 | 228.32 | 1.030x |

## scene

R^ uses 120-byte nodes with inline local/world transforms, four u32 child indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and f32 matrix/translation arithmetic are preserved. Composition writes through references to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array storage; Goose's 117-byte limited-array/relative-reference layout is not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 12 | R^ inline u32-child arena (LLVM/Clang) | 37.75 | 8.46 | 1.000x |
| small / 12 | C++ owning-pointer tree (msvc) | 71.38 | 10.11 | 1.891x |
| small / 12 | C++ index arena (msvc) | 36.20 | 10.91 | 0.959x |
| small / 12 | C++ owning-pointer tree (clangcl) | 60.98 | 10.12 | 1.616x |
| small / 12 | C++ index arena (clangcl) | 34.69 | 10.91 | 0.919x |
| small / 12 | Rust scene_box | 55.76 | 10.81 | 1.477x |
| small / 12 | Rust scene_arena | 33.81 | 13.58 | 0.896x |
| medium / 14 | R^ inline u32-child arena (LLVM/Clang) | 156.20 | 32.90 | 1.000x |
| medium / 14 | C++ owning-pointer tree (msvc) | 497.16 | 41.99 | 3.183x |
| medium / 14 | C++ index arena (msvc) | 177.45 | 52.12 | 1.136x |
| medium / 14 | C++ owning-pointer tree (clangcl) | 487.82 | 42.00 | 3.123x |
| medium / 14 | C++ index arena (clangcl) | 179.20 | 52.12 | 1.147x |
| medium / 14 | Rust scene_box | 507.50 | 43.96 | 3.249x |
| medium / 14 | Rust scene_arena | 155.14 | 36.08 | 0.993x |
| large / 16 | R^ inline u32-child arena (LLVM/Clang) | 883.82 | 185.90 | 1.000x |
| large / 16 | C++ owning-pointer tree (msvc) | 3040.08 | 242.62 | 3.440x |
| large / 16 | C++ index arena (msvc) | 1096.42 | 365.10 | 1.241x |
| large / 16 | C++ owning-pointer tree (clangcl) | 2959.87 | 242.62 | 3.349x |
| large / 16 | C++ index arena (clangcl) | 1023.57 | 365.10 | 1.158x |
| large / 16 | Rust scene_box | 3265.94 | 252.75 | 3.695x |
| large / 16 | Rust scene_arena | 864.70 | 246.05 | 0.978x |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays use statically assigned stable 1 GiB data stacks and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 139.86 | 11.66 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 653.08 | 27.05 | 4.669x |
| small / 2000000 | C++ index arena / open table (msvc) | 165.27 | 15.65 | 1.182x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 660.60 | 27.05 | 4.723x |
| small / 2000000 | C++ index arena / open table (clangcl) | 130.54 | 15.64 | 0.933x |
| small / 2000000 | Rust lru_hashmap | 204.82 | 16.17 | 1.464x |
| small / 2000000 | Rust lru_open | 167.78 | 13.19 | 1.200x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 823.40 | 35.10 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3383.23 | 96.62 | 4.109x |
| medium / 8000000 | C++ index arena / open table (msvc) | 1049.06 | 43.18 | 1.274x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 3162.78 | 96.62 | 3.841x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 904.49 | 41.11 | 1.098x |
| medium / 8000000 | Rust lru_hashmap | 1566.04 | 46.89 | 1.902x |
| medium / 8000000 | Rust lru_open | 944.00 | 37.19 | 1.146x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 3938.53 | 128.85 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 14560.31 | 416.23 | 3.697x |
| large / 32000000 | C++ index arena / open table (msvc) | 4987.18 | 175.88 | 1.266x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 13673.43 | 416.17 | 3.472x |
| large / 32000000 | C++ index arena / open table (clangcl) | 4321.84 | 175.88 | 1.097x |
| large / 32000000 | Rust lru_hashmap | 8008.90 | 169.74 | 2.033x |
| large / 32000000 | Rust lru_open | 4834.61 | 133.17 | 1.228x |

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 19.71 | 11.60 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 28.62 | 14.52 | 1.452x |
| small / 1000000 | C++ reserved vector indices (msvc) | 20.40 | 11.51 | 1.035x |
| small / 1000000 | C++ deque pointers (msvc) | 59.43 | 23.50 | 3.015x |
| small / 1000000 | C++ vector indices (clangcl) | 29.50 | 14.65 | 1.497x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 24.27 | 11.74 | 1.231x |
| small / 1000000 | C++ deque pointers (clangcl) | 56.48 | 23.56 | 2.865x |
| small / 1000000 | Rust push | 21.86 | 12.04 | 1.109x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 102.44 | 127.85 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 256.96 | 189.17 | 2.508x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 100.88 | 129.54 | 0.985x |
| medium / 16000000 | C++ deque pointers (msvc) | 943.52 | 315.91 | 9.210x |
| medium / 16000000 | C++ vector indices (clangcl) | 252.49 | 189.19 | 2.465x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 167.54 | 129.54 | 1.635x |
| medium / 16000000 | C++ deque pointers (clangcl) | 746.72 | 315.95 | 7.289x |
| medium / 16000000 | Rust push | 109.87 | 129.20 | 1.073x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 330.98 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 965.29 | 936.27 | 2.917x |
| large / 64000000 | C++ reserved vector indices (msvc) | 375.57 | 501.43 | 1.135x |
| large / 64000000 | C++ deque pointers (msvc) | 3141.41 | 1251.09 | 9.491x |
| large / 64000000 | C++ vector indices (clangcl) | 1001.10 | 936.29 | 3.025x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 633.00 | 501.48 | 1.913x |
| large / 64000000 | C++ deque pointers (clangcl) | 2994.11 | 1251.09 | 9.046x |
| large / 64000000 | Rust push | 352.98 | 501.13 | 1.066x |

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

## tree

R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ u32-index node pool (LLVM/Clang) | 17.80 | 5.28 | 1.000x |
| small / 16 | C++ unique_ptr (msvc) | 26.55 | 7.82 | 1.492x |
| small / 16 | C++ new/delete (msvc) | 27.42 | 7.79 | 1.541x |
| small / 16 | C++ index arena (msvc) | 18.99 | 5.27 | 1.067x |
| small / 16 | C++ unique_ptr (clangcl) | 28.82 | 7.82 | 1.619x |
| small / 16 | C++ new/delete (clangcl) | 27.48 | 7.79 | 1.544x |
| small / 16 | C++ index arena (clangcl) | 20.55 | 5.28 | 1.155x |
| small / 16 | Rust tree_box | 29.04 | 8.25 | 1.632x |
| small / 16 | Rust tree_arena | 19.25 | 5.72 | 1.082x |
| medium / 20 | R^ u32-index node pool (LLVM/Clang) | 104.76 | 27.78 | 1.000x |
| medium / 20 | C++ unique_ptr (msvc) | 325.29 | 68.25 | 3.105x |
| medium / 20 | C++ new/delete (msvc) | 319.46 | 68.22 | 3.049x |
| medium / 20 | C++ index arena (msvc) | 118.76 | 27.77 | 1.134x |
| medium / 20 | C++ unique_ptr (clangcl) | 317.61 | 68.25 | 3.032x |
| medium / 20 | C++ new/delete (clangcl) | 290.88 | 68.22 | 2.777x |
| medium / 20 | C++ index arena (clangcl) | 121.76 | 27.78 | 1.162x |
| medium / 20 | Rust tree_box | 306.20 | 68.68 | 2.923x |
| medium / 20 | Rust tree_arena | 115.28 | 28.22 | 1.100x |
| large / 24 | R^ u32-index node pool (LLVM/Clang) | 1444.23 | 387.78 | 1.000x |
| large / 24 | C++ unique_ptr (msvc) | 4899.77 | 1034.96 | 3.393x |
| large / 24 | C++ new/delete (msvc) | 4958.63 | 1034.93 | 3.433x |
| large / 24 | C++ index arena (msvc) | 1628.73 | 387.77 | 1.128x |
| large / 24 | C++ unique_ptr (clangcl) | 4966.00 | 1034.96 | 3.439x |
| large / 24 | C++ new/delete (clangcl) | 4729.90 | 1034.92 | 3.275x |
| large / 24 | C++ index arena (clangcl) | 1606.60 | 387.78 | 1.112x |
| large / 24 | Rust tree_box | 4879.35 | 1035.38 | 3.379x |
| large / 24 | Rust tree_arena | 1433.19 | 388.22 | 0.992x |

## graph

R^ uses the original two-pass CSR count/prefix/fill representation, matching C++ VARIANT 1 and Rust CSR. Source and destination arrays, start offsets, fill cursors and contiguous edges retain all construction cost. The previous linked index implementation is preserved in rhat/variants/graph_linked.rh. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 45.00 | 14.72 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 161.23 | 13.58 | 3.583x |
| small / 100000 | C++ CSR (msvc) | 48.00 | 14.47 | 1.067x |
| small / 100000 | C++ linked index arena (msvc) | 124.53 | 11.41 | 2.767x |
| small / 100000 | C++ nested vectors (clangcl) | 147.56 | 13.59 | 3.279x |
| small / 100000 | C++ CSR (clangcl) | 46.03 | 14.48 | 1.023x |
| small / 100000 | C++ linked index arena (clangcl) | 121.02 | 11.41 | 2.689x |
| small / 100000 | Rust graph_nested | 114.82 | 15.59 | 2.552x |
| small / 100000 | Rust graph_csr | 44.64 | 14.93 | 0.992x |
| small / 100000 | Rust graph_arena | 159.27 | 11.86 | 3.539x |
| medium / 500000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 332.95 | 57.47 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1334.65 | 52.52 | 4.009x |
| medium / 500000 | C++ CSR (msvc) | 332.56 | 57.20 | 0.999x |
| medium / 500000 | C++ linked index arena (msvc) | 1537.81 | 41.93 | 4.619x |
| medium / 500000 | C++ nested vectors (clangcl) | 1365.11 | 52.52 | 4.100x |
| medium / 500000 | C++ CSR (clangcl) | 409.77 | 57.21 | 1.231x |
| medium / 500000 | C++ linked index arena (clangcl) | 1347.70 | 41.94 | 4.048x |
| medium / 500000 | Rust graph_nested | 802.15 | 61.00 | 2.409x |
| medium / 500000 | Rust graph_csr | 334.88 | 57.65 | 1.006x |
| medium / 500000 | Rust graph_arena | 1491.96 | 42.38 | 4.481x |
| large / 2000000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 2276.31 | 217.72 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 7626.40 | 207.24 | 3.350x |
| large / 2000000 | C++ CSR (msvc) | 2688.04 | 217.42 | 1.181x |
| large / 2000000 | C++ linked index arena (msvc) | 8888.96 | 156.37 | 3.905x |
| large / 2000000 | C++ nested vectors (clangcl) | 7330.93 | 207.25 | 3.221x |
| large / 2000000 | C++ CSR (clangcl) | 2226.65 | 217.43 | 0.978x |
| large / 2000000 | C++ linked index arena (clangcl) | 6880.18 | 156.38 | 3.023x |
| large / 2000000 | Rust graph_nested | 4939.14 | 244.95 | 2.170x |
| large / 2000000 | Rust graph_csr | 2330.36 | 217.87 | 1.024x |
| large / 2000000 | Rust graph_arena | 7260.48 | 156.82 | 3.190x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../../results.md).
