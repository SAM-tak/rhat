# R^ vs C++ vs Rust — all 16 workloads

**All 48 size conditions include R^, C++ and Rust timings and relative performance.**

**Fresh threshold verification:** [graph, blur, lru and bintrees](target08/results.md) were rebuilt and remeasured, including every original C++/Rust variant.
Those four workloads use the fresh rows below; the other 12 retain the dated full-suite reference above.
Graph now uses CSR, including all construction cost; its previous linked-index port is retained in rhat/variants/graph_linked.rh.
Refresh dates: 2026-10-24T08:44:09.980173+00:00; 2026-10-24T08:40:46.669248+00:00; refresh compiler SHA-256: `33e18d7d5db330b389e2ae762303bd6cc211a5a8261e1b17cec26bb5f835374b`.

**Subsequent internal-call and checked-loop optimizations:** [focused paired measurements](abi_ranges/results.md).
The complete matrix below retains the preceding full-suite session; the focused report labels reused C++/Rust reference timings explicitly.
Implementation limits and the sum control repeat are in [the validation notes](abi_ranges/README.md).

R^ uses statically assigned data stacks, scope watermark rewinds and Windows fault-driven page commitment.
The push fast path is emitted directly as LLVM IR; bounds/range checks remain enabled.

Array indexing uses inline full-width bounds checks and address calculations.
Push uses byte-capacity checks and directly updates the top where growth analysis proves it safe.
Integer division/remainder guards are also inline, allowing safe constant-divisor checks to fold away.
The failure paths retain their runtime diagnostics; checks are not disabled.

Before these optimizations: [static-data-stack reference](pre_array_fastpaths/README.md).
Same-session before/after check: [paired array comparison](array_codegen_comparison/results.md).

Additional workloads: 2026-10-24T03:33:36.565830+00:00 UTC; bintrees: 2026-10-24T03:30:02.558939+00:00 UTC.

The table combines the fresh target refresh with the preceding full-suite reference for the other 12 workloads. Goose is not run.
Original small/medium/large sizes; two warm-ups and best of three whole-process wall times. No suite-wide average is claimed.
Every run checks its checksum and its executable SHA-256 against the artifact recorded immediately after build.
R^ retains bounds/range checks and OS-backed stable-address arrays; no affinity is set.

**Correction:** older R^ rows for sum, particles, push and records ran overwritten Rust binaries and are invalid.
Those claims are withdrawn. [Archived evidence](invalid_runs/README.md) retains the original raw data. All current rows use separate language-specific executable paths.

Records uses the original fixed-mode variant; respond uses the original streaming variant. Other representation differences are in [coverage.md](coverage.md).

Before this change: [per-owner-region measurements](pre_data_stacks/README.md).

Full matrices, all implementation labels, raw samples, source/LLVM/executable hashes and build commands:
[15 workloads](current/workloads_results.md), [bintrees](current_bintrees/results.md).

CPU: AMD Ryzen 5 2600 Six-Core Processor; OS: Windows-10-10.0.26300-SP0; compiler SHA-256: `e5e63c7e119cc6354e917b2d2ea77f63dafc310fb04c45dae796fc922251f3a4`.

The C++ and Rust columns select the fastest measured variant/backend for each size. Ratios are comparison / R^; above 1 means R^ is faster. Peak memory is the maximum measured R^ resident working set.

