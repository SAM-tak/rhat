# Additional R^ workloads

Measured: 2026-10-24T11:19:01.438148+00:00 (UTC).

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

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | 4ba4c51bea2ee601dc6e49ff1565325818743a16 |
| dirty | M bench/README.md;  M bench/coverage.md;  M bench/report_suite.py;  M bench/results.md;  M bench/rhat/bintrees.rh;  M bench/rhat/graph.rh;  M bench/run_bench.py;  M bench/run_workloads.py;  M docs/design.md;  M src/arrays.c;  M src/llvm.c;  M tests/ranges.py;  M tests/run.py; ?? bench/all08/; ?? bench/rhat/variants/; ?? bench/target08/ |
| compiler_sha256 | f6654f017c5209255d300bb8b509a0d5cbd0c089595c3fc9b8dee735870b7196 |
| runtime_sha256 | fa716c2b975c1a319f24c7a08fdce07d0194298765efbf629955eab2ade9e034 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## sexp

R^ generates the original text, parses it into 24-byte nominal union nodes linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks and all five checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 20000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 172.63 | 52.34 | 1.000x |
| small / 20000 | C++ owning-pointer / owned symbols (msvc) | 462.04 | 130.01 | 2.676x |
| small / 20000 | C++ index arena / borrowed symbols (msvc) | 212.86 | 61.14 | 1.233x |
| small / 20000 | C++ owning-pointer / owned symbols (clangcl) | 391.42 | 130.01 | 2.267x |
| small / 20000 | C++ index arena / borrowed symbols (clangcl) | 173.27 | 61.15 | 1.004x |
| small / 20000 | Rust sexp_box | 392.40 | 96.25 | 2.273x |
| small / 20000 | Rust sexp_arena | 164.13 | 62.56 | 0.951x |
| medium / 100000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 790.70 | 245.84 | 1.000x |
| medium / 100000 | C++ owning-pointer / owned symbols (msvc) | 2337.83 | 633.54 | 2.957x |
| medium / 100000 | C++ index arena / borrowed symbols (msvc) | 1007.91 | 293.77 | 1.275x |
| medium / 100000 | C++ owning-pointer / owned symbols (clangcl) | 1923.34 | 633.54 | 2.432x |
| medium / 100000 | C++ index arena / borrowed symbols (clangcl) | 810.88 | 293.80 | 1.026x |
| medium / 100000 | Rust sexp_box | 1940.11 | 463.20 | 2.454x |
| medium / 100000 | Rust sexp_arena | 740.16 | 243.15 | 0.936x |
| large / 300000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 2339.07 | 729.59 | 1.000x |
| large / 300000 | C++ owning-pointer / owned symbols (msvc) | 7137.30 | 1891.71 | 3.051x |
| large / 300000 | C++ index arena / borrowed symbols (msvc) | 3056.22 | 964.02 | 1.307x |
| large / 300000 | C++ owning-pointer / owned symbols (clangcl) | 5732.33 | 1891.68 | 2.451x |
| large / 300000 | C++ index arena / borrowed symbols (clangcl) | 2451.77 | 964.05 | 1.048x |
| large / 300000 | Rust sexp_box | 5858.75 | 1380.83 | 2.505x |
| large / 300000 | Rust sexp_arena | 2268.53 | 911.04 | 0.970x |

## calc

R^ generates and damages the original arithmetic inputs, parses every input with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and division-by-zero failures. All partially built nodes count toward the total. Text and node regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, RNG, corruption, parse/evaluation order and all eight checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 77.66 | 4.20 | 1.000x |
| small / 100000 | C++ owning AST / exceptions (msvc) | 311.38 | 4.24 | 4.009x |
| small / 100000 | C++ index arena / error codes (msvc) | 78.86 | 4.18 | 1.015x |
| small / 100000 | C++ owning AST / exceptions (clangcl) | 294.58 | 4.23 | 3.793x |
| small / 100000 | C++ index arena / error codes (clangcl) | 70.64 | 4.19 | 0.910x |
| small / 100000 | Rust calc_box | 154.88 | 4.71 | 1.994x |
| small / 100000 | Rust calc_arena | 66.22 | 4.70 | 0.853x |
| medium / 400000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 255.80 | 4.20 | 1.000x |
| medium / 400000 | C++ owning AST / exceptions (msvc) | 1213.28 | 4.24 | 4.743x |
| medium / 400000 | C++ index arena / error codes (msvc) | 264.64 | 4.18 | 1.035x |
| medium / 400000 | C++ owning AST / exceptions (clangcl) | 1151.32 | 4.23 | 4.501x |
| medium / 400000 | C++ index arena / error codes (clangcl) | 225.18 | 4.19 | 0.880x |
| medium / 400000 | Rust calc_box | 562.74 | 4.70 | 2.200x |
| medium / 400000 | Rust calc_arena | 216.38 | 4.69 | 0.846x |
| large / 1600000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 988.64 | 4.20 | 1.000x |
| large / 1600000 | C++ owning AST / exceptions (msvc) | 4873.99 | 4.24 | 4.930x |
| large / 1600000 | C++ index arena / error codes (msvc) | 1037.21 | 4.18 | 1.049x |
| large / 1600000 | C++ owning AST / exceptions (clangcl) | 4603.25 | 4.23 | 4.656x |
| large / 1600000 | C++ index arena / error codes (clangcl) | 855.87 | 4.19 | 0.866x |
| large / 1600000 | Rust calc_box | 2227.41 | 4.66 | 2.253x |
| large / 1600000 | Rust calc_arena | 810.15 | 4.69 | 0.819x |

