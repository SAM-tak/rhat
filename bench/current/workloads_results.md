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

## respond

R^ ports the original respond_stream.goose variant, generating the same requests and JSON bytes, including real quote/backslash escaping. The output byte region is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, item counts, quantities, prices and all three checksums are preserved. The DTO-building Goose variant and general nested owned strings/arrays are not implemented by this port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 291.48 | 4.20 | 1.000x |
| small / 100000 | C++ response DTO (msvc) | 182.19 | 4.27 | 0.625x |
| small / 100000 | C++ streaming JSON (msvc) | 84.46 | 4.18 | 0.290x |
| small / 100000 | C++ response DTO (clangcl) | 158.84 | 4.27 | 0.545x |
| small / 100000 | C++ streaming JSON (clangcl) | 67.02 | 4.20 | 0.230x |
| small / 100000 | Rust respond_dto | 218.94 | 4.74 | 0.751x |
| small / 100000 | Rust respond_stream | 99.41 | 4.70 | 0.341x |
| medium / 400000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 1204.05 | 4.20 | 1.000x |
| medium / 400000 | C++ response DTO (msvc) | 686.96 | 4.27 | 0.571x |
| medium / 400000 | C++ streaming JSON (msvc) | 266.54 | 4.18 | 0.221x |
| medium / 400000 | C++ response DTO (clangcl) | 585.15 | 4.27 | 0.486x |
| medium / 400000 | C++ streaming JSON (clangcl) | 226.65 | 4.20 | 0.188x |
| medium / 400000 | Rust respond_dto | 842.73 | 4.73 | 0.700x |
| medium / 400000 | Rust respond_stream | 343.15 | 4.69 | 0.285x |
| large / 1600000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 5198.96 | 4.20 | 1.000x |
| large / 1600000 | C++ response DTO (msvc) | 2921.14 | 4.27 | 0.562x |
| large / 1600000 | C++ streaming JSON (msvc) | 1120.55 | 4.18 | 0.216x |
| large / 1600000 | C++ response DTO (clangcl) | 2429.28 | 4.27 | 0.467x |
| large / 1600000 | C++ streaming JSON (clangcl) | 852.10 | 4.20 | 0.164x |
| large / 1600000 | Rust respond_dto | 3424.99 | 4.71 | 0.659x |
| large / 1600000 | Rust respond_stream | 1407.51 | 4.68 | 0.271x |

## words

R^ stores vocabulary entries as 21-byte fixed-capacity words and copies split words into one owned packed byte region (one-byte varint lengths). The open-addressed table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, skewed word selection, RNG sequence, text construction, split and all three checksums are preserved. Packed strings are a library representation; general variable-size array elements and a built-in dictionary are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 159.75 | 19.98 | 1.000x |
| small / 500000 | C++ owned-key unordered_map (msvc) | 86.33 | 25.00 | 0.540x |
| small / 500000 | C++ borrowed-key unordered_map (msvc) | 84.21 | 24.98 | 0.527x |
| small / 500000 | C++ open table (msvc) | 77.21 | 25.01 | 0.483x |
| small / 500000 | C++ owned-key unordered_map (clangcl) | 72.51 | 25.00 | 0.454x |
| small / 500000 | C++ borrowed-key unordered_map (clangcl) | 82.92 | 25.01 | 0.519x |
| small / 500000 | C++ open table (clangcl) | 78.60 | 25.01 | 0.492x |
| small / 500000 | Rust words_hashmap | 79.25 | 22.42 | 0.496x |
| small / 500000 | Rust words_open | 71.14 | 25.21 | 0.445x |
| medium / 4000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 1830.04 | 132.48 | 1.000x |
| medium / 4000000 | C++ owned-key unordered_map (msvc) | 1339.12 | 161.32 | 0.732x |
| medium / 4000000 | C++ borrowed-key unordered_map (msvc) | 1301.64 | 161.30 | 0.711x |
| medium / 4000000 | C++ open table (msvc) | 865.85 | 162.30 | 0.473x |
| medium / 4000000 | C++ owned-key unordered_map (clangcl) | 1179.47 | 161.32 | 0.645x |
| medium / 4000000 | C++ borrowed-key unordered_map (clangcl) | 1176.77 | 161.34 | 0.643x |
| medium / 4000000 | C++ open table (clangcl) | 802.34 | 162.28 | 0.438x |
| medium / 4000000 | Rust words_hashmap | 1229.24 | 142.12 | 0.672x |
| medium / 4000000 | Rust words_open | 888.91 | 172.05 | 0.486x |
| large / 16000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 9267.37 | 517.83 | 1.000x |
| large / 16000000 | C++ owned-key unordered_map (msvc) | 6425.83 | 563.45 | 0.693x |
| large / 16000000 | C++ borrowed-key unordered_map (msvc) | 6863.84 | 563.45 | 0.741x |
| large / 16000000 | C++ open table (msvc) | 4451.01 | 634.69 | 0.480x |
| large / 16000000 | C++ owned-key unordered_map (clangcl) | 5735.73 | 563.45 | 0.619x |
| large / 16000000 | C++ borrowed-key unordered_map (clangcl) | 6078.55 | 563.43 | 0.656x |
| large / 16000000 | C++ open table (clangcl) | 3779.40 | 634.67 | 0.408x |
| large / 16000000 | Rust words_hashmap | 6800.21 | 555.80 | 0.734x |
| large / 16000000 | Rust words_open | 4366.83 | 672.64 | 0.471x |

