# Additional R^ workloads

Measured: 2026-10-24T12:25:48.375707+00:00 (UTC).

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
| dirty | M bench/all08/README.md;  M bench/all08/current_bintrees/results.json;  M bench/all08/current_bintrees/results.md;  M bench/all08/report.py;  M bench/report_suite.py;  M bench/run_bench.py;  M bench/run_workloads.py;  M docs/design.md;  M src/llvm.c;  M tests/run.py; ?? bench/all08/pre_pop/ |
| compiler_sha256 | e158210a7381786015554b9a50430ea5d4340cb848642aad531e1ddbf44bbab2 |
| runtime_sha256 | fa716c2b975c1a319f24c7a08fdce07d0194298765efbf629955eab2ade9e034 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## sexp

R^ generates the original text, parses it into 24-byte nominal union nodes linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks and all five checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 20000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 175.40 | 52.34 | 1.000x |
| small / 20000 | C++ owning-pointer / owned symbols (msvc) | 502.33 | 130.01 | 2.864x |
| small / 20000 | C++ index arena / borrowed symbols (msvc) | 227.13 | 61.06 | 1.295x |
| small / 20000 | C++ owning-pointer / owned symbols (clangcl) | 474.12 | 130.00 | 2.703x |
| small / 20000 | C++ index arena / borrowed symbols (clangcl) | 182.33 | 61.06 | 1.039x |
| small / 20000 | Rust sexp_box | 414.77 | 96.25 | 2.365x |
| small / 20000 | Rust sexp_arena | 169.34 | 62.50 | 0.965x |
| medium / 100000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 856.62 | 245.84 | 1.000x |
| medium / 100000 | C++ owning-pointer / owned symbols (msvc) | 2435.12 | 633.55 | 2.843x |
| medium / 100000 | C++ index arena / borrowed symbols (msvc) | 1058.30 | 293.78 | 1.235x |
| medium / 100000 | C++ owning-pointer / owned symbols (clangcl) | 2027.95 | 633.56 | 2.367x |
| medium / 100000 | C++ index arena / borrowed symbols (clangcl) | 854.56 | 293.54 | 0.998x |
| medium / 100000 | Rust sexp_box | 2028.41 | 463.20 | 2.368x |
| medium / 100000 | Rust sexp_arena | 782.68 | 242.21 | 0.914x |
| large / 300000 | R^ nominal union / copied-symbol arenas (LLVM/Clang) | 2433.11 | 729.59 | 1.000x |
| large / 300000 | C++ owning-pointer / owned symbols (msvc) | 7420.40 | 1891.69 | 3.050x |
| large / 300000 | C++ index arena / borrowed symbols (msvc) | 3245.84 | 963.79 | 1.334x |
| large / 300000 | C++ owning-pointer / owned symbols (clangcl) | 6034.43 | 1891.71 | 2.480x |
| large / 300000 | C++ index arena / borrowed symbols (clangcl) | 2582.38 | 963.78 | 1.061x |
| large / 300000 | Rust sexp_box | 6147.13 | 1380.87 | 2.526x |
| large / 300000 | Rust sexp_arena | 2359.01 | 911.06 | 0.970x |

## calc

R^ generates and damages the original arithmetic inputs, parses every input with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and division-by-zero failures. All partially built nodes count toward the total. Text and node regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, RNG, corruption, parse/evaluation order and all eight checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 80.55 | 4.20 | 1.000x |
| small / 100000 | C++ owning AST / exceptions (msvc) | 324.27 | 4.24 | 4.026x |
| small / 100000 | C++ index arena / error codes (msvc) | 81.11 | 4.18 | 1.007x |
| small / 100000 | C++ owning AST / exceptions (clangcl) | 309.07 | 4.23 | 3.837x |
| small / 100000 | C++ index arena / error codes (clangcl) | 73.07 | 4.19 | 0.907x |
| small / 100000 | Rust calc_box | 161.74 | 4.66 | 2.008x |
| small / 100000 | Rust calc_arena | 68.41 | 4.69 | 0.849x |
| medium / 400000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 265.76 | 4.21 | 1.000x |
| medium / 400000 | C++ owning AST / exceptions (msvc) | 1300.63 | 4.24 | 4.894x |
| medium / 400000 | C++ index arena / error codes (msvc) | 283.25 | 4.18 | 1.066x |
| medium / 400000 | C++ owning AST / exceptions (clangcl) | 1192.07 | 4.23 | 4.486x |
| medium / 400000 | C++ index arena / error codes (clangcl) | 237.54 | 4.18 | 0.894x |
| medium / 400000 | Rust calc_box | 577.87 | 4.67 | 2.174x |
| medium / 400000 | Rust calc_arena | 225.10 | 4.69 | 0.847x |
| large / 1600000 | R^ nominal AST / explicit result unions (LLVM/Clang) | 1017.25 | 4.20 | 1.000x |
| large / 1600000 | C++ owning AST / exceptions (msvc) | 5188.83 | 4.24 | 5.101x |
| large / 1600000 | C++ index arena / error codes (msvc) | 1082.06 | 4.18 | 1.064x |
| large / 1600000 | C++ owning AST / exceptions (clangcl) | 4698.84 | 4.23 | 4.619x |
| large / 1600000 | C++ index arena / error codes (clangcl) | 895.56 | 4.19 | 0.880x |
| large / 1600000 | Rust calc_box | 2311.97 | 4.66 | 2.273x |
| large / 1600000 | Rust calc_arena | 859.71 | 4.69 | 0.845x |