## records

R^ ports records_fixed.goose with a native nominal Event union, 24-byte records and a 15-byte inline text capacity. Tag tests and checked payload extraction are generated from types; no manually synchronized kind/payload struct is used. Original sizes, RNG consumption, all four alternatives, actual per-Say word generation and four scans are preserved. This is the original fixed-mode variant; variable-size ADTs and separately owned string payloads are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ native nominal union / inline text (LLVM/Clang) | 39.45 | 15.28 | 1.000x |
| small / 500000 | C++ virtual objects (msvc) | 93.77 | 28.05 | 2.377x |
| small / 500000 | C++ variant / owned string (msvc) | 81.88 | 48.68 | 2.076x |
| small / 500000 | C++ variant / inline text (msvc) | 47.33 | 26.04 | 1.200x |
| small / 500000 | C++ virtual objects (clangcl) | 91.33 | 28.07 | 2.315x |
| small / 500000 | C++ variant / owned string (clangcl) | 73.56 | 48.70 | 1.865x |
| small / 500000 | C++ variant / inline text (clangcl) | 48.28 | 26.05 | 1.224x |
| small / 500000 | Rust records | 67.94 | 22.62 | 1.722x |
| medium / 4000000 | R^ native nominal union / inline text (LLVM/Clang) | 201.13 | 95.34 | 1.000x |
| medium / 4000000 | C++ virtual objects (msvc) | 654.86 | 187.05 | 3.256x |
| medium / 4000000 | C++ variant / owned string (msvc) | 536.26 | 330.36 | 2.666x |
| medium / 4000000 | C++ variant / inline text (msvc) | 264.03 | 166.88 | 1.313x |
| medium / 4000000 | C++ virtual objects (clangcl) | 628.48 | 187.06 | 3.125x |
| medium / 4000000 | C++ variant / owned string (clangcl) | 465.94 | 330.38 | 2.317x |
| medium / 4000000 | C++ variant / inline text (clangcl) | 266.14 | 166.88 | 1.323x |
| medium / 4000000 | Rust records | 392.62 | 150.69 | 1.952x |
| large / 16000000 | R^ native nominal union / inline text (LLVM/Clang) | 771.30 | 370.02 | 1.000x |
| large / 16000000 | C++ virtual objects (msvc) | 2600.28 | 709.24 | 3.371x |
| large / 16000000 | C++ variant / owned string (msvc) | 1938.45 | 1100.81 | 2.513x |
| large / 16000000 | C++ variant / inline text (msvc) | 971.31 | 552.10 | 1.259x |
| large / 16000000 | C++ virtual objects (clangcl) | 2462.67 | 709.27 | 3.193x |
| large / 16000000 | C++ variant / owned string (clangcl) | 1687.54 | 1100.82 | 2.188x |
| large / 16000000 | C++ variant / inline text (clangcl) | 1003.76 | 552.11 | 1.301x |
| large / 16000000 | Rust records | 1561.44 | 590.05 | 2.024x |

## interp

