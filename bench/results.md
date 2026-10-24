# R^ vs C++ vs Rust — all 16 workloads

**All 48 size conditions include R^, C++ and Rust timings and relative performance.**

R^ uses statically assigned data stacks, scope watermark rewinds and Windows fault-driven page commitment.
The push fast path is emitted directly as LLVM IR; bounds/range checks remain enabled.

Additional workloads: 2026-10-24T01:49:57.138083+00:00 UTC; bintrees: 2026-10-24T01:46:18.962542+00:00 UTC.

All rows below were measured with the same current R^ compiler/runtime. Goose is not run.
Original small/medium/large sizes; two warm-ups and best of three whole-process wall times. No suite-wide average is claimed.
Every run checks its checksum and its executable SHA-256 against the artifact recorded immediately after build.
R^ retains bounds/range checks and OS-backed stable-address arrays; no affinity is set.

**Correction:** older R^ rows for sum, particles, push and records ran overwritten Rust binaries and are invalid.
Those claims are withdrawn. [Archived evidence](invalid_runs/README.md) retains the original raw data. All current rows use separate language-specific executable paths.

Records uses the original fixed-mode variant; respond uses the original streaming variant. Other representation differences are in [coverage.md](coverage.md).

Before this change: [per-owner-region measurements](pre_data_stacks/README.md).

Full matrices, all implementation labels, raw samples, source/LLVM/executable hashes and build commands:
[15 workloads](current/workloads_results.md), [bintrees](current_bintrees/results.md).

CPU: AMD Ryzen 5 2600 Six-Core Processor; OS: Windows-10-10.0.26300-SP0; compiler SHA-256: `b009c93c77498b146d5d3838515e81a157d7c7fb4e23920c9ac243276cd421c7`.

The C++ and Rust columns select the fastest measured variant/backend for each size. Ratios are comparison / R^; above 1 means R^ is faster. Peak memory is the maximum measured R^ resident working set.

