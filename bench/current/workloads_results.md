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

## respond

R^ ports the original respond_stream.goose variant, generating the same requests and JSON bytes, including real quote/backslash escaping. The output byte region is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, item counts, quantities, prices and all three checksums are preserved. The DTO-building Goose variant and general nested owned strings/arrays are not implemented by this port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 85.04 | 4.19 | 1.000x |
| small / 100000 | C++ response DTO (msvc) | 188.63 | 4.26 | 2.218x |
| small / 100000 | C++ streaming JSON (msvc) | 79.36 | 4.18 | 0.933x |
| small / 100000 | C++ response DTO (clangcl) | 154.12 | 4.27 | 1.812x |
| small / 100000 | C++ streaming JSON (clangcl) | 66.05 | 4.20 | 0.777x |
| small / 100000 | Rust respond_dto | 214.18 | 4.73 | 2.518x |
| small / 100000 | Rust respond_stream | 99.34 | 4.69 | 1.168x |
| medium / 400000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 290.21 | 4.19 | 1.000x |
| medium / 400000 | C++ response DTO (msvc) | 695.09 | 4.27 | 2.395x |
| medium / 400000 | C++ streaming JSON (msvc) | 280.65 | 4.18 | 0.967x |
| medium / 400000 | C++ response DTO (clangcl) | 589.14 | 4.27 | 2.030x |
| medium / 400000 | C++ streaming JSON (clangcl) | 213.02 | 4.20 | 0.734x |
| medium / 400000 | Rust respond_dto | 815.76 | 4.73 | 2.811x |
| medium / 400000 | Rust respond_stream | 340.87 | 4.69 | 1.175x |
| large / 1600000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 1102.93 | 4.19 | 1.000x |
| large / 1600000 | C++ response DTO (msvc) | 2778.18 | 4.27 | 2.519x |
| large / 1600000 | C++ streaming JSON (msvc) | 1059.40 | 4.18 | 0.961x |
| large / 1600000 | C++ response DTO (clangcl) | 2293.14 | 4.27 | 2.079x |
| large / 1600000 | C++ streaming JSON (clangcl) | 824.20 | 4.20 | 0.747x |
| large / 1600000 | Rust respond_dto | 3236.88 | 4.73 | 2.935x |
| large / 1600000 | Rust respond_stream | 1324.45 | 4.69 | 1.201x |

## words

R^ stores vocabulary entries as 21-byte fixed-capacity words and copies split words into one owned packed byte region (one-byte varint lengths). The open-addressed table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, skewed word selection, RNG sequence, text construction, split and all three checksums are preserved. Packed strings are a library representation; general variable-size array elements and a built-in dictionary are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 71.60 | 19.97 | 1.000x |
| small / 500000 | C++ owned-key unordered_map (msvc) | 83.14 | 24.97 | 1.161x |
| small / 500000 | C++ borrowed-key unordered_map (msvc) | 88.63 | 24.99 | 1.238x |
| small / 500000 | C++ open table (msvc) | 83.13 | 25.00 | 1.161x |
| small / 500000 | C++ owned-key unordered_map (clangcl) | 86.49 | 24.98 | 1.208x |
| small / 500000 | C++ borrowed-key unordered_map (clangcl) | 87.75 | 24.96 | 1.225x |
| small / 500000 | C++ open table (clangcl) | 80.20 | 24.95 | 1.120x |
| small / 500000 | Rust words_hashmap | 82.11 | 21.64 | 1.147x |
| small / 500000 | Rust words_open | 74.54 | 25.18 | 1.041x |
| medium / 4000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 821.55 | 132.42 | 1.000x |
| medium / 4000000 | C++ owned-key unordered_map (msvc) | 1360.13 | 161.30 | 1.656x |
| medium / 4000000 | C++ borrowed-key unordered_map (msvc) | 1272.26 | 161.30 | 1.549x |
| medium / 4000000 | C++ open table (msvc) | 861.71 | 162.30 | 1.049x |
| medium / 4000000 | C++ owned-key unordered_map (clangcl) | 1150.01 | 161.32 | 1.400x |
| medium / 4000000 | C++ borrowed-key unordered_map (clangcl) | 1199.18 | 161.29 | 1.460x |
| medium / 4000000 | C++ open table (clangcl) | 761.67 | 162.27 | 0.927x |
| medium / 4000000 | Rust words_hashmap | 1256.91 | 142.15 | 1.530x |
| medium / 4000000 | Rust words_open | 843.63 | 172.06 | 1.027x |
| large / 16000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 4080.16 | 517.85 | 1.000x |
| large / 16000000 | C++ owned-key unordered_map (msvc) | 6197.68 | 563.41 | 1.519x |
| large / 16000000 | C++ borrowed-key unordered_map (msvc) | 6259.94 | 563.43 | 1.534x |
| large / 16000000 | C++ open table (msvc) | 4116.15 | 634.65 | 1.009x |
| large / 16000000 | C++ owned-key unordered_map (clangcl) | 5237.19 | 563.44 | 1.284x |
| large / 16000000 | C++ borrowed-key unordered_map (clangcl) | 6020.59 | 563.41 | 1.476x |
| large / 16000000 | C++ open table (clangcl) | 4003.41 | 634.67 | 0.981x |
| large / 16000000 | Rust words_hashmap | 6610.01 | 555.78 | 1.620x |
| large / 16000000 | Rust words_open | 4167.21 | 672.61 | 1.021x |