R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 child indices, matching the C++ switch / Rust arena representation. The generated tag tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, postorder construction, seeds, depths and eight pass-dependent evaluations modulo 1000003 are preserved. Goose's variable-size nodes and relative references are not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ native nominal union / u32 arena (LLVM/Clang) | 18.74 | 4.22 | 1.000x |
| small / 16 | C++ virtual owning tree (msvc) | 19.90 | 4.88 | 1.062x |
| small / 16 | C++ variant arena (msvc) | 18.06 | 4.77 | 0.964x |
| small / 16 | C++ switch arena (msvc) | 18.43 | 4.77 | 0.983x |
| small / 16 | C++ virtual owning tree (clangcl) | 21.71 | 4.88 | 1.158x |
| small / 16 | C++ variant arena (clangcl) | 21.15 | 4.77 | 1.128x |
| small / 16 | C++ switch arena (clangcl) | 19.13 | 4.77 | 1.021x |
| small / 16 | Rust interp_box | 23.39 | 5.31 | 1.248x |
| small / 16 | Rust interp_arena | 20.62 | 5.58 | 1.100x |
| medium / 20 | R^ native nominal union / u32 arena (LLVM/Clang) | 31.62 | 7.22 | 1.000x |
| medium / 20 | C++ virtual owning tree (msvc) | 68.80 | 12.88 | 2.176x |
| medium / 20 | C++ variant arena (msvc) | 31.16 | 10.22 | 0.986x |
| medium / 20 | C++ switch arena (msvc) | 29.46 | 10.23 | 0.932x |
| medium / 20 | C++ virtual owning tree (clangcl) | 66.69 | 12.88 | 2.109x |
| medium / 20 | C++ variant arena (clangcl) | 33.39 | 10.23 | 1.056x |
| medium / 20 | C++ switch arena (clangcl) | 32.45 | 10.23 | 1.026x |
| medium / 20 | Rust interp_box | 76.33 | 13.32 | 2.414x |
| medium / 20 | Rust interp_arena | 32.66 | 11.68 | 1.033x |
| large / 24 | R^ native nominal union / u32 arena (LLVM/Clang) | 218.11 | 39.34 | 1.000x |
| large / 24 | C++ virtual owning tree (msvc) | 538.90 | 99.16 | 2.471x |
| large / 24 | C++ variant arena (msvc) | 198.46 | 59.55 | 0.910x |
| large / 24 | C++ switch arena (msvc) | 188.92 | 59.55 | 0.866x |
| large / 24 | C++ virtual owning tree (clangcl) | 561.93 | 99.16 | 2.576x |
| large / 24 | C++ variant arena (clangcl) | 235.62 | 59.55 | 1.080x |
| large / 24 | C++ switch arena (clangcl) | 202.88 | 59.55 | 0.930x |
| large / 24 | Rust interp_box | 660.29 | 99.59 | 3.027x |
| large / 24 | Rust interp_arena | 216.96 | 53.69 | 0.995x |

## respond

R^ ports the original respond_stream.goose variant, generating the same requests and JSON bytes, including real quote/backslash escaping. The output byte region is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, item counts, quantities, prices and all three checksums are preserved. The DTO-building Goose variant and general nested owned strings/arrays are not implemented by this port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 75.05 | 4.18 | 1.000x |
| small / 100000 | C++ response DTO (msvc) | 183.45 | 4.27 | 2.444x |
| small / 100000 | C++ streaming JSON (msvc) | 81.39 | 4.18 | 1.084x |
| small / 100000 | C++ response DTO (clangcl) | 152.59 | 4.27 | 2.033x |
| small / 100000 | C++ streaming JSON (clangcl) | 64.90 | 4.20 | 0.865x |
| small / 100000 | Rust respond_dto | 217.03 | 4.73 | 2.892x |
| small / 100000 | Rust respond_stream | 95.47 | 4.68 | 1.272x |
| medium / 400000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 251.92 | 4.18 | 1.000x |
| medium / 400000 | C++ response DTO (msvc) | 668.51 | 4.27 | 2.654x |
| medium / 400000 | C++ streaming JSON (msvc) | 274.72 | 4.18 | 1.090x |
| medium / 400000 | C++ response DTO (clangcl) | 567.28 | 4.27 | 2.252x |
| medium / 400000 | C++ streaming JSON (clangcl) | 213.13 | 4.20 | 0.846x |
| medium / 400000 | Rust respond_dto | 800.79 | 4.74 | 3.179x |
| medium / 400000 | Rust respond_stream | 333.48 | 4.69 | 1.324x |
| large / 1600000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 985.49 | 4.18 | 1.000x |
| large / 1600000 | C++ response DTO (msvc) | 2639.41 | 4.27 | 2.678x |
| large / 1600000 | C++ streaming JSON (msvc) | 1041.87 | 4.18 | 1.057x |
| large / 1600000 | C++ response DTO (clangcl) | 2225.87 | 4.27 | 2.259x |
| large / 1600000 | C++ streaming JSON (clangcl) | 808.82 | 4.20 | 0.821x |
| large / 1600000 | Rust respond_dto | 3198.78 | 4.73 | 3.246x |
| large / 1600000 | Rust respond_stream | 1303.92 | 4.69 | 1.323x |

## words