| Workload | Size / parameter | R^ ms | Best C++ ms | Best Rust ms | C++ / R^ | Rust / R^ | R^ peak MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| sum | small / 1000000 | 46.21 | 20.95 | 21.78 | 0.453x | 0.471x | 7.66 |
| sum | medium / 16000000 | 534.12 | 110.92 | 103.25 | 0.208x | 0.193x | 64.84 |
| sum | large / 128000000 | 4209.05 | 793.23 | 747.62 | 0.188x | 0.178x | 492.09 |
| push | small / 1000000 | 61.39 | 21.87 | 21.60 | 0.356x | 0.352x | 11.59 |
| push | medium / 16000000 | 810.96 | 105.46 | 115.12 | 0.130x | 0.142x | 127.84 |
| push | large / 64000000 | 3413.73 | 402.74 | 380.73 | 0.118x | 0.112x | 499.79 |
| strlist | small / 200000 | 68.79 | 28.39 | 30.79 | 0.413x | 0.448x | 8.66 |
| strlist | medium / 2000000 | 556.01 | 161.21 | 172.43 | 0.290x | 0.310x | 51.54 |
| strlist | large / 8000000 | 2162.96 | 569.29 | 663.16 | 0.263x | 0.307x | 194.54 |
| records | small / 500000 | 113.05 | 61.18 | 78.52 | 0.541x | 0.695x | 15.27 |
| records | medium / 4000000 | 759.35 | 274.64 | 431.18 | 0.362x | 0.568x | 95.34 |
| records | large / 16000000 | 2957.28 | 979.48 | 1578.38 | 0.331x | 0.534x | 370.02 |
| tree | small / 16 | 29.60 | 18.11 | 19.47 | 0.612x | 0.658x | 5.28 |
| tree | medium / 20 | 264.44 | 112.64 | 99.41 | 0.426x | 0.376x | 27.78 |
| tree | large / 24 | 4170.21 | 1580.88 | 1418.32 | 0.379x | 0.340x | 387.78 |
| interp | small / 16 | 28.50 | 19.44 | 21.35 | 0.682x | 0.749x | 4.21 |
| interp | medium / 20 | 104.63 | 37.81 | 38.35 | 0.361x | 0.367x | 7.21 |
| interp | large / 24 | 1000.40 | 218.15 | 255.77 | 0.218x | 0.256x | 39.34 |
| graph | small / 100000 | 254.94 | 45.49 | 43.12 | 0.178x | 0.169x | 11.53 |
| graph | medium / 500000 | 2181.21 | 337.12 | 378.46 | 0.155x | 0.174x | 42.03 |
| graph | large / 2000000 | 11452.43 | 2250.50 | 2302.02 | 0.197x | 0.201x | 156.52 |
| words | small / 500000 | 159.75 | 72.51 | 71.14 | 0.454x | 0.445x | 19.98 |
| words | medium / 4000000 | 1830.04 | 802.34 | 888.91 | 0.438x | 0.486x | 132.48 |
| words | large / 16000000 | 9267.37 | 3779.40 | 4366.83 | 0.408x | 0.471x | 517.83 |
| particles | small / 100000 | 95.27 | 49.30 | 47.07 | 0.517x | 0.494x | 6.09 |
| particles | medium / 1000000 | 918.65 | 647.69 | 671.95 | 0.705x | 0.731x | 26.72 |
| particles | large / 4000000 | 3840.48 | 2598.98 | 2572.18 | 0.677x | 0.670x | 95.34 |
| sexp | small / 20000 | 594.00 | 189.15 | 165.88 | 0.318x | 0.279x | 52.35 |
| sexp | medium / 100000 | 3009.82 | 830.09 | 750.69 | 0.276x | 0.249x | 245.84 |
| sexp | large / 300000 | 9007.11 | 2495.70 | 2303.98 | 0.277x | 0.256x | 729.60 |
| lru | small / 2000000 | 569.94 | 141.25 | 144.82 | 0.248x | 0.254x | 11.67 |
| lru | medium / 8000000 | 3113.86 | 910.53 | 926.55 | 0.292x | 0.298x | 35.11 |
| lru | large / 32000000 | 14739.87 | 4514.32 | 5224.72 | 0.306x | 0.354x | 128.87 |
| scene | small / 12 | 132.03 | 31.71 | 33.88 | 0.240x | 0.257x | 8.47 |
| scene | medium / 14 | 785.17 | 183.44 | 158.29 | 0.234x | 0.202x | 32.91 |
| scene | large / 16 | 4864.35 | 1091.82 | 982.61 | 0.224x | 0.202x | 185.91 |
| calc | small / 100000 | 207.42 | 68.19 | 69.41 | 0.329x | 0.335x | 4.20 |
| calc | medium / 400000 | 792.81 | 229.67 | 222.86 | 0.290x | 0.281x | 4.20 |
| calc | large / 1600000 | 3220.66 | 897.36 | 841.50 | 0.279x | 0.261x | 4.20 |
| bintrees | small / 15 | 91.95 | 58.24 | 49.01 | 0.633x | 0.533x | 5.02 |
| bintrees | medium / 17 | 381.14 | 197.96 | 169.73 | 0.519x | 0.445x | 8.78 |
| bintrees | large / 19 | 1680.67 | 860.79 | 671.02 | 0.512x | 0.399x | 23.78 |
| respond | small / 100000 | 291.48 | 67.02 | 99.41 | 0.230x | 0.341x | 4.20 |
| respond | medium / 400000 | 1204.05 | 226.65 | 343.15 | 0.188x | 0.285x | 4.20 |
| respond | large / 1600000 | 5198.96 | 852.10 | 1407.51 | 0.164x | 0.271x | 4.20 |
| blur | small / 1024 | 295.93 | 26.14 | 28.69 | 0.088x | 0.097x | 5.78 |
| blur | medium / 2048 | 1091.49 | 60.14 | 61.73 | 0.055x | 0.057x | 11.78 |
| blur | large / 8192 | 18483.12 | 610.75 | 703.77 | 0.033x | 0.038x | 131.78 |

Historical sessions remain available in the individual folders; their compiler/runtime revisions differ. The current tables above supersede their performance claims.
The original bintrees raw baseline remains in `results.json` and its controlled page-commit study in `page_commit_results.json`.
