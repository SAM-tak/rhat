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

## respond

R^ ports the original respond_stream.goose variant, generating the same requests and JSON bytes, including real quote/backslash escaping. The output byte region is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, item counts, quantities, prices and all three checksums are preserved. The DTO-building Goose variant and general nested owned strings/arrays are not implemented by this port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 392.36 | 4.18 | 1.000x |
| small / 100000 | C++ response DTO (msvc) | 182.75 | 4.27 | 0.466x |
| small / 100000 | C++ streaming JSON (msvc) | 80.25 | 4.18 | 0.205x |
| small / 100000 | C++ response DTO (clangcl) | 151.91 | 4.27 | 0.387x |
| small / 100000 | C++ streaming JSON (clangcl) | 67.99 | 4.20 | 0.173x |
| small / 100000 | Rust respond_dto | 208.96 | 4.68 | 0.533x |
| small / 100000 | Rust respond_stream | 97.43 | 4.69 | 0.248x |
| medium / 400000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 1521.54 | 4.18 | 1.000x |
| medium / 400000 | C++ response DTO (msvc) | 693.51 | 4.27 | 0.456x |
| medium / 400000 | C++ streaming JSON (msvc) | 267.46 | 4.18 | 0.176x |
| medium / 400000 | C++ response DTO (clangcl) | 573.38 | 4.27 | 0.377x |
| medium / 400000 | C++ streaming JSON (clangcl) | 211.79 | 4.20 | 0.139x |
| medium / 400000 | Rust respond_dto | 795.96 | 4.73 | 0.523x |
| medium / 400000 | Rust respond_stream | 333.75 | 4.69 | 0.219x |
| large / 1600000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 6426.43 | 4.18 | 1.000x |
| large / 1600000 | C++ response DTO (msvc) | 2636.67 | 4.27 | 0.410x |
| large / 1600000 | C++ streaming JSON (msvc) | 1023.02 | 4.18 | 0.159x |
| large / 1600000 | C++ response DTO (clangcl) | 2219.28 | 4.27 | 0.345x |
| large / 1600000 | C++ streaming JSON (clangcl) | 797.38 | 4.20 | 0.124x |
| large / 1600000 | Rust respond_dto | 3165.09 | 4.67 | 0.493x |
| large / 1600000 | Rust respond_stream | 1310.65 | 4.69 | 0.204x |

## words

R^ stores vocabulary entries as 21-byte fixed-capacity words and copies split words into one owned packed byte region (one-byte varint lengths). The open-addressed table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, skewed word selection, RNG sequence, text construction, split and all three checksums are preserved. Packed strings are a library representation; general variable-size array elements and a built-in dictionary are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 186.98 | 19.84 | 1.000x |
| small / 500000 | C++ owned-key unordered_map (msvc) | 88.09 | 24.96 | 0.471x |
| small / 500000 | C++ borrowed-key unordered_map (msvc) | 95.76 | 24.97 | 0.512x |
| small / 500000 | C++ open table (msvc) | 76.27 | 24.96 | 0.408x |
| small / 500000 | C++ owned-key unordered_map (clangcl) | 74.96 | 24.99 | 0.401x |
| small / 500000 | C++ borrowed-key unordered_map (clangcl) | 79.58 | 24.99 | 0.426x |
| small / 500000 | C++ open table (clangcl) | 74.58 | 24.99 | 0.399x |
| small / 500000 | Rust words_hashmap | 79.07 | 21.63 | 0.423x |
| small / 500000 | Rust words_open | 69.55 | 25.16 | 0.372x |
| medium / 4000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 1947.16 | 132.33 | 1.000x |
| medium / 4000000 | C++ owned-key unordered_map (msvc) | 1238.59 | 161.32 | 0.636x |
| medium / 4000000 | C++ borrowed-key unordered_map (msvc) | 1266.39 | 161.32 | 0.650x |
| medium / 4000000 | C++ open table (msvc) | 869.71 | 162.25 | 0.447x |
| medium / 4000000 | C++ owned-key unordered_map (clangcl) | 1030.22 | 161.32 | 0.529x |
| medium / 4000000 | C++ borrowed-key unordered_map (clangcl) | 1118.77 | 161.30 | 0.575x |
| medium / 4000000 | C++ open table (clangcl) | 814.97 | 162.27 | 0.419x |
| medium / 4000000 | Rust words_hashmap | 1142.92 | 142.16 | 0.587x |
| medium / 4000000 | Rust words_open | 852.35 | 172.04 | 0.438x |
| large / 16000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 9502.84 | 517.74 | 1.000x |
| large / 16000000 | C++ owned-key unordered_map (msvc) | 6238.28 | 563.43 | 0.656x |
| large / 16000000 | C++ borrowed-key unordered_map (msvc) | 6769.26 | 563.43 | 0.712x |
| large / 16000000 | C++ open table (msvc) | 4164.57 | 634.69 | 0.438x |
| large / 16000000 | C++ owned-key unordered_map (clangcl) | 5356.83 | 563.43 | 0.564x |
| large / 16000000 | C++ borrowed-key unordered_map (clangcl) | 5944.71 | 563.41 | 0.626x |
| large / 16000000 | C++ open table (clangcl) | 3803.40 | 634.65 | 0.400x |
| large / 16000000 | Rust words_hashmap | 6516.76 | 555.75 | 0.686x |
| large / 16000000 | Rust words_open | 4161.04 | 672.64 | 0.438x |

