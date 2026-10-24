"""Paired production before/after measurements; original C++/Rust reference rows.

Run from scripts/devshell.ps1 after integration tests pass. The unchanged full
suite binaries in bench/current and current_bintrees are required as controls.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'bench'))
from artifacts import Executables
from run_bench import measure, sized


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def report(data, out):
    lines = ['# Internal calls and checked loop versioning', '',
             'Paired production R^ measurements. Original workload sources and sizes are unchanged.',
             'Two warmups, best of three whole-process times; execution order alternates.',
             'All checks remain enabled. Executable identity and checksums are verified on every run.', '',
             f"Measured: {data['started_utc']} UTC; compiler SHA-256: `{data['compiler_sha256']}`.", '',
             '**C++/Rust columns reuse the verified previous full-suite measurements. They are reference timings, not new runs in this session.**',
             'Their dates, all variants and raw samples are retained in [the full matrix](../results.md) and results.json.',
             'Only the before/after R^ columns form a same-session paired comparison.', '',
             '| Workload | Size / parameter | Before R^ ms | New R^ ms | Speedup | Reference C++ ms | Reference Rust ms | C++ / new R^ | Rust / new R^ |',
             '|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for old, new in zip(data['rows'][::2], data['rows'][1::2]):
        refs = [r for r in data['reference_rows'] if r.get('workload', 'bintrees') == new['workload'] and r['size'] == new['size']]
        cpp = min(r['best_ms'] for r in refs if r['language'] == 'cpp')
        rust = min(r['best_ms'] for r in refs if r['language'] == 'rust')
        a, b = old['best_ms'], new['best_ms']
        lines.append(f"| {new['workload']} | {new['size']} / {new['n']} | {a:.2f} | {b:.2f} | {a/b:.2f}x | {cpp:.2f} | {rust:.2f} | {cpp/b:.3f}x | {rust/b:.3f}x |")
    lines += ['', 'These are focused measurements of seven workloads, not a fresh full 16-workload suite.',
              'Raw samples, original reference rows, build commands, source/IR/executable hashes and the measuring script are in [results.json](results.json).', '']
    (out / 'results.md').write_text('\n'.join(lines), encoding='utf-8')
    (out / 'results.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def main():
    out = Path(__file__).resolve().parent
    work = ROOT / 'build/abi-ranges-bench'
    work.mkdir(exist_ok=True)
    compiler = ROOT / 'build/Release/rhat.exe'
    clang = shutil.which('clang')
    assert clang
    sessions = {key: json.loads((ROOT / path).read_text(encoding='utf-8')) for key, path in
                [('other', 'bench/current/workloads_results.json'), ('bintrees', 'bench/current_bintrees/results.json')]}
    data = dict(started_utc=datetime.now(timezone.utc).isoformat(),
                compiler_sha256=sha(compiler), runtime_sha256=sha(ROOT / 'runtime/rhat_runtime.c'),
                clang=subprocess.check_output([clang, '--version'], text=True).splitlines()[0],
                reference_environments={k: v['environment'] for k, v in sessions.items()},
                reference_dates={k: v['started_utc'] for k, v in sessions.items()},
                policy='two warmups; best of three; alternating order; original inputs; checks enabled',
                builds=[], rows=[], reference_rows=[], harness_source=Path(__file__).read_text(),
                source_hashes={str(p.relative_to(ROOT)): sha(p) for pattern in ('*.c', '*.h')
                               for p in (ROOT / 'src').glob(pattern)})
    registry = Executables()
    for name in ('blur', 'lru', 'bintrees', 'scene', 'calc', 'sum', 'push'):
        session = sessions['bintrees' if name == 'bintrees' else 'other']
        for size in ('small', 'medium', 'large'):
            original = next(r for r in session['rows'] if r.get('workload', 'bintrees') == name and r['size'] == size and r['language'] == 'rhat')
            before = Path(original['executable'])
            assert sha(before) == original['executable_sha256']
            n = original.get('n', original.get('depth'))
            src = work / f'{name}_{size}.rh'
            sized(ROOT / f'bench/rhat/{name}.rh', src, n)
            assert sha(src) == sha(before.parent / f'{name}_{size}.rh'), 'input changed'
            exe = work / f'after_{name}_{size}.exe'
            rir = exe.with_suffix('.rir')
            cmd = list(map(str, [compiler, 'build', src, '-o', exe, '-O2', '--clang', clang, '--dump-ir', rir]))
            subprocess.run(cmd, check=True)
            data['builds'].append(cmd)
            variants = [('before', before), ('after', exe)]
            for _, path in variants:
                registry.add(path)
            samples = {v: [] for v, _ in variants}
            warmups = {v: [] for v, _ in variants}
            for iteration in range(5):
                for variant, path in (variants if iteration % 2 == 0 else list(reversed(variants))):
                    result = registry.measure(path, lambda p: measure(p, 1800))
                    assert result['output'] == original['samples'][0]['output']
                    (warmups if iteration < 2 else samples)[variant].append(result)
            for variant, path in variants:
                data['rows'].append(dict(workload=name, size=size, n=n, variant=variant,
                                         executable=str(path), executable_sha256=sha(path),
                                         source_sha256=sha(src), input_ir_sha256=sha(str(path) + '.ll'),
                                         best_ms=min(r['ms'] for r in samples[variant]),
                                         samples=samples[variant], warmups=warmups[variant]))
            refs = [r for r in session['rows'] if r.get('workload', 'bintrees') == name and r['size'] == size and r['language'] != 'rhat']
            assert all(sha(r['executable']) == r['executable_sha256'] for r in refs)
            data['reference_rows'] += refs
            data['executable_hashes'] = registry.hashes
            assert sha(compiler) == data['compiler_sha256'] and sha(ROOT / 'runtime/rhat_runtime.c') == data['runtime_sha256']
            report(data, out)
            a, b = data['rows'][-2:]
            print(name, size, round(a['best_ms'], 2), '->', round(b['best_ms'], 2), 'ms', flush=True)


if __name__ == '__main__':
    main()
