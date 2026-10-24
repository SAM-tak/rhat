# R^ benchmark results — all 16 workloads

Additional workloads: 2026-10-23T21:02:09.397008+00:00 UTC; bintrees: 2026-10-23T21:56:17.966056+00:00 UTC.

All rows below were measured with the same current R^ compiler/runtime. Goose is not run.
Original small/medium/large sizes; two warm-ups and best of three whole-process wall times. No suite-wide average is claimed.
Every run checks its checksum and its executable SHA-256 against the artifact recorded immediately after build.
R^ retains bounds/range checks and OS-backed stable-address arrays; no affinity is set.

**Correction:** older R^ rows for sum, particles, push and records ran overwritten Rust binaries and are invalid.
Those claims are withdrawn. [Archived evidence](invalid_runs/README.md) retains the original raw data. All current rows use separate language-specific executable paths.

Records uses the original fixed-mode variant; respond uses the original streaming variant. Other representation differences are in [coverage.md](coverage.md).

Full matrices, all implementation labels, raw samples, source/LLVM/executable hashes and build commands:
[15 workloads](current/workloads_results.md), [bintrees](current_bintrees/results.md).

CPU: AMD Ryzen 5 2600 Six-Core Processor; OS: Windows-10-10.0.26300-SP0; compiler SHA-256: `0d5be2d1de7705f476440990257e52d7dc67328af099a3ecc38c88355e5113af`.

The C++ and Rust columns select the fastest measured variant/backend for each size. Ratios are comparison / R^; above 1 means R^ is faster. Peak memory is the maximum measured R^ resident working set.