## strlist

R^ copies each split word into a single owned byte region, prefixed by its one-byte varint length (original generated lengths are 3..20). The original text and copied word list coexist; scans decode the packed lengths four times. A WordList library struct holds the count and owned data region. This does not claim general variable-size array elements or nested owned strings in the language. No word is replaced by a borrowed view or offset pair. All original sizes, seed sequence, text splitting, copies, passes and two checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 200000 | R^ owned packed word list (LLVM/Clang) | 68.79 | 8.66 | 1.000x |
| small / 200000 | C++ owned strings (msvc) | 49.02 | 16.45 | 0.713x |
| small / 200000 | C++ borrowed string views (msvc) | 33.67 | 11.31 | 0.490x |
| small / 200000 | C++ flat offsets (msvc) | 32.85 | 9.35 | 0.478x |
| small / 200000 | C++ owned strings (clangcl) | 46.78 | 16.46 | 0.680x |
| small / 200000 | C++ borrowed string views (clangcl) | 29.71 | 11.32 | 0.432x |
| small / 200000 | C++ flat offsets (clangcl) | 28.39 | 9.32 | 0.413x |
| small / 200000 | Rust strlist_owned | 50.03 | 16.42 | 0.727x |
| small / 200000 | Rust strlist_borrowed | 30.79 | 11.38 | 0.448x |
| medium / 2000000 | R^ owned packed word list (LLVM/Clang) | 556.01 | 51.54 | 1.000x |
| medium / 2000000 | C++ owned strings (msvc) | 378.78 | 144.04 | 0.681x |
| medium / 2000000 | C++ borrowed string views (msvc) | 201.76 | 76.62 | 0.363x |
| medium / 2000000 | C++ flat offsets (msvc) | 192.78 | 55.46 | 0.347x |
| medium / 2000000 | C++ owned strings (clangcl) | 342.00 | 144.02 | 0.615x |
| medium / 2000000 | C++ borrowed string views (clangcl) | 172.39 | 76.60 | 0.310x |
| medium / 2000000 | C++ flat offsets (clangcl) | 161.21 | 55.44 | 0.290x |
| medium / 2000000 | Rust strlist_owned | 362.42 | 125.23 | 0.652x |
| medium / 2000000 | Rust strlist_borrowed | 172.43 | 60.81 | 0.310x |
| large / 8000000 | R^ owned packed word list (LLVM/Clang) | 2162.96 | 194.54 | 1.000x |
| large / 8000000 | C++ owned strings (msvc) | 1554.69 | 687.74 | 0.719x |
| large / 8000000 | C++ borrowed string views (msvc) | 775.35 | 343.35 | 0.358x |
| large / 8000000 | C++ flat offsets (msvc) | 722.13 | 191.15 | 0.334x |
| large / 8000000 | C++ owned strings (clangcl) | 1375.59 | 687.73 | 0.636x |
| large / 8000000 | C++ borrowed string views (clangcl) | 639.52 | 343.36 | 0.296x |
| large / 8000000 | C++ flat offsets (clangcl) | 569.29 | 191.13 | 0.263x |
| large / 8000000 | Rust strlist_owned | 1409.88 | 487.77 | 0.652x |
| large / 8000000 | Rust strlist_borrowed | 663.16 | 228.31 | 0.307x |

