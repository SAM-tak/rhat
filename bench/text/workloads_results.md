# Additional R^ workloads

Measured: 2026-10-23T19:47:09.819070+00:00 (UTC).

R^ replaces Goose. All comparison implementations were built and measured in this session.

Whole-process wall time, two warm-ups, best of 3 timed samples; peak resident working set.
R^ retains bounds/range checks and OS-backed, stable-address arrays. No affinity is set.

Comparison / R^ time: above 1 means R^ is faster. No suite-wide average is claimed.

Smoke sizes are correctness checks and are not performance evidence.

## Environment

| Item | Value |
|---|---|
| cpu | AMD Ryzen 5 2600 Six-Core Processor |
| os | Windows-10-10.0.26300-SP0 |
| commit | 555c77b2b33232521f985c722825a47145b8e30d |
| dirty | M README.md;  M bench/README.md;  M bench/coverage.md;  M bench/results.md;  M bench/run_bench.py;  M bench/run_workloads.py;  M bench/upstream/PROVENANCE.md;  M docs/design.md;  M runtime/rhat_runtime.c;  M src/front.c;  M src/llvm.c;  M src/rhat.h;  M src/sema.c;  M tests/run.py;  M tests/runtime_regions.c; ?? bench/lru/; ?? bench/rhat/lru.rh; ?? bench/rhat/respond.rh; ?? bench/rhat/scene.rh; ?? bench/rhat/strlist.rh; ?? bench/rhat/words.rh; ?? bench/scene/; ?? bench/upstream/cpp/lru.cpp; ?? bench/upstream/cpp/respond.cpp; ?? bench/upstream/cpp/scene.cpp; ?? bench/upstream/cpp/strlist.cpp; ?? bench/upstream/cpp/words.cpp; ?? bench/upstream/rust/lru_hashmap.rs; ?? bench/upstream/rust/lru_open.rs; ?? bench/upstream/rust/respond_dto.rs; ?? bench/upstream/rust/respond_stream.rs; ?? bench/upstream/rust/scene_arena.rs; ?? bench/upstream/rust/scene_box.rs; ?? bench/upstream/rust/strlist_borrowed.rs; ?? bench/upstream/rust/strlist_owned.rs; ?? bench/upstream/rust/words_hashmap.rs; ?? bench/upstream/rust/words_open.rs |
| compiler_sha256 | 6e705e9085323892536267fefa3dcd91d20611f0b5b97232a215cc518822e897 |
| runtime_sha256 | fe7f351421346c1d77421320872d779a8d76176506ef687d6ee6cee3fb2bda78 |
| clang | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| msvc | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 |
| clangcl | clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211) |
| rustc | rustc 1.93.1 (01f6ddf75 2026-02-11) |

## strlist