## strlist

R^ copies each split word into a single owned byte region, prefixed by its one-byte varint length (original generated lengths are 3..20). The original text and copied word list coexist; scans decode the packed lengths four times. A WordList library struct holds the count and owned data region. This does not claim general variable-size array elements or nested owned strings in the language. No word is replaced by a borrowed view or offset pair. All original sizes, seed sequence, text splitting, copies, passes and two checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 200000 | R^ owned packed word list (LLVM/Clang) | 33.08 | 8.66 | 1.000x |
| small / 200000 | C++ owned strings (msvc) | 54.10 | 16.47 | 1.636x |
| small / 200000 | C++ borrowed string views (msvc) | 36.40 | 11.27 | 1.100x |
| small / 200000 | C++ flat offsets (msvc) | 35.70 | 9.29 | 1.079x |
| small / 200000 | C++ owned strings (clangcl) | 47.15 | 16.46 | 1.425x |
| small / 200000 | C++ borrowed string views (clangcl) | 33.14 | 11.30 | 1.002x |
| small / 200000 | C++ flat offsets (clangcl) | 34.69 | 9.29 | 1.049x |
| small / 200000 | Rust strlist_owned | 57.50 | 16.46 | 1.738x |
| small / 200000 | Rust strlist_borrowed | 36.44 | 11.37 | 1.101x |
| medium / 2000000 | R^ owned packed word list (LLVM/Clang) | 176.69 | 51.54 | 1.000x |
| medium / 2000000 | C++ owned strings (msvc) | 385.88 | 144.04 | 2.184x |
| medium / 2000000 | C++ borrowed string views (msvc) | 201.52 | 76.59 | 1.141x |
| medium / 2000000 | C++ flat offsets (msvc) | 193.46 | 55.42 | 1.095x |
| medium / 2000000 | C++ owned strings (clangcl) | 323.83 | 144.03 | 1.833x |
| medium / 2000000 | C++ borrowed string views (clangcl) | 166.96 | 76.59 | 0.945x |
| medium / 2000000 | C++ flat offsets (clangcl) | 156.14 | 55.41 | 0.884x |
| medium / 2000000 | Rust strlist_owned | 362.41 | 125.25 | 2.051x |
| medium / 2000000 | Rust strlist_borrowed | 174.80 | 60.81 | 0.989x |
| large / 8000000 | R^ owned packed word list (LLVM/Clang) | 617.86 | 194.54 | 1.000x |
| large / 8000000 | C++ owned strings (msvc) | 1523.06 | 687.75 | 2.465x |
| large / 8000000 | C++ borrowed string views (msvc) | 768.44 | 343.30 | 1.244x |
| large / 8000000 | C++ flat offsets (msvc) | 715.43 | 191.11 | 1.158x |
| large / 8000000 | C++ owned strings (clangcl) | 1315.85 | 687.74 | 2.130x |
| large / 8000000 | C++ borrowed string views (clangcl) | 651.12 | 343.33 | 1.054x |
| large / 8000000 | C++ flat offsets (clangcl) | 570.44 | 191.12 | 0.923x |
| large / 8000000 | Rust strlist_owned | 1458.95 | 487.76 | 2.361x |
| large / 8000000 | Rust strlist_borrowed | 638.77 | 228.31 | 1.034x |

