# Inline array access and leaner push: paired comparison

Measured: 2026-10-24T03:29:43.475383+00:00 UTC.

Same medium inputs, same toolchain, checked array access, alternating execution order.
Two warmups and best of three whole-process times; executable SHA-256 is checked before and after each run.
The before binaries use static data stacks already. This compares inline array access, leaner push, and inline integer division/remainder guards.
C++/Rust comparisons remain in [the full suite](../results.md).

| Workload | Parameter | Before ms | After ms | Before / after |
|---|---:|---:|---:|---:|
| sum | 16000000 | 516.27 | 103.85 | 4.97x |
| push | 16000000 | 799.02 | 98.23 | 8.13x |
| blur | 2048 | 1044.94 | 316.59 | 3.30x |

Raw samples, checksums, executable identities, compiler/runtime hashes and the measuring script are in [results.json](results.json).
