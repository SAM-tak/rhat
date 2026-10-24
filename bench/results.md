# R^ vs C++ vs Rust — all 16 workloads

**All 48 size conditions include R^, C++ and Rust timings and relative performance.**

**Fresh full verification: all 48 conditions meet 0.8x against both fastest C++ and fastest Rust.** [Gate and raw evidence](all08/results.md).
All original comparison variants were rebuilt and remeasured. No earlier timings are reused in this matrix.

R^ uses statically assigned data stacks, scope watermark rewinds and Windows fault-driven page commitment.
The push fast path is emitted directly as LLVM IR; bounds/range checks remain enabled.

Array indexing uses inline full-width bounds checks and address calculations.
Push uses byte-capacity checks and directly updates the top where growth analysis proves it safe.
Integer division/remainder guards are also inline, allowing safe constant-divisor checks to fold away.
The failure paths retain their runtime diagnostics; checks are not disabled.

Before these optimizations: [static-data-stack reference](pre_array_fastpaths/README.md).
Same-session before/after check: [paired array comparison](array_codegen_comparison/results.md).

Internal functions pass scalar/reference values directly.
Canonical i64 loops use range guards, stable pointer snapshots and a checked fallback.

Non-owning values up to 16 bytes pass directly; union and float-to-integer guards are inline.
Literal byte appends use checked constant copies; arbitrary overlapping appends keep memmove semantics.

pop の通常経路も LLVM IR に展開。非所有値と配列管理情報の型による別領域性を LLVM に伝える。

Additional workloads: 2026-10-24T13:36:23.528961+00:00 UTC; bintrees: 2026-10-24T13:32:53.546261+00:00 UTC.

All rows below were measured with the same R^ compiler/runtime recorded for this full-suite session. Goose is not run.
Original small/medium/large sizes; two warm-ups and best of three whole-process wall times. No suite-wide average is claimed.
Every run checks its checksum and its executable SHA-256 against the artifact recorded immediately after build.
R^ retains bounds/range checks and OS-backed stable-address arrays; no affinity is set.

**Correction:** older R^ rows for sum, particles, push and records ran overwritten Rust binaries and are invalid.
Those claims are withdrawn. [Archived evidence](invalid_runs/README.md) retains the original raw data. All current rows use separate language-specific executable paths.

Records uses the original fixed-mode variant; respond uses the original streaming variant. Other representation differences are in [coverage.md](coverage.md).

Before this change: [per-owner-region measurements](pre_data_stacks/README.md).

Full matrices, all implementation labels, raw samples, source/LLVM/executable hashes and build commands:
[15 workloads](all08/current/workloads_results.md), [bintrees](all08/current_bintrees/results.md).

CPU: AMD Ryzen 5 2600 Six-Core Processor; OS: Windows-10-10.0.26300-SP0; compiler SHA-256: `14306253a5809f5a5a4426f4677d268b45eb8a9defcda3e91ab83d1a7948c33d`.

The C++ and Rust columns select the fastest measured variant/backend for each size. Ratios are comparison / R^; above 1 means R^ is faster. Peak memory is the maximum measured R^ resident working set.