| Workload | Size / parameter | R^ ms | Best C++ ms | Best Rust ms | C++ / R^ | Rust / R^ | R^ peak MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| sum | small / 1000000 | 21.63 | 21.32 | 22.00 | 0.986x | 1.017x | 7.65 |
| sum | medium / 16000000 | 100.91 | 102.52 | 100.14 | 1.016x | 0.992x | 64.84 |
| sum | large / 128000000 | 690.77 | 742.86 | 666.86 | 1.075x | 0.965x | 492.09 |
| push | small / 1000000 | 23.50 | 22.93 | 24.15 | 0.976x | 1.028x | 11.60 |
| push | medium / 16000000 | 103.14 | 112.10 | 100.86 | 1.087x | 0.978x | 127.84 |
| push | large / 64000000 | 348.55 | 364.51 | 341.10 | 1.046x | 0.979x | 499.79 |
| strlist | small / 200000 | 33.08 | 33.14 | 36.44 | 1.002x | 1.101x | 8.66 |
| strlist | medium / 2000000 | 176.69 | 156.14 | 174.80 | 0.884x | 0.989x | 51.54 |
| strlist | large / 8000000 | 617.86 | 570.44 | 638.77 | 0.923x | 1.034x | 194.54 |
| records | small / 500000 | 37.73 | 49.13 | 64.26 | 1.302x | 1.703x | 15.27 |
| records | medium / 4000000 | 184.22 | 270.14 | 401.20 | 1.466x | 2.178x | 95.34 |
| records | large / 16000000 | 710.40 | 986.45 | 1571.77 | 1.389x | 2.213x | 370.02 |
| tree | small / 16 | 21.93 | 18.49 | 20.01 | 0.843x | 0.912x | 5.28 |
| tree | medium / 20 | 139.53 | 117.71 | 98.74 | 0.844x | 0.708x | 27.78 |
| tree | large / 24 | 2199.64 | 1558.37 | 1347.64 | 0.708x | 0.613x | 387.78 |
| interp | small / 16 | 19.49 | 18.24 | 19.49 | 0.936x | 1.000x | 4.21 |
| interp | medium / 20 | 37.88 | 31.19 | 33.39 | 0.824x | 0.881x | 7.21 |
| interp | large / 24 | 276.96 | 195.96 | 221.41 | 0.708x | 0.799x | 39.34 |
| graph | small / 100000 | 43.45 | 42.05 | 42.09 | 0.968x | 0.969x | 14.72 |
| graph | medium / 500000 | 318.73 | 305.90 | 330.71 | 0.960x | 1.038x | 57.47 |
| graph | large / 2000000 | 1949.15 | 2116.31 | 2164.12 | 1.086x | 1.110x | 217.72 |
| words | small / 500000 | 71.60 | 80.20 | 74.54 | 1.120x | 1.041x | 19.97 |
| words | medium / 4000000 | 821.55 | 761.67 | 843.63 | 0.927x | 1.027x | 132.42 |
| words | large / 16000000 | 4080.16 | 4003.41 | 4167.21 | 0.981x | 1.021x | 517.85 |
| particles | small / 100000 | 54.71 | 45.07 | 44.77 | 0.824x | 0.818x | 6.09 |
| particles | medium / 1000000 | 753.84 | 654.09 | 648.64 | 0.868x | 0.860x | 26.72 |
| particles | large / 4000000 | 2980.13 | 2606.77 | 2614.59 | 0.875x | 0.877x | 95.34 |
| sexp | small / 20000 | 254.21 | 181.75 | 168.49 | 0.715x | 0.663x | 52.36 |
| sexp | medium / 100000 | 1225.75 | 840.35 | 795.64 | 0.686x | 0.649x | 245.86 |
| sexp | large / 300000 | 3799.99 | 2511.50 | 2320.62 | 0.661x | 0.611x | 729.61 |
| lru | small / 2000000 | 99.97 | 118.69 | 113.85 | 1.187x | 1.139x | 11.66 |
| lru | medium / 8000000 | 738.23 | 810.26 | 1030.69 | 1.098x | 1.396x | 35.10 |
| lru | large / 32000000 | 3914.45 | 4228.68 | 4713.71 | 1.080x | 1.204x | 128.85 |
| scene | small / 12 | 56.46 | 33.16 | 33.35 | 0.587x | 0.591x | 8.46 |
| scene | medium / 14 | 288.49 | 173.09 | 162.06 | 0.600x | 0.562x | 32.89 |
| scene | large / 16 | 1727.81 | 975.82 | 905.10 | 0.565x | 0.524x | 185.89 |
| calc | small / 100000 | 92.77 | 71.34 | 70.96 | 0.769x | 0.765x | 4.19 |
| calc | medium / 400000 | 322.02 | 225.02 | 222.82 | 0.699x | 0.692x | 4.19 |
| calc | large / 1600000 | 1272.34 | 861.59 | 832.43 | 0.677x | 0.654x | 4.18 |
| bintrees | small / 15 | 51.42 | 55.39 | 46.00 | 1.077x | 0.895x | 5.04 |
| bintrees | medium / 17 | 185.01 | 198.37 | 154.80 | 1.072x | 0.837x | 8.79 |
| bintrees | large / 19 | 806.49 | 830.32 | 654.06 | 1.030x | 0.811x | 23.78 |
| respond | small / 100000 | 85.04 | 66.05 | 99.34 | 0.777x | 1.168x | 4.19 |
| respond | medium / 400000 | 290.21 | 213.02 | 340.87 | 0.734x | 1.175x | 4.19 |
| respond | large / 1600000 | 1102.93 | 824.20 | 1324.45 | 0.747x | 1.201x | 4.19 |
| blur | small / 1024 | 23.91 | 25.25 | 27.08 | 1.056x | 1.133x | 5.78 |
| blur | medium / 2048 | 44.75 | 52.11 | 59.86 | 1.164x | 1.337x | 11.78 |
| blur | large / 8192 | 486.27 | 595.44 | 667.65 | 1.225x | 1.373x | 131.78 |

Historical sessions remain available in the individual folders; their compiler/runtime revisions differ. The current tables above supersede their performance claims.
The original bintrees raw baseline remains in `results.json` and its controlled page-commit study in `page_commit_results.json`.
