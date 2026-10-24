# R^ vs C++ vs Rust — all 16 workloads

Measured: **2026-10-25 07:07 JST**. All 48 original small/medium/large conditions were remeasured.

**R^, C++ and Rust were rebuilt and timed in this session. No earlier timings are reused in the current comparison columns.**

For each condition, C++ and Rust use the variant/backend that was fastest in the preceding full-variant session.
Only those selected controls were remeasured; this is not a fresh search over every comparison variant.
Each condition uses two warm-ups and the minimum of three whole-process wall times. Execution order alternates.
Every run checks the output and the executable SHA-256 before and after execution. No affinity is set.

R^ uses the current builtin IR lowering and separate module objects. Benchmark RNGs are shared through `import^ std.random`.
Bounds/range/lifetime checks remain enabled. Source, native object, module IR and executable identities are recorded.
Changes since the preceding full run include builtin lowering and module compilation; differences cannot be attributed to import alone.

[Previous complete matrix](../pre_modules_results.md) · [previous all-variant measurements](../all08/results.md) · [raw samples and build commands](../modules_refresh/results.json) · [verification](../modules_refresh/verification.json)

CPU: AMD Ryzen 5 2600 Six-Core Processor; OS: Windows-10-10.0.26300-SP0.
R^ compiler SHA-256: `d8d2e2b4ffd2f6e40db22d9d1e77c25f41693748a0ef33ca8829c174214c7ed0`. The measured compiler includes uncommitted changes recorded in the raw data.

Ratios are comparison / R^ time; above 1 means R^ is faster. C++/Rust columns refer to the selected controls described above.

| Workload | Size / parameter | R^ ms | C++ ms | Rust ms | C++ / R^ | Rust / R^ | R^ peak MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| sum | small / 1000000 | 22.84 | 21.08 | 25.09 | 0.923x | 1.099x | 7.90 |
| sum | medium / 16000000 | 103.19 | 107.22 | 104.49 | 1.039x | 1.013x | 65.08 |
| sum | large / 128000000 | 740.19 | 735.06 | 674.29 | 0.993x | 0.911x | 492.33 |
| push | small / 1000000 | 23.31 | 22.41 | 24.20 | 0.961x | 1.038x | 11.83 |
| push | medium / 16000000 | 108.21 | 105.80 | 101.99 | 0.978x | 0.943x | 128.08 |
| push | large / 64000000 | 358.57 | 373.15 | 354.81 | 1.041x | 0.990x | 500.02 |
| strlist | small / 200000 | 35.64 | 32.17 | 39.66 | 0.903x | 1.113x | 8.89 |
| strlist | medium / 2000000 | 151.20 | 163.29 | 176.58 | 1.080x | 1.168x | 51.77 |
| strlist | large / 8000000 | 563.66 | 562.15 | 652.18 | 0.997x | 1.157x | 194.77 |
| records | small / 500000 | 46.33 | 51.46 | 72.74 | 1.111x | 1.570x | 15.52 |
| records | medium / 4000000 | 227.88 | 294.91 | 453.60 | 1.294x | 1.990x | 95.58 |
| records | large / 16000000 | 795.35 | 1041.06 | 1699.26 | 1.309x | 2.136x | 370.27 |
| tree | small / 16 | 21.81 | 21.74 | 22.86 | 0.997x | 1.048x | 5.51 |
| tree | medium / 20 | 101.01 | 123.49 | 103.07 | 1.223x | 1.020x | 28.01 |
| tree | large / 24 | 1328.26 | 1676.70 | 1364.36 | 1.262x | 1.027x | 388.02 |
| interp | small / 16 | 26.26 | 28.19 | 30.23 | 1.073x | 1.151x | 4.45 |
| interp | medium / 20 | 36.79 | 37.71 | 46.97 | 1.025x | 1.277x | 7.45 |
| interp | large / 24 | 218.72 | 208.22 | 219.56 | 0.952x | 1.004x | 39.57 |
| graph | small / 100000 | 54.15 | 45.63 | 48.64 | 0.843x | 0.898x | 14.97 |
| graph | medium / 500000 | 317.14 | 342.95 | 377.44 | 1.081x | 1.190x | 57.71 |
| graph | large / 2000000 | 2024.59 | 2172.70 | 2311.83 | 1.073x | 1.142x | 217.96 |
| words | small / 500000 | 64.57 | 80.15 | 78.52 | 1.241x | 1.216x | 20.21 |
| words | medium / 4000000 | 689.94 | 781.22 | 896.48 | 1.132x | 1.299x | 132.71 |
| words | large / 16000000 | 3606.11 | 3801.56 | 4375.27 | 1.054x | 1.213x | 518.09 |
| particles | small / 100000 | 49.27 | 47.18 | 50.03 | 0.958x | 1.016x | 6.33 |
| particles | medium / 1000000 | 680.66 | 741.93 | 770.46 | 1.090x | 1.132x | 26.96 |
| particles | large / 4000000 | 2671.30 | 2670.36 | 2680.77 | 1.000x | 1.004x | 95.58 |
| sexp | small / 20000 | 178.25 | 184.04 | 171.38 | 1.032x | 0.961x | 52.59 |
| sexp | medium / 100000 | 805.14 | 885.08 | 803.67 | 1.099x | 0.998x | 246.09 |
| sexp | large / 300000 | 2417.85 | 2544.46 | 2348.48 | 1.052x | 0.971x | 729.85 |
| lru | small / 2000000 | 114.77 | 127.66 | 158.21 | 1.112x | 1.379x | 11.91 |
| lru | medium / 8000000 | 802.81 | 841.30 | 918.47 | 1.048x | 1.144x | 35.35 |
| lru | large / 32000000 | 3764.96 | 4352.23 | 4777.29 | 1.156x | 1.269x | 129.09 |
| scene | small / 12 | 35.82 | 38.30 | 33.21 | 1.069x | 0.927x | 8.71 |
| scene | medium / 14 | 159.43 | 176.91 | 158.38 | 1.110x | 0.993x | 33.16 |
| scene | large / 16 | 892.69 | 1023.07 | 898.31 | 1.146x | 1.006x | 186.15 |
| calc | small / 100000 | 81.91 | 77.78 | 78.50 | 0.950x | 0.958x | 4.45 |
| calc | medium / 400000 | 263.89 | 231.61 | 224.27 | 0.878x | 0.850x | 4.45 |
| calc | large / 1600000 | 1019.90 | 883.03 | 840.97 | 0.866x | 0.825x | 4.45 |
| bintrees | small / 15 | 53.96 | 61.49 | 55.10 | 1.140x | 1.021x | 5.02 |
| bintrees | medium / 17 | 184.30 | 203.22 | 164.11 | 1.103x | 0.890x | 8.77 |
| bintrees | large / 19 | 804.63 | 859.17 | 693.79 | 1.068x | 0.862x | 23.77 |
| respond | small / 100000 | 81.47 | 72.73 | 112.87 | 0.893x | 1.385x | 4.45 |
| respond | medium / 400000 | 255.43 | 224.54 | 347.62 | 0.879x | 1.361x | 4.45 |
| respond | large / 1600000 | 991.49 | 835.84 | 1319.11 | 0.843x | 1.330x | 4.45 |
| blur | small / 1024 | 25.62 | 26.23 | 29.65 | 1.024x | 1.157x | 6.02 |
| blur | medium / 2048 | 48.41 | 55.11 | 61.27 | 1.138x | 1.266x | 12.02 |
| blur | large / 8192 | 500.56 | 583.31 | 659.11 | 1.165x | 1.317x | 132.02 |