## scene

R^ uses 120-byte nodes with inline local/world transforms, four u32 child indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and f32 matrix/translation arithmetic are preserved. Composition writes through references to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array storage; Goose's 117-byte limited-array/relative-reference layout is not claimed.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 12 | R^ inline u32-child arena (LLVM/Clang) | 132.03 | 8.47 | 1.000x |
| small / 12 | C++ owning-pointer tree (msvc) | 60.47 | 10.11 | 0.458x |
| small / 12 | C++ index arena (msvc) | 35.08 | 10.91 | 0.266x |
| small / 12 | C++ owning-pointer tree (clangcl) | 55.59 | 10.12 | 0.421x |
| small / 12 | C++ index arena (clangcl) | 31.71 | 10.91 | 0.240x |
| small / 12 | Rust scene_box | 60.33 | 10.82 | 0.457x |
| small / 12 | Rust scene_arena | 33.88 | 13.57 | 0.257x |
| medium / 14 | R^ inline u32-child arena (LLVM/Clang) | 785.17 | 32.91 | 1.000x |
| medium / 14 | C++ owning-pointer tree (msvc) | 552.49 | 41.99 | 0.704x |
| medium / 14 | C++ index arena (msvc) | 188.19 | 52.12 | 0.240x |
| medium / 14 | C++ owning-pointer tree (clangcl) | 531.67 | 42.00 | 0.677x |
| medium / 14 | C++ index arena (clangcl) | 183.44 | 52.12 | 0.234x |
| medium / 14 | Rust scene_box | 635.65 | 43.95 | 0.810x |
| medium / 14 | Rust scene_arena | 158.29 | 36.05 | 0.202x |
| large / 16 | R^ inline u32-child arena (LLVM/Clang) | 4864.35 | 185.91 | 1.000x |
| large / 16 | C++ owning-pointer tree (msvc) | 3249.06 | 242.62 | 0.668x |
| large / 16 | C++ index arena (msvc) | 1175.81 | 365.10 | 0.242x |
| large / 16 | C++ owning-pointer tree (clangcl) | 3360.74 | 242.62 | 0.691x |
| large / 16 | C++ index arena (clangcl) | 1091.82 | 365.10 | 0.224x |
| large / 16 | Rust scene_box | 3165.44 | 252.75 | 0.651x |
| large / 16 | Rust scene_arena | 982.61 | 246.08 | 0.202x |

## lru

R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table representation, including hash, eviction, explicit removal and recency walk. Original sizes, seed and all six checksum lines are preserved. Indices are ordinary integers, without Goose's pool-bound relative-reference guarantees or generation checks. Arrays use statically assigned stable 1 GiB data stacks and retain committed pages when their logical length decreases.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 2000000 | R^ reusable index arena / open table (LLVM/Clang) | 569.94 | 11.67 | 1.000x |
| small / 2000000 | C++ list / unordered_map (msvc) | 710.54 | 27.05 | 1.247x |
| small / 2000000 | C++ index arena / open table (msvc) | 167.11 | 15.65 | 0.293x |
| small / 2000000 | C++ list / unordered_map (clangcl) | 648.36 | 27.05 | 1.138x |
| small / 2000000 | C++ index arena / open table (clangcl) | 141.25 | 15.64 | 0.248x |
| small / 2000000 | Rust lru_hashmap | 198.62 | 15.66 | 0.348x |
| small / 2000000 | Rust lru_open | 144.82 | 13.20 | 0.254x |
| medium / 8000000 | R^ reusable index arena / open table (LLVM/Clang) | 3113.86 | 35.11 | 1.000x |
| medium / 8000000 | C++ list / unordered_map (msvc) | 3463.17 | 96.62 | 1.112x |
| medium / 8000000 | C++ index arena / open table (msvc) | 961.41 | 41.11 | 0.309x |
| medium / 8000000 | C++ list / unordered_map (clangcl) | 3236.86 | 96.62 | 1.039x |
| medium / 8000000 | C++ index arena / open table (clangcl) | 910.53 | 41.11 | 0.292x |
| medium / 8000000 | Rust lru_hashmap | 1495.25 | 46.89 | 0.480x |
| medium / 8000000 | Rust lru_open | 926.55 | 37.20 | 0.298x |
| large / 32000000 | R^ reusable index arena / open table (LLVM/Clang) | 14739.87 | 128.87 | 1.000x |
| large / 32000000 | C++ list / unordered_map (msvc) | 14823.35 | 416.30 | 1.006x |
| large / 32000000 | C++ index arena / open table (msvc) | 5058.43 | 175.88 | 0.343x |
| large / 32000000 | C++ list / unordered_map (clangcl) | 14489.93 | 416.30 | 0.983x |
| large / 32000000 | C++ index arena / open table (clangcl) | 4514.32 | 175.88 | 0.306x |
| large / 32000000 | Rust lru_hashmap | 8772.43 | 169.71 | 0.595x |
| large / 32000000 | Rust lru_open | 5224.72 | 133.19 | 0.354x |