| Workload | Size / parameter | R^ ms | Best C++ ms | Best Rust ms | C++ / R^ | Rust / R^ | R^ peak MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| sum | small / 1000000 | 61.63 | 20.72 | 21.81 | 0.336x | 0.354x | 7.59 |
| sum | medium / 16000000 | 773.63 | 100.66 | 95.63 | 0.130x | 0.124x | 64.81 |
| sum | large / 128000000 | 6358.22 | 710.66 | 651.10 | 0.112x | 0.102x | 492.06 |
| push | small / 1000000 | 74.33 | 22.78 | 23.26 | 0.306x | 0.313x | 11.54 |
| push | medium / 16000000 | 1013.88 | 104.36 | 95.74 | 0.103x | 0.094x | 127.76 |
| push | large / 64000000 | 4118.67 | 354.81 | 335.68 | 0.086x | 0.082x | 499.69 |
| strlist | small / 200000 | 106.22 | 31.31 | 34.05 | 0.295x | 0.321x | 8.54 |
| strlist | medium / 2000000 | 962.73 | 157.12 | 170.85 | 0.163x | 0.177x | 51.47 |
| strlist | large / 8000000 | 3968.70 | 552.78 | 624.22 | 0.139x | 0.157x | 194.45 |
| records | small / 500000 | 116.90 | 49.72 | 65.17 | 0.425x | 0.557x | 15.21 |
| records | medium / 4000000 | 808.54 | 265.06 | 396.07 | 0.328x | 0.490x | 95.32 |
| records | large / 16000000 | 3195.26 | 976.49 | 1531.97 | 0.306x | 0.479x | 369.98 |
| tree | small / 16 | 31.93 | 20.31 | 20.89 | 0.636x | 0.654x | 5.28 |
| tree | medium / 20 | 290.56 | 113.70 | 98.77 | 0.391x | 0.340x | 27.78 |
| tree | large / 24 | 4492.72 | 1536.07 | 1285.69 | 0.342x | 0.286x | 387.78 |
| interp | small / 16 | 26.44 | 18.10 | 19.11 | 0.684x | 0.723x | 4.19 |
| interp | medium / 20 | 104.57 | 32.41 | 36.33 | 0.310x | 0.347x | 7.15 |
| interp | large / 24 | 904.83 | 188.27 | 213.80 | 0.208x | 0.236x | 39.28 |
| graph | small / 100000 | 218.75 | 41.09 | 44.22 | 0.188x | 0.202x | 11.42 |
| graph | medium / 500000 | 2253.00 | 305.56 | 342.04 | 0.136x | 0.152x | 41.94 |
| graph | large / 2000000 | 11734.53 | 2005.47 | 2011.34 | 0.171x | 0.171x | 156.38 |
| words | small / 500000 | 186.98 | 74.58 | 69.55 | 0.399x | 0.372x | 19.84 |
| words | medium / 4000000 | 1947.16 | 814.97 | 852.35 | 0.419x | 0.438x | 132.33 |
| words | large / 16000000 | 9502.84 | 3803.40 | 4161.04 | 0.400x | 0.438x | 517.74 |
| particles | small / 100000 | 95.49 | 45.40 | 46.13 | 0.475x | 0.483x | 6.07 |
| particles | medium / 1000000 | 925.38 | 610.15 | 619.58 | 0.659x | 0.670x | 26.67 |
| particles | large / 4000000 | 3718.61 | 2478.54 | 2464.58 | 0.667x | 0.663x | 95.33 |
| sexp | small / 20000 | 715.98 | 181.05 | 166.44 | 0.253x | 0.232x | 52.21 |
| sexp | medium / 100000 | 3654.98 | 828.21 | 761.39 | 0.227x | 0.208x | 245.70 |
| sexp | large / 300000 | 11750.20 | 2473.14 | 2270.29 | 0.210x | 0.193x | 729.51 |
| lru | small / 2000000 | 562.08 | 124.61 | 124.23 | 0.222x | 0.221x | 11.60 |
| lru | medium / 8000000 | 3026.36 | 890.77 | 942.56 | 0.294x | 0.311x | 35.04 |
| lru | large / 32000000 | 13672.60 | 3942.17 | 4428.42 | 0.288x | 0.324x | 128.82 |
| scene | small / 12 | 137.86 | 34.70 | 35.31 | 0.252x | 0.256x | 8.44 |
| scene | medium / 14 | 764.41 | 166.36 | 160.82 | 0.218x | 0.210x | 32.89 |
| scene | large / 16 | 4911.58 | 1070.51 | 832.91 | 0.218x | 0.170x | 185.90 |
| calc | small / 100000 | 257.24 | 68.83 | 68.83 | 0.268x | 0.268x | 4.19 |
| calc | medium / 400000 | 1003.17 | 227.61 | 219.33 | 0.227x | 0.219x | 4.19 |
| calc | large / 1600000 | 4024.96 | 848.04 | 823.24 | 0.211x | 0.205x | 4.19 |
| bintrees | small / 15 | 797.44 | 55.66 | 45.30 | 0.070x | 0.057x | 4.77 |
| bintrees | medium / 17 | 3346.79 | 201.46 | 157.13 | 0.060x | 0.047x | 7.77 |
| bintrees | large / 19 | 11239.03 | 846.31 | 714.51 | 0.075x | 0.064x | 19.77 |
| respond | small / 100000 | 392.36 | 67.99 | 97.43 | 0.173x | 0.248x | 4.18 |
| respond | medium / 400000 | 1521.54 | 211.79 | 333.75 | 0.139x | 0.219x | 4.18 |
| respond | large / 1600000 | 6426.43 | 797.38 | 1310.65 | 0.124x | 0.204x | 4.18 |
| blur | small / 1024 | 333.93 | 25.23 | 26.14 | 0.076x | 0.078x | 5.78 |
| blur | medium / 2048 | 1301.01 | 53.70 | 56.79 | 0.041x | 0.044x | 11.78 |
| blur | large / 8192 | 21609.55 | 566.55 | 668.70 | 0.026x | 0.031x | 131.78 |

Historical sessions remain available in the individual folders; their compiler/runtime revisions differ. The current tables above supersede their performance claims.
The original bintrees raw baseline remains in `results.json` and its controlled page-commit study in `page_commit_results.json`.