## Change from the preceding R^ session

These are different-session measurements, not a paired experiment. Small differences can reflect scheduling/startup/clock variation.
Previous / current above 1 means the current run was faster; the previous R^ compiler and source revision differ.

| Workload | Size | Previous R^ ms | Current R^ ms | Previous / current |
|---|---|---:|---:|---:|
| sum | small | 21.62 | 22.84 | 0.946x |
| sum | medium | 98.45 | 103.19 | 0.954x |
| sum | large | 701.36 | 740.19 | 0.948x |
| push | small | 19.71 | 23.31 | 0.846x |
| push | medium | 102.44 | 108.21 | 0.947x |
| push | large | 330.98 | 358.57 | 0.923x |
| strlist | small | 31.95 | 35.64 | 0.897x |
| strlist | medium | 165.11 | 151.20 | 1.092x |
| strlist | large | 622.12 | 563.66 | 1.104x |
| records | small | 41.02 | 46.33 | 0.885x |
| records | medium | 203.70 | 227.88 | 0.894x |
| records | large | 777.38 | 795.35 | 0.977x |
| tree | small | 17.80 | 21.81 | 0.816x |
| tree | medium | 104.76 | 101.01 | 1.037x |
| tree | large | 1444.23 | 1328.26 | 1.087x |
| interp | small | 18.66 | 26.26 | 0.710x |
| interp | medium | 32.87 | 36.79 | 0.893x |
| interp | large | 224.72 | 218.72 | 1.027x |
| graph | small | 45.00 | 54.15 | 0.831x |
| graph | medium | 332.95 | 317.14 | 1.050x |
| graph | large | 2276.31 | 2024.59 | 1.124x |
| words | small | 82.07 | 64.57 | 1.271x |
| words | medium | 865.42 | 689.94 | 1.254x |
| words | large | 4332.80 | 3606.11 | 1.202x |
| particles | small | 43.22 | 49.27 | 0.877x |
| particles | medium | 657.23 | 680.66 | 0.966x |
| particles | large | 2666.65 | 2671.30 | 0.998x |
| sexp | small | 174.69 | 178.25 | 0.980x |
| sexp | medium | 806.47 | 805.14 | 1.002x |
| sexp | large | 2409.51 | 2417.85 | 0.997x |
| lru | small | 139.86 | 114.77 | 1.219x |
| lru | medium | 823.40 | 802.81 | 1.026x |
| lru | large | 3938.53 | 3764.96 | 1.046x |
| scene | small | 37.75 | 35.82 | 1.054x |
| scene | medium | 156.20 | 159.43 | 0.980x |
| scene | large | 883.82 | 892.69 | 0.990x |
| calc | small | 78.84 | 81.91 | 0.962x |
| calc | medium | 266.14 | 263.89 | 1.009x |
| calc | large | 1022.61 | 1019.90 | 1.003x |
| bintrees | small | 51.96 | 53.96 | 0.963x |
| bintrees | medium | 191.99 | 184.30 | 1.042x |
| bintrees | large | 825.74 | 804.63 | 1.026x |
| respond | small | 76.01 | 81.47 | 0.933x |
| respond | medium | 256.41 | 255.43 | 1.004x |
| respond | large | 982.93 | 991.49 | 0.991x |
| blur | small | 21.84 | 25.62 | 0.852x |
| blur | medium | 44.05 | 48.41 | 0.910x |
| blur | large | 493.17 | 500.56 | 0.985x |