## records

R^ ports records_fixed.goose with a native nominal Event union, 24-byte records and a 15-byte inline text capacity. Tag tests and checked payload extraction are generated from types; no manually synchronized kind/payload struct is used. Original sizes, RNG consumption, all four alternatives, actual per-Say word generation and four scans are preserved. This is the original fixed-mode variant; variable-size ADTs and separately owned string payloads are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ native nominal union / inline text (LLVM/Clang) | 42.26 | 15.28 | 1.000x |
| small / 500000 | C++ virtual objects (msvc) | 103.18 | 28.05 | 2.442x |
| small / 500000 | C++ variant / owned string (msvc) | 86.38 | 48.64 | 2.044x |
| small / 500000 | C++ variant / inline text (msvc) | 52.69 | 26.00 | 1.247x |
| small / 500000 | C++ virtual objects (clangcl) | 101.56 | 28.06 | 2.403x |
| small / 500000 | C++ variant / owned string (clangcl) | 79.12 | 48.66 | 1.872x |
| small / 500000 | C++ variant / inline text (clangcl) | 49.84 | 26.01 | 1.179x |
| small / 500000 | Rust records | 69.16 | 22.63 | 1.637x |
| medium / 4000000 | R^ native nominal union / inline text (LLVM/Clang) | 218.79 | 95.34 | 1.000x |
| medium / 4000000 | C++ virtual objects (msvc) | 724.20 | 187.05 | 3.310x |
| medium / 4000000 | C++ variant / owned string (msvc) | 543.74 | 330.33 | 2.485x |
| medium / 4000000 | C++ variant / inline text (msvc) | 273.11 | 166.84 | 1.248x |
| medium / 4000000 | C++ virtual objects (clangcl) | 659.60 | 187.06 | 3.015x |
| medium / 4000000 | C++ variant / owned string (clangcl) | 488.07 | 330.34 | 2.231x |
| medium / 4000000 | C++ variant / inline text (clangcl) | 278.32 | 166.85 | 1.272x |
| medium / 4000000 | Rust records | 415.70 | 150.70 | 1.900x |
| large / 16000000 | R^ native nominal union / inline text (LLVM/Clang) | 818.23 | 370.03 | 1.000x |
| large / 16000000 | C++ virtual objects (msvc) | 2910.74 | 709.25 | 3.557x |
| large / 16000000 | C++ variant / owned string (msvc) | 2059.81 | 1100.77 | 2.517x |
| large / 16000000 | C++ variant / inline text (msvc) | 1039.20 | 552.07 | 1.270x |
| large / 16000000 | C++ virtual objects (clangcl) | 2666.45 | 709.26 | 3.259x |
| large / 16000000 | C++ variant / owned string (clangcl) | 1804.11 | 1100.79 | 2.205x |
| large / 16000000 | C++ variant / inline text (clangcl) | 1046.47 | 552.07 | 1.279x |
| large / 16000000 | Rust records | 1674.94 | 590.04 | 2.047x |

## interp

