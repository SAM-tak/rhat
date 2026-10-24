"""Remeasure all 48 conditions with freshly built, previously fastest controls."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'bench'
SESSION = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from artifacts import Executables
from run_bench import command, measure, sized
from report_suite import NAMES


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(data):
    (SESSION/'results.json').write_text(json.dumps(data, indent=2)+'\n', encoding='utf-8')


def validate(data, files=False):
    assert data['complete'] and len(data['conditions']) == 48
    assert {r['workload'] for r in data['conditions']} == set(NAMES)
    assert data['warmups'] == 2 and data['reps'] == 3
    paths = set()
    for row in data['conditions']:
        assert set(row['measurements']) == {'rhat', 'cpp', 'rust'}
        for language, measured in row['measurements'].items():
            assert measured['executable'] not in paths
            paths.add(measured['executable'])
            assert len(measured['warmups']) == 2 and len(measured['samples']) == 3
            assert measured['best_ms'] == min(s['ms'] for s in measured['samples'])
            for sample in measured['warmups']+measured['samples']:
                assert sample['output'] == row['expected']
                assert sample['executable_sha256'] == measured['executable_sha256']
                assert math.isfinite(sample['ms']) and sample['ms'] > 0
            if files:
                assert sha(measured['executable']) == measured['executable_sha256']
    if files:
        assert sha(data['compiler']) == data['environment']['compiler_sha256']
        for group in ('sources', 'baseline_reports', 'artifacts'):
            for path, digest in data[group].items():
                assert sha(ROOT/path) == digest, path


def write_report(data):
    validate(data)
    snapshot = json.loads((SESSION/'verification.json').read_text())
    assert snapshot['results_sha256'] == sha(SESSION/'results.json'), 'measurement record changed'
    jst = timezone(timedelta(hours=9))
    start = datetime.fromisoformat(data['started_utc']).astimezone(jst).strftime('%Y-%m-%d %H:%M JST')
    lines = ['# R^ vs C++ vs Rust — all 16 workloads', '',
        f'Measured: **{start}**. All 48 original small/medium/large conditions were remeasured.', '',
        '**R^, C++ and Rust were rebuilt and timed in this session. No earlier timings are reused in the current comparison columns.**', '',
        'For each condition, C++ and Rust use the variant/backend that was fastest in the preceding full-variant session.',
        'Only those selected controls were remeasured; this is not a fresh search over every comparison variant.',
        'Each condition uses two warm-ups and the minimum of three whole-process wall times. Execution order alternates.',
        'Every run checks the output and the executable SHA-256 before and after execution. No affinity is set.', '',
        'R^ uses the current builtin IR lowering and separate module objects. Benchmark RNGs are shared through `import^ std.random`.',
        'Bounds/range/lifetime checks remain enabled. Source, native object, module IR and executable identities are recorded.',
        'Changes since the preceding full run include builtin lowering and module compilation; differences cannot be attributed to import alone.', '',
        '[Previous complete matrix](pre_modules_results.md) · [previous all-variant measurements](all08/results.md) · '
        '[raw samples and build commands](modules_refresh/results.json) · [verification](modules_refresh/verification.json)', '',
        f"CPU: {data['environment']['cpu']}; OS: {data['environment']['os']}.",
        f"R^ compiler SHA-256: `{data['environment']['compiler_sha256']}`. The measured compiler includes uncommitted changes recorded in the raw data.", '',
        'Ratios are comparison / R^ time; above 1 means R^ is faster. C++/Rust columns refer to the selected controls described above.', '',
        '| Workload | Size / parameter | R^ ms | C++ ms | Rust ms | C++ / R^ | Rust / R^ | R^ peak MiB |',
        '|---|---|---:|---:|---:|---:|---:|---:|']
    changes = []
    for row in data['conditions']:
        m = row['measurements']
        r, c, u = (m[k]['best_ms'] for k in ('rhat','cpp','rust'))
        lines.append(f"| {row['workload']} | {row['size']} / {row['n']} | {r:.2f} | {c:.2f} | {u:.2f} | {c/r:.3f}x | {u/r:.3f}x | {m['rhat']['peak_bytes']/1048576:.2f} |")
        changes.append(dict(workload=row['workload'], size=row['size'], old=row['previous_rhat_ms'],
                            new=r, ratio=row['previous_rhat_ms']/r))
    lines += ['', '## Change from the preceding R^ session', '',
        'These are different-session measurements, not a paired experiment. Small differences can reflect scheduling/startup/clock variation.',
        'Previous / current above 1 means the current run was faster; the previous R^ compiler and source revision differ.', '',
        '| Workload | Size | Previous R^ ms | Current R^ ms | Previous / current |',
        '|---|---|---:|---:|---:|']
    for row in changes:
        lines.append(f"| {row['workload']} | {row['size']} | {row['old']:.2f} | {row['new']:.2f} | {row['ratio']:.3f}x |")
    lines += ['', '## Selected comparison implementations', '',
              '| Workload | Size | C++ variant/backend | Rust variant |', '|---|---|---|---|']
    for row in data['conditions']:
        lines.append(f"| {row['workload']} | {row['size']} | {row['measurements']['cpp']['label']} | {row['measurements']['rust']['label']} |")
    lines += ['', 'Historical raw results and the previous complete matrix are preserved. Goose was not run.', '']
    text = '\n'.join(lines)
    (HERE/'results.md').write_text(text, encoding='utf-8')
    # The snapshot lives two directories below bench; repair its relative links.
    local = re.sub(r'\]\((?!https?://)([^)]+)\)', lambda m: '](../../'+m[1]+')', text)
    (SESSION/'results.md').write_text(local, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=ROOT/'build/Release/rhat.exe')
    parser.add_argument('--work', type=Path, default=ROOT/'build/modules-timing')
    parser.add_argument('--report-only', action='store_true')
    args = parser.parse_args()
    if args.report_only:
        write_report(json.loads((SESSION/'results.json').read_text()))
        return
    if (SESSION/'results.json').exists() and json.loads((SESSION/'results.json').read_text()).get('complete'):
        parser.error('archive the completed session before recording another run; --report-only is safe')
    if os.name != 'nt':
        parser.error('Windows timing/working-set harness required')
    compiler, work = args.compiler.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    tools = {k: shutil.which(v) for k,v in [('clang','clang'),('msvc','cl'),('clangcl','clang-cl'),('rustc','rustc')]}
    assert compiler.is_file() and all(tools.values()), 'load scripts/devshell.ps1 and build Release'
    sources = [HERE/'all08/current/workloads_results.json', HERE/'all08/current_bintrees/results.json']
    old = []
    for source in sources:
        data = json.loads(source.read_text())
        old += [dict(r, workload=r.get('workload','bintrees'), n=r.get('n',r.get('depth')), baseline=str(source),
                     original_builds=data['builds']) for r in data['rows']]
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
        cpu = winreg.QueryValueEx(key,'ProcessorNameString')[0].strip()
    environment = dict(cpu=cpu, os=platform.platform(), compiler_sha256=sha(compiler),
        commit=command(['git','rev-parse','HEAD'],cwd=ROOT), dirty=command(['git','status','--porcelain'],cwd=ROOT))
    for name,exe in tools.items():
        banner = command([exe,'/Bv','/?'] if name=='msvc' else [exe,'--version'])
        environment[name] = next((s.strip() for s in banner.splitlines() if 'Compiler Version' in s or 'コンパイラ バージョン' in s), banner.splitlines()[0]) if name=='msvc' else banner.splitlines()[0]
    data = dict(complete=False, compiler=str(compiler), started_utc=datetime.now(timezone.utc).isoformat(),
        environment=environment, warmups=2, reps=3, comparison_policy='fresh previous-session winners per condition',
        baseline_reports={str(p.relative_to(ROOT)):sha(p) for p in sources}, sources={}, artifacts={}, builds=[], conditions=[])
    for directory in (ROOT/'src',ROOT/'runtime',ROOT/'stdlib',HERE/'upstream',HERE/'rhat'):
        for path in directory.rglob('*'):
            if path.is_file() and path.suffix in ('.c','.h','.rh','.cpp','.rs'):
                data['sources'][str(path.relative_to(ROOT))]=sha(path)
    for path in (Path(__file__),HERE/'artifacts.py',HERE/'run_bench.py',HERE/'report_suite.py',ROOT/'CMakeLists.txt'):
        data['sources'][str(path.relative_to(ROOT))]=sha(path)
    binaries = Executables()
    shutil.copy2(HERE/'upstream/rust/bench.rs',work/'bench.rs')
    archive = HERE/'pre_modules_results.md'
    if not archive.exists():
        shutil.copy2(HERE/'results.md',archive)
    data['baseline_reports'][str(archive.relative_to(ROOT))]=sha(archive)

    def build(argv):
        data['builds'].append(list(map(str,argv)))
        return command(argv,cwd=work)

    for name in NAMES:
        for size in ('small','medium','large'):
            prior = [r for r in old if r['workload']==name and r['size']==size]
            selected = {lang:min((r for r in prior if r['language']==lang),key=lambda r:r['best_ms']) for lang in ('rhat','cpp','rust')}
            n = selected['rhat']['n']
            outputs = {s['output'] for r in prior for s in r['samples']+r['warmups']}
            assert len(outputs)==1
            expected = outputs.pop()
            assert sha(compiler)==environment['compiler_sha256']
            implementations = {}
            src = work/f'{name}_{size}.rh'
            sized(HERE/f'rhat/{name}.rh',src,n)
            exe = work/f'rhat_{name}_{size}.exe'
            output = build([compiler,'build',src,'-o',exe,'-O2','--clang',tools['clang'],'--verbose','--dump-ir',src.with_suffix('.rir')])
            implementations['rhat'] = dict(label='R^ modules / builtin LLVM IR',executable=str(exe))
            for line in output.splitlines():
                match = re.match(r'IR: .+ -> (.+)$',line)
                if match:
                    path=Path(match[1]); data['artifacts'][str(path)]=sha(path)
            data['artifacts'][str(src)]=sha(src)
            data['artifacts'][str(exe)+'.ll']=sha(str(exe)+'.ll')
            for language in ('cpp','rust'):
                row=selected[language]
                if language=='cpp':
                    def target(argv):
                        return next((s[3:] for s in argv if s.startswith('/Fe')),None)
                    original=next(a for a in row['original_builds'] if target(a)==row['executable'])
                    tc='clangcl' if 'clang-cl' in Path(original[0]).name.lower() else 'msvc'
                    defines=[s for s in original if s.startswith('/D')]
                    exe=work/f'cpp_{name}_{size}_{tc}.exe'
                    build([tools[tc],'/nologo','/O2','/EHsc','/std:c++20',*defines,HERE/f'upstream/cpp/{name}.cpp','/Fe'+str(exe),'/Fo'+str(exe.with_suffix('.obj'))])
                else:
                    variant=row['label'].removeprefix('Rust ')
                    if name=='bintrees': variant='bintrees_'+variant
                    src=work/f'rust_{variant}_{size}.rs'
                    sized(HERE/f'upstream/rust/{variant}.rs',src,n)
                    exe=src.with_suffix('.exe')
                    build([tools['rustc'],'-O','-C','codegen-units=1',src,'-o',exe])
                    data['artifacts'][str(src)]=sha(src)
                implementations[language]=dict(label=row['label'],executable=str(exe),
                    selection_reference=row['baseline'],selection_best_ms=row['best_ms'])
            for implementation in implementations.values():
                exe=Path(implementation['executable']); binaries.add(exe)
                implementation.update(executable_sha256=sha(exe),warmups=[],samples=[])
            for iteration in range(5):
                order=list(implementations) if iteration%2==0 else list(reversed(implementations))
                for language in order:
                    implementation=implementations[language]
                    sample=binaries.measure(Path(implementation['executable']),lambda p:measure(p,1800))
                    assert sample['output']==expected,(name,size,language,sample['output'],expected)
                    implementation['warmups' if iteration<2 else 'samples'].append(sample)
                    print(f'{name}/{size} {language} round {iteration+1}: {sample["ms"]:.2f} ms verified',flush=True)
            for measured in implementations.values():
                measured['best_ms']=min(s['ms'] for s in measured['samples'])
                measured['peak_bytes']=max(s['peak_bytes'] for s in measured['samples'])
            data['conditions'].append(dict(workload=name,size=size,n=n,expected=expected,
                previous_rhat_ms=selected['rhat']['best_ms'],measurements=implementations))
            save(data)
            print(f'COMPLETE {name}/{size} ({len(data["conditions"])}/48)',flush=True)
    for path in (work/'.rhat-cache').glob('*'):
        if path.suffix in ('.ll','.i','.obj') and '.tmp.' not in path.name:
            data['artifacts'][str(path)]=sha(path)
    data['complete']=True
    data['completed_utc']=datetime.now(timezone.utc).isoformat()
    validate(data,files=True)
    save(data)
    (SESSION/'verification.json').write_text(json.dumps(dict(results_sha256=sha(SESSION/'results.json'),
        conditions=48,executables=144,compiler_sha256=environment['compiler_sha256'],
        checked_at_utc=data['completed_utc']),indent=2)+'\n')
    write_report(data)
    print('VERIFIED all 16 workloads / 48 conditions; 144 freshly built executables.',flush=True)


if __name__=='__main__':
    main()