## scene

R^ uses 120-byte nodes with inline local/world transforms, four u32 child indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and f32 matrix/translation arithmetic are preserved. Composition writes through references to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array storage; Goose's 117-byte limited-array/relative-reference layout is not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 12 | R^ inline u32-child arena (LLVM/Clang) | 56.46 | 8.46 | 1.000x |
| small / 12 | C++ owning-pointer tree (msvc) | 63.91 | 10.11 | 1.132x |
| small / 12 | C++ index arena (msvc) | 36.53 | 10.87 | 0.647x |
| small / 12 | C++ owning-pointer tree (clangcl) | 58.90 | 10.12 | 1.043x |
| small / 12 | C++ index arena (clangcl) | 33.16 | 10.88 | 0.587x |
| small / 12 | Rust scene_box | 55.51 | 10.81 | 0.983x |
| small / 12 | Rust scene_arena | 33.35 | 13.57 | 0.591x |
| medium / 14 | R^ inline u32-child arena (LLVM/Clang) | 288.49 | 32.89 | 1.000x |
| medium / 14 | C++ owning-pointer tree (msvc) | 484.56 | 41.99 | 1.680x |
| medium / 14 | C++ index arena (msvc) | 181.69 | 52.08 | 0.630x |
| medium / 14 | C++ owning-pointer tree (clangcl) | 488.15 | 42.00 | 1.692x |
| medium / 14 | C++ index arena (clangcl) | 173.09 | 52.09 | 0.600x |
| medium / 14 | Rust scene_box | 501.74 | 43.96 | 1.739x |
| medium / 14 | Rust scene_arena | 162.06 | 36.07 | 0.562x |
| large / 16 | R^ inline u32-child arena (LLVM/Clang) | 1727.81 | 185.89 | 1.000x |
| large / 16 | C++ owning-pointer tree (msvc) | 3108.98 | 242.62 | 1.799x |
| large / 16 | C++ index arena (msvc) | 1037.51 | 365.06 | 0.600x |
| large / 16 | C++ owning-pointer tree (clangcl) | 2971.01 | 242.62 | 1.720x |
| large / 16 | C++ index arena (clangcl) | 975.82 | 365.06 | 0.565x |
| large / 16 | Rust scene_box | 3119.75 | 252.75 | 1.806x |
| large / 16 | Rust scene_arena | 905.10 | 246.06 | 0.524x |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays use statically assigned stable 1 GiB data stacks and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 292.33 | 11.66 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 683.78 | 27.05 | 2.339x |
| small / 2000000 | C++ index arena / open table (msvc) | 167.18 | 16.16 | 0.572x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 615.42 | 27.05 | 2.105x |
| small / 2000000 | C++ index arena / open table (clangcl) | 123.70 | 16.16 | 0.423x |
| small / 2000000 | Rust lru_hashmap | 191.65 | 16.22 | 0.656x |
| small / 2000000 | Rust lru_open | 167.10 | 13.19 | 0.572x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 1474.28 | 35.10 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3297.36 | 96.62 | 2.237x |
| medium / 8000000 | C++ index arena / open table (msvc) | 979.21 | 43.18 | 0.664x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 3094.40 | 96.62 | 2.099x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 842.89 | 43.17 | 0.572x |
| medium / 8000000 | Rust lru_hashmap | 1501.54 | 46.91 | 1.018x |
| medium / 8000000 | Rust lru_open | 1001.53 | 37.20 | 0.679x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 7507.82 | 128.85 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 14304.92 | 416.23 | 1.905x |
| large / 32000000 | C++ index arena / open table (msvc) | 4999.40 | 175.88 | 0.666x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 13829.98 | 416.30 | 1.842x |
| large / 32000000 | C++ index arena / open table (clangcl) | 4443.07 | 175.88 | 0.592x |
| large / 32000000 | Rust lru_hashmap | 7885.85 | 169.72 | 1.050x |
| large / 32000000 | Rust lru_open | 4817.90 | 133.18 | 0.642x |

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 23.50 | 11.60 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 31.85 | 14.53 | 1.355x |
| small / 1000000 | C++ reserved vector indices (msvc) | 22.93 | 11.51 | 0.976x |
| small / 1000000 | C++ deque pointers (msvc) | 67.26 | 23.50 | 2.862x |
| small / 1000000 | C++ vector indices (clangcl) | 33.86 | 14.52 | 1.441x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 27.47 | 11.51 | 1.169x |
| small / 1000000 | C++ deque pointers (clangcl) | 63.40 | 23.51 | 2.697x |
| small / 1000000 | Rust push | 24.15 | 12.04 | 1.028x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 103.14 | 127.84 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 242.42 | 189.38 | 2.350x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 112.10 | 129.52 | 1.087x |
| medium / 16000000 | C++ deque pointers (msvc) | 743.50 | 315.89 | 7.208x |
| medium / 16000000 | C++ vector indices (clangcl) | 240.00 | 189.38 | 2.327x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 167.51 | 129.49 | 1.624x |
| medium / 16000000 | C++ deque pointers (clangcl) | 740.46 | 315.88 | 7.179x |
| medium / 16000000 | Rust push | 100.86 | 129.20 | 0.978x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 348.55 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 943.13 | 936.46 | 2.706x |
| large / 64000000 | C++ reserved vector indices (msvc) | 364.51 | 501.43 | 1.046x |
| large / 64000000 | C++ deque pointers (msvc) | 3053.28 | 1251.09 | 8.760x |
| large / 64000000 | C++ vector indices (clangcl) | 988.25 | 936.46 | 2.835x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 635.52 | 501.46 | 1.823x |
| large / 64000000 | C++ deque pointers (clangcl) | 2998.11 | 1251.10 | 8.602x |
| large / 64000000 | Rust push | 341.10 | 501.13 | 0.979x |

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 21.63 | 7.65 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 23.53 | 11.00 | 1.088x |
| small / 1000000 | C++ reserved vector (msvc) | 21.32 | 7.59 | 0.986x |
| small / 1000000 | C++ vector (clangcl) | 24.05 | 11.00 | 1.112x |
| small / 1000000 | C++ reserved vector (clangcl) | 22.27 | 7.59 | 1.030x |
| small / 1000000 | Rust sum | 22.00 | 8.02 | 1.017x |
| medium / 16000000 | R^ LLVM/Clang | 100.91 | 64.84 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 133.77 | 96.89 | 1.326x |
| medium / 16000000 | C++ reserved vector (msvc) | 102.52 | 64.81 | 1.016x |
| medium / 16000000 | C++ vector (clangcl) | 149.18 | 96.89 | 1.478x |
| medium / 16000000 | C++ reserved vector (clangcl) | 127.50 | 64.82 | 1.263x |
| medium / 16000000 | Rust sum | 100.14 | 65.25 | 0.992x |
| large / 128000000 | R^ LLVM/Clang | 690.77 | 492.09 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 949.20 | 698.49 | 1.374x |
| large / 128000000 | C++ reserved vector (msvc) | 742.86 | 492.06 | 1.075x |
| large / 128000000 | C++ vector (clangcl) | 1117.13 | 698.49 | 1.617x |
| large / 128000000 | C++ reserved vector (clangcl) | 879.14 | 492.06 | 1.273x |
| large / 128000000 | Rust sum | 666.86 | 492.50 | 0.965x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 86.07 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 61.97 | 7.68 | 0.720x |
| small / 1024 | C++ row pointers (msvc) | 31.51 | 5.77 | 0.366x |
| small / 1024 | C++ flat (clangcl) | 64.32 | 7.68 | 0.747x |
| small / 1024 | C++ row pointers (clangcl) | 23.59 | 5.79 | 0.274x |
| small / 1024 | Rust blur_index | 26.19 | 7.41 | 0.304x |
| small / 1024 | Rust blur_windows | 27.24 | 7.41 | 0.316x |
| medium / 2048 | R^ LLVM/Clang | 315.37 | 11.77 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 204.88 | 15.81 | 0.650x |
| medium / 2048 | C++ row pointers (msvc) | 82.52 | 11.77 | 0.262x |
| medium / 2048 | C++ flat (clangcl) | 217.12 | 15.80 | 0.688x |
| medium / 2048 | C++ row pointers (clangcl) | 50.66 | 11.79 | 0.161x |
| medium / 2048 | Rust blur_index | 58.14 | 13.41 | 0.184x |
| medium / 2048 | Rust blur_windows | 57.00 | 13.41 | 0.181x |
| large / 8192 | R^ LLVM/Clang | 4939.43 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3212.73 | 178.88 | 0.650x |
| large / 8192 | C++ row pointers (msvc) | 1176.05 | 131.77 | 0.238x |
| large / 8192 | C++ flat (clangcl) | 3605.70 | 178.87 | 0.730x |
| large / 8192 | C++ row pointers (clangcl) | 584.01 | 131.79 | 0.118x |
| large / 8192 | Rust blur_index | 729.42 | 133.41 | 0.148x |
| large / 8192 | Rust blur_windows | 714.85 | 133.41 | 0.145x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 54.71 | 6.09 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 50.87 | 6.07 | 0.930x |
| small / 100000 | C++ SoA (msvc) | 146.02 | 6.08 | 2.669x |
| small / 100000 | C++ vector AoS (msvc) | 61.54 | 6.07 | 1.125x |
| small / 100000 | C++ scalar AoS (clangcl) | 45.07 | 6.07 | 0.824x |
| small / 100000 | C++ SoA (clangcl) | 83.77 | 6.07 | 1.531x |
| small / 100000 | C++ vector AoS (clangcl) | 50.31 | 6.07 | 0.920x |
| small / 100000 | Rust particles_scalar | 44.77 | 6.51 | 0.818x |
| small / 100000 | Rust particles | 49.02 | 6.51 | 0.896x |
| medium / 1000000 | R^ LLVM/Clang | 753.84 | 26.72 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 711.19 | 26.67 | 0.943x |
| medium / 1000000 | C++ SoA (msvc) | 2927.30 | 26.67 | 3.883x |
| medium / 1000000 | C++ vector AoS (msvc) | 705.48 | 26.67 | 0.936x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 654.09 | 26.67 | 0.868x |
| medium / 1000000 | C++ SoA (clangcl) | 2709.69 | 26.67 | 3.595x |
| medium / 1000000 | C++ vector AoS (clangcl) | 663.29 | 26.67 | 0.880x |
| medium / 1000000 | Rust particles_scalar | 648.64 | 27.11 | 0.860x |
| medium / 1000000 | Rust particles | 701.00 | 27.11 | 0.930x |
| large / 4000000 | R^ LLVM/Clang | 2980.13 | 95.34 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2666.64 | 95.33 | 0.895x |
| large / 4000000 | C++ SoA (msvc) | 11171.35 | 95.34 | 3.749x |
| large / 4000000 | C++ vector AoS (msvc) | 2866.97 | 95.33 | 0.962x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2606.77 | 95.33 | 0.875x |
| large / 4000000 | C++ SoA (clangcl) | 10721.43 | 95.34 | 3.598x |
| large / 4000000 | C++ vector AoS (clangcl) | 2688.32 | 95.33 | 0.902x |
| large / 4000000 | Rust particles_scalar | 2614.59 | 95.77 | 0.877x |
| large / 4000000 | Rust particles | 2662.62 | 95.77 | 0.893x |