R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 child indices, matching the C++ switch / Rust arena representation. The generated tag tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, postorder construction, seeds, depths and eight pass-dependent evaluations modulo 1000003 are preserved. Goose's variable-size nodes and relative references are not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ native nominal union / u32 arena (LLVM/Clang) | 19.35 | 4.22 | 1.000x |
| small / 16 | C++ virtual owning tree (msvc) | 24.09 | 4.88 | 1.245x |
| small / 16 | C++ variant arena (msvc) | 19.75 | 4.96 | 1.021x |
| small / 16 | C++ switch arena (msvc) | 19.42 | 4.96 | 1.004x |
| small / 16 | C++ virtual owning tree (clangcl) | 21.61 | 4.88 | 1.117x |
| small / 16 | C++ variant arena (clangcl) | 20.04 | 4.97 | 1.036x |
| small / 16 | C++ switch arena (clangcl) | 19.98 | 4.98 | 1.033x |
| small / 16 | Rust interp_box | 22.10 | 5.31 | 1.142x |
| small / 16 | Rust interp_arena | 21.80 | 5.62 | 1.127x |
| medium / 20 | R^ native nominal union / u32 arena (LLVM/Clang) | 36.56 | 7.22 | 1.000x |
| medium / 20 | C++ virtual owning tree (msvc) | 79.79 | 12.88 | 2.182x |
| medium / 20 | C++ variant arena (msvc) | 39.20 | 10.19 | 1.072x |
| medium / 20 | C++ switch arena (msvc) | 35.80 | 10.20 | 0.979x |
| medium / 20 | C++ virtual owning tree (clangcl) | 77.83 | 12.88 | 2.129x |
| medium / 20 | C++ variant arena (clangcl) | 36.54 | 10.20 | 0.999x |
| medium / 20 | C++ switch arena (clangcl) | 35.56 | 10.20 | 0.973x |
| medium / 20 | Rust interp_box | 77.60 | 13.32 | 2.122x |
| medium / 20 | Rust interp_arena | 33.46 | 11.68 | 0.915x |
| large / 24 | R^ native nominal union / u32 arena (LLVM/Clang) | 233.03 | 39.34 | 1.000x |
| large / 24 | C++ virtual owning tree (msvc) | 611.93 | 99.16 | 2.626x |
| large / 24 | C++ variant arena (msvc) | 226.26 | 59.51 | 0.971x |
| large / 24 | C++ switch arena (msvc) | 215.53 | 59.52 | 0.925x |
| large / 24 | C++ virtual owning tree (clangcl) | 624.68 | 99.16 | 2.681x |
| large / 24 | C++ variant arena (clangcl) | 252.33 | 59.52 | 1.083x |
| large / 24 | C++ switch arena (clangcl) | 210.98 | 59.52 | 0.905x |
| large / 24 | Rust interp_box | 673.92 | 99.60 | 2.892x |
| large / 24 | Rust interp_arena | 228.95 | 53.67 | 0.982x |

## respond

R^ ports the original respond_stream.goose variant, generating the same requests and JSON bytes, including real quote/backslash escaping. The output byte region is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, item counts, quantities, prices and all three checksums are preserved. The DTO-building Goose variant and general nested owned strings/arrays are not implemented by this port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 76.51 | 4.18 | 1.000x |
| small / 100000 | C++ response DTO (msvc) | 184.37 | 4.27 | 2.410x |
| small / 100000 | C++ streaming JSON (msvc) | 83.37 | 4.18 | 1.090x |
| small / 100000 | C++ response DTO (clangcl) | 155.41 | 4.27 | 2.031x |
| small / 100000 | C++ streaming JSON (clangcl) | 68.77 | 4.19 | 0.899x |
| small / 100000 | Rust respond_dto | 220.07 | 4.73 | 2.876x |
| small / 100000 | Rust respond_stream | 103.28 | 4.68 | 1.350x |
| medium / 400000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 269.29 | 4.18 | 1.000x |
| medium / 400000 | C++ response DTO (msvc) | 718.58 | 4.27 | 2.668x |
| medium / 400000 | C++ streaming JSON (msvc) | 277.49 | 4.17 | 1.030x |
| medium / 400000 | C++ response DTO (clangcl) | 583.74 | 4.27 | 2.168x |
| medium / 400000 | C++ streaming JSON (clangcl) | 223.76 | 4.20 | 0.831x |
| medium / 400000 | Rust respond_dto | 919.20 | 4.73 | 3.413x |
| medium / 400000 | Rust respond_stream | 375.26 | 4.69 | 1.394x |
| large / 1600000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 1005.15 | 4.18 | 1.000x |
| large / 1600000 | C++ response DTO (msvc) | 2822.92 | 4.27 | 2.808x |
| large / 1600000 | C++ streaming JSON (msvc) | 1121.31 | 4.18 | 1.116x |
| large / 1600000 | C++ response DTO (clangcl) | 2291.15 | 4.27 | 2.279x |
| large / 1600000 | C++ streaming JSON (clangcl) | 827.54 | 4.19 | 0.823x |
| large / 1600000 | Rust respond_dto | 3897.27 | 4.73 | 3.877x |
| large / 1600000 | Rust respond_stream | 1334.98 | 4.68 | 1.328x |

## words