R^ stores vocabulary entries as 21-byte fixed-capacity words and copies split words into one owned packed byte region (one-byte varint lengths). The open-addressed table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, skewed word selection, RNG sequence, text construction, split and all three checksums are preserved. Packed strings are a library representation; general variable-size array elements and a built-in dictionary are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 65.74 | 19.97 | 1.000x |
| small / 500000 | C++ owned-key unordered_map (msvc) | 78.26 | 24.98 | 1.190x |
| small / 500000 | C++ borrowed-key unordered_map (msvc) | 82.23 | 25.02 | 1.251x |
| small / 500000 | C++ open table (msvc) | 72.10 | 25.02 | 1.097x |
| small / 500000 | C++ owned-key unordered_map (clangcl) | 74.25 | 25.02 | 1.129x |
| small / 500000 | C++ borrowed-key unordered_map (clangcl) | 88.75 | 25.00 | 1.350x |
| small / 500000 | C++ open table (clangcl) | 75.40 | 25.00 | 1.147x |
| small / 500000 | Rust words_hashmap | 77.67 | 22.43 | 1.181x |
| small / 500000 | Rust words_open | 68.37 | 25.20 | 1.040x |
| medium / 4000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 801.89 | 132.47 | 1.000x |
| medium / 4000000 | C++ owned-key unordered_map (msvc) | 1244.25 | 161.33 | 1.552x |
| medium / 4000000 | C++ borrowed-key unordered_map (msvc) | 1240.89 | 161.34 | 1.547x |
| medium / 4000000 | C++ open table (msvc) | 864.84 | 162.31 | 1.079x |
| medium / 4000000 | C++ owned-key unordered_map (clangcl) | 1025.76 | 161.33 | 1.279x |
| medium / 4000000 | C++ borrowed-key unordered_map (clangcl) | 1126.12 | 161.33 | 1.404x |
| medium / 4000000 | C++ open table (clangcl) | 776.61 | 162.29 | 0.968x |
| medium / 4000000 | Rust words_hashmap | 1077.54 | 142.16 | 1.344x |
| medium / 4000000 | Rust words_open | 875.60 | 172.06 | 1.092x |
| large / 16000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 4037.56 | 517.84 | 1.000x |
| large / 16000000 | C++ owned-key unordered_map (msvc) | 6187.41 | 563.45 | 1.532x |
| large / 16000000 | C++ borrowed-key unordered_map (msvc) | 6092.03 | 563.47 | 1.509x |
| large / 16000000 | C++ open table (msvc) | 3964.35 | 634.70 | 0.982x |
| large / 16000000 | C++ owned-key unordered_map (clangcl) | 5086.58 | 563.45 | 1.260x |
| large / 16000000 | C++ borrowed-key unordered_map (clangcl) | 5733.79 | 563.45 | 1.420x |
| large / 16000000 | C++ open table (clangcl) | 3611.84 | 634.68 | 0.895x |
| large / 16000000 | Rust words_hashmap | 6241.21 | 555.74 | 1.546x |
| large / 16000000 | Rust words_open | 3959.51 | 672.62 | 0.981x |

## strlist

R^ copies each split word into a single owned byte region, prefixed by its one-byte varint length (original generated lengths are 3..20). The original text and copied word list coexist; scans decode the packed lengths four times. A WordList library struct holds the count and owned data region. This does not claim general variable-size array elements or nested owned strings in the language. No word is replaced by a borrowed view or offset pair. All original sizes, seed sequence, text splitting, copies, passes and two checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 200000 | R^ owned packed word list (LLVM/Clang) | 30.67 | 8.66 | 1.000x |
| small / 200000 | C++ owned strings (msvc) | 50.46 | 16.45 | 1.645x |
| small / 200000 | C++ borrowed string views (msvc) | 35.29 | 11.31 | 1.150x |
| small / 200000 | C++ flat offsets (msvc) | 33.77 | 9.32 | 1.101x |
| small / 200000 | C++ owned strings (clangcl) | 44.05 | 16.46 | 1.436x |
| small / 200000 | C++ borrowed string views (clangcl) | 31.51 | 11.31 | 1.027x |
| small / 200000 | C++ flat offsets (clangcl) | 29.65 | 9.34 | 0.967x |
| small / 200000 | Rust strlist_owned | 51.58 | 16.43 | 1.682x |
| small / 200000 | Rust strlist_borrowed | 32.91 | 11.34 | 1.073x |
| medium / 2000000 | R^ owned packed word list (LLVM/Clang) | 166.45 | 51.54 | 1.000x |
| medium / 2000000 | C++ owned strings (msvc) | 375.44 | 144.01 | 2.256x |
| medium / 2000000 | C++ borrowed string views (msvc) | 202.30 | 76.61 | 1.215x |
| medium / 2000000 | C++ flat offsets (msvc) | 193.41 | 55.47 | 1.162x |
| medium / 2000000 | C++ owned strings (clangcl) | 328.00 | 144.04 | 1.971x |
| medium / 2000000 | C++ borrowed string views (clangcl) | 167.34 | 76.60 | 1.005x |
| medium / 2000000 | C++ flat offsets (clangcl) | 152.53 | 55.44 | 0.916x |
| medium / 2000000 | Rust strlist_owned | 369.73 | 125.24 | 2.221x |
| medium / 2000000 | Rust strlist_borrowed | 183.76 | 60.82 | 1.104x |
| large / 8000000 | R^ owned packed word list (LLVM/Clang) | 627.11 | 194.54 | 1.000x |
| large / 8000000 | C++ owned strings (msvc) | 1549.17 | 687.74 | 2.470x |
| large / 8000000 | C++ borrowed string views (msvc) | 768.32 | 343.36 | 1.225x |
| large / 8000000 | C++ flat offsets (msvc) | 706.60 | 191.15 | 1.127x |
| large / 8000000 | C++ owned strings (clangcl) | 1311.27 | 687.71 | 2.091x |
| large / 8000000 | C++ borrowed string views (clangcl) | 649.99 | 343.36 | 1.036x |
| large / 8000000 | C++ flat offsets (clangcl) | 558.79 | 191.17 | 0.891x |
| large / 8000000 | Rust strlist_owned | 1369.25 | 487.80 | 2.183x |
| large / 8000000 | Rust strlist_borrowed | 643.42 | 228.35 | 1.026x |