R^ copies each split word into a single owned byte region, prefixed by its one-byte varint length (original generated lengths are 3..20). The original text and copied word list coexist; scans decode the packed lengths four times. A WordList library struct holds the count and owned data region. This does not claim general variable-size array elements or nested owned strings in the language. No word is replaced by a borrowed view or offset pair. All original sizes, seed sequence, text splitting, copies, passes and two checksum lines are preserved.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 200000 | R^ owned packed word list (LLVM/Clang) | 106.93 | 8.55 | 1.000x |
| small / 200000 | C++ owned strings (msvc) | 50.45 | 16.47 | 0.472x |
| small / 200000 | C++ borrowed string views (msvc) | 34.07 | 11.29 | 0.319x |
| small / 200000 | C++ flat offsets (msvc) | 34.02 | 9.30 | 0.318x |
| small / 200000 | C++ owned strings (clangcl) | 43.96 | 16.45 | 0.411x |
| small / 200000 | C++ borrowed string views (clangcl) | 32.28 | 11.30 | 0.302x |
| small / 200000 | C++ flat offsets (clangcl) | 31.92 | 9.26 | 0.298x |
| small / 200000 | Rust strlist_owned | 55.26 | 16.45 | 0.517x |
| small / 200000 | Rust strlist_borrowed | 34.87 | 11.33 | 0.326x |
| medium / 2000000 | R^ owned packed word list (LLVM/Clang) | 950.99 | 51.46 | 1.000x |
| medium / 2000000 | C++ owned strings (msvc) | 372.09 | 144.03 | 0.391x |
| medium / 2000000 | C++ borrowed string views (msvc) | 198.97 | 76.59 | 0.209x |
| medium / 2000000 | C++ flat offsets (msvc) | 193.57 | 55.41 | 0.204x |
| medium / 2000000 | C++ owned strings (clangcl) | 326.82 | 144.02 | 0.344x |
| medium / 2000000 | C++ borrowed string views (clangcl) | 166.78 | 76.60 | 0.175x |
| medium / 2000000 | C++ flat offsets (clangcl) | 155.44 | 55.43 | 0.163x |
| medium / 2000000 | Rust strlist_owned | 343.85 | 125.24 | 0.362x |
| medium / 2000000 | Rust strlist_borrowed | 170.13 | 60.82 | 0.179x |
| large / 8000000 | R^ owned packed word list (LLVM/Clang) | 3914.97 | 194.45 | 1.000x |
| large / 8000000 | C++ owned strings (msvc) | 1495.86 | 687.72 | 0.382x |
| large / 8000000 | C++ borrowed string views (msvc) | 772.95 | 343.31 | 0.197x |
| large / 8000000 | C++ flat offsets (msvc) | 698.96 | 191.11 | 0.179x |
| large / 8000000 | C++ owned strings (clangcl) | 1276.50 | 687.74 | 0.326x |
| large / 8000000 | C++ borrowed string views (clangcl) | 611.34 | 343.30 | 0.156x |
| large / 8000000 | C++ flat offsets (clangcl) | 552.92 | 191.13 | 0.141x |
| large / 8000000 | Rust strlist_owned | 1359.47 | 487.80 | 0.347x |
| large / 8000000 | Rust strlist_borrowed | 622.42 | 228.30 | 0.159x |

## words

R^ stores vocabulary entries as 21-byte fixed-capacity words and copies split words into one owned packed byte region (one-byte varint lengths). The open-addressed table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, skewed word selection, RNG sequence, text construction, split and all three checksums are preserved. Packed strings are a library representation; general variable-size array elements and a built-in dictionary are not claimed.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 500000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 179.27 | 19.84 | 1.000x |
| small / 500000 | C++ owned-key unordered_map (msvc) | 84.71 | 24.99 | 0.473x |
| small / 500000 | C++ borrowed-key unordered_map (msvc) | 86.19 | 24.99 | 0.481x |
| small / 500000 | C++ open table (msvc) | 79.40 | 24.99 | 0.443x |
| small / 500000 | C++ owned-key unordered_map (clangcl) | 74.94 | 25.00 | 0.418x |
| small / 500000 | C++ borrowed-key unordered_map (clangcl) | 83.91 | 24.99 | 0.468x |
| small / 500000 | C++ open table (clangcl) | 69.00 | 24.99 | 0.385x |
| small / 500000 | Rust words_hashmap | 78.11 | 22.38 | 0.436x |
| small / 500000 | Rust words_open | 71.49 | 25.21 | 0.399x |
| medium / 4000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 1914.09 | 132.33 | 1.000x |
| medium / 4000000 | C++ owned-key unordered_map (msvc) | 1227.58 | 161.31 | 0.641x |
| medium / 4000000 | C++ borrowed-key unordered_map (msvc) | 1188.96 | 161.32 | 0.621x |
| medium / 4000000 | C++ open table (msvc) | 798.81 | 162.26 | 0.417x |
| medium / 4000000 | C++ owned-key unordered_map (clangcl) | 1009.05 | 161.32 | 0.527x |
| medium / 4000000 | C++ borrowed-key unordered_map (clangcl) | 1090.88 | 161.30 | 0.570x |
| medium / 4000000 | C++ open table (clangcl) | 742.89 | 162.29 | 0.388x |
| medium / 4000000 | Rust words_hashmap | 1067.24 | 142.14 | 0.558x |
| medium / 4000000 | Rust words_open | 824.20 | 172.02 | 0.431x |
| large / 16000000 | R^ packed words / borrowed-key open table (LLVM/Clang) | 9283.08 | 517.74 | 1.000x |
| large / 16000000 | C++ owned-key unordered_map (msvc) | 6092.41 | 563.43 | 0.656x |
| large / 16000000 | C++ borrowed-key unordered_map (msvc) | 6314.73 | 563.43 | 0.680x |
| large / 16000000 | C++ open table (msvc) | 3912.66 | 634.67 | 0.421x |
| large / 16000000 | C++ owned-key unordered_map (clangcl) | 5129.95 | 563.43 | 0.553x |
| large / 16000000 | C++ borrowed-key unordered_map (clangcl) | 5797.47 | 563.42 | 0.625x |
| large / 16000000 | C++ open table (clangcl) | 3702.08 | 634.69 | 0.399x |
| large / 16000000 | Rust words_hashmap | 6200.91 | 555.79 | 0.668x |
| large / 16000000 | Rust words_open | 3972.67 | 672.62 | 0.428x |