R^ stores vocabulary entries as 21-byte fixed-capacity words and copies split words into one owned packed byte region (one-byte varint lengths). The open-addressed table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, skewed word selection, RNG sequence, text construction, split and all three checksums are preserved. Packed strings are a library representation; general variable-size array elements and a built-in dictionary are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 78.61 | 19.97 | 1.000x |
| small / 500000 | C++ owned-key unordered_map (msvc) | 84.06 | 24.98 | 1.069x |
| small / 500000 | C++ borrowed-key unordered_map (msvc) | 87.84 | 25.01 | 1.117x |
| small / 500000 | C++ open table (msvc) | 87.09 | 25.01 | 1.108x |
| small / 500000 | C++ owned-key unordered_map (clangcl) | 90.97 | 25.00 | 1.157x |
| small / 500000 | C++ borrowed-key unordered_map (clangcl) | 100.81 | 24.99 | 1.282x |
| small / 500000 | C++ open table (clangcl) | 75.21 | 24.99 | 0.957x |
| small / 500000 | Rust words_hashmap | 81.61 | 22.38 | 1.038x |
| small / 500000 | Rust words_open | 77.89 | 25.20 | 0.991x |
| medium / 4000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 897.83 | 132.41 | 1.000x |
| medium / 4000000 | C++ owned-key unordered_map (msvc) | 1386.98 | 161.30 | 1.545x |
| medium / 4000000 | C++ borrowed-key unordered_map (msvc) | 1407.12 | 161.32 | 1.567x |
| medium / 4000000 | C++ open table (msvc) | 908.48 | 162.26 | 1.012x |
| medium / 4000000 | C++ owned-key unordered_map (clangcl) | 1187.35 | 161.31 | 1.322x |
| medium / 4000000 | C++ borrowed-key unordered_map (clangcl) | 1300.96 | 161.31 | 1.449x |
| medium / 4000000 | C++ open table (clangcl) | 886.13 | 162.27 | 0.987x |
| medium / 4000000 | Rust words_hashmap | 1341.46 | 142.13 | 1.494x |
| medium / 4000000 | Rust words_open | 995.65 | 172.05 | 1.109x |
| large / 16000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 4730.93 | 517.84 | 1.000x |
| large / 16000000 | C++ owned-key unordered_map (msvc) | 6770.91 | 563.45 | 1.431x |
| large / 16000000 | C++ borrowed-key unordered_map (msvc) | 7194.53 | 563.46 | 1.521x |
| large / 16000000 | C++ open table (msvc) | 4477.90 | 634.70 | 0.947x |
| large / 16000000 | C++ owned-key unordered_map (clangcl) | 5715.87 | 563.43 | 1.208x |
| large / 16000000 | C++ borrowed-key unordered_map (clangcl) | 6547.27 | 563.41 | 1.384x |
| large / 16000000 | C++ open table (clangcl) | 4369.75 | 634.71 | 0.924x |
| large / 16000000 | Rust words_hashmap | 7118.95 | 555.79 | 1.505x |
| large / 16000000 | Rust words_open | 4861.19 | 672.59 | 1.028x |

## strlist

R^ copies each split word into a single owned byte region, prefixed by its one-byte varint length (original generated lengths are 3..20). The original text and copied word list coexist; scans decode the packed lengths four times. A WordList library struct holds the count and owned data region. This does not claim general variable-size array elements or nested owned strings in the language. No word is replaced by a borrowed view or offset pair. All original sizes, seed sequence, text splitting, copies, passes and two checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 200000 | R^ owned packed word list (LLVM/Clang) | 32.76 | 8.66 | 1.000x |
| small / 200000 | C++ owned strings (msvc) | 51.36 | 16.46 | 1.568x |
| small / 200000 | C++ borrowed string views (msvc) | 33.98 | 11.25 | 1.037x |
| small / 200000 | C++ flat offsets (msvc) | 34.92 | 9.28 | 1.066x |
| small / 200000 | C++ owned strings (clangcl) | 45.33 | 16.46 | 1.383x |
| small / 200000 | C++ borrowed string views (clangcl) | 33.11 | 11.29 | 1.011x |
| small / 200000 | C++ flat offsets (clangcl) | 31.15 | 9.28 | 0.951x |
| small / 200000 | Rust strlist_owned | 56.56 | 16.40 | 1.726x |
| small / 200000 | Rust strlist_borrowed | 35.63 | 11.40 | 1.088x |
| medium / 2000000 | R^ owned packed word list (LLVM/Clang) | 196.29 | 51.54 | 1.000x |
| medium / 2000000 | C++ owned strings (msvc) | 449.58 | 143.98 | 2.290x |
| medium / 2000000 | C++ borrowed string views (msvc) | 231.41 | 76.57 | 1.179x |
| medium / 2000000 | C++ flat offsets (msvc) | 205.57 | 55.41 | 1.047x |
| medium / 2000000 | C++ owned strings (clangcl) | 369.49 | 144.03 | 1.882x |
| medium / 2000000 | C++ borrowed string views (clangcl) | 181.16 | 76.60 | 0.923x |
| medium / 2000000 | C++ flat offsets (clangcl) | 174.59 | 55.43 | 0.889x |
| medium / 2000000 | Rust strlist_owned | 424.05 | 125.23 | 2.160x |
| medium / 2000000 | Rust strlist_borrowed | 199.93 | 60.83 | 1.019x |
| large / 8000000 | R^ owned packed word list (LLVM/Clang) | 643.38 | 194.54 | 1.000x |
| large / 8000000 | C++ owned strings (msvc) | 1586.28 | 687.74 | 2.466x |
| large / 8000000 | C++ borrowed string views (msvc) | 790.10 | 343.33 | 1.228x |
| large / 8000000 | C++ flat offsets (msvc) | 740.75 | 191.10 | 1.151x |
| large / 8000000 | C++ owned strings (clangcl) | 1420.90 | 687.75 | 2.209x |
| large / 8000000 | C++ borrowed string views (clangcl) | 649.40 | 343.30 | 1.009x |
| large / 8000000 | C++ flat offsets (clangcl) | 610.94 | 191.12 | 0.950x |
| large / 8000000 | Rust strlist_owned | 1603.39 | 487.79 | 2.492x |
| large / 8000000 | Rust strlist_borrowed | 671.67 | 228.34 | 1.044x |

