"""Measure newly ported workloads, keeping historical bintrees data intact."""
import argparse
from datetime import datetime, timezone
import json
import os
import pathlib
import platform
import shutil

from run_bench import HERE, ROOT, UPSTREAM, command, measure, sha, sized

WORKLOADS = {
    "sum": dict(sizes=(1_000_000, 16_000_000, 128_000_000), smoke=1000,
                cpp=("vector", "reserved vector"), rust=("sum",)),
    "blur": dict(sizes=(1024, 2048, 8192), smoke=16,
                 cpp=("flat", "row pointers"), rust=("blur_index", "blur_windows")),
    "particles": dict(sizes=(100_000, 1_000_000, 4_000_000), smoke=32,
                      cpp=("scalar AoS", "SoA", "vector AoS"),
                      rust=("particles_scalar", "particles")),
    "tree": dict(sizes=(16, 20, 24), smoke=6, parameter="depth",
                 cpp=("unique_ptr", "new/delete", "index arena"), rust=("tree_box", "tree_arena"),
                 rhat_label="R^ u32-index node pool (LLVM/Clang)",
                 representation="R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. "
                 "It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. "
                 "Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved."),
    "graph": dict(sizes=(100_000, 500_000, 2_000_000), smoke=64, parameter="vertices",
                  cpp=("nested vectors", "CSR", "linked index arena"),
                  rust=("graph_nested", "graph_csr", "graph_arena"),
                  rhat_label="R^ linked u32-index pool (LLVM/Clang)",
                  representation="R^ uses 8-byte edges (i32 target, u32 next index), including one list-head slot per vertex. "
                  "Its queue overwrites an initialized prefix and pushes only when its high-water length grows; "
                  "a separate tail resets between the four BFS sources. There is no unbounded append across sources. "
                  "This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged."),
}


def next_random(r):
    mask = (1 << 64) - 1
    r ^= (r << 13) & mask
    r ^= r >> 7
    return (r ^ (r << 17)) & mask


def oracle(name, n):
    """Independent, untimed smoke oracle. Larger runs cross-check C++ and Rust."""
    r = 12345
    if name == "sum":
        total = 0
        for _ in range(n):
            r = next_random(r)
            total += sum((r % 1000) ^ p for p in range(8))
    elif name == "blur":
        a, b = [], [0] * (n*n)
        for _ in range(n*n):
            r = next_random(r)
            a.append(r % 256)
        for _ in range(16):
            for y in range(1, n-1):
                for x in range(1, n-1):
                    i = y*n+x
                    b[i] = (a[i-n-1] + 2*a[i-n] + a[i-n+1]
                            + 2*a[i-1] + 4*a[i] + 2*a[i+1]
                            + a[i+n-1] + 2*a[i+n] + a[i+n+1]) >> 4
            a, b = b, a
        total = sum(a)
    elif name == "particles":
        total = 0
        for _ in range(n):
            r = next_random(r)
            x, y, z = (float((r >> shift) % 1024) for shift in (0, 12, 24))
            vx, vy, vz = (((r >> shift) % 256 - 128) / 16.0 for shift in (36, 44, 52))
            # All these dyadic values are exactly representable in binary32.
            for _ in range(200):
                vy -= 0.0625
                x, y, z = x+vx, y+vy, z+vz
                if y < 0:
                    y, vy = -y, -(vy*0.5)
            total += int(x*16) + int(y*16) + int(z*16)
    elif name == "tree":
        def visit(depth, seed):
            own = sum((seed % 1000) ^ p for p in range(8))
            if depth == 0:
                return own
            return own + visit(depth-1, next_random(seed)) + visit(depth-1, next_random((seed+1) & ((1 << 64)-1)))
        return f"{(1 << (n+1))-1}\n{visit(n, r)}"
    elif name == "graph":
        # Lists in generation order and Python integers, independent of the
        # R^ linked pool and its reusable queue representation.
        adj = [[] for _ in range(n)]
        for _ in range(n*8):
            r = next_random(r)
            adj[r % n].append((r >> 24) % n)
        total, seed = 0, 999
        for _ in range(4):
            seed = next_random(seed)
            src = seed % n
            distance = [-1] * n
            distance[src] = 0
            queue = [src]
            for u in queue:
                total += distance[u]
                for v in adj[u]:
                    if distance[v] == -1:
                        distance[v] = distance[u]+1
                        queue.append(v)
        return f"{n*9}\n{total}"
    else:
        raise ValueError(f"no independent oracle for {name}")
    return f"{n}\n{total}"


