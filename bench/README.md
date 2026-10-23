# R^ benchmarks

Ports from the [Goose benchmark suite](https://github.com/aardappel/goose/tree/master/bench),
with R^ replacing Goose and freshly measured C++/Rust comparison rows.
Goose itself is neither built nor measured. Sources are pinned and vendored;
running the suite requires no network access.

Current coverage is `bintrees`, `sum`, `blur`, `particles`, `tree`, `graph` (6/16). See [coverage.md](coverage.md) for
the remaining workloads and their missing language/runtime features.
See [results.md](results.md) for the local measurements and
[upstream/PROVENANCE.md](upstream/PROVENANCE.md) for source attribution.

The three additional workloads use [run_workloads.py](run_workloads.py) and
write [workloads_results.md](workloads_results.md) / `workloads_results.json`.
All C++/Rust comparison rows are freshly measured with R^ in the same session.
Historical bintrees data remains separate. Original small/medium/large sizes,
two warm-ups, best of three, checksum checks and alternating order are retained.

```powershell
. .\scripts\devshell.ps1 -Configuration Release
python bench/run_workloads.py
# Quick correctness check against independent Python oracles (not publishable timings):
python bench/run_workloads.py --sizes smoke --reps 1 --out build/bench-workloads-smoke
# A selected workload/size, or regenerate the saved report:
python bench/run_workloads.py --workloads sum --sizes small --out build/bench-sum
python bench/run_workloads.py --report-only
```

`sum` uses i32 values and eight scans; `blur` uses two flat u8 buffers and
16 passes with unchanged borders; `particles` uses 24-byte binary32 AoS
particles and 200 scalar updates. R^ regions now reserve 1 GiB of virtual
address space per array; physical pages are committed only as used. This
allows the original 512 MB large sum input without moving existing elements.
No shrinking, reusable regions or check-elimination optimization is included.

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
unchanged, including scratch reuse in their arena variants. R^ currently
reserves/releases an OS region for every tree. The original recorded run
committed memory on every push; the current runtime commits only newly needed
pages and queries the page size once. These costs remain inside the
measurement. Its nodes use two i32
indices (8 bytes), preserving tree shape and checksum without introducing
unsupported relative-reference or nullable-reference syntax.