## push

R^ stores 8-byte Items and retains real Item references returned by push, marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual address space and commits only used pages; elements never move. The original sizes, RNG, two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ stable region / real pointer marks (LLVM/Clang) | 61.39 | 11.59 | 1.000x |
| small / 1000000 | C++ vector indices (msvc) | 28.89 | 14.66 | 0.471x |
| small / 1000000 | C++ reserved vector indices (msvc) | 21.87 | 11.74 | 0.356x |
| small / 1000000 | C++ deque pointers (msvc) | 61.71 | 23.55 | 1.005x |
| small / 1000000 | C++ vector indices (clangcl) | 29.48 | 14.65 | 0.480x |
| small / 1000000 | C++ reserved vector indices (clangcl) | 25.47 | 11.74 | 0.415x |
| small / 1000000 | C++ deque pointers (clangcl) | 56.67 | 23.56 | 0.923x |
| small / 1000000 | Rust push | 21.60 | 12.04 | 0.352x |
| medium / 16000000 | R^ stable region / real pointer marks (LLVM/Clang) | 810.96 | 127.84 | 1.000x |
| medium / 16000000 | C++ vector indices (msvc) | 237.91 | 189.19 | 0.293x |
| medium / 16000000 | C++ reserved vector indices (msvc) | 105.46 | 129.55 | 0.130x |
| medium / 16000000 | C++ deque pointers (msvc) | 824.89 | 315.93 | 1.017x |
| medium / 16000000 | C++ vector indices (clangcl) | 245.75 | 189.19 | 0.303x |
| medium / 16000000 | C++ reserved vector indices (clangcl) | 172.25 | 129.52 | 0.212x |
| medium / 16000000 | C++ deque pointers (clangcl) | 745.03 | 315.93 | 0.919x |
| medium / 16000000 | Rust push | 115.12 | 129.21 | 0.142x |
| large / 64000000 | R^ stable region / real pointer marks (LLVM/Clang) | 3413.73 | 499.79 | 1.000x |
| large / 64000000 | C++ vector indices (msvc) | 1030.51 | 936.29 | 0.302x |
| large / 64000000 | C++ reserved vector indices (msvc) | 402.74 | 501.45 | 0.118x |
| large / 64000000 | C++ deque pointers (msvc) | 3429.72 | 1251.09 | 1.005x |
| large / 64000000 | C++ vector indices (clangcl) | 1071.73 | 936.27 | 0.314x |
| large / 64000000 | C++ reserved vector indices (clangcl) | 670.32 | 501.45 | 0.196x |
| large / 64000000 | C++ deque pointers (clangcl) | 3266.61 | 1251.07 | 0.957x |
| large / 64000000 | Rust push | 380.73 | 501.11 | 0.112x |