## strlist

R^ copies each split word into a single owned byte region, prefixed by its one-byte varint length (original generated lengths are 3..20). The original text and copied word list coexist; scans decode the packed lengths four times. A WordList library struct holds the count and owned data region. This does not claim general variable-size array elements or nested owned strings in the language. No word is replaced by a borrowed view or offset pair. All original sizes, seed sequence, text splitting, copies, passes and two checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 200000 | R^ owned packed word list (LLVM/Clang) | 106.22 | 8.54 | 1.000x |
| small / 200000 | C++ owned strings (msvc) | 53.46 | 16.47 | 0.503x |
| small / 200000 | C++ borrowed string views (msvc) | 35.55 | 11.29 | 0.335x |
| small / 200000 | C++ flat offsets (msvc) | 34.66 | 9.26 | 0.326x |
| small / 200000 | C++ owned strings (clangcl) | 44.89 | 16.45 | 0.423x |
| small / 200000 | C++ borrowed string views (clangcl) | 32.67 | 11.31 | 0.308x |
| small / 200000 | C++ flat offsets (clangcl) | 31.31 | 9.29 | 0.295x |
| small / 200000 | Rust strlist_owned | 53.13 | 16.39 | 0.500x |
| small / 200000 | Rust strlist_borrowed | 34.05 | 11.39 | 0.321x |
| medium / 2000000 | R^ owned packed word list (LLVM/Clang) | 962.73 | 51.47 | 1.000x |
| medium / 2000000 | C++ owned strings (msvc) | 374.35 | 144.04 | 0.389x |
| medium / 2000000 | C++ borrowed string views (msvc) | 200.61 | 76.58 | 0.208x |
| medium / 2000000 | C++ flat offsets (msvc) | 190.22 | 55.43 | 0.198x |
| medium / 2000000 | C++ owned strings (clangcl) | 314.60 | 144.03 | 0.327x |
| medium / 2000000 | C++ borrowed string views (clangcl) | 166.82 | 76.60 | 0.173x |
| medium / 2000000 | C++ flat offsets (clangcl) | 157.12 | 55.41 | 0.163x |
| medium / 2000000 | Rust strlist_owned | 354.94 | 125.24 | 0.369x |
| medium / 2000000 | Rust strlist_borrowed | 170.85 | 60.84 | 0.177x |
| large / 8000000 | R^ owned packed word list (LLVM/Clang) | 3968.70 | 194.45 | 1.000x |
| large / 8000000 | C++ owned strings (msvc) | 1546.02 | 687.73 | 0.390x |
| large / 8000000 | C++ borrowed string views (msvc) | 756.36 | 343.32 | 0.191x |
| large / 8000000 | C++ flat offsets (msvc) | 724.45 | 191.11 | 0.183x |
| large / 8000000 | C++ owned strings (clangcl) | 1324.43 | 687.74 | 0.334x |
| large / 8000000 | C++ borrowed string views (clangcl) | 626.21 | 343.33 | 0.158x |
| large / 8000000 | C++ flat offsets (clangcl) | 552.78 | 191.12 | 0.139x |
| large / 8000000 | Rust strlist_owned | 1372.04 | 487.77 | 0.346x |
| large / 8000000 | Rust strlist_borrowed | 624.22 | 228.32 | 0.157x |

