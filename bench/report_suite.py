"""Summarize the complete, artifact-verified current 16-workload suite."""
import json
import pathlib
import hashlib

HERE=pathlib.Path(__file__).resolve().parent
NAMES=('sum','push','strlist','records','tree','interp','graph','words','particles','sexp','lru','scene','calc','bintrees','respond','blur')


def write_suite():
    complete=None
    all_verified=HERE/'all08/verification.json'
    if all_verified.exists():
        candidate=json.loads(all_verified.read_text(encoding='utf-8'))
        if candidate.get('gate_passed'):
            for name,expected in candidate['raw_report_hashes'].items():
                if hashlib.sha256((HERE.parent/name).read_bytes()).hexdigest()!=expected:
                    raise ValueError('full refresh raw data changed')
            complete=candidate
    base=HERE/'all08' if complete else HERE
    additional=json.loads((base/'current/workloads_results.json').read_text(encoding='utf-8'))
    binary=json.loads((base/'current_bintrees/results.json').read_text(encoding='utf-8'))
    for key in ('cpu','os','compiler_sha256','runtime_sha256','clang','msvc','rustc'):
        if additional['environment'][key] != binary['environment'][key]:
            raise ValueError(f'current suite environment mismatch: {key}')
    if additional.get('codegen_model') != binary.get('codegen_model'):
        raise ValueError('current suite codegen mismatch')
    if any(data.get('memory_model') != 'static_data_stacks_fault_commit' for data in (additional, binary)):
        raise ValueError('current suite must use static data stacks')
    if set(additional['workloads']) != set(NAMES)-{'bintrees'}:
        raise ValueError('current suite must contain all 15 additional workloads')
    groups={name:[r for r in additional['rows'] if r['workload']==name] for name in NAMES if name!='bintrees'}
    groups['bintrees']=[dict(r,n=r['depth']) for r in binary['rows']]
    refresh=None
    verified=HERE/'target08/verification.json'
    if not complete and verified.exists():
        candidate=json.loads(verified.read_text(encoding='utf-8'))
        if candidate.get('gate_passed'):
            for name,expected in candidate['raw_report_hashes'].items():
                if hashlib.sha256((HERE.parent/name).read_bytes()).hexdigest()!=expected:
                    raise ValueError('target refresh raw data changed')
            fresh=json.loads((HERE/'target08/current/workloads_results.json').read_text(encoding='utf-8'))
            fresh_trees=json.loads((HERE/'target08/current_bintrees/results.json').read_text(encoding='utf-8'))
            for name in ('graph','blur','lru'):
                groups[name]=[r for r in fresh['rows'] if r['workload']==name]
            groups['bintrees']=[dict(r,n=r['depth']) for r in fresh_trees['rows']]
            refresh=candidate
    for name,rows in groups.items():
        for size in ('small','medium','large'):
            selected=[r for r in rows if r['size']==size]
            if sum(r['language']=='rhat' for r in selected)!=1 or not any(r['language']=='cpp' for r in selected) or not any(r['language']=='rust' for r in selected):
                raise ValueError(f'incomplete current suite rows: {name}/{size}')
            outputs=set()
            for row in selected:
                if row.get('valid') is False or not row.get('executable_sha256'):
                    raise ValueError(f'unverified executable: {name}/{size}')
                if len(row['samples'])!=3 or len(row['warmups'])!=2:
                    raise ValueError(f'incomplete measurement policy: {name}/{size}')
                for sample in row['samples']+row['warmups']:
                    if sample.get('executable_sha256')!=row['executable_sha256']:
                        raise ValueError(f'executable identity mismatch: {name}/{size}')
                    outputs.add(sample['output'])
            if len(outputs)!=1:
                raise ValueError(f'checksum mismatch: {name}/{size}')
    env=additional['environment']
    lines=['# R^ vs C++ vs Rust — all 16 workloads','',
           '**All 48 size conditions include R^, C++ and Rust timings and relative performance.**','',
           'R^ uses statically assigned data stacks, scope watermark rewinds and Windows fault-driven page commitment.',
           'The push fast path is emitted directly as LLVM IR; bounds/range checks remain enabled.','',
           *(['Array indexing uses inline full-width bounds checks and address calculations.',
              'Push uses byte-capacity checks and directly updates the top where growth analysis proves it safe.',
              'Integer division/remainder guards are also inline, allowing safe constant-divisor checks to fold away.',
              'The failure paths retain their runtime diagnostics; checks are not disabled.','',
              'Before these optimizations: [static-data-stack reference](pre_array_fastpaths/README.md).',
              'Same-session before/after check: [paired array comparison](array_codegen_comparison/results.md).','']
             if additional.get('codegen_model') in ('inline_array_and_arithmetic_checks', 'direct_scalar_checked_range_loops', 'small_values_inline_guards_literals') else []),
           *(['Internal functions pass scalar/reference values directly.',
              'Canonical i64 loops use range guards, stable pointer snapshots and a checked fallback.','']
             if additional.get('codegen_model') in ('direct_scalar_checked_range_loops','small_values_inline_guards_literals') else []),
           *(['Non-owning values up to 16 bytes pass directly; union and float-to-integer guards are inline.',
              'Literal byte appends use checked constant copies; arbitrary overlapping appends keep memmove semantics.','']
             if additional.get('codegen_model')=='small_values_inline_guards_literals' else []),
           f"Additional workloads: {additional['started_utc']} UTC; bintrees: {binary['started_utc']} UTC.",'',
           'All rows below were measured with the same R^ compiler/runtime recorded for this full-suite session. Goose is not run.',
           'Original small/medium/large sizes; two warm-ups and best of three whole-process wall times. No suite-wide average is claimed.',
           'Every run checks its checksum and its executable SHA-256 against the artifact recorded immediately after build.',
           'R^ retains bounds/range checks and OS-backed stable-address arrays; no affinity is set.','',
           '**Correction:** older R^ rows for sum, particles, push and records ran overwritten Rust binaries and are invalid.',
           'Those claims are withdrawn. [Archived evidence](invalid_runs/README.md) retains the original raw data. All current rows use separate language-specific executable paths.','',
           'Records uses the original fixed-mode variant; respond uses the original streaming variant. Other representation differences are in [coverage.md](coverage.md).','',
           'Before this change: [per-owner-region measurements](pre_data_stacks/README.md).','',
           'Full matrices, all implementation labels, raw samples, source/LLVM/executable hashes and build commands:',
           '[15 workloads](current/workloads_results.md), [bintrees](current_bintrees/results.md).','',
           f"CPU: {env['cpu']}; OS: {env['os']}; compiler SHA-256: `{env['compiler_sha256']}`.",'',
           'The C++ and Rust columns select the fastest measured variant/backend for each size. Ratios are comparison / R^; above 1 means R^ is faster. Peak memory is the maximum measured R^ resident working set.','',
           '| Workload | Size / parameter | R^ ms | Best C++ ms | Best Rust ms | C++ / R^ | Rust / R^ | R^ peak MiB |',
           '|---|---|---:|---:|---:|---:|---:|---:|']
    focused=HERE/'abi_ranges/results.json'
    if not complete and focused.exists():
        newer=json.loads(focused.read_text(encoding='utf-8'))
        if len(newer.get('rows',[]))==42 and newer['compiler_sha256'] != env['compiler_sha256']:
            lines[4:4]=['**Subsequent internal-call and checked-loop optimizations:** [focused paired measurements](abi_ranges/results.md).',
                        'The complete matrix below retains the preceding full-suite session; the focused report labels reused C++/Rust reference timings explicitly.',
                        'Implementation limits and the sum control repeat are in [the validation notes](abi_ranges/README.md).','']
    if refresh:
        lines=[line.replace('All rows below were measured with the same R^ compiler/runtime recorded for this full-suite session. Goose is not run.',
                            'The table combines the fresh target refresh with the preceding full-suite reference for the other 12 workloads. Goose is not run.')
               .replace('All rows below were measured with the same current R^ compiler/runtime. Goose is not run.',
                        'The table combines the fresh target refresh with the preceding full-suite reference for the other 12 workloads. Goose is not run.')
               for line in lines]
        lines[4:4]=['**Fresh threshold verification:** [graph, blur, lru and bintrees](target08/results.md) were rebuilt and remeasured, including every original C++/Rust variant.',
                    'Those four workloads use the fresh rows below; the other 12 retain the dated full-suite reference above.',
                    'Graph now uses CSR, including all construction cost; its previous linked-index port is retained in rhat/variants/graph_linked.rh.',
                    f"Refresh dates: {'; '.join(refresh['measured_dates'])}; refresh compiler SHA-256: `{refresh['compiler_sha256']}`.",'']
    if complete:
        lines[4:4]=['**Fresh full verification: all 48 conditions meet 0.8x against both fastest C++ and fastest Rust.** [Gate and raw evidence](all08/results.md).',
                    'All original comparison variants were rebuilt and remeasured. No earlier timings are reused in this matrix.','']
        lines=[line.replace('(current/workloads_results.md)','(all08/current/workloads_results.md)')
                   .replace('(current_bintrees/results.md)','(all08/current_bintrees/results.md)') for line in lines]
    for name in NAMES:
        for size in ('small','medium','large'):
            selected=[r for r in groups[name] if r['size']==size]
            rhat=next(r for r in selected if r['language']=='rhat')
            cpp=min((r for r in selected if r['language']=='cpp'),key=lambda r:r['best_ms'])
            rust=min((r for r in selected if r['language']=='rust'),key=lambda r:r['best_ms'])
            lines.append(f"| {name} | {size} / {rhat['n']} | {rhat['best_ms']:.2f} | {cpp['best_ms']:.2f} | {rust['best_ms']:.2f} | {cpp['best_ms']/rhat['best_ms']:.3f}x | {rust['best_ms']/rhat['best_ms']:.3f}x | {rhat['peak_bytes']/1048576:.2f} |")
    lines += ['', 'Historical sessions remain available in the individual folders; their compiler/runtime revisions differ. The current tables above supersede their performance claims.',
              'The original bintrees raw baseline remains in `results.json` and its controlled page-commit study in `page_commit_results.json`.', '']
    (HERE/'results.md').write_text('\n'.join(lines),encoding='utf-8')


if __name__=='__main__':
    write_suite()