## scene

R^ uses 120-byte nodes with inline local/world transforms, four u32 child indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and f32 matrix/translation arithmetic are preserved. Composition writes through references to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array storage; Goose's 117-byte limited-array/relative-reference layout is not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 12 | R^ inline u32-child arena (LLVM/Clang) | 33.90 | 8.46 | 1.000x |
| small / 12 | C++ owning-pointer tree (msvc) | 68.00 | 10.11 | 2.006x |
| small / 12 | C++ index arena (msvc) | 35.35 | 10.91 | 1.043x |
| small / 12 | C++ owning-pointer tree (clangcl) | 50.95 | 10.11 | 1.503x |
| small / 12 | C++ index arena (clangcl) | 34.48 | 10.91 | 1.017x |
| small / 12 | Rust scene_box | 79.62 | 10.82 | 2.348x |
| small / 12 | Rust scene_arena | 34.10 | 13.55 | 1.006x |
| medium / 14 | R^ inline u32-child arena (LLVM/Clang) | 154.10 | 32.90 | 1.000x |
| medium / 14 | C++ owning-pointer tree (msvc) | 491.70 | 41.99 | 3.191x |
| medium / 14 | C++ index arena (msvc) | 190.12 | 52.12 | 1.234x |
| medium / 14 | C++ owning-pointer tree (clangcl) | 487.53 | 42.00 | 3.164x |
| medium / 14 | C++ index arena (clangcl) | 167.55 | 52.12 | 1.087x |
| medium / 14 | Rust scene_box | 504.88 | 43.96 | 3.276x |
| medium / 14 | Rust scene_arena | 159.57 | 36.08 | 1.035x |
| large / 16 | R^ inline u32-child arena (LLVM/Clang) | 862.97 | 185.90 | 1.000x |
| large / 16 | C++ owning-pointer tree (msvc) | 3066.77 | 242.62 | 3.554x |
| large / 16 | C++ index arena (msvc) | 1044.25 | 365.10 | 1.210x |
| large / 16 | C++ owning-pointer tree (clangcl) | 2995.69 | 242.62 | 3.471x |
| large / 16 | C++ index arena (clangcl) | 963.09 | 365.10 | 1.116x |
| large / 16 | Rust scene_box | 3198.22 | 252.75 | 3.706x |
| large / 16 | Rust scene_arena | 859.22 | 246.07 | 0.996x |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays use statically assigned stable 1 GiB data stacks and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 205.24 | 11.66 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 687.64 | 27.05 | 3.350x |
| small / 2000000 | C++ index arena / open table (msvc) | 159.79 | 15.65 | 0.779x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 642.85 | 27.05 | 3.132x |
| small / 2000000 | C++ index arena / open table (clangcl) | 158.84 | 15.64 | 0.774x |
| small / 2000000 | Rust lru_hashmap | 202.85 | 16.22 | 0.988x |
| small / 2000000 | Rust lru_open | 183.76 | 13.20 | 0.895x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 778.35 | 35.10 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3307.12 | 96.62 | 4.249x |
| medium / 8000000 | C++ index arena / open table (msvc) | 1035.54 | 41.11 | 1.330x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 3127.93 | 96.62 | 4.019x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 842.28 | 41.11 | 1.082x |
| medium / 8000000 | Rust lru_hashmap | 1462.11 | 46.88 | 1.878x |
| medium / 8000000 | Rust lru_open | 905.08 | 37.17 | 1.163x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 4039.52 | 128.85 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 16526.76 | 416.30 | 4.091x |
| large / 32000000 | C++ index arena / open table (msvc) | 5064.45 | 175.88 | 1.254x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 15032.73 | 416.23 | 3.721x |
| large / 32000000 | C++ index arena / open table (clangcl) | 4512.57 | 175.88 | 1.117x |
| large / 32000000 | Rust lru_hashmap | 9117.27 | 169.18 | 2.257x |
| large / 32000000 | Rust lru_open | 4870.54 | 133.20 | 1.206x |

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 22.56 | 11.59 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 32.55 | 14.66 | 1.443x |
| small / 1000000 | C++ reserved vector indices (msvc) | 23.39 | 11.73 | 1.037x |
| small / 1000000 | C++ deque pointers (msvc) | 61.55 | 23.55 | 2.728x |
| small / 1000000 | C++ vector indices (clangcl) | 31.57 | 14.65 | 1.399x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 27.41 | 11.74 | 1.215x |
| small / 1000000 | C++ deque pointers (clangcl) | 60.37 | 23.56 | 2.675x |
| small / 1000000 | Rust push | 23.34 | 12.04 | 1.034x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 96.76 | 127.85 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 233.77 | 189.19 | 2.416x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 103.84 | 129.54 | 1.073x |
| medium / 16000000 | C++ deque pointers (msvc) | 740.43 | 315.93 | 7.652x |
| medium / 16000000 | C++ vector indices (clangcl) | 237.37 | 189.19 | 2.453x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 167.66 | 129.53 | 1.733x |
| medium / 16000000 | C++ deque pointers (clangcl) | 729.39 | 315.96 | 7.538x |
| medium / 16000000 | Rust push | 102.07 | 129.20 | 1.055x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 336.25 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 946.26 | 936.31 | 2.814x |
| large / 64000000 | C++ reserved vector indices (msvc) | 371.03 | 501.49 | 1.103x |
| large / 64000000 | C++ deque pointers (msvc) | 2994.87 | 1251.09 | 8.907x |
| large / 64000000 | C++ vector indices (clangcl) | 961.85 | 936.28 | 2.861x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 621.82 | 501.48 | 1.849x |
| large / 64000000 | C++ deque pointers (clangcl) | 2991.53 | 1251.09 | 8.897x |
| large / 64000000 | Rust push | 338.85 | 501.14 | 1.008x |

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 22.41 | 7.66 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 25.40 | 11.03 | 1.133x |
| small / 1000000 | C++ reserved vector (msvc) | 23.78 | 7.59 | 1.061x |
| small / 1000000 | C++ vector (clangcl) | 24.62 | 11.03 | 1.099x |
| small / 1000000 | C++ reserved vector (clangcl) | 21.84 | 7.59 | 0.975x |
| small / 1000000 | Rust sum | 23.09 | 8.03 | 1.031x |
| medium / 16000000 | R^ LLVM/Clang | 104.37 | 64.84 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 141.04 | 96.93 | 1.351x |
| medium / 16000000 | C++ reserved vector (msvc) | 106.92 | 64.81 | 1.024x |
| medium / 16000000 | C++ vector (clangcl) | 146.77 | 96.93 | 1.406x |
| medium / 16000000 | C++ reserved vector (clangcl) | 123.84 | 64.82 | 1.187x |
| medium / 16000000 | Rust sum | 98.08 | 65.25 | 0.940x |
| large / 128000000 | R^ LLVM/Clang | 735.69 | 492.09 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 943.65 | 698.52 | 1.283x |
| large / 128000000 | C++ reserved vector (msvc) | 721.00 | 492.06 | 0.980x |
| large / 128000000 | C++ vector (clangcl) | 1116.09 | 698.52 | 1.517x |
| large / 128000000 | C++ reserved vector (clangcl) | 860.78 | 492.06 | 1.170x |
| large / 128000000 | Rust sum | 675.37 | 492.50 | 0.918x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 24.62 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 63.25 | 6.44 | 2.569x |
| small / 1024 | C++ row pointers (msvc) | 35.14 | 5.77 | 1.427x |
| small / 1024 | C++ flat (clangcl) | 67.43 | 6.44 | 2.739x |
| small / 1024 | C++ row pointers (clangcl) | 29.27 | 5.79 | 1.189x |
| small / 1024 | Rust blur_index | 28.86 | 7.41 | 1.172x |
| small / 1024 | Rust blur_windows | 31.32 | 7.40 | 1.272x |
| medium / 2048 | R^ LLVM/Clang | 47.77 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 215.70 | 14.56 | 4.516x |
| medium / 2048 | C++ row pointers (msvc) | 87.32 | 11.77 | 1.828x |
| medium / 2048 | C++ flat (clangcl) | 219.44 | 14.56 | 4.594x |
| medium / 2048 | C++ row pointers (clangcl) | 53.80 | 11.79 | 1.126x |
| medium / 2048 | Rust blur_index | 62.28 | 13.41 | 1.304x |
| medium / 2048 | Rust blur_windows | 58.18 | 13.41 | 1.218x |
| large / 8192 | R^ LLVM/Clang | 484.51 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3130.90 | 177.63 | 6.462x |
| large / 8192 | C++ row pointers (msvc) | 1161.92 | 131.77 | 2.398x |
| large / 8192 | C++ flat (clangcl) | 3493.05 | 177.62 | 7.209x |
| large / 8192 | C++ row pointers (clangcl) | 608.48 | 131.79 | 1.256x |
| large / 8192 | Rust blur_index | 673.83 | 133.40 | 1.391x |
| large / 8192 | Rust blur_windows | 665.80 | 133.40 | 1.374x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 45.98 | 6.09 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 53.33 | 6.06 | 1.160x |
| small / 100000 | C++ SoA (msvc) | 148.93 | 6.08 | 3.239x |
| small / 100000 | C++ vector AoS (msvc) | 62.70 | 6.07 | 1.364x |
| small / 100000 | C++ scalar AoS (clangcl) | 46.69 | 6.07 | 1.016x |
| small / 100000 | C++ SoA (clangcl) | 76.86 | 6.07 | 1.672x |
| small / 100000 | C++ vector AoS (clangcl) | 54.85 | 6.07 | 1.193x |
| small / 100000 | Rust particles_scalar | 48.29 | 6.51 | 1.050x |
| small / 100000 | Rust particles | 52.18 | 6.51 | 1.135x |
| medium / 1000000 | R^ LLVM/Clang | 672.21 | 26.72 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 715.83 | 26.66 | 1.065x |
| medium / 1000000 | C++ SoA (msvc) | 2782.83 | 26.67 | 4.140x |
| medium / 1000000 | C++ vector AoS (msvc) | 720.07 | 26.66 | 1.071x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 654.47 | 26.67 | 0.974x |
| medium / 1000000 | C++ SoA (clangcl) | 2897.91 | 26.67 | 4.311x |
| medium / 1000000 | C++ vector AoS (clangcl) | 665.43 | 26.66 | 0.990x |
| medium / 1000000 | Rust particles_scalar | 695.59 | 27.11 | 1.035x |
| medium / 1000000 | Rust particles | 697.87 | 27.11 | 1.038x |
| large / 4000000 | R^ LLVM/Clang | 2674.17 | 95.34 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2688.21 | 95.33 | 1.005x |
| large / 4000000 | C++ SoA (msvc) | 12489.63 | 95.34 | 4.670x |
| large / 4000000 | C++ vector AoS (msvc) | 2927.29 | 95.33 | 1.095x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2643.26 | 95.33 | 0.988x |
| large / 4000000 | C++ SoA (clangcl) | 11213.73 | 95.34 | 4.193x |
| large / 4000000 | C++ vector AoS (clangcl) | 2636.41 | 95.33 | 0.986x |
| large / 4000000 | Rust particles_scalar | 2613.18 | 95.77 | 0.977x |
| large / 4000000 | Rust particles | 2673.16 | 95.77 | 1.000x |

