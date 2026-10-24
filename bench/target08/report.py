"""Audit the frozen <=0.5x target set against fresh complete variant matrices."""
from pathlib import Path
import hashlib
import json
import math
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'bench'))
from artifacts import overwritten_rhat_rows


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    frozen = json.loads((HERE / 'targets.json').read_text())
    assert frozen['threshold'] == .8
    original = []
    expected_targets = set()
    for name, sha in frozen['source_reports'].items():
        path = ROOT / name
        assert digest(path) == sha, 'frozen baseline changed'
        data = json.loads(path.read_text())
        original += data['rows']
        for row in data['rows']:
            if row['language'] != 'rhat':
                continue
            name = row.get('workload', 'bintrees')
            peers = [r for r in data['rows'] if r.get('workload', 'bintrees') == name and r['size'] == row['size'] and r['language'] != 'rhat']
            if min(r['best_ms'] for r in peers) / row['best_ms'] <= .5:
                expected_targets.add((name, row['size']))
    assert len(expected_targets) == 8
    assert expected_targets == {(r['workload'], r['size']) for r in frozen['conditions']}
    additional = json.loads((HERE / 'current/workloads_results.json').read_text())
    trees = json.loads((HERE / 'current_bintrees/results.json').read_text())
    assert set(additional['workloads']) == {'graph', 'blur', 'lru'}
    for key in ('cpu', 'os', 'clang', 'msvc', 'rustc', 'compiler_sha256', 'runtime_sha256'):
        assert additional['environment'][key] == trees['environment'][key], key
    assert additional['environment']['compiler_sha256'] == digest(ROOT / 'build/Release/rhat.exe')
    assert additional['environment']['runtime_sha256'] == digest(ROOT / 'runtime/rhat_runtime.c')
    rows = additional['rows'] + [dict(r, workload='bintrees', n=r['depth']) for r in trees['rows']]
    assert len(rows) == 93
    for data in (additional, trees):
        assert data['warmups'] == 2 and data['reps'] == 3
        assert not overwritten_rhat_rows(data)
        for r in data['rows']:
            sha = digest(r['executable'])
            assert sha == r['executable_sha256'] == data['executable_hashes'][r['executable']]
            assert len(r['samples']) == 3 and len(r['warmups']) == 2
            assert r['best_ms'] == min(s['ms'] for s in r['samples'])
            for s in r['samples'] + r['warmups']:
                assert s['executable_sha256'] == sha and math.isfinite(s['ms']) and s['ms'] > 0
        for name, sha in data['source_hashes'].items():
            path = Path(name)
            if not path.is_absolute():
                path = ROOT / path
            assert digest(path) == sha, name
    for c in additional['controls']:
        assert c['output'] == c['cpp_control']['output'] == c['rust_control']['output']
        for sample, path in ((c['cpp_control'], c['cpp']), (c['rust_control'], c['rust'])):
            assert sample['executable_sha256'] == additional['executable_hashes'][path]
    assert len(additional['controls']) == 9 and len(trees['startup']) == 4
    for r in trees['startup']:
        assert len(r['samples']) == 5
        sha = r['samples'][0]['executable_sha256']
        paths = [path for path, h in trees['executable_hashes'].items()
                 if Path(path).stem.startswith('empty_') and h == sha]
        assert paths and all(digest(path) == sha for path in paths)
        assert all(s['output'] == '' and s['executable_sha256'] == sha for s in r['samples'])
    for p in (ROOT/'bench/upstream').rglob('*'):
        if p.suffix in ('.cpp', '.rs', '.h'):
            assert p.read_bytes() == (ROOT/'build/upstream-goose/bench'/p.relative_to(ROOT/'bench/upstream')).read_bytes(), p
    summary = []
    for name in ('graph', 'blur', 'lru', 'bintrees'):
        for size in ('small', 'medium', 'large'):
            selected = [r for r in rows if r['workload'] == name and r['size'] == size]
            old = [r for r in original if r.get('workload', 'bintrees') == name and r['size'] == size]
            assert {(r['language'], r['label']) for r in selected if r['language'] != 'rhat'} == {
                (r['language'], r['label']) for r in old if r['language'] != 'rhat'}, 'comparison variants changed'
            assert len({s['output'] for r in selected for s in r['samples'] + r['warmups']}) == 1
            rhat = next(r for r in selected if r['language'] == 'rhat')
            cpp = min((r for r in selected if r['language'] == 'cpp'), key=lambda r: r['best_ms'])
            rust = min((r for r in selected if r['language'] == 'rust'), key=lambda r: r['best_ms'])
            assert rhat['n'] == next(r.get('n', r.get('depth')) for r in old if r['language'] == 'rhat')
            cr, rr = cpp['best_ms']/rhat['best_ms'], rust['best_ms']/rhat['best_ms']
            summary.append(dict(workload=name, size=size, n=rhat['n'],
                                original_target=(name, size) in expected_targets,
                                rhat_ms=rhat['best_ms'], cpp_ms=cpp['best_ms'], rust_ms=rust['best_ms'],
                                cpp_label=cpp['label'], rust_label=rust['label'],
                                cpp_ratio=cr, rust_ratio=rr, passed=min(cr, rr) >= .8))
    passed = all(r['passed'] for r in summary)
    verdict = dict(objective=frozen['objective'], threshold=.8, original_target_count=8,
                   verified_condition_count=12, gate_passed=passed,
                   compiler_sha256=additional['environment']['compiler_sha256'],
                   runtime_sha256=additional['environment']['runtime_sha256'],
                   measured_dates=[additional['started_utc'], trees['started_utc']],
                   raw_report_hashes={str(p.relative_to(ROOT)): digest(p) for p in
                                      (HERE/'current/workloads_results.json', HERE/'current_bintrees/results.json')},
                   rows=summary)
    (HERE/'verification.json').write_text(json.dumps(verdict, indent=2)+'\n')
    lines = ['# 0.5x 以下の項目を 0.8x 以上にする検証', '',
             '元の完全測定で C++ または Rust 比が 0.5x 以下だった8条件を固定し、対象4種の全12条件を再測定した。',
             '**C++／Rust も全比較バリアントを再ビルド・再測定。** 過去の時間を再利用していない。',
             '比率は比較言語 / R^。各言語の最速バリアントを選び、丸める前の両比率が 0.8 以上で合格。', '',
             '2回ウォームアップ＋3回本測定の最小値、実行順を交互に反転。チェックサム・バイナリ・ソースを照合。',
             f"コンパイラ SHA-256: `{verdict['compiler_sha256']}`。", '',
             '| 種目 | サイズ / 入力 | 元の対象 | R^ ms | C++ ms | Rust ms | C++ / R^ | Rust / R^ | 判定 |',
             '|---|---|---|---:|---:|---:|---:|---:|---|']
    for r in summary:
        lines.append(f"| {r['workload']} | {r['size']} / {r['n']} | {'対象' if r['original_target'] else '追加確認'} | {r['rhat_ms']:.2f} | {r['cpp_ms']:.2f} | {r['rust_ms']:.2f} | {r['cpp_ratio']:.3f}x | {r['rust_ratio']:.3f}x | {'合格' if r['passed'] else '未達'} |")
    lines += ['', '**全12条件合格。**' if passed else '**未達条件が残る。目標は未完了。**', '',
              'graph は C++ VARIANT 1 / Rust CSR と同じ二段階構築を行う CSR 配置。構築の全処理を測定に含む。',
              '元の連結インデックス版は rhat/variants/graph_linked.rh に保持した。',
              'bintrees は配列長と重複していたカウンタを除去。ノード構造・木の構築・走査・反復数は維持。',
              'コンパイラはプログラム全体に縮小操作がないと証明した場合、借用配列の push でも先端を直接更新する。', '',
              '[全バリアントと生データ（graph / blur / lru）](current/workloads_results.md)、',
              '[bintrees の全バリアント](current_bintrees/results.md)、[機械検証結果](verification.json)、[固定した元の対象](targets.json)。',
              '他の12種の過去の完全測定は [results.md](../results.md) に保持しており、この検証で再測定したとは主張しない。', '']
    (HERE/'results.md').write_text('\n'.join(lines), encoding='utf-8')
    print('Target audit:', 'PASS' if passed else 'NOT ACHIEVED', '8 original / 12 verified conditions; 93 rows; 503 executions.')
    for r in summary:
        print(r['workload'], r['size'], round(r['cpp_ratio'], 3), round(r['rust_ratio'], 3), r['passed'])
    if not passed:
        sys.exit(1)


if __name__ == '__main__':
    main()