## scene

R^ uses 120-byte nodes with inline local/world transforms, four u32 child indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and f32 matrix/translation arithmetic are preserved. Composition writes through references to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array storage; Goose's 117-byte limited-array/relative-reference layout is not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 12 | R^ inline u32-child arena (LLVM/Clang) | 137.86 | 8.44 | 1.000x |
| small / 12 | C++ owning-pointer tree (msvc) | 63.18 | 10.11 | 0.458x |
| small / 12 | C++ index arena (msvc) | 34.70 | 10.88 | 0.252x |
| small / 12 | C++ owning-pointer tree (clangcl) | 64.52 | 10.11 | 0.468x |
| small / 12 | C++ index arena (clangcl) | 35.21 | 10.88 | 0.255x |
| small / 12 | Rust scene_box | 61.28 | 10.81 | 0.445x |
| small / 12 | Rust scene_arena | 35.31 | 13.59 | 0.256x |
| medium / 14 | R^ inline u32-child arena (LLVM/Clang) | 764.41 | 32.89 | 1.000x |
| medium / 14 | C++ owning-pointer tree (msvc) | 471.99 | 41.99 | 0.617x |
| medium / 14 | C++ index arena (msvc) | 174.07 | 52.09 | 0.228x |
| medium / 14 | C++ owning-pointer tree (clangcl) | 464.71 | 42.00 | 0.608x |
| medium / 14 | C++ index arena (clangcl) | 166.36 | 52.09 | 0.218x |
| medium / 14 | Rust scene_box | 474.13 | 43.95 | 0.620x |
| medium / 14 | Rust scene_arena | 160.82 | 36.06 | 0.210x |
| large / 16 | R^ inline u32-child arena (LLVM/Clang) | 4911.58 | 185.90 | 1.000x |
| large / 16 | C++ owning-pointer tree (msvc) | 2937.02 | 242.62 | 0.598x |
| large / 16 | C++ index arena (msvc) | 1124.87 | 365.06 | 0.229x |
| large / 16 | C++ owning-pointer tree (clangcl) | 2814.76 | 242.62 | 0.573x |
| large / 16 | C++ index arena (clangcl) | 1070.51 | 365.06 | 0.218x |
| large / 16 | Rust scene_box | 2961.35 | 252.76 | 0.603x |
| large / 16 | Rust scene_arena | 832.91 | 246.03 | 0.170x |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays reserve stable 1 GiB regions and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 562.08 | 11.60 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 672.86 | 27.05 | 1.197x |
| small / 2000000 | C++ index arena / open table (msvc) | 139.22 | 16.16 | 0.248x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 605.33 | 27.05 | 1.077x |
| small / 2000000 | C++ index arena / open table (clangcl) | 124.61 | 16.16 | 0.222x |
| small / 2000000 | Rust lru_hashmap | 184.92 | 16.20 | 0.329x |
| small / 2000000 | Rust lru_open | 124.23 | 13.18 | 0.221x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 3026.36 | 35.04 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3158.57 | 96.62 | 1.044x |
| medium / 8000000 | C++ index arena / open table (msvc) | 1008.25 | 43.18 | 0.333x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 2997.13 | 96.62 | 0.990x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 890.77 | 43.17 | 0.294x |
| medium / 8000000 | Rust lru_hashmap | 1414.77 | 46.92 | 0.467x |
| medium / 8000000 | Rust lru_open | 942.56 | 37.18 | 0.311x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 13672.60 | 128.82 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 13710.83 | 416.30 | 1.003x |
| large / 32000000 | C++ index arena / open table (msvc) | 4627.29 | 175.88 | 0.338x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 13007.35 | 416.30 | 0.951x |
| large / 32000000 | C++ index arena / open table (clangcl) | 3942.17 | 175.88 | 0.288x |
| large / 32000000 | Rust lru_hashmap | 7593.34 | 169.71 | 0.555x |
| large / 32000000 | Rust lru_open | 4428.42 | 133.19 | 0.324x |

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

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 61.63 | 7.59 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 23.18 | 11.00 | 0.376x |
| small / 1000000 | C++ reserved vector (msvc) | 20.72 | 7.59 | 0.336x |
| small / 1000000 | C++ vector (clangcl) | 23.47 | 11.00 | 0.381x |
| small / 1000000 | C++ reserved vector (clangcl) | 21.29 | 7.59 | 0.345x |
| small / 1000000 | Rust sum | 21.81 | 8.02 | 0.354x |
| medium / 16000000 | R^ LLVM/Clang | 773.63 | 64.81 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 138.56 | 96.89 | 0.179x |
| medium / 16000000 | C++ reserved vector (msvc) | 100.66 | 64.81 | 0.130x |
| medium / 16000000 | C++ vector (clangcl) | 137.01 | 96.89 | 0.177x |
| medium / 16000000 | C++ reserved vector (clangcl) | 119.30 | 64.81 | 0.154x |
| medium / 16000000 | Rust sum | 95.63 | 65.25 | 0.124x |
| large / 128000000 | R^ LLVM/Clang | 6358.22 | 492.06 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 915.75 | 698.49 | 0.144x |
| large / 128000000 | C++ reserved vector (msvc) | 710.66 | 492.06 | 0.112x |
| large / 128000000 | C++ vector (clangcl) | 1066.96 | 698.49 | 0.168x |
| large / 128000000 | C++ reserved vector (clangcl) | 857.60 | 492.06 | 0.135x |
| large / 128000000 | Rust sum | 651.10 | 492.50 | 0.102x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 333.93 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 63.92 | 7.68 | 0.191x |
| small / 1024 | C++ row pointers (msvc) | 33.70 | 5.77 | 0.101x |
| small / 1024 | C++ flat (clangcl) | 61.17 | 7.68 | 0.183x |
| small / 1024 | C++ row pointers (clangcl) | 25.23 | 5.79 | 0.076x |
| small / 1024 | Rust blur_index | 26.98 | 7.41 | 0.081x |
| small / 1024 | Rust blur_windows | 26.14 | 7.41 | 0.078x |
| medium / 2048 | R^ LLVM/Clang | 1301.01 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 201.29 | 15.81 | 0.155x |
| medium / 2048 | C++ row pointers (msvc) | 83.86 | 11.77 | 0.064x |
| medium / 2048 | C++ flat (clangcl) | 216.96 | 15.80 | 0.167x |
| medium / 2048 | C++ row pointers (clangcl) | 53.70 | 11.79 | 0.041x |
| medium / 2048 | Rust blur_index | 57.81 | 13.41 | 0.044x |
| medium / 2048 | Rust blur_windows | 56.79 | 13.41 | 0.044x |
| large / 8192 | R^ LLVM/Clang | 21609.55 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3026.90 | 178.88 | 0.140x |
| large / 8192 | C++ row pointers (msvc) | 1079.90 | 131.77 | 0.050x |
| large / 8192 | C++ flat (clangcl) | 3440.59 | 178.87 | 0.159x |
| large / 8192 | C++ row pointers (clangcl) | 566.55 | 131.79 | 0.026x |
| large / 8192 | Rust blur_index | 668.70 | 133.41 | 0.031x |
| large / 8192 | Rust blur_windows | 671.00 | 133.41 | 0.031x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 95.49 | 6.07 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 54.96 | 6.07 | 0.576x |
| small / 100000 | C++ SoA (msvc) | 149.44 | 6.08 | 1.565x |
| small / 100000 | C++ vector AoS (msvc) | 60.62 | 6.07 | 0.635x |
| small / 100000 | C++ scalar AoS (clangcl) | 45.40 | 6.07 | 0.475x |
| small / 100000 | C++ SoA (clangcl) | 88.79 | 6.07 | 0.930x |
| small / 100000 | C++ vector AoS (clangcl) | 49.99 | 6.07 | 0.523x |
| small / 100000 | Rust particles_scalar | 46.13 | 6.51 | 0.483x |
| small / 100000 | Rust particles | 51.04 | 6.51 | 0.535x |
| medium / 1000000 | R^ LLVM/Clang | 925.38 | 26.67 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 641.64 | 26.67 | 0.693x |
| medium / 1000000 | C++ SoA (msvc) | 2618.60 | 26.67 | 2.830x |
| medium / 1000000 | C++ vector AoS (msvc) | 668.70 | 26.67 | 0.723x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 610.15 | 26.67 | 0.659x |
| medium / 1000000 | C++ SoA (clangcl) | 2597.73 | 26.67 | 2.807x |
| medium / 1000000 | C++ vector AoS (clangcl) | 625.13 | 26.67 | 0.676x |
| medium / 1000000 | Rust particles_scalar | 619.58 | 27.11 | 0.670x |
| medium / 1000000 | Rust particles | 635.44 | 27.11 | 0.687x |
| large / 4000000 | R^ LLVM/Clang | 3718.61 | 95.33 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2549.34 | 95.33 | 0.686x |
| large / 4000000 | C++ SoA (msvc) | 11416.97 | 95.34 | 3.070x |
| large / 4000000 | C++ vector AoS (msvc) | 2669.71 | 95.33 | 0.718x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2478.54 | 95.33 | 0.667x |
| large / 4000000 | C++ SoA (clangcl) | 10629.52 | 95.34 | 2.858x |
| large / 4000000 | C++ vector AoS (clangcl) | 2529.82 | 95.33 | 0.680x |
| large / 4000000 | Rust particles_scalar | 2464.58 | 95.77 | 0.663x |
| large / 4000000 | Rust particles | 2505.73 | 95.77 | 0.674x |

