# Internal calls and checked loop versioning

Paired production R^ measurements. Original workload sources and sizes are unchanged.
Two warmups, best of three whole-process times; execution order alternates.
All checks remain enabled. Executable identity and checksums are verified on every run.

Measured: 2026-10-24T05:28:00.674772+00:00 UTC; compiler SHA-256: `89593bc2a226338c972d057a5f4ddb8926520ed54c9c440f73672909ed999fd7`.

**C++/Rust columns reuse the verified previous full-suite measurements. They are reference timings, not new runs in this session.**
Their dates, all variants and raw samples are retained in [the full matrix](../results.md) and results.json.
Only the before/after R^ columns form a same-session paired comparison.

| Workload | Size / parameter | Before R^ ms | New R^ ms | Speedup | Reference C++ ms | Reference Rust ms | C++ / new R^ | Rust / new R^ |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| blur | small / 1024 | 91.18 | 25.77 | 3.54x | 23.59 | 26.19 | 0.915x | 1.016x |
| blur | medium / 2048 | 300.18 | 45.83 | 6.55x | 50.66 | 57.00 | 1.105x | 1.244x |
| blur | large / 8192 | 4897.97 | 492.07 | 9.95x | 584.01 | 714.85 | 1.187x | 1.453x |
| lru | small / 2000000 | 205.05 | 111.42 | 1.84x | 123.70 | 167.10 | 1.110x | 1.500x |
| lru | medium / 8000000 | 1428.41 | 738.72 | 1.93x | 842.89 | 1001.53 | 1.141x | 1.356x |
| lru | large / 32000000 | 7566.02 | 3892.31 | 1.94x | 4443.07 | 4817.90 | 1.141x | 1.238x |
| bintrees | small / 15 | 85.80 | 53.43 | 1.61x | 57.11 | 47.01 | 1.069x | 0.880x |
| bintrees | medium / 17 | 306.99 | 185.08 | 1.66x | 204.65 | 165.43 | 1.106x | 0.894x |
| bintrees | large / 19 | 1430.20 | 819.50 | 1.75x | 856.58 | 678.31 | 1.045x | 0.828x |
| scene | small / 12 | 56.93 | 52.14 | 1.09x | 33.16 | 33.35 | 0.636x | 0.640x |
| scene | medium / 14 | 282.69 | 262.36 | 1.08x | 173.09 | 162.06 | 0.660x | 0.618x |
| scene | large / 16 | 1731.28 | 1493.59 | 1.16x | 975.82 | 905.10 | 0.653x | 0.606x |
| calc | small / 100000 | 92.93 | 86.81 | 1.07x | 71.34 | 70.96 | 0.822x | 0.817x |
| calc | medium / 400000 | 313.69 | 277.58 | 1.13x | 225.02 | 222.82 | 0.811x | 0.803x |
| calc | large / 1600000 | 1238.68 | 1081.97 | 1.14x | 861.59 | 832.43 | 0.796x | 0.769x |
| sum | small / 1000000 | 22.36 | 23.53 | 0.95x | 21.32 | 22.00 | 0.906x | 0.935x |
| sum | medium / 16000000 | 105.38 | 101.24 | 1.04x | 102.52 | 100.14 | 1.013x | 0.989x |
| sum | large / 128000000 | 691.50 | 731.26 | 0.95x | 742.86 | 666.86 | 1.016x | 0.912x |
| push | small / 1000000 | 21.57 | 21.45 | 1.01x | 22.93 | 24.15 | 1.069x | 1.126x |
| push | medium / 16000000 | 101.04 | 95.19 | 1.06x | 112.10 | 100.86 | 1.178x | 1.060x |
| push | large / 64000000 | 354.37 | 327.29 | 1.08x | 364.51 | 341.10 | 1.114x | 1.042x |

These are focused measurements of seven workloads, not a fresh full 16-workload suite.
Raw samples, original reference rows, build commands, source/IR/executable hashes and the measuring script are in [results.json](results.json).
