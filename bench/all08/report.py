"""Verify the all-workload 0.8x gate with fresh original comparison variants."""
from pathlib import Path
import hashlib
import json
import math
import sys

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'bench'))
from artifacts import overwritten_rhat_rows
from run_workloads import WORKLOADS
from check_codegen import verify as verify_codegen


def digest(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    frozen=json.loads((HERE/'targets.json').read_text())
    assert frozen['threshold']==.8 and len(frozen['conditions'])==48
    original=[]
    for path,sha in frozen['source_reports'].items():
        assert digest(ROOT/path)==sha,'baseline changed'
        original+=json.loads((ROOT/path).read_text())['rows']
    a=json.loads((HERE/'current/workloads_results.json').read_text())
    b=json.loads((HERE/'current_bintrees/results.json').read_text())
    assert set(a['workloads'])==set(WORKLOADS) and len(WORKLOADS)==15
    for key in ('cpu','os','clang','msvc','rustc','compiler_sha256','runtime_sha256'):
        assert a['environment'][key]==b['environment'][key],key
    assert a['environment']['compiler_sha256']==digest(ROOT/'build/Release/rhat.exe')
    assert a['environment']['runtime_sha256']==digest(ROOT/'runtime/rhat_runtime.c')
    assert a['codegen_model']==b['codegen_model']
    assert a['codegen_model']=='typed_storage_inline_pop'
    rows=a['rows']+[dict(r,workload='bintrees',n=r['depth']) for r in b['rows']]
    assert len(rows)==378
    for data in (a,b):
        assert data['warmups']==2 and data['reps']==3 and not overwritten_rhat_rows(data)
        for r in data['rows']:
            sha=digest(r['executable'])
            assert sha==r['executable_sha256']==data['executable_hashes'][r['executable']]
            assert len(r['samples'])==3 and len(r['warmups'])==2
            assert r['best_ms']==min(s['ms'] for s in r['samples'])
            for s in r['samples']+r['warmups']:
                assert s['executable_sha256']==sha and math.isfinite(s['ms']) and s['ms']>0
        for path,sha in data['source_hashes'].items():
            p=Path(path)
            assert digest(p if p.is_absolute() else ROOT/p)==sha,path
    assert a['harness_source']==(ROOT/'bench/run_workloads.py').read_bytes().decode('utf-8')
    assert len(a['controls'])==45 and len(b['startup'])==4
    for c in a['controls']:
        assert c['output']==c['cpp_control']['output']==c['rust_control']['output']
        for sample,path in ((c['cpp_control'],c['cpp']),(c['rust_control'],c['rust'])):
            assert sample['executable_sha256']==a['executable_hashes'][path]
    for r in b['startup']:
        assert len(r['samples'])==5
        sha=r['samples'][0]['executable_sha256']
        paths=[p for p,h in b['executable_hashes'].items() if Path(p).stem.startswith('empty_') and h==sha]
        assert paths and all(digest(p)==sha for p in paths)
        assert all(s['output']=='' and s['executable_sha256']==sha for s in r['samples'])
    for p in (ROOT/'bench/upstream').rglob('*'):
        if p.suffix in ('.cpp','.rs','.h'):
            assert p.read_bytes()==(ROOT/'build/upstream-goose/bench'/p.relative_to(ROOT/'bench/upstream')).read_bytes(),p
    summary=[]
    initial={(r['workload'],r['size']) for r in frozen['initially_unmet']}
    for before in frozen['conditions']:
        name,size=before['workload'],before['size']
        rs=[r for r in rows if r['workload']==name and r['size']==size]
        old=[r for r in original if r.get('workload','bintrees')==name and r['size']==size]
        assert {(r['language'],r['label']) for r in rs if r['language']!='rhat'}=={(r['language'],r['label']) for r in old if r['language']!='rhat'}
        assert len({s['output'] for r in rs for s in r['samples']+r['warmups']})==1
        rh=next(r for r in rs if r['language']=='rhat')
        assert rh['n']==before['n']
        cpp=min((r for r in rs if r['language']=='cpp'),key=lambda r:r['best_ms'])
        rust=min((r for r in rs if r['language']=='rust'),key=lambda r:r['best_ms'])
        cr,rr=cpp['best_ms']/rh['best_ms'],rust['best_ms']/rh['best_ms']
        summary.append(dict(workload=name,size=size,n=rh['n'],initially_unmet=(name,size) in initial,
                            rhat_ms=rh['best_ms'],cpp_ms=cpp['best_ms'],rust_ms=rust['best_ms'],
                            cpp_label=cpp['label'],rust_label=rust['label'],cpp_ratio=cr,rust_ratio=rr,
                            passed=min(cr,rr)>=.8))
    codegen=verify_codegen(a,ROOT)
    passed=all(r['passed'] for r in summary)
    verdict=dict(objective=frozen['objective'],threshold=.8,gate_passed=passed,
                 verified_condition_count=48,initially_unmet_count=len(initial),
                 compiler_sha256=a['environment']['compiler_sha256'],runtime_sha256=a['environment']['runtime_sha256'],
                 codegen_evidence=dict(path=codegen,sha256=digest(ROOT/codegen),descriptor_loads_per_remove=dict(length=1,data=1)),
                 measured_dates=[a['started_utc'],b['started_utc']],
                 raw_report_hashes={str(p.relative_to(ROOT)):digest(p) for p in (HERE/'current/workloads_results.json',HERE/'current_bintrees/results.json')},rows=summary)
    (HERE/'verification.json').write_text(json.dumps(verdict,indent=2)+'\n')
    lines=['# 全16種・48条件の 0.8x 検証','','C++／Rust は元の全バリアントを再ビルド・再測定。過去の時間は再利用していない。',
           '各言語の最速値 / R^ を用い、丸める前の両比率が全48条件で 0.8 以上で合格。平均では判定しない。',
           '2回ウォームアップ、3回本測定の最小値。実行順を反転しながら交互に実行し、チェックサムと実行ファイルを毎回識別する。','',
           '| 種目 | サイズ / 入力 | R^ ms | C++ ms | Rust ms | C++ / R^ | Rust / R^ | 判定 |',
           '|---|---|---:|---:|---:|---:|---:|---|']
    for r in summary:
        lines.append(f"| {r['workload']} | {r['size']} / {r['n']} | {r['rhat_ms']:.2f} | {r['cpp_ms']:.2f} | {r['rust_ms']:.2f} | {r['cpp_ratio']:.3f}x | {r['rust_ratio']:.3f}x | {'合格' if r['passed'] else '未達'} |")
    lines+=['','**全48条件合格。**' if passed else '**未達条件が残る。目標は未完了。**','',
            '[全バリアントと生データ](current/workloads_results.md)、[bintrees](current_bintrees/results.md)、[機械判定](verification.json)、[元の条件](targets.json)。','']
    (HERE/'results.md').write_text('\n'.join(lines),encoding='utf-8')
    print('ALL-WORKLOAD GATE:', 'PASS' if passed else 'NOT ACHIEVED','48 conditions, 378 rows, 2000 executions.')
    for r in summary:
        if not r['passed']:print('UNMET',r['workload'],r['size'],r['cpp_ratio'],r['rust_ratio'])
    if not passed:sys.exit(1)


if __name__=='__main__':main()