def report(data, out):
    out.mkdir(parents=True, exist_ok=True)
    (out / "workloads_results.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    lines = ["# Additional R^ workloads", "", f"Measured: {data['started_utc']} (UTC).", "",
             "R^ replaces Goose. All comparison implementations were built and measured in this session.", "",
             f"Whole-process wall time, two warm-ups, best of {data['reps']} timed samples; peak resident working set.",
             "R^ retains bounds/range checks and OS-backed, stable-address arrays. No affinity is set.", "",
             "Comparison / R^ time: above 1 means R^ is faster. No suite-wide average is claimed.", "",
             "Smoke sizes are correctness checks and are not performance evidence.", "",
             "## Environment", "", "| Item | Value |", "|---|---|"]
    for key, value in data["environment"].items():
        text = str(value).replace("|", "/").replace("\n", "; ")
        lines.append(f"| {key} | {text} |")
    if data.get("metadata_corrections"):
        lines += ["", *data["metadata_corrections"]]
    for name in data["workloads"]:
        lines += ["", "## " + name, ""]
        if data.get("representations", {}).get(name):
            lines += [data["representations"][name], ""]
        parameter = data.get("parameters", {}).get(name, "N")
        lines += [f"| Size / {parameter} | Implementation | Best ms | Peak MiB | Comparison / R^ time |",
                  "|---|---|---:|---:|---:|"]
        for row in data["rows"]:
            if row["workload"] != name:
                continue
            rhat = next(r for r in data["rows"] if r["workload"] == name
                        and r["size"] == row["size"] and r["language"] == "rhat")
            lines.append(f"| {row['size']} / {row['n']} | {row['label']} | {row['best_ms']:.2f} | "
                         f"{row['peak_bytes']/1048576:.2f} | {row['best_ms']/rhat['best_ms']:.3f}x |")
    lines += ["", "Every warm-up/sample checks its checksum. Each size first cross-checks independently built",
              "C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.",
              "Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions",
              "are saved in workloads_results.json. Historical bintrees measurements are in "
              f"[results.md]({pathlib.Path(os.path.relpath(HERE / 'results.md', out)).as_posix()}).", ""]
    (out / "workloads_results.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workloads", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    parser.add_argument("--sizes", nargs="+", choices=("smoke", "small", "medium", "large"), default=["small", "medium", "large"])
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--compiler", type=pathlib.Path, default=ROOT / "build/Release/rhat.exe")
    parser.add_argument("--work", type=pathlib.Path, default=ROOT / "build/bench-workloads")
    parser.add_argument("--out", type=pathlib.Path, default=HERE)
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    if args.report_only:
        report(json.loads((args.out / "workloads_results.json").read_text(encoding="utf-8")), args.out)
        return
    if os.name != "nt" or args.reps < 1 or args.timeout < 1:
        parser.error("Windows and positive --reps/--timeout required")
    tools = {name: shutil.which(exe) for name, exe in
             (("clang", "clang"), ("msvc", "cl"), ("clangcl", "clang-cl"), ("rustc", "rustc"))}
    if not all(tools.values()):
        parser.error("load scripts/devshell.ps1; Clang, MSVC, Rust required")
    compiler, work = args.compiler.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
        cpu = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    env = dict(cpu=cpu, os=platform.platform(), commit=command(["git", "rev-parse", "HEAD"], cwd=ROOT),
               dirty=command(["git", "status", "--porcelain"], cwd=ROOT),
               compiler_sha256=sha(compiler), runtime_sha256=sha(ROOT / "runtime/rhat_runtime.c"))
    for key, exe in tools.items():
        banner = command([exe, "/Bv", "/?"] if key == "msvc" else [exe, "--version"])
        if key == "msvc":
            env[key] = next((line.strip() for line in banner.splitlines()
                             if "Compiler Version" in line or "コンパイラ バージョン" in line), None)
            if not env[key]:
                raise RuntimeError("could not identify MSVC version")
        else:
            env[key] = banner.splitlines()[0]
    data = dict(started_utc=datetime.now(timezone.utc).isoformat(), upstream_commit=UPSTREAM,
                environment=env, workloads=args.workloads, warmups=2, reps=args.reps,
                builds=[], rows=[], controls=[], source_hashes={},
                representations={name: WORKLOADS[name].get("representation", "") for name in args.workloads},
                parameters={name: WORKLOADS[name].get("parameter", "N") for name in args.workloads},
                harness_source=pathlib.Path(__file__).read_bytes().decode("utf-8"))
    for directory in (ROOT / "src", ROOT / "runtime", HERE):
        for path in directory.rglob("*"):
            if path.is_file() and path.suffix in (".c", ".h", ".rh", ".rs", ".cpp", ".py"):
                data["source_hashes"][str(path.relative_to(ROOT))] = sha(path)

    def build(argv):
        data["builds"].append(list(map(str, argv)))
        command(argv, cwd=work)

    shutil.copyfile(HERE / "upstream/rust/bench.rs", work / "bench.rs")
    for name in args.workloads:
        spec = WORKLOADS[name]
        for size in args.sizes:
            n = spec["smoke"] if size == "smoke" else spec["sizes"][("small", "medium", "large").index(size)]
            implementations = []
            src = work / f"{name}_{size}.rh"
            sized(HERE / f"rhat/{name}.rh", src, n)
            exe = src.with_suffix(".exe")
            build([compiler, "build", src, "-o", exe, "-O2", "--clang", tools["clang"],
                   "--dump-ir", src.with_suffix(".rir")])
            data["source_hashes"][str(src)] = sha(src)
            data["source_hashes"][str(exe) + ".ll"] = sha(pathlib.Path(str(exe) + ".ll"))
            implementations.append((spec.get("rhat_label", "R^ LLVM/Clang"), "rhat", exe))
            for tc in ("msvc", "clangcl"):
                for variant, label in enumerate(spec["cpp"]):
                    exe = work / f"{name}_{size}_{tc}_{variant}.exe"
                    build([tools[tc], "/nologo", "/O2", "/EHsc", "/std:c++20", f"/DBENCH_N={n}",
                           f"/DVARIANT={variant}", HERE / f"upstream/cpp/{name}.cpp",
                           "/Fe" + str(exe), "/Fo" + str(exe.with_suffix(".obj"))])
                    implementations.append((f"C++ {label} ({tc})", "cpp", exe))
            for rust in spec["rust"]:
                src = work / f"{rust}_{size}.rs"
                sized(HERE / f"upstream/rust/{rust}.rs", src, n)
                exe = src.with_suffix(".exe")
                build([tools["rustc"], "-O", "-C", "codegen-units=1", src, "-o", exe])
                implementations.append((f"Rust {rust}", "rust", exe))
            # Establish the result from two independent controls before timing.
            cpp = next(exe for _, lang, exe in implementations if lang == "cpp")
            rust = next(exe for _, lang, exe in implementations if lang == "rust")
            cpp_control, rust_control = measure(cpp, args.timeout), measure(rust, args.timeout)
            expected = cpp_control["output"]
            if rust_control["output"] != expected:
                raise RuntimeError(f"{name}/{size}: C++/Rust control mismatch")
            if size == "smoke" and expected != oracle(name, n):
                raise RuntimeError(f"{name}: Python oracle mismatch")
            data["controls"].append(dict(workload=name, size=size, n=n, output=expected,
                                         cpp=str(cpp), rust=str(rust), cpp_control=cpp_control,
                                         rust_control=rust_control, python_oracle=size == "smoke"))
            samples = {label: [] for label, _, _ in implementations}
            warmups = {label: [] for label, _, _ in implementations}
            for iteration in range(2 + args.reps):
                ordered = implementations if iteration % 2 == 0 else list(reversed(implementations))
                for label, _, exe in ordered:
                    result = measure(exe, args.timeout)
                    if result["output"] != expected:
                        raise RuntimeError(f"{name}/{size} {label}: checksum {result['output']} != {expected}")
                    (warmups if iteration < 2 else samples)[label].append(result)
                    print(f"{name}/{size} {label} round {iteration+1}: {result['ms']:.2f} ms verified", flush=True)
            for label, language, _ in implementations:
                data["rows"].append(dict(workload=name, size=size, n=n, label=label, language=language,
                                         best_ms=min(s["ms"] for s in samples[label]),
                                         peak_bytes=max(s["peak_bytes"] for s in samples[label]),
                                         samples=samples[label], warmups=warmups[label]))
            report(data, args.out)


if __name__ == "__main__":
    main()