## sum

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1000000 | R^ LLVM/Clang | 46.21 | 7.66 | 1.000x |
| small / 1000000 | C++ vector (msvc) | 23.16 | 11.03 | 0.501x |
| small / 1000000 | C++ reserved vector (msvc) | 21.56 | 7.59 | 0.467x |
| small / 1000000 | C++ vector (clangcl) | 23.17 | 11.03 | 0.501x |
| small / 1000000 | C++ reserved vector (clangcl) | 20.95 | 7.59 | 0.453x |
| small / 1000000 | Rust sum | 21.78 | 8.03 | 0.471x |
| medium / 16000000 | R^ LLVM/Clang | 534.12 | 64.84 | 1.000x |
| medium / 16000000 | C++ vector (msvc) | 143.57 | 96.93 | 0.269x |
| medium / 16000000 | C++ reserved vector (msvc) | 110.92 | 64.81 | 0.208x |
| medium / 16000000 | C++ vector (clangcl) | 155.42 | 96.93 | 0.291x |
| medium / 16000000 | C++ reserved vector (clangcl) | 120.80 | 64.82 | 0.226x |
| medium / 16000000 | Rust sum | 103.25 | 65.25 | 0.193x |
| large / 128000000 | R^ LLVM/Clang | 4209.05 | 492.09 | 1.000x |
| large / 128000000 | C++ vector (msvc) | 1045.84 | 698.52 | 0.248x |
| large / 128000000 | C++ reserved vector (msvc) | 793.23 | 492.06 | 0.188x |
| large / 128000000 | C++ vector (clangcl) | 1167.91 | 698.52 | 0.277x |
| large / 128000000 | C++ reserved vector (clangcl) | 893.21 | 492.06 | 0.212x |
| large / 128000000 | Rust sum | 747.62 | 492.50 | 0.178x |

## blur

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 1024 | R^ LLVM/Clang | 295.93 | 5.78 | 1.000x |
| small / 1024 | C++ flat (msvc) | 68.92 | 6.44 | 0.233x |
| small / 1024 | C++ row pointers (msvc) | 33.58 | 5.77 | 0.113x |
| small / 1024 | C++ flat (clangcl) | 67.63 | 6.44 | 0.229x |
| small / 1024 | C++ row pointers (clangcl) | 26.14 | 5.79 | 0.088x |
| small / 1024 | Rust blur_index | 31.28 | 7.39 | 0.106x |
| small / 1024 | Rust blur_windows | 28.69 | 7.41 | 0.097x |
| medium / 2048 | R^ LLVM/Clang | 1091.49 | 11.78 | 1.000x |
| medium / 2048 | C++ flat (msvc) | 239.31 | 14.56 | 0.219x |
| medium / 2048 | C++ row pointers (msvc) | 93.63 | 11.77 | 0.086x |
| medium / 2048 | C++ flat (clangcl) | 237.42 | 14.56 | 0.218x |
| medium / 2048 | C++ row pointers (clangcl) | 60.14 | 11.79 | 0.055x |
| medium / 2048 | Rust blur_index | 62.41 | 13.41 | 0.057x |
| medium / 2048 | Rust blur_windows | 61.73 | 13.41 | 0.057x |
| large / 8192 | R^ LLVM/Clang | 18483.12 | 131.78 | 1.000x |
| large / 8192 | C++ flat (msvc) | 3304.60 | 177.63 | 0.179x |
| large / 8192 | C++ row pointers (msvc) | 1244.67 | 131.77 | 0.067x |
| large / 8192 | C++ flat (clangcl) | 3746.41 | 177.62 | 0.203x |
| large / 8192 | C++ row pointers (clangcl) | 610.75 | 131.79 | 0.033x |
| large / 8192 | Rust blur_index | 714.49 | 133.41 | 0.039x |
| large / 8192 | Rust blur_windows | 703.77 | 133.41 | 0.038x |

