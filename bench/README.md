# R^ benchmarks

Ports from the [Goose benchmark suite](https://github.com/aardappel/goose/tree/master/bench),
with R^ replacing Goose and freshly measured C++/Rust comparison rows.
Goose itself is neither built nor measured. Sources are pinned and vendored;
running the suite requires no network access.

Current coverage is `bintrees` (1/16). See [coverage.md](coverage.md) for
the remaining workloads and their missing language/runtime features.
See [results.md](results.md) for the local measurements and
[upstream/PROVENANCE.md](upstream/PROVENANCE.md) for source attribution.

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