## tree

R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ u32-index node pool (LLVM/Clang) | 21.93 | 5.28 | 1.000x |
| small / 16 | C++ unique_ptr (msvc) | 27.63 | 7.82 | 1.260x |
| small / 16 | C++ new/delete (msvc) | 27.68 | 7.79 | 1.262x |
| small / 16 | C++ index arena (msvc) | 18.49 | 5.27 | 0.843x |
| small / 16 | C++ unique_ptr (clangcl) | 27.73 | 7.82 | 1.264x |
| small / 16 | C++ new/delete (clangcl) | 27.36 | 7.79 | 1.247x |
| small / 16 | C++ index arena (clangcl) | 20.18 | 5.28 | 0.920x |
| small / 16 | Rust tree_box | 28.49 | 8.25 | 1.299x |
| small / 16 | Rust tree_arena | 20.01 | 5.72 | 0.912x |
| medium / 20 | R^ u32-index node pool (LLVM/Clang) | 139.53 | 27.78 | 1.000x |
| medium / 20 | C++ unique_ptr (msvc) | 316.10 | 68.25 | 2.265x |
| medium / 20 | C++ new/delete (msvc) | 321.99 | 68.22 | 2.308x |
| medium / 20 | C++ index arena (msvc) | 117.71 | 27.77 | 0.844x |
| medium / 20 | C++ unique_ptr (clangcl) | 322.00 | 68.25 | 2.308x |
| medium / 20 | C++ new/delete (clangcl) | 280.52 | 68.22 | 2.010x |
| medium / 20 | C++ index arena (clangcl) | 119.37 | 27.78 | 0.855x |
| medium / 20 | Rust tree_box | 302.40 | 68.68 | 2.167x |
| medium / 20 | Rust tree_arena | 98.74 | 28.22 | 0.708x |
| large / 24 | R^ u32-index node pool (LLVM/Clang) | 2199.64 | 387.78 | 1.000x |
| large / 24 | C++ unique_ptr (msvc) | 4966.81 | 1034.96 | 2.258x |
| large / 24 | C++ new/delete (msvc) | 4758.78 | 1034.92 | 2.163x |
| large / 24 | C++ index arena (msvc) | 1682.00 | 387.77 | 0.765x |
| large / 24 | C++ unique_ptr (clangcl) | 4810.28 | 1034.96 | 2.187x |
| large / 24 | C++ new/delete (clangcl) | 4572.02 | 1034.92 | 2.079x |
| large / 24 | C++ index arena (clangcl) | 1558.37 | 387.78 | 0.708x |
| large / 24 | Rust tree_box | 4836.54 | 1035.39 | 2.199x |
| large / 24 | Rust tree_arena | 1347.64 | 388.22 | 0.613x |

