"""Controlled Windows runtime comparison using identical R^ LLVM IR.

Before: the selected committed runtime. After: the current working runtime.
Both are built by the same Clang, -O2, against the very same sized LLVM IR.
The slow before-runtime is measured at small only; after runs all three sizes.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
from datetime import datetime, timezone

import run_bench as bench

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=pathlib.Path, default=ROOT / "build/Release/rhat.exe")
    parser.add_argument("--before", default="9bfaf5f", help="commit containing the before runtime")
    parser.add_argument("--work", type=pathlib.Path, default=ROOT / "build/page-commit-bench")
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "bench")
    parser.add_argument("--reps", type=int, default=3)
    args = parser.parse_args()
    if os.name != "nt" or args.reps < 1:
        parser.error("Windows and a positive --reps are required")
    args.compiler = args.compiler.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    clang = os.environ.get("RHAT_CLANG") or shutil.which("clang")
    if not clang or not args.compiler.is_file():
        parser.error("build Release and load scripts/devshell.ps1")
    before_commit = bench.command(["git", "rev-parse", args.before], cwd=ROOT)
    before_bytes = subprocess.check_output(
        ["git", "show", before_commit + ":runtime/rhat_runtime.c"], cwd=ROOT)
    before_path = work / "runtime_before.c"
    before_path.write_bytes(before_bytes)
    after_path = work / "runtime_after.c"
    after_path.write_bytes((ROOT / "runtime/rhat_runtime.c").read_bytes())
    sources = {"before": before_path, "after": after_path}
    data = dict(started_utc=datetime.now(timezone.utc).isoformat(),
                before_commit=before_commit,
                working_commit=bench.command(["git", "rev-parse", "HEAD"], cwd=ROOT),
                dirty=bench.command(["git", "status", "--porcelain"], cwd=ROOT),
                cpu=platform.processor(), os=platform.platform(),
                clang=bench.command([clang, "--version"]).splitlines()[0],
                compiler_sha256=bench.sha(args.compiler),
                benchmark_sha256=bench.sha(ROOT / "bench/rhat/bintrees.rh"),
                harness_sha256=bench.sha(pathlib.Path(__file__)),
                measurement_helper_sha256=bench.sha(ROOT / "bench/run_bench.py"),
                runtime_sources={name: path.read_text(encoding="utf-8") for name, path in sources.items()},
                runtime_sha256={name: bench.sha(path) for name, path in sources.items()},
                reps=args.reps, warmups=2, builds=[], rows=[], llvm_sha256={})
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                        r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
        data["cpu"] = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()

    def build(command):
        data["builds"].append([str(a) for a in command])
        bench.command(command, cwd=work)

    def sample(exe, depth):
        result = bench.measure(exe, 180)
        if result["output"] != bench.oracle(depth):
            raise RuntimeError(f"checksum mismatch: {exe}")
        return result

    def save():
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "page_commit_results.json").write_text(
            json.dumps(data, indent=2) + "\n", encoding="utf-8")
        lines = ["# Windows page-commit optimization", "",
                 f"Started {data['started_utc']} (UTC).", "",
                 f"CPU: {data['cpu']}. OS: {data['os']}.", "",
                 f"Clang: {data['clang']}.", "",
                 f"Before runtime: `{before_commit}`. After: current runtime recorded in JSON.", "",
                 "The compiler, benchmark source, LLVM IR and Clang flags are identical",
                 "between before/after. Compilation is excluded. Whole-process times include",
                 "tree construction, checks and teardown. Two warm-ups, best of three samples",
                 "by default; peak memory is the maximum of timed samples. Each sample is",
                 "checked against an independent node-count oracle.", "",
                 "| Size | Runtime | Depth | Best ms | Peak MiB |", "|---|---|---:|---:|---:|"]
        for row in data["rows"]:
            lines.append(f"| {row['size']} | {row['runtime']} | {row['depth']} | "
                         f"{row['best_ms']:.2f} | {row['peak_bytes']/1048576:.2f} |")
        small = {r["runtime"]: r for r in data["rows"] if r["size"] == "small"}
        if "before" in small and "after" in small:
            speedup = small["before"]["best_ms"] / small["after"]["best_ms"]
            lines += ["", f"Controlled small-size speedup: **{speedup:.2f}x**."]
        lines += ["", "Only small has a fresh controlled before/after comparison. Medium/large",
                  "measure the after runtime only; no before result is estimated or reused",
                  "from the older compiler's report. C++/Rust are not remeasured here.", "",
                  "The runtime commits only the newly required pages, and caches the page",
                  "size using Windows one-time initialization. The 24-byte array descriptor,",
                  "64 MiB reservation, stable addresses and scope-based release are preserved.",
                  "Each short-lived tree still reserves/releases a separate OS region;",
                  "there is no shared data-stack or scratch-region reuse optimization.", "",
                  "Raw samples, sources, hashes and build commands are in `page_commit_results.json`.", ""]
        (args.out / "page_commit_results.md").write_text("\n".join(lines), encoding="utf-8")

    for size in ("small", "medium", "large"):
        depth = bench.SIZES[size]
        src = work / f"bintrees_{size}.rh"
        bench.sized(ROOT / "bench/rhat/bintrees.rh", src, depth)
        llvm = src.with_suffix(".ll")
        build([args.compiler, "emit", src, "-o", llvm])
        data["llvm_sha256"][size] = bench.sha(llvm)
        implementations = ["before", "after"] if size == "small" else ["after"]
        exes = {}
        for name in implementations:
            exe = work / f"bintrees_{size}_{name}.exe"
            build([clang, "-Wno-override-module", "-std=c11", "-O2", "-rtlib=compiler-rt",
                   llvm, sources[name], "-o", exe])
            exes[name] = exe
        samples = {name: [] for name in implementations}
        warmups = {name: [] for name in implementations}
        for iteration in range(2 + args.reps):
            ordered = implementations if iteration % 2 == 0 else list(reversed(implementations))
            for name in ordered:
                print(f"{size} {name} run {iteration+1}/{2+args.reps}", flush=True)
                result = sample(exes[name], depth)
                (warmups if iteration < 2 else samples)[name].append(result)
                print(f"  {result['ms']:.2f} ms; verified", flush=True)
        for name in implementations:
            values = samples[name]
            data["rows"].append(dict(size=size, runtime=name, depth=depth,
                                     best_ms=min(s["ms"] for s in values),
                                     peak_bytes=max(s["peak_bytes"] for s in values),
                                     samples=values, warmups=warmups[name]))
        save()


if __name__ == "__main__":
    main()