## Selected comparison implementations

| Workload | Size | C++ variant/backend | Rust variant |
|---|---|---|---|
| sum | small | C++ reserved vector (clangcl) | Rust sum |
| sum | medium | C++ reserved vector (msvc) | Rust sum |
| sum | large | C++ reserved vector (msvc) | Rust sum |
| push | small | C++ reserved vector indices (msvc) | Rust push |
| push | medium | C++ reserved vector indices (msvc) | Rust push |
| push | large | C++ reserved vector indices (msvc) | Rust push |
| strlist | small | C++ flat offsets (clangcl) | Rust strlist_borrowed |
| strlist | medium | C++ flat offsets (clangcl) | Rust strlist_borrowed |
| strlist | large | C++ flat offsets (clangcl) | Rust strlist_borrowed |
| records | small | C++ variant / inline text (msvc) | Rust records |
| records | medium | C++ variant / inline text (clangcl) | Rust records |
| records | large | C++ variant / inline text (clangcl) | Rust records |
| tree | small | C++ index arena (msvc) | Rust tree_arena |
| tree | medium | C++ index arena (msvc) | Rust tree_arena |
| tree | large | C++ index arena (clangcl) | Rust tree_arena |
| interp | small | C++ switch arena (msvc) | Rust interp_arena |
| interp | medium | C++ switch arena (msvc) | Rust interp_arena |
| interp | large | C++ switch arena (msvc) | Rust interp_arena |
| graph | small | C++ CSR (clangcl) | Rust graph_csr |
| graph | medium | C++ CSR (msvc) | Rust graph_csr |
| graph | large | C++ CSR (clangcl) | Rust graph_csr |
| words | small | C++ open table (clangcl) | Rust words_open |
| words | medium | C++ open table (clangcl) | Rust words_open |
| words | large | C++ open table (clangcl) | Rust words_open |
| particles | small | C++ scalar AoS (clangcl) | Rust particles_scalar |
| particles | medium | C++ vector AoS (clangcl) | Rust particles |
| particles | large | C++ scalar AoS (clangcl) | Rust particles_scalar |
| sexp | small | C++ index arena / borrowed symbols (clangcl) | Rust sexp_arena |
| sexp | medium | C++ index arena / borrowed symbols (clangcl) | Rust sexp_arena |
| sexp | large | C++ index arena / borrowed symbols (clangcl) | Rust sexp_arena |
| lru | small | C++ index arena / open table (clangcl) | Rust lru_open |
| lru | medium | C++ index arena / open table (clangcl) | Rust lru_open |
| lru | large | C++ index arena / open table (clangcl) | Rust lru_open |
| scene | small | C++ index arena (clangcl) | Rust scene_arena |
| scene | medium | C++ index arena (msvc) | Rust scene_arena |
| scene | large | C++ index arena (clangcl) | Rust scene_arena |
| calc | small | C++ index arena / error codes (clangcl) | Rust calc_arena |
| calc | medium | C++ index arena / error codes (clangcl) | Rust calc_arena |
| calc | large | C++ index arena / error codes (clangcl) | Rust calc_arena |
| bintrees | small | C++ reused arena (msvc) | Rust arena |
| bintrees | medium | C++ reused arena (msvc) | Rust arena |
| bintrees | large | C++ reused arena (msvc) | Rust arena |
| respond | small | C++ streaming JSON (clangcl) | Rust respond_stream |
| respond | medium | C++ streaming JSON (clangcl) | Rust respond_stream |
| respond | large | C++ streaming JSON (clangcl) | Rust respond_stream |
| blur | small | C++ row pointers (clangcl) | Rust blur_windows |
| blur | medium | C++ row pointers (clangcl) | Rust blur_index |
| blur | large | C++ row pointers (clangcl) | Rust blur_windows |

Historical raw results and the previous complete matrix are preserved. Goose was not run.