## tree

R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ u32-index node pool (LLVM/Clang) | 31.93 | 5.28 | 1.000x |
| small / 16 | C++ unique_ptr (msvc) | 28.38 | 7.82 | 0.889x |
| small / 16 | C++ new/delete (msvc) | 27.77 | 7.79 | 0.870x |
| small / 16 | C++ index arena (msvc) | 20.31 | 5.27 | 0.636x |
| small / 16 | C++ unique_ptr (clangcl) | 28.04 | 7.82 | 0.878x |
| small / 16 | C++ new/delete (clangcl) | 27.59 | 7.79 | 0.864x |
| small / 16 | C++ index arena (clangcl) | 21.48 | 5.28 | 0.673x |
| small / 16 | Rust tree_box | 30.32 | 8.25 | 0.950x |
| small / 16 | Rust tree_arena | 20.89 | 5.72 | 0.654x |
| medium / 20 | R^ u32-index node pool (LLVM/Clang) | 290.56 | 27.78 | 1.000x |
| medium / 20 | C++ unique_ptr (msvc) | 288.72 | 68.25 | 0.994x |
| medium / 20 | C++ new/delete (msvc) | 274.91 | 68.22 | 0.946x |
| medium / 20 | C++ index arena (msvc) | 114.32 | 27.77 | 0.393x |
| medium / 20 | C++ unique_ptr (clangcl) | 285.77 | 68.25 | 0.984x |
| medium / 20 | C++ new/delete (clangcl) | 267.53 | 68.22 | 0.921x |
| medium / 20 | C++ index arena (clangcl) | 113.70 | 27.78 | 0.391x |
| medium / 20 | Rust tree_box | 297.13 | 68.69 | 1.023x |
| medium / 20 | Rust tree_arena | 98.77 | 28.22 | 0.340x |
| large / 24 | R^ u32-index node pool (LLVM/Clang) | 4492.72 | 387.78 | 1.000x |
| large / 24 | C++ unique_ptr (msvc) | 4619.90 | 1034.96 | 1.028x |
| large / 24 | C++ new/delete (msvc) | 4398.36 | 1034.93 | 0.979x |
| large / 24 | C++ index arena (msvc) | 1541.93 | 387.77 | 0.343x |
| large / 24 | C++ unique_ptr (clangcl) | 4480.94 | 1034.96 | 0.997x |
| large / 24 | C++ new/delete (clangcl) | 4324.76 | 1034.92 | 0.963x |
| large / 24 | C++ index arena (clangcl) | 1536.07 | 387.78 | 0.342x |
| large / 24 | Rust tree_box | 4567.60 | 1035.38 | 1.017x |
| large / 24 | Rust tree_arena | 1285.69 | 388.22 | 0.286x |