## respond

R^ ports the original respond_stream.goose variant, generating the same requests and JSON bytes, including real quote/backslash escaping. The output byte region is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, item counts, quantities, prices and all three checksums are preserved. The DTO-building Goose variant and general nested owned strings/arrays are not implemented by this port.

| Size / N | Implementation | Best ms | Peak MiB | Comparison / R^ time |
|---|---|---:|---:|---:|
| small / 100000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 411.29 | 4.18 | 1.000x |
| small / 100000 | C++ response DTO (msvc) | 183.75 | 4.27 | 0.447x |
| small / 100000 | C++ streaming JSON (msvc) | 81.69 | 4.18 | 0.199x |
| small / 100000 | C++ response DTO (clangcl) | 152.92 | 4.27 | 0.372x |
| small / 100000 | C++ streaming JSON (clangcl) | 65.62 | 4.20 | 0.160x |
| small / 100000 | Rust respond_dto | 207.89 | 4.68 | 0.505x |
| small / 100000 | Rust respond_stream | 96.68 | 4.68 | 0.235x |
| medium / 400000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 1563.33 | 4.18 | 1.000x |
| medium / 400000 | C++ response DTO (msvc) | 668.49 | 4.27 | 0.428x |
| medium / 400000 | C++ streaming JSON (msvc) | 264.21 | 4.18 | 0.169x |
| medium / 400000 | C++ response DTO (clangcl) | 563.16 | 4.27 | 0.360x |
| medium / 400000 | C++ streaming JSON (clangcl) | 215.48 | 4.20 | 0.138x |
| medium / 400000 | Rust respond_dto | 778.70 | 4.68 | 0.498x |
| medium / 400000 | Rust respond_stream | 334.15 | 4.69 | 0.214x |
| large / 1600000 | R^ streaming JSON / reused byte region (LLVM/Clang) | 6496.60 | 4.18 | 1.000x |
| large / 1600000 | C++ response DTO (msvc) | 2628.09 | 4.27 | 0.405x |
| large / 1600000 | C++ streaming JSON (msvc) | 1029.48 | 4.18 | 0.158x |
| large / 1600000 | C++ response DTO (clangcl) | 2204.26 | 4.27 | 0.339x |
| large / 1600000 | C++ streaming JSON (clangcl) | 805.68 | 4.20 | 0.124x |
| large / 1600000 | Rust respond_dto | 3113.07 | 4.68 | 0.479x |
| large / 1600000 | Rust respond_stream | 1293.90 | 4.69 | 0.199x |

Every warm-up/sample checks its checksum. Each size first cross-checks independently built
C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.
Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions
are saved in workloads_results.json. Historical bintrees measurements are in [results.md](../results.md).