| Workload | Size / parameter | R^ ms | Best C++ ms | Best Rust ms | C++ / R^ | Rust / R^ | R^ peak MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| sum | small / 1000000 | 21.62 | 20.17 | 21.19 | 0.933x | 0.980x | 7.65 |
| sum | medium / 16000000 | 98.45 | 103.67 | 96.08 | 1.053x | 0.976x | 64.84 |
| sum | large / 128000000 | 701.36 | 766.29 | 689.48 | 1.093x | 0.983x | 492.09 |
| push | small / 1000000 | 19.71 | 20.40 | 21.86 | 1.035x | 1.109x | 11.60 |
| push | medium / 16000000 | 102.44 | 100.88 | 109.87 | 0.985x | 1.073x | 127.85 |
| push | large / 64000000 | 330.98 | 375.57 | 352.98 | 1.135x | 1.066x | 499.79 |
| strlist | small / 200000 | 31.95 | 31.09 | 33.22 | 0.973x | 1.040x | 8.66 |
| strlist | medium / 2000000 | 165.11 | 165.49 | 173.15 | 1.002x | 1.049x | 51.54 |
| strlist | large / 8000000 | 622.12 | 559.01 | 640.54 | 0.899x | 1.030x | 194.54 |
| records | small / 500000 | 41.02 | 49.28 | 65.50 | 1.201x | 1.597x | 15.28 |
| records | medium / 4000000 | 203.70 | 270.43 | 408.73 | 1.328x | 2.007x | 95.34 |
| records | large / 16000000 | 777.38 | 1008.58 | 1584.83 | 1.297x | 2.039x | 370.03 |
| tree | small / 16 | 17.80 | 18.99 | 19.25 | 1.067x | 1.082x | 5.28 |
| tree | medium / 20 | 104.76 | 118.76 | 115.28 | 1.134x | 1.100x | 27.78 |
| tree | large / 24 | 1444.23 | 1606.60 | 1433.19 | 1.112x | 0.992x | 387.78 |
| interp | small / 16 | 18.66 | 17.79 | 21.56 | 0.954x | 1.156x | 4.22 |
| interp | medium / 20 | 32.87 | 30.68 | 33.40 | 0.934x | 1.016x | 7.22 |
| interp | large / 24 | 224.72 | 198.44 | 224.22 | 0.883x | 0.998x | 39.34 |
| graph | small / 100000 | 45.00 | 46.03 | 44.64 | 1.023x | 0.992x | 14.72 |
| graph | medium / 500000 | 332.95 | 332.56 | 334.88 | 0.999x | 1.006x | 57.47 |
| graph | large / 2000000 | 2276.31 | 2226.65 | 2330.36 | 0.978x | 1.024x | 217.72 |
| words | small / 500000 | 82.07 | 76.02 | 73.96 | 0.926x | 0.901x | 19.97 |
| words | medium / 4000000 | 865.42 | 806.25 | 878.86 | 0.932x | 1.016x | 132.47 |
| words | large / 16000000 | 4332.80 | 3861.24 | 4331.23 | 0.891x | 1.000x | 517.83 |
| particles | small / 100000 | 43.22 | 47.85 | 45.62 | 1.107x | 1.055x | 6.09 |
| particles | medium / 1000000 | 657.23 | 677.94 | 705.82 | 1.031x | 1.074x | 26.72 |
| particles | large / 4000000 | 2666.65 | 2638.19 | 2714.97 | 0.989x | 1.018x | 95.34 |
| sexp | small / 20000 | 174.69 | 183.95 | 169.89 | 1.053x | 0.973x | 52.34 |
| sexp | medium / 100000 | 806.47 | 825.15 | 759.43 | 1.023x | 0.942x | 245.84 |
| sexp | large / 300000 | 2409.51 | 2481.38 | 2332.73 | 1.030x | 0.968x | 729.60 |
| lru | small / 2000000 | 139.86 | 130.54 | 167.78 | 0.933x | 1.200x | 11.66 |
| lru | medium / 8000000 | 823.40 | 904.49 | 944.00 | 1.098x | 1.146x | 35.10 |
| lru | large / 32000000 | 3938.53 | 4321.84 | 4834.61 | 1.097x | 1.228x | 128.85 |
| scene | small / 12 | 37.75 | 34.69 | 33.81 | 0.919x | 0.896x | 8.46 |
| scene | medium / 14 | 156.20 | 177.45 | 155.14 | 1.136x | 0.993x | 32.90 |
| scene | large / 16 | 883.82 | 1023.57 | 864.70 | 1.158x | 0.978x | 185.90 |
| calc | small / 100000 | 78.84 | 68.14 | 68.48 | 0.864x | 0.869x | 4.20 |
| calc | medium / 400000 | 266.14 | 237.92 | 217.39 | 0.894x | 0.817x | 4.20 |
| calc | large / 1600000 | 1022.61 | 861.40 | 837.96 | 0.842x | 0.819x | 4.19 |
| bintrees | small / 15 | 51.96 | 55.87 | 48.49 | 1.075x | 0.933x | 5.04 |
| bintrees | medium / 17 | 191.99 | 207.26 | 158.74 | 1.080x | 0.827x | 8.79 |
| bintrees | large / 19 | 825.74 | 845.88 | 665.25 | 1.024x | 0.806x | 23.79 |
| respond | small / 100000 | 76.01 | 68.80 | 98.05 | 0.905x | 1.290x | 4.18 |
| respond | medium / 400000 | 256.41 | 213.13 | 337.85 | 0.831x | 1.318x | 4.18 |
| respond | large / 1600000 | 982.93 | 844.41 | 1308.74 | 0.859x | 1.331x | 4.18 |
| blur | small / 1024 | 21.84 | 23.47 | 25.61 | 1.075x | 1.172x | 5.79 |
| blur | medium / 2048 | 44.05 | 50.19 | 56.97 | 1.139x | 1.293x | 11.79 |
| blur | large / 8192 | 493.17 | 594.34 | 680.97 | 1.205x | 1.381x | 131.79 |

Historical sessions remain available in the individual folders; their compiler/runtime revisions differ. The current tables above supersede their performance claims.
The original bintrees raw baseline remains in `results.json` and its controlled page-commit study in `page_commit_results.json`.