## scene

R^ uses 120-byte nodes with inline local/world transforms, four u32 child indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and f32 matrix/translation arithmetic are preserved. Composition writes through references to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array storage; Goose's 117-byte limited-array/relative-reference layout is not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 12 | R^ inline u32-child arena (LLVM/Clang) | 31.21 | 8.46 | 1.000x |
| small / 12 | C++ owning-pointer tree (msvc) | 62.12 | 10.11 | 1.991x |
| small / 12 | C++ index arena (msvc) | 34.99 | 10.91 | 1.121x |
| small / 12 | C++ owning-pointer tree (clangcl) | 65.21 | 10.12 | 2.090x |
| small / 12 | C++ index arena (clangcl) | 34.76 | 10.88 | 1.114x |
| small / 12 | Rust scene_box | 61.72 | 10.81 | 1.978x |
| small / 12 | Rust scene_arena | 33.23 | 13.55 | 1.065x |
| medium / 14 | R^ inline u32-child arena (LLVM/Clang) | 153.36 | 32.90 | 1.000x |
| medium / 14 | C++ owning-pointer tree (msvc) | 536.09 | 41.99 | 3.496x |
| medium / 14 | C++ index arena (msvc) | 191.55 | 52.09 | 1.249x |
| medium / 14 | C++ owning-pointer tree (clangcl) | 497.56 | 42.05 | 3.244x |
| medium / 14 | C++ index arena (clangcl) | 175.13 | 52.09 | 1.142x |
| medium / 14 | Rust scene_box | 532.78 | 43.96 | 3.474x |
| medium / 14 | Rust scene_arena | 159.28 | 36.06 | 1.039x |
| large / 16 | R^ inline u32-child arena (LLVM/Clang) | 942.83 | 185.90 | 1.000x |
| large / 16 | C++ owning-pointer tree (msvc) | 3354.25 | 242.62 | 3.558x |
| large / 16 | C++ index arena (msvc) | 1084.40 | 365.10 | 1.150x |
| large / 16 | C++ owning-pointer tree (clangcl) | 3195.64 | 242.67 | 3.389x |
| large / 16 | C++ index arena (clangcl) | 1032.38 | 365.07 | 1.095x |
| large / 16 | Rust scene_box | 3370.84 | 252.75 | 3.575x |
| large / 16 | Rust scene_arena | 883.57 | 246.08 | 0.937x |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays use statically assigned stable 1 GiB data stacks and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 134.33 | 11.66 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 722.47 | 27.05 | 5.378x |
| small / 2000000 | C++ index arena / open table (msvc) | 145.15 | 16.16 | 1.081x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 656.65 | 27.05 | 4.888x |
| small / 2000000 | C++ index arena / open table (clangcl) | 130.07 | 16.16 | 0.968x |
| small / 2000000 | Rust lru_hashmap | 225.68 | 15.68 | 1.680x |
| small / 2000000 | Rust lru_open | 166.90 | 13.19 | 1.243x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 775.71 | 35.09 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3372.30 | 96.62 | 4.347x |
| medium / 8000000 | C++ index arena / open table (msvc) | 1025.59 | 43.18 | 1.322x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 3237.85 | 96.62 | 4.174x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 838.78 | 43.17 | 1.081x |
| medium / 8000000 | Rust lru_hashmap | 1819.94 | 46.89 | 2.346x |
| medium / 8000000 | Rust lru_open | 959.73 | 37.19 | 1.237x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 3979.19 | 128.85 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 14697.08 | 416.30 | 3.693x |
| large / 32000000 | C++ index arena / open table (msvc) | 5031.61 | 175.88 | 1.264x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 14129.02 | 416.23 | 3.551x |
| large / 32000000 | C++ index arena / open table (clangcl) | 4520.87 | 175.88 | 1.136x |
| large / 32000000 | Rust lru_hashmap | 8222.06 | 169.20 | 2.066x |
| large / 32000000 | Rust lru_open | 4958.17 | 133.20 | 1.246x |

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 24.11 | 11.60 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 30.48 | 14.52 | 1.264x |
| small / 1000000 | C++ reserved vector indices (msvc) | 22.92 | 11.51 | 0.951x |
| small / 1000000 | C++ deque pointers (msvc) | 61.35 | 23.50 | 2.545x |
| small / 1000000 | C++ vector indices (clangcl) | 30.78 | 14.51 | 1.277x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 26.36 | 11.51 | 1.093x |
| small / 1000000 | C++ deque pointers (clangcl) | 60.58 | 23.51 | 2.513x |
| small / 1000000 | Rust push | 23.50 | 12.04 | 0.975x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 100.49 | 127.85 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 247.77 | 189.18 | 2.466x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 107.77 | 129.54 | 1.072x |
| medium / 16000000 | C++ deque pointers (msvc) | 765.10 | 315.93 | 7.614x |
| medium / 16000000 | C++ vector indices (clangcl) | 253.41 | 189.18 | 2.522x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 177.97 | 129.50 | 1.771x |
| medium / 16000000 | C++ deque pointers (clangcl) | 747.03 | 315.91 | 7.434x |
| medium / 16000000 | Rust push | 103.95 | 129.19 | 1.034x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 378.58 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 1058.73 | 936.26 | 2.797x |
| large / 64000000 | C++ reserved vector indices (msvc) | 402.49 | 501.43 | 1.063x |
| large / 64000000 | C++ deque pointers (msvc) | 3265.14 | 1251.09 | 8.625x |
| large / 64000000 | C++ vector indices (clangcl) | 1000.61 | 936.28 | 2.643x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 678.45 | 501.44 | 1.792x |
| large / 64000000 | C++ deque pointers (clangcl) | 3312.51 | 1251.11 | 8.750x |
| large / 64000000 | Rust push | 354.43 | 501.14 | 0.936x |

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 24.22 | 7.66 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 27.24 | 11.00 | 1.125x |
| small / 1000000 | C++ reserved vector (msvc) | 24.39 | 7.59 | 1.007x |
| small / 1000000 | C++ vector (clangcl) | 26.89 | 11.00 | 1.110x |
| small / 1000000 | C++ reserved vector (clangcl) | 24.16 | 7.59 | 0.998x |
| small / 1000000 | Rust sum | 24.49 | 8.03 | 1.011x |
| medium / 16000000 | R^ LLVM/Clang | 103.73 | 64.84 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 138.88 | 96.89 | 1.339x |
| medium / 16000000 | C++ reserved vector (msvc) | 107.38 | 64.81 | 1.035x |
| medium / 16000000 | C++ vector (clangcl) | 143.40 | 96.89 | 1.382x |
| medium / 16000000 | C++ reserved vector (clangcl) | 135.29 | 64.82 | 1.304x |
| medium / 16000000 | Rust sum | 103.11 | 65.25 | 0.994x |
| large / 128000000 | R^ LLVM/Clang | 721.87 | 492.09 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 1023.86 | 698.49 | 1.418x |
| large / 128000000 | C++ reserved vector (msvc) | 774.99 | 492.06 | 1.074x |
| large / 128000000 | C++ vector (clangcl) | 1143.55 | 698.49 | 1.584x |
| large / 128000000 | C++ reserved vector (clangcl) | 904.75 | 492.06 | 1.253x |
| large / 128000000 | Rust sum | 696.34 | 492.50 | 0.965x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 23.73 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 64.30 | 7.68 | 2.710x |
| small / 1024 | C++ row pointers (msvc) | 33.75 | 5.77 | 1.422x |
| small / 1024 | C++ flat (clangcl) | 67.59 | 7.68 | 2.848x |
| small / 1024 | C++ row pointers (clangcl) | 28.27 | 5.79 | 1.191x |
| small / 1024 | Rust blur_index | 29.13 | 7.39 | 1.228x |
| small / 1024 | Rust blur_windows | 29.25 | 7.41 | 1.233x |
| medium / 2048 | R^ LLVM/Clang | 48.47 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 221.67 | 14.56 | 4.573x |
| medium / 2048 | C++ row pointers (msvc) | 89.39 | 11.77 | 1.844x |
| medium / 2048 | C++ flat (clangcl) | 222.99 | 15.80 | 4.600x |
| medium / 2048 | C++ row pointers (clangcl) | 55.84 | 11.79 | 1.152x |
| medium / 2048 | Rust blur_index | 66.81 | 13.40 | 1.378x |
| medium / 2048 | Rust blur_windows | 60.40 | 13.41 | 1.246x |
| large / 8192 | R^ LLVM/Clang | 508.18 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3294.05 | 178.88 | 6.482x |
| large / 8192 | C++ row pointers (msvc) | 1253.30 | 131.77 | 2.466x |
| large / 8192 | C++ flat (clangcl) | 3578.04 | 178.87 | 7.041x |
| large / 8192 | C++ row pointers (clangcl) | 625.47 | 131.79 | 1.231x |
| large / 8192 | Rust blur_index | 707.49 | 133.41 | 1.392x |
| large / 8192 | Rust blur_windows | 696.84 | 133.41 | 1.371x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 47.76 | 6.09 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 56.53 | 6.07 | 1.184x |
| small / 100000 | C++ SoA (msvc) | 154.10 | 6.08 | 3.227x |
| small / 100000 | C++ vector AoS (msvc) | 69.64 | 6.07 | 1.458x |
| small / 100000 | C++ scalar AoS (clangcl) | 48.74 | 6.07 | 1.021x |
| small / 100000 | C++ SoA (clangcl) | 100.65 | 6.07 | 2.108x |
| small / 100000 | C++ vector AoS (clangcl) | 57.29 | 6.07 | 1.200x |
| small / 100000 | Rust particles_scalar | 54.39 | 6.51 | 1.139x |
| small / 100000 | Rust particles | 56.42 | 6.51 | 1.181x |
| medium / 1000000 | R^ LLVM/Clang | 733.09 | 26.72 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 752.06 | 26.67 | 1.026x |
| medium / 1000000 | C++ SoA (msvc) | 3062.06 | 26.67 | 4.177x |
| medium / 1000000 | C++ vector AoS (msvc) | 781.91 | 26.67 | 1.067x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 673.61 | 26.67 | 0.919x |
| medium / 1000000 | C++ SoA (clangcl) | 2730.18 | 26.67 | 3.724x |
| medium / 1000000 | C++ vector AoS (clangcl) | 691.20 | 26.67 | 0.943x |
| medium / 1000000 | Rust particles_scalar | 651.71 | 27.11 | 0.889x |
| medium / 1000000 | Rust particles | 684.71 | 27.11 | 0.934x |
| large / 4000000 | R^ LLVM/Clang | 2637.92 | 95.34 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2732.94 | 95.33 | 1.036x |
| large / 4000000 | C++ SoA (msvc) | 11862.05 | 95.34 | 4.497x |
| large / 4000000 | C++ vector AoS (msvc) | 2892.60 | 95.33 | 1.097x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2669.38 | 95.33 | 1.012x |
| large / 4000000 | C++ SoA (clangcl) | 10820.19 | 95.34 | 4.102x |
| large / 4000000 | C++ vector AoS (clangcl) | 2627.79 | 95.33 | 0.996x |
| large / 4000000 | Rust particles_scalar | 2591.88 | 95.77 | 0.983x |
| large / 4000000 | Rust particles | 2668.21 | 95.78 | 1.011x |