## particles

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ LLVM/Clang | 95.27 | 6.09 | 1.000x |
| small / 100000 | C++ scalar AoS (msvc) | 53.00 | 6.07 | 0.556x |
| small / 100000 | C++ SoA (msvc) | 156.18 | 6.07 | 1.639x |
| small / 100000 | C++ vector AoS (msvc) | 67.23 | 6.07 | 0.706x |
| small / 100000 | C++ scalar AoS (clangcl) | 49.30 | 6.07 | 0.517x |
| small / 100000 | C++ SoA (clangcl) | 98.13 | 6.07 | 1.030x |
| small / 100000 | C++ vector AoS (clangcl) | 56.64 | 6.07 | 0.594x |
| small / 100000 | Rust particles_scalar | 47.07 | 6.51 | 0.494x |
| small / 100000 | Rust particles | 55.29 | 6.51 | 0.580x |
| medium / 1000000 | R^ LLVM/Clang | 918.65 | 26.72 | 1.000x |
| medium / 1000000 | C++ scalar AoS (msvc) | 686.29 | 26.67 | 0.747x |
| medium / 1000000 | C++ SoA (msvc) | 2878.18 | 26.67 | 3.133x |
| medium / 1000000 | C++ vector AoS (msvc) | 715.48 | 26.67 | 0.779x |
| medium / 1000000 | C++ scalar AoS (clangcl) | 672.20 | 26.67 | 0.732x |
| medium / 1000000 | C++ SoA (clangcl) | 2706.16 | 26.67 | 2.946x |
| medium / 1000000 | C++ vector AoS (clangcl) | 647.69 | 26.67 | 0.705x |
| medium / 1000000 | Rust particles_scalar | 671.95 | 27.11 | 0.731x |
| medium / 1000000 | Rust particles | 706.80 | 27.11 | 0.769x |
| large / 4000000 | R^ LLVM/Clang | 3840.48 | 95.34 | 1.000x |
| large / 4000000 | C++ scalar AoS (msvc) | 2760.99 | 95.33 | 0.719x |
| large / 4000000 | C++ SoA (msvc) | 12526.58 | 95.34 | 3.262x |
| large / 4000000 | C++ vector AoS (msvc) | 2772.55 | 95.33 | 0.722x |
| large / 4000000 | C++ scalar AoS (clangcl) | 2598.98 | 95.33 | 0.677x |
| large / 4000000 | C++ SoA (clangcl) | 11255.81 | 95.34 | 2.931x |
| large / 4000000 | C++ vector AoS (clangcl) | 2676.79 | 95.33 | 0.697x |
| large / 4000000 | Rust particles_scalar | 2572.18 | 95.78 | 0.670x |
| large / 4000000 | Rust particles | 2643.59 | 95.78 | 0.688x |

## tree

R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved.

| Size / depth | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 16 | R^ u32-index node pool (LLVM/Clang) | 29.60 | 5.28 | 1.000x |
| small / 16 | C++ unique_ptr (msvc) | 26.34 | 7.81 | 0.890x |
| small / 16 | C++ new/delete (msvc) | 26.11 | 7.79 | 0.882x |
| small / 16 | C++ index arena (msvc) | 18.11 | 5.27 | 0.612x |
| small / 16 | C++ unique_ptr (clangcl) | 28.16 | 7.82 | 0.952x |
| small / 16 | C++ new/delete (clangcl) | 27.64 | 7.79 | 0.934x |
| small / 16 | C++ index arena (clangcl) | 19.53 | 5.28 | 0.660x |
| small / 16 | Rust tree_box | 28.34 | 8.25 | 0.957x |
| small / 16 | Rust tree_arena | 19.47 | 5.71 | 0.658x |
| medium / 20 | R^ u32-index node pool (LLVM/Clang) | 264.44 | 27.78 | 1.000x |
| medium / 20 | C++ unique_ptr (msvc) | 305.48 | 68.25 | 1.155x |
| medium / 20 | C++ new/delete (msvc) | 286.27 | 68.22 | 1.083x |
| medium / 20 | C++ index arena (msvc) | 119.93 | 27.77 | 0.454x |
| medium / 20 | C++ unique_ptr (clangcl) | 302.54 | 68.25 | 1.144x |
| medium / 20 | C++ new/delete (clangcl) | 275.79 | 68.22 | 1.043x |
| medium / 20 | C++ index arena (clangcl) | 112.64 | 27.78 | 0.426x |
| medium / 20 | Rust tree_box | 299.46 | 68.69 | 1.132x |
| medium / 20 | Rust tree_arena | 99.41 | 28.22 | 0.376x |
| large / 24 | R^ u32-index node pool (LLVM/Clang) | 4170.21 | 387.78 | 1.000x |
| large / 24 | C++ unique_ptr (msvc) | 5013.46 | 1034.96 | 1.202x |
| large / 24 | C++ new/delete (msvc) | 4747.02 | 1034.93 | 1.138x |
| large / 24 | C++ index arena (msvc) | 1648.72 | 387.77 | 0.395x |
| large / 24 | C++ unique_ptr (clangcl) | 4758.02 | 1034.96 | 1.141x |
| large / 24 | C++ new/delete (clangcl) | 4621.15 | 1034.92 | 1.108x |
| large / 24 | C++ index arena (clangcl) | 1580.88 | 387.78 | 0.379x |
| large / 24 | Rust tree_box | 4891.06 | 1035.39 | 1.173x |
| large / 24 | Rust tree_arena | 1418.32 | 388.22 | 0.340x |