## tree

R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ u32-index node pool (LLVM/Clang) | 21.54 | 5.28 | 1.000x |
| small / 16 | C++ unique_ptr (msvc) | 29.41 | 7.81 | 1.366x |
| small / 16 | C++ new/delete (msvc) | 28.63 | 7.79 | 1.329x |
| small / 16 | C++ index arena (msvc) | 20.47 | 5.27 | 0.951x |
| small / 16 | C++ unique_ptr (clangcl) | 29.19 | 7.81 | 1.355x |
| small / 16 | C++ new/delete (clangcl) | 28.19 | 7.79 | 1.309x |
| small / 16 | C++ index arena (clangcl) | 20.91 | 5.28 | 0.971x |
| small / 16 | Rust tree_box | 31.60 | 8.25 | 1.467x |
| small / 16 | Rust tree_arena | 21.87 | 5.72 | 1.016x |
| medium / 20 | R^ u32-index node pool (LLVM/Clang) | 94.15 | 27.78 | 1.000x |
| medium / 20 | C++ unique_ptr (msvc) | 318.46 | 68.25 | 3.383x |
| medium / 20 | C++ new/delete (msvc) | 311.34 | 68.22 | 3.307x |
| medium / 20 | C++ index arena (msvc) | 119.47 | 27.77 | 1.269x |
| medium / 20 | C++ unique_ptr (clangcl) | 293.77 | 68.25 | 3.120x |
| medium / 20 | C++ new/delete (clangcl) | 279.70 | 68.22 | 2.971x |
| medium / 20 | C++ index arena (clangcl) | 117.68 | 27.78 | 1.250x |
| medium / 20 | Rust tree_box | 306.79 | 68.68 | 3.259x |
| medium / 20 | Rust tree_arena | 101.16 | 28.22 | 1.074x |
| large / 24 | R^ u32-index node pool (LLVM/Clang) | 1298.97 | 387.78 | 1.000x |
| large / 24 | C++ unique_ptr (msvc) | 5012.81 | 1034.96 | 3.859x |
| large / 24 | C++ new/delete (msvc) | 5135.45 | 1034.93 | 3.953x |
| large / 24 | C++ index arena (msvc) | 1726.30 | 387.77 | 1.329x |
| large / 24 | C++ unique_ptr (clangcl) | 5362.05 | 1034.96 | 4.128x |
| large / 24 | C++ new/delete (clangcl) | 5163.17 | 1034.92 | 3.975x |
| large / 24 | C++ index arena (clangcl) | 1699.25 | 387.78 | 1.308x |
| large / 24 | Rust tree_box | 5248.70 | 1035.38 | 4.041x |
| large / 24 | Rust tree_arena | 1369.25 | 388.22 | 1.054x |