## tree

R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ u32-index node pool (LLVM/Clang) | 17.77 | 5.28 | 1.000x |
| small / 16 | C++ unique_ptr (msvc) | 30.89 | 7.82 | 1.738x |
| small / 16 | C++ new/delete (msvc) | 28.06 | 7.79 | 1.579x |
| small / 16 | C++ index arena (msvc) | 19.03 | 5.27 | 1.071x |
| small / 16 | C++ unique_ptr (clangcl) | 30.72 | 7.82 | 1.729x |
| small / 16 | C++ new/delete (clangcl) | 27.62 | 7.79 | 1.554x |
| small / 16 | C++ index arena (clangcl) | 19.21 | 5.28 | 1.081x |
| small / 16 | Rust tree_box | 29.66 | 8.25 | 1.669x |
| small / 16 | Rust tree_arena | 19.42 | 5.72 | 1.093x |
| medium / 20 | R^ u32-index node pool (LLVM/Clang) | 97.42 | 27.78 | 1.000x |
| medium / 20 | C++ unique_ptr (msvc) | 307.23 | 68.25 | 3.154x |
| medium / 20 | C++ new/delete (msvc) | 289.69 | 68.22 | 2.974x |
| medium / 20 | C++ index arena (msvc) | 119.14 | 27.77 | 1.223x |
| medium / 20 | C++ unique_ptr (clangcl) | 300.57 | 68.25 | 3.085x |
| medium / 20 | C++ new/delete (clangcl) | 280.18 | 68.22 | 2.876x |
| medium / 20 | C++ index arena (clangcl) | 115.60 | 27.78 | 1.187x |
| medium / 20 | Rust tree_box | 303.88 | 68.68 | 3.119x |
| medium / 20 | Rust tree_arena | 99.16 | 28.22 | 1.018x |
| large / 24 | R^ u32-index node pool (LLVM/Clang) | 1294.15 | 387.78 | 1.000x |
| large / 24 | C++ unique_ptr (msvc) | 5133.01 | 1034.96 | 3.966x |
| large / 24 | C++ new/delete (msvc) | 4888.30 | 1034.93 | 3.777x |
| large / 24 | C++ index arena (msvc) | 1660.91 | 387.77 | 1.283x |
| large / 24 | C++ unique_ptr (clangcl) | 4847.44 | 1034.96 | 3.746x |
| large / 24 | C++ new/delete (clangcl) | 4858.26 | 1034.92 | 3.754x |
| large / 24 | C++ index arena (clangcl) | 1657.48 | 387.78 | 1.281x |
| large / 24 | Rust tree_box | 4844.57 | 1035.38 | 3.743x |
| large / 24 | Rust tree_arena | 1421.41 | 388.22 | 1.098x |

