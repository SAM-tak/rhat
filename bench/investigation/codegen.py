"""Diagnostic ablations, not production optimizations or official suite timings.

Run from scripts/devshell.ps1 after the complete current suite has been built.
The unchecked variants deliberately remove index guards in temporary LLVM files;
they estimate opportunity only and must never be shipped as safe R^ code.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'bench'))
from artifacts import Executables
from run_bench import measure

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    work = ROOT / 'build/codegen-investigation'
    work.mkdir(parents=True, exist_ok=True)
    out = Path(__file__).resolve().parent
    clang = shutil.which('clang')
    assert clang, 'Load scripts/devshell.ps1 first'
    current = json.loads((ROOT / 'bench/current/workloads_results.json').read_text())
    trees = json.loads((ROOT / 'bench/current_bintrees/results.json').read_text())
    data = dict(started_utc=datetime.now(timezone.utc).isoformat(),
                commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                policy='alternating order; two warmups; best of three; medium inputs',
                warning='unchecked variants are unsafe diagnostic ablations, not proposed compiler behavior',
                environment=current['environment'], builds=[], rows=[],
                harness_source=Path(__file__).read_text())
    registry = Executables()
    for name in ('blur', 'lru', 'bintrees', 'scene', 'calc'):
        session = trees if name == 'bintrees' else current
        row = next(r for r in session['rows'] if r.get('workload', 'bintrees') == name
                   and r['size'] == 'medium' and r['language'] == 'rhat')
        baseline = Path(row['executable'])
        assert sha(baseline) == row['executable_sha256']
        original = Path(str(baseline) + '.ll').read_text()
        variants = [('baseline', baseline)]
        for variant in ('internal', 'unchecked', 'internal_unchecked'):
            ir = original
            if 'internal' in variant:
                ir = ir.replace('define void @rh_fn_', 'define internal void @rh_fn_')
            if 'unchecked' in variant:
                ir, count = re.subn(r'br i1 %t\d+, label (%index_ok_\d+), label %index_bad_\d+, !prof !0',
                                    r'br label \1', ir)
                assert count > 0
            ll = work / f'{name}_{variant}.ll'
            ll.write_text(ir)
            exe = ll.with_suffix('.exe')
            cmd = [clang, '-Wno-override-module', '-std=c11', '-O2', '-rtlib=compiler-rt',
                   str(ll), str(ROOT / 'runtime/rhat_runtime.c'), '-o', str(exe)]
            subprocess.run(cmd, check=True, capture_output=True)
            data['builds'].append(cmd)
            remarks = subprocess.run([clang, '-Wno-override-module', '-O2', '-S', '-emit-llvm',
                                      '-Rpass=loop-vectorize', '-Rpass-missed=loop-vectorize',
                                      '-Rpass-analysis=loop-vectorize', str(ll), '-o',
                                      str(ll.with_suffix('.opt.ll'))], capture_output=True, text=True, check=True)
            (work / f'{name}_{variant}.remarks.txt').write_text(remarks.stderr)
            variants.append((variant, exe))
        samples = {v: [] for v, _ in variants}
        warmups = {v: [] for v, _ in variants}
        for _, exe in variants:
            registry.add(exe)
        for iteration in range(5):
            order = variants if iteration % 2 == 0 else list(reversed(variants))
            for variant, exe in order:
                result = registry.measure(exe, lambda p: measure(p, 1800))
                assert result['output'] == row['samples'][0]['output']
                (warmups if iteration < 2 else samples)[variant].append(result)
        for variant, exe in variants:
            data['rows'].append(dict(workload=name, variant=variant, size='medium',
                                     executable=str(exe), executable_sha256=sha(exe),
                                     input_ir_sha256=sha(str(baseline) + '.ll'),
                                     best_ms=min(s['ms'] for s in samples[variant]),
                                     samples=samples[variant], warmups=warmups[variant]))
        print(name, {v: round(min(s['ms'] for s in samples[v]), 2) for v, _ in variants}, flush=True)
        data['executable_hashes'] = registry.hashes
        (out / 'codegen.json').write_text(json.dumps(data, indent=2) + '\n')

if __name__ == '__main__':
    main()