## graph

R^ uses 8-byte edges (i32 target, u32 next index), including one list-head slot per vertex. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ linked u32-index pool (LLVM/Clang) | 116.50 | 11.54 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 149.72 | 13.57 | 1.285x |
| small / 100000 | C++ CSR (msvc) | 41.36 | 14.47 | 0.355x |
| small / 100000 | C++ linked index arena (msvc) | 114.16 | 11.41 | 0.980x |
| small / 100000 | C++ nested vectors (clangcl) | 145.73 | 13.57 | 1.251x |
| small / 100000 | C++ CSR (clangcl) | 42.45 | 14.48 | 0.364x |
| small / 100000 | C++ linked index arena (clangcl) | 109.97 | 11.41 | 0.944x |
| small / 100000 | Rust graph_nested | 105.85 | 15.60 | 0.909x |
| small / 100000 | Rust graph_csr | 43.48 | 14.93 | 0.373x |
| small / 100000 | Rust graph_arena | 195.45 | 11.86 | 1.678x |
| medium / 500000 | R^ linked u32-index pool (LLVM/Clang) | 1338.96 | 42.04 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1305.98 | 52.44 | 0.975x |
| medium / 500000 | C++ CSR (msvc) | 327.22 | 57.20 | 0.244x |
| medium / 500000 | C++ linked index arena (msvc) | 1466.15 | 41.93 | 1.095x |
| medium / 500000 | C++ nested vectors (clangcl) | 1291.35 | 52.45 | 0.964x |
| medium / 500000 | C++ CSR (clangcl) | 356.09 | 57.21 | 0.266x |
| medium / 500000 | C++ linked index arena (clangcl) | 1257.77 | 41.94 | 0.939x |
| medium / 500000 | Rust graph_nested | 783.15 | 60.93 | 0.585x |
| medium / 500000 | Rust graph_csr | 370.16 | 57.65 | 0.276x |
| medium / 500000 | Rust graph_arena | 1428.80 | 42.38 | 1.067x |
| large / 2000000 | R^ linked u32-index pool (LLVM/Clang) | 6732.20 | 156.54 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 6177.53 | 207.09 | 0.918x |
| large / 2000000 | C++ CSR (msvc) | 2161.42 | 217.42 | 0.321x |
| large / 2000000 | C++ linked index arena (msvc) | 7638.24 | 156.37 | 1.135x |
| large / 2000000 | C++ nested vectors (clangcl) | 6018.85 | 207.10 | 0.894x |
| large / 2000000 | C++ CSR (clangcl) | 2182.04 | 217.43 | 0.324x |
| large / 2000000 | C++ linked index arena (clangcl) | 6615.95 | 156.37 | 0.983x |
| large / 2000000 | Rust graph_nested | 3890.12 | 245.08 | 0.578x |
| large / 2000000 | Rust graph_csr | 2183.24 | 217.87 | 0.324x |
| large / 2000000 | Rust graph_arena | 7089.08 | 156.82 | 1.053x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