## graph

R^ uses the original two-pass CSR count/prefix/fill representation, matching C++ VARIANT 1 and Rust CSR. Source and destination arrays, start offsets, fill cursors and contiguous edges retain all construction cost. The previous linked index implementation is preserved in rhat/variants/graph_linked.rh. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 42.55 | 14.72 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 156.87 | 13.57 | 3.687x |
| small / 100000 | C++ CSR (msvc) | 41.71 | 14.47 | 0.980x |
| small / 100000 | C++ linked index arena (msvc) | 150.00 | 11.41 | 3.525x |
| small / 100000 | C++ nested vectors (clangcl) | 141.83 | 13.55 | 3.333x |
| small / 100000 | C++ CSR (clangcl) | 41.64 | 14.48 | 0.979x |
| small / 100000 | C++ linked index arena (clangcl) | 120.11 | 11.41 | 2.823x |
| small / 100000 | Rust graph_nested | 107.33 | 15.59 | 2.523x |
| small / 100000 | Rust graph_csr | 41.39 | 14.92 | 0.973x |
| small / 100000 | Rust graph_arena | 130.46 | 11.86 | 3.066x |
| medium / 500000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 339.05 | 57.48 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1283.50 | 52.51 | 3.786x |
| medium / 500000 | C++ CSR (msvc) | 321.57 | 57.20 | 0.948x |
| medium / 500000 | C++ linked index arena (msvc) | 1463.85 | 41.93 | 4.318x |
| medium / 500000 | C++ nested vectors (clangcl) | 1253.84 | 52.52 | 3.698x |
| medium / 500000 | C++ CSR (clangcl) | 341.14 | 57.21 | 1.006x |
| medium / 500000 | C++ linked index arena (clangcl) | 1276.85 | 41.94 | 3.766x |
| medium / 500000 | Rust graph_nested | 774.91 | 60.93 | 2.286x |
| medium / 500000 | Rust graph_csr | 357.87 | 57.65 | 1.056x |
| medium / 500000 | Rust graph_arena | 1398.15 | 42.38 | 4.124x |
| large / 2000000 | R^ CSR / checked i32 arrays (LLVM/Clang) | 2048.07 | 217.72 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 6132.13 | 207.09 | 2.994x |
| large / 2000000 | C++ CSR (msvc) | 2274.32 | 217.42 | 1.110x |
| large / 2000000 | C++ linked index arena (msvc) | 7783.66 | 156.38 | 3.800x |
| large / 2000000 | C++ nested vectors (clangcl) | 6205.71 | 207.39 | 3.030x |
| large / 2000000 | C++ CSR (clangcl) | 2229.00 | 217.43 | 1.088x |
| large / 2000000 | C++ linked index arena (clangcl) | 6584.84 | 156.38 | 3.215x |
| large / 2000000 | Rust graph_nested | 3952.05 | 244.95 | 1.930x |
| large / 2000000 | Rust graph_csr | 2334.95 | 217.86 | 1.140x |
| large / 2000000 | Rust graph_arena | 7127.01 | 156.82 | 3.480x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../../results.md).
