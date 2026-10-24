# R^ benchmarks

Ports from the [Goose benchmark suite](https://github.com/aardappel/goose/tree/master/bench),
with R^ replacing Goose and freshly measured C++/Rust comparison rows.
Goose itself is neither built nor measured. Sources are pinned and vendored;
running the suite requires no network access.

Current coverage is `bintrees`, `sum`, `blur`, `particles`, `tree`, `graph`, `push`, `lru`, `scene`, `strlist`, `words`, `respond`, `records`, `interp`, `sexp`, `calc` (16/16). See [coverage.md](coverage.md) for
the selected original variants, representation differences and remaining language features.
See [results.md](results.md) for the local measurements and
[upstream/PROVENANCE.md](upstream/PROVENANCE.md) for source attribution.

The complete additional-workload matrix uses [run_workloads.py](run_workloads.py) and
writes [current/workloads_results.md](current/workloads_results.md) / `workloads_results.json`.
The numeric subset is also published in [workloads_results.md](workloads_results.md).
All C++/Rust comparison rows are freshly measured with R^ in the same session.
Historical bintrees data remains separate. Original small/medium/large sizes,
two warm-ups, best of three, checksum checks and alternating order are retained.

```powershell
. .\scripts\devshell.ps1 -Configuration Release
python bench/run_workloads.py --workloads sum blur particles --out build/bench-numeric --work build/bench-numeric
# Quick correctness check against independent Python oracles (not publishable timings):
python bench/run_workloads.py --sizes smoke --reps 1 --out build/bench-workloads-smoke
# A selected workload/size, or regenerate the saved report:
python bench/run_workloads.py --workloads sum --sizes small --out build/bench-sum
python bench/run_workloads.py --report-only
```

`sum` uses i32 values and eight scans; `blur` uses two flat u8 buffers and
16 passes with unchanged borders; `particles` uses 24-byte binary32 AoS
particles and 200 scalar updates. R^ now assigns reusable data stacks statically, reserving 1 GiB plus a
1 MiB guard per stack at startup. Windows commits up to 1 MiB on faults.
Scope exit rewinds the stack without releasing its reservation or pages. This
allows the original 512 MB large sum input without moving existing elements.
The current reference uses the same compiler/runtime as the complete suite.
Pre-fix numeric data is retained in invalid_runs for audit.
Array bounds/address calculations and the push fast path are emitted in LLVM IR.
Push uses byte-capacity checks and a conservative control-flow/callee-effect analysis
to omit retained-top comparisons only where proven safe. General bounds-check
elimination and explicit SIMD optimizations are not included.
See [the paired comparison](array_codegen_comparison/results.md) for a same-session
before/after check against the previous data-stack binaries; all current C++/Rust
comparisons remain in [results.md](results.md).

`tree` / `graph` have a separate measurement session in
[tree_graph/workloads_results.md](tree_graph/workloads_results.md), retaining
the earlier three-workload raw data and timestamps. Reproduce with:

```powershell
python bench/run_workloads.py --workloads tree graph --out bench/tree_graph --work build/bench-tree-graph
python bench/run_workloads.py --report-only --out bench/tree_graph
```

The tree uses 12-byte nodes with u32 indices (zero as null), compared with
both owning-pointer and index-arena controls. Graph uses an 8-byte linked
index pool, compared with nested adjacency, CSR and linked arena controls.
The BFS queue overwrites its initialized prefix between four sources and
pushes only if its high-water length grows. This preserves reusable storage
without adding a language-level clear operation, and adds a branch at enqueue.
All original inputs, traversal/source counts and checksums are retained.
R^ still checks indices; it does not implement relative-reference or
check-free vertex types. These representations are identified in the report.

`push` is measured separately in [push/workloads_results.md](push/workloads_results.md):

```powershell
python bench/run_workloads.py --workloads push --out bench/push --work build/bench-push
python bench/run_workloads.py --report-only --out bench/push
```

The R^ port retains real references returned by push, marking every 64th
8-byte Item across later growth. It uses `(&Item)[>..]`, not an index substitute.
C++ vector and Rust Vec controls use indices; the C++ deque control retains
pointers. All original sizes, RNG outputs and four checksum lines are retained.
The largest Item storage is 512 MB and fits the existing 1 GiB reservation.
Invalid pre-fix push data is archived; the current push matrix is an artifact-verified
subset of the complete current session, using reference-returning push and owner checks.

The original measurements in `results.json` predate the numeric-type
expansion and mandatory `^` suffix. `rhat/bintrees.rh` now uses the required
suffix; the original source is available in commit `0c5eb3f`. Those raw
measurements and hashes remain unchanged. `results.md` combines the latest
R^ measurements with the original C++/Rust rows and identifies both sessions.
Re-running the full suite measures all implementations with the selected tools.

The subsequent Windows page-commit change is measured separately in
[page_commit_results.md](page_commit_results.md). Reproduce its controlled
comparison using identical LLVM IR and the same Clang:

```powershell
. .\scripts\devshell.ps1 -Configuration Release
python bench/compare_page_commits.py
```

This compares the runtime from commit `9bfaf5f` with the current runtime at
the small size, then measures the current runtime at medium/large. It keeps
the original raw measurements intact and does not rerun C++/Rust. Raw samples, both
runtime sources, LLVM hashes and build commands are saved separately.
Run `python bench/run_bench.py --report-only` afterwards to update `results.md`.
When newer page-commit data is present, report-only generation uses its R^
rows and recalculates the comparison ratios; it retains the original JSON.

From the repository root on Windows x64 with Visual Studio C++/Clang,
Python 3 and Rust installed:

```powershell
.\scripts\build.ps1 -Configuration Release
. .\scripts\devshell.ps1 -Configuration Release
python bench/run_bench.py
```

This builds all implementations at the original small/medium/large depths
(15/17/19) and `-O2`/`/O2`/Rust `-O`, runs two warm-ups and three timed samples,
and writes `bench/results.md` and `bench/results.json`. All runs must match
an independent mathematical checksum. A mismatch, build failure or timeout
stops the run; it is never reported as a fast result.

```powershell
# Short correctness/runner check; not a published benchmark size:
python bench/run_bench.py --sizes smoke --reps 1 --out build/bench-smoke

# One original size, retaining the normal measurement policy:
python bench/run_bench.py --sizes small --out build/bench-small

# Regenerate Markdown from the saved raw data without running benchmarks:
python bench/run_bench.py --report-only
```

Executable files, sized source copies, LLVM IR, R^ IR, object files and
captured output go under `build/bench/`. `--work` changes that directory;
`--compiler` selects the R^ executable; `--timeout` sets the per-process
limit in seconds (default 1800). Do not run two harnesses in the same work
directory. Measurement is sequential; avoid unrelated heavy workloads.

Times include input construction, traversal, teardown and startup. Peak
memory means peak resident working set, not reserved virtual address space.
Compilation is excluded. Every raw sample, warm-up, build command, source
SHA-256, toolchain version, CPU and repository state is saved in JSON.
Sample order alternates forwards/backwards across implementations.

R^ emits LLVM IR, so its single Clang result is compared with both C++
backends and rustc. There is no R^/MSVC result. Original C++/Rust sources are
unchanged, including scratch reuse in their arena variants. R^ now reserves its
data stacks once and rewinds the temporary tree's watermark, retaining pages.
The pre-data-stack reports reserved/released a region per tree; those measurements
remain available in pre_data_stacks_bintrees. All storage costs stay inside the
measurement. Its nodes use two i32 indices (8 bytes), preserving tree shape and checksum without introducing
unsupported relative-reference or nullable-reference syntax.

`lru` is measured separately in [lru/workloads_results.md](lru/workloads_results.md):

```powershell
python bench/run_workloads.py --workloads lru --out bench/lru --work build/bench-lru
python bench/run_workloads.py --report-only --out bench/lru
```

The port uses 16-byte nodes and 8-byte open-addressed hash slots, matching the
C++ expert / Rust open-table algorithm. A u32 free list uses pop to recycle
removed nodes, so the pool is bounded by capacity plus two sentinels. Eviction,
backward-shift deletion, recency order, all original sizes and six checksums are
preserved. It uses integer indices without pool-bound relative-reference or
generation guarantees. Array pop/clear retain initialized storage and committed
pages. This historical LRU session uses the earlier 32-byte descriptor;
the current full suite uses the 24-byte data-stack descriptor. Earlier
measurements are not silently refreshed.

`scene` has a separate session in [scene/workloads_results.md](scene/workloads_results.md):

```powershell
python bench/run_workloads.py --workloads scene --out bench/scene --work build/bench-scene
python bench/run_workloads.py --report-only --out bench/scene
```

Its 120-byte nodes match the original C++/Rust arena representation: two inline
48-byte transforms, four u32 child slots, a u8 child count and an i32 id. The port
keeps the original depths (12/14/16), preorder ids, seed sequence, 16 animation /
recursive update / gather frames and f32 operations. No compiler or runtime
changes were needed for this port. Composition writes through references to
disjoint fields directly; indices and ordinary fixed arrays replace Goose's
relative references and limited array. A Python oracle uses integer affine
algebra and a parent-ordered list rather than recursively mutating an arena.

The text workloads have a separate session in [text/workloads_results.md](text/workloads_results.md):

```powershell
python bench/run_workloads.py --workloads strlist words respond --out bench/text --work build/bench-text
python bench/run_workloads.py --report-only --out bench/text
```

Strlist copies split words into an owned packed byte region: each generated
3..20-byte word has the same one-byte varint length followed by its bytes. The
original text remains alive as in the source. Words uses the same copied packed
list and a borrowed-key FNV open table, plus fixed-capacity vocabulary entries.
These are R^ library representations; the language does not yet support general
variable-size array elements or nested owned strings. No copied word list is
replaced by borrowed views or offset pairs.

Respond ports the original **streaming** Goose variant and reuses its output
buffer, as in the C++/Rust streaming controls. It keeps the exact request bytes,
quote/backslash escaping, decimal formatting, RNG sequence and FNV checksum.
The separate DTO-building variant remains future work. Python independently
checks these workloads with bytes.split, Counter and the JSON serializer. Raw
measurements identify the exact compiler/runtime/harness source hashes for this
session; prior reports retain their original data.

The four variant/parser ports are measured in
[variants_parsers/workloads_results.md](variants_parsers/workloads_results.md):

```powershell
python bench/run_workloads.py --workloads records interp sexp calc --out bench/variants_parsers --work build/bench-variants-parsers
python bench/run_workloads.py --report-only --out bench/variants_parsers
```

Records selects the original fixed-mode workload, using a native 24-byte Event
sum and 15-byte inline text. Interp uses a native 12-byte Expr sum with u32 child
indices. Sexp uses a native 24-byte node sum, u32 first-child/previous-sibling
links and copied owned symbol bytes in a separate region. Calc uses 24-byte AST
nodes (i64 numbers/u32 children), explicit nominal result unions and reusable
text/AST regions. Its parser preserves precedence, error order, partial-node
counting, input corruption and wrapping u64 evaluation. Sexp propagates real
EOF/unclosed-list failures despite the generated input being valid. No nonlocal
return or general variable-size ADT representation is claimed. Python oracles
use independently generated semantic s-expression trees and a Pratt arithmetic
parser; C++/Rust check every original-sized run. All variants, sizes and source
hashes are identified in the report. No suite-wide average is claimed across
these separate implementation/measurement sessions.

## Executable identity correction

An older runner reused executable basenames for R^ and Rust in `sum`, `particles`,
`push` and `records`. Rust replaced the R^ binary before measurement, so those old
R^ rows measured Rust and are invalid. Output checksums did not detect this because
both implementations deliberately compute the same outputs. The old affected rows
are explicitly invalidated; verbatim pre-fix snapshots and SHA-256 are retained in
[invalid_runs/README.md](invalid_runs/README.md).

The current runner uses separate R^ / C++ / Rust filenames, rejects duplicate
artifact paths and verifies each executable against its build-time SHA-256 before
and after every execution. JSON records executable paths/hashes per row and sample.
Corrected numeric results are in
[current/workloads_results.md](current/workloads_results.md),
push results are freshly replaced in [push/workloads_results.md](push/workloads_results.md),
and variant/parser results use the corrected runner. The root numeric
[workloads_results.md](workloads_results.md), push and variant/parser matrices are
current-session subsets; their invalid predecessors remain in invalid_runs. All
16 workloads and all comparison controls have been remeasured at the original
sizes; previous apparent R^/Rust parity is withdrawn.

## Current complete suite

The current, artifact-verified complete suite is in
[current/workloads_results.md](current/workloads_results.md) (all 15 additional
workloads) and [current_bintrees/results.md](current_bintrees/results.md).
[results.md](results.md) summarizes all 16 using the same current R^ compiler and
runtime; the detailed reports preserve every comparison variant and all samples.

```powershell
python bench/run_workloads.py --out bench/current --work build/bench-current
python bench/run_bench.py --out bench/current_bintrees --work build/bench-current-bintrees
python bench/run_bench.py --report-only
```

The last command verifies completeness, sample identities, checksums and matching
compiler/runtime/toolchain metadata before regenerating the 16-workload summary.
Previous per-stage reports are historical; invalid executable-collision rows are
explicitly flagged and are not used in the current summary.
