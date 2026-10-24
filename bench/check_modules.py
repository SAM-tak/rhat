"""Verify all 48 original inputs after sharing RNGs; deliberately no timing report."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

from run_bench import sized


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--work', default='build/module-bench-check')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    compiler, clang = Path(args.compiler).resolve(), Path(args.clang).resolve()
    compiler_hash = sha(compiler)
    references = [repo/'bench/all08/current/workloads_results.json',
                  repo/'bench/all08/current_bintrees/results.json']
    conditions = {}
    for path in references:
        data = json.loads(path.read_text())
        for row in data['rows']:
            if row['size'] not in ('small', 'medium', 'large'):
                continue
            key = row.get('workload', 'bintrees'), row['size']
            outputs = {s['output'].strip() for s in row['samples']+row['warmups']}
            assert len(outputs) == 1
            expected = next(iter(outputs))
            if key in conditions:
                assert conditions[key]['expected'] == expected, ('reference mismatch', key)
            else:
                conditions[key] = dict(expected=expected, n=row.get('n', row.get('depth')), languages=set())
            conditions[key]['languages'].add(row['language'])
    assert len(conditions) == 48
    assert all({'cpp', 'rust', 'rhat'} <= row['languages'] for row in conditions.values())
    data = dict(compiler_sha256=compiler_hash, clang_sha256=sha(clang),
                source_hashes={str(p.relative_to(repo)): sha(p)
                               for p in [*sorted((repo/'stdlib').rglob('*.rh')),
                                         repo/'runtime/rhat_runtime.c', repo/'runtime/rhat_random.c']},
                references={str(p.relative_to(repo)): sha(p) for p in references}, rows=[])

    def run(command, timeout=180):
        p = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=timeout)
        assert p.returncode == 0, (command, p.stdout, p.stderr)
        return p

    for (name, size), row in conditions.items():
        source = work/f'{name}_{size}.rh'
        original = repo/f'bench/rhat/{name}.rh'
        sized(original, source, row['n'])
        exe = source.with_suffix('.exe')
        assert sha(compiler) == compiler_hash
        built = run([compiler, 'build', source, '-o', exe, '--clang', clang, '-O2', '--verbose'])
        exe_hash = sha(exe)
        actual = run([exe], timeout=300).stdout.strip()
        assert sha(exe) == exe_hash and actual == row['expected'], (name, size, actual, row['expected'])
        emitted = re.search(r'^IR: <entry> -> (.+)$', built.stderr, re.M)
        assert emitted, built.stderr
        module_ir = Path(emitted[1].strip())
        optimized = source.with_suffix('.optimized.ll')
        run([clang, '-Wno-override-module', '-S', '-emit-llvm', '-O2', module_ir, '-o', optimized])
        raw, opt = module_ir.read_text(), optimized.read_text()
        xorshift = re.search(r'; std\.random\.xorshift64\ndefine \w+ i64 @(\w+)\(', raw)
        if name != 'bintrees':
            assert xorshift, (name, 'shared RNG missing')
            assert not re.search(r'\bcall\b[^\n]*@'+xorshift[1]+r'\(', opt), (name, 'RNG not inlined')
        data['rows'].append(dict(workload=name, size=size, n=row['n'], output=actual,
                                 original_source_sha256=sha(original), generated_source_sha256=sha(source),
                                 executable_sha256=exe_hash, module_ir_sha256=sha(module_ir),
                                 optimized_ir_sha256=sha(optimized), shared_rng_inlined=name != 'bintrees'))
        data['checked_at_utc'] = datetime.now(timezone.utc).isoformat()
        (work/'verification.json').write_text(json.dumps(data, indent=2)+'\n')
        print(f'PASS {name}/{size} checksum and generated code ({len(data["rows"])}/48)', flush=True)
    assert len(data['rows']) == 48 and sha(compiler) == compiler_hash
    print('All 16 workloads / 48 conditions verified; no performance timings were recorded.', flush=True)


if __name__ == '__main__':
    main()