## graph

R^ uses the original two-pass CSR count/prefix/fill representation, matching C++ VARIANT 1 and Rust CSR. Source and destination arrays, start offsets, fill cursors and contiguous edges retain all construction cost. The previous linked index implementation is preserved in rhat/variants/graph_linked.rh. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 47.40 | 14.72 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 153.07 | 13.57 | 3.229x |
| small / 100000 | C++ CSR (msvc) | 50.42 | 14.47 | 1.064x |
| small / 100000 | C++ linked index arena (msvc) | 130.45 | 11.41 | 2.752x |
| small / 100000 | C++ nested vectors (clangcl) | 145.90 | 13.57 | 3.078x |
| small / 100000 | C++ CSR (clangcl) | 44.48 | 14.48 | 0.938x |
| small / 100000 | C++ linked index arena (clangcl) | 132.37 | 11.41 | 2.793x |
| small / 100000 | Rust graph_nested | 112.47 | 15.66 | 2.373x |
| small / 100000 | Rust graph_csr | 53.00 | 14.93 | 1.118x |
| small / 100000 | Rust graph_arena | 212.36 | 11.86 | 4.480x |
| medium / 500000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 322.55 | 57.47 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1342.62 | 52.44 | 4.162x |
| medium / 500000 | C++ CSR (msvc) | 357.38 | 57.25 | 1.108x |
| medium / 500000 | C++ linked index arena (msvc) | 1506.71 | 41.93 | 4.671x |
| medium / 500000 | C++ nested vectors (clangcl) | 1292.14 | 52.45 | 4.006x |
| medium / 500000 | C++ CSR (clangcl) | 363.63 | 57.21 | 1.127x |
| medium / 500000 | C++ linked index arena (clangcl) | 1307.24 | 41.94 | 4.053x |
| medium / 500000 | Rust graph_nested | 816.23 | 60.94 | 2.531x |
| medium / 500000 | Rust graph_csr | 467.49 | 57.65 | 1.449x |
| medium / 500000 | Rust graph_arena | 1400.90 | 42.38 | 4.343x |
| large / 2000000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 2194.92 | 217.72 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 6833.65 | 207.09 | 3.113x |
| large / 2000000 | C++ CSR (msvc) | 2327.23 | 217.41 | 1.060x |
| large / 2000000 | C++ linked index arena (msvc) | 8157.88 | 156.37 | 3.717x |
| large / 2000000 | C++ nested vectors (clangcl) | 6679.71 | 207.10 | 3.043x |
| large / 2000000 | C++ CSR (clangcl) | 2435.82 | 217.43 | 1.110x |
| large / 2000000 | C++ linked index arena (clangcl) | 6928.13 | 156.38 | 3.156x |
| large / 2000000 | Rust graph_nested | 4338.17 | 244.96 | 1.976x |
| large / 2000000 | Rust graph_csr | 2260.79 | 217.87 | 1.030x |
| large / 2000000 | Rust graph_arena | 7288.32 | 156.82 | 3.321x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../../results.md).