## graph

R^ uses 8-byte edges (i32 target, u32 next index), including one list-head slot per vertex. Its queue overwrites an initialized prefix and pushes only when its high-water length grows; a separate tail resets between the four BFS sources. There is no unbounded append across sources. This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged.

| Size / vertices | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ linked u32-index pool (LLVM/Clang) | 254.94 | 11.53 | 1.000x |
| small / 100000 | C++ nested vectors (msvc) | 194.56 | 13.57 | 0.763x |
| small / 100000 | C++ CSR (msvc) | 45.49 | 14.47 | 0.178x |
| small / 100000 | C++ linked index arena (msvc) | 139.22 | 11.41 | 0.546x |
| small / 100000 | C++ nested vectors (clangcl) | 181.63 | 13.57 | 0.712x |
| small / 100000 | C++ CSR (clangcl) | 46.47 | 14.48 | 0.182x |
| small / 100000 | C++ linked index arena (clangcl) | 119.47 | 11.41 | 0.469x |
| small / 100000 | Rust graph_nested | 103.36 | 15.60 | 0.405x |
| small / 100000 | Rust graph_csr | 43.12 | 14.93 | 0.169x |
| small / 100000 | Rust graph_arena | 199.26 | 11.86 | 0.782x |
| medium / 500000 | R^ linked u32-index pool (LLVM/Clang) | 2181.21 | 42.03 | 1.000x |
| medium / 500000 | C++ nested vectors (msvc) | 1360.78 | 52.44 | 0.624x |
| medium / 500000 | C++ CSR (msvc) | 337.12 | 57.20 | 0.155x |
| medium / 500000 | C++ linked index arena (msvc) | 1501.71 | 41.93 | 0.688x |
| medium / 500000 | C++ nested vectors (clangcl) | 1282.28 | 52.45 | 0.588x |
| medium / 500000 | C++ CSR (clangcl) | 339.84 | 57.20 | 0.156x |
| medium / 500000 | C++ linked index arena (clangcl) | 1276.26 | 41.93 | 0.585x |
| medium / 500000 | Rust graph_nested | 823.41 | 60.94 | 0.378x |
| medium / 500000 | Rust graph_csr | 378.46 | 57.65 | 0.174x |
| medium / 500000 | Rust graph_arena | 1384.88 | 42.38 | 0.635x |
| large / 2000000 | R^ linked u32-index pool (LLVM/Clang) | 11452.43 | 156.52 | 1.000x |
| large / 2000000 | C++ nested vectors (msvc) | 6166.98 | 207.09 | 0.538x |
| large / 2000000 | C++ CSR (msvc) | 2304.42 | 217.41 | 0.201x |
| large / 2000000 | C++ linked index arena (msvc) | 7595.73 | 156.37 | 0.663x |
| large / 2000000 | C++ nested vectors (clangcl) | 6120.32 | 207.10 | 0.534x |
| large / 2000000 | C++ CSR (clangcl) | 2250.50 | 217.43 | 0.197x |
| large / 2000000 | C++ linked index arena (clangcl) | 6587.04 | 156.38 | 0.575x |
| large / 2000000 | Rust graph_nested | 3977.18 | 245.07 | 0.347x |
| large / 2000000 | Rust graph_csr | 2302.02 | 217.87 | 0.201x |
| large / 2000000 | Rust graph_arena | 7104.12 | 156.82 | 0.620x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
