# Windows page-commit optimization

Started 2026-10-23T16:09:01.680145+00:00 (UTC).

CPU: AMD Ryzen 5 2600 Six-Core Processor. OS: Windows-10-10.0.26300-SP0.

Clang: clang version 22.1.3 (https://github.com/llvm/llvm-project e9846648fd6183ee6d8cbdb4502213fcf902a211).

Before runtime: `9bfaf5f59c3cf330ed88c4b8bd8a6fda3785dec4`. After: current runtime recorded in JSON.

The compiler, benchmark source, LLVM IR and Clang flags are identical
between before/after. Compilation is excluded. Whole-process times include
tree construction, checks and teardown. Two warm-ups, best of three samples
by default; peak memory is the maximum of timed samples. Each sample is
checked against an independent node-count oracle.

| Size | Runtime | Depth | Best ms | Peak MiB |
|---|---|---:|---:|---:|
| small | before | 15 | 9831.65 | 4.77 |
| small | after | 15 | 768.34 | 4.77 |
| medium | after | 17 | 2596.45 | 7.77 |
| large | after | 19 | 10520.95 | 19.77 |

Controlled small-size speedup: **12.80x**.

Only small has a fresh controlled before/after comparison. Medium/large
measure the after runtime only; no before result is estimated or reused
from the older compiler's report. C++/Rust are not remeasured here.

The runtime commits only the newly required pages, and caches the page
size using Windows one-time initialization. The 24-byte array descriptor,
64 MiB reservation, stable addresses and scope-based release are preserved.
Each short-lived tree still reserves/releases a separate OS region;
there is no shared data-stack or scratch-region reuse optimization.

Raw samples, sources, hashes and build commands are in `page_commit_results.json`.