## graph

R^ uses 8-byte edges (i32 target, u32 next index), including one list-head slot per vertex. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ linked u32-index pool (LLVM/Clang) | 218.75 | 11.42 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 124.26 | 13.57 | 0.568x |
| small / 100000 | C++ CSR (msvc) | 41.09 | 14.47 | 0.188x |
| small / 100000 | C++ linked index arena (msvc) | 87.49 | 11.41 | 0.400x |
| small / 100000 | C++ nested vectors (clangcl) | 126.99 | 13.57 | 0.580x |
| small / 100000 | C++ CSR (clangcl) | 43.46 | 14.48 | 0.199x |
| small / 100000 | C++ linked index arena (clangcl) | 87.98 | 11.41 | 0.402x |
| small / 100000 | Rust graph_nested | 95.43 | 15.60 | 0.436x |
| small / 100000 | Rust graph_csr | 44.22 | 14.93 | 0.202x |
| small / 100000 | Rust graph_arena | 107.78 | 11.86 | 0.493x |
| medium / 500000 | R^ linked u32-index pool (LLVM/Clang) | 2253.00 | 41.94 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1166.36 | 52.44 | 0.518x |
| medium / 500000 | C++ CSR (msvc) | 305.56 | 57.20 | 0.136x |
| medium / 500000 | C++ linked index arena (msvc) | 1368.59 | 41.93 | 0.607x |
| medium / 500000 | C++ nested vectors (clangcl) | 1146.53 | 52.45 | 0.509x |
| medium / 500000 | C++ CSR (clangcl) | 332.87 | 57.21 | 0.148x |
| medium / 500000 | C++ linked index arena (clangcl) | 1181.44 | 41.94 | 0.524x |
| medium / 500000 | Rust graph_nested | 743.79 | 60.92 | 0.330x |
| medium / 500000 | Rust graph_csr | 342.04 | 57.65 | 0.152x |
| medium / 500000 | Rust graph_arena | 1267.99 | 42.38 | 0.563x |
| large / 2000000 | R^ linked u32-index pool (LLVM/Clang) | 11734.53 | 156.38 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 5709.79 | 207.09 | 0.487x |
| large / 2000000 | C++ CSR (msvc) | 2005.47 | 217.42 | 0.171x |
| large / 2000000 | C++ linked index arena (msvc) | 7231.95 | 156.37 | 0.616x |
| large / 2000000 | C++ nested vectors (clangcl) | 5630.21 | 207.10 | 0.480x |
| large / 2000000 | C++ CSR (clangcl) | 2098.03 | 217.43 | 0.179x |
| large / 2000000 | C++ linked index arena (clangcl) | 6217.47 | 156.38 | 0.530x |
| large / 2000000 | Rust graph_nested | 3586.74 | 244.95 | 0.306x |
| large / 2000000 | Rust graph_csr | 2011.34 | 217.87 | 0.171x |
| large / 2000000 | Rust graph_arena | 6788.25 | 156.82 | 0.578x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
