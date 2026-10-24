# R^ vs C++ vs Rust — all 16 workloads

**All 48 size conditions include R^, C++ and Rust timings and relative performance.**

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

All rows below were measured with the same R^ compiler/runtime recorded for this full-suite session. Goose is not run.
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
| graph | small / 100000 | 116.50 | 41.36 | 43.48 | 0.355x | 0.373x | 11.54 |
| graph | medium / 500000 | 1338.96 | 327.22 | 370.16 | 0.244x | 0.276x | 42.04 |
| graph | large / 2000000 | 6732.20 | 2161.42 | 2183.24 | 0.321x | 0.324x | 156.54 |
| words | small / 500000 | 71.60 | 80.20 | 74.54 | 1.120x | 1.041x | 19.97 |
| words | medium / 4000000 | 821.55 | 761.67 | 843.63 | 0.927x | 1.027x | 132.42 |
| words | large / 16000000 | 4080.16 | 4003.41 | 4167.21 | 0.981x | 1.021x | 517.85 |
| particles | small / 100000 | 54.71 | 45.07 | 44.77 | 0.824x | 0.818x | 6.09 |
| particles | medium / 1000000 | 753.84 | 654.09 | 648.64 | 0.868x | 0.860x | 26.72 |
| particles | large / 4000000 | 2980.13 | 2606.77 | 2614.59 | 0.875x | 0.877x | 95.34 |
| sexp | small / 20000 | 254.21 | 181.75 | 168.49 | 0.715x | 0.663x | 52.36 |
| sexp | medium / 100000 | 1225.75 | 840.35 | 795.64 | 0.686x | 0.649x | 245.86 |
| sexp | large / 300000 | 3799.99 | 2511.50 | 2320.62 | 0.661x | 0.611x | 729.61 |
| lru | small / 2000000 | 292.33 | 123.70 | 167.10 | 0.423x | 0.572x | 11.66 |
| lru | medium / 8000000 | 1474.28 | 842.89 | 1001.53 | 0.572x | 0.679x | 35.10 |
| lru | large / 32000000 | 7507.82 | 4443.07 | 4817.90 | 0.592x | 0.642x | 128.85 |
| scene | small / 12 | 56.46 | 33.16 | 33.35 | 0.587x | 0.591x | 8.46 |
| scene | medium / 14 | 288.49 | 173.09 | 162.06 | 0.600x | 0.562x | 32.89 |
| scene | large / 16 | 1727.81 | 975.82 | 905.10 | 0.565x | 0.524x | 185.89 |
| calc | small / 100000 | 92.77 | 71.34 | 70.96 | 0.769x | 0.765x | 4.19 |
| calc | medium / 400000 | 322.02 | 225.02 | 222.82 | 0.699x | 0.692x | 4.19 |
| calc | large / 1600000 | 1272.34 | 861.59 | 832.43 | 0.677x | 0.654x | 4.18 |
| bintrees | small / 15 | 78.16 | 57.11 | 47.01 | 0.731x | 0.601x | 5.03 |
| bintrees | medium / 17 | 319.37 | 204.65 | 165.43 | 0.641x | 0.518x | 8.78 |
| bintrees | large / 19 | 1441.40 | 856.58 | 678.31 | 0.594x | 0.471x | 23.78 |
| respond | small / 100000 | 85.04 | 66.05 | 99.34 | 0.777x | 1.168x | 4.19 |
| respond | medium / 400000 | 290.21 | 213.02 | 340.87 | 0.734x | 1.175x | 4.19 |
| respond | large / 1600000 | 1102.93 | 824.20 | 1324.45 | 0.747x | 1.201x | 4.19 |
| blur | small / 1024 | 86.07 | 23.59 | 26.19 | 0.274x | 0.304x | 5.78 |
| blur | medium / 2048 | 315.37 | 50.66 | 57.00 | 0.161x | 0.181x | 11.77 |
| blur | large / 8192 | 4939.43 | 584.01 | 714.85 | 0.118x | 0.145x | 131.78 |

Historical sessions remain available in the individual folders; their compiler/runtime revisions differ. The current tables above supersede their performance claims.
The original bintrees raw baseline remains in `results.json` and its controlled page-commit study in `page_commit_results.json`.
