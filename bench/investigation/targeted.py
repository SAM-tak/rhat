"""Targeted diagnostic transforms: scalar bintrees ABI and inline union guards.

Run after codegen.py. No production compiler or benchmark source is changed.
These are limited experiments, not general-purpose LLVM transformations.
"""
from codegen import ROOT, sha
from artifacts import Executables
from run_bench import measure
from pathlib import Path
import json
import re
import shutil
import subprocess

work = ROOT / 'build/codegen-investigation'
out = Path(__file__).resolve().parent
clang = shutil.which('clang')
assert clang
data = dict(harness_source=Path(__file__).read_text(), builds=[], rows=[])
registry = Executables()

def scalar_abi(ir):
    results = {'tree_check': 'i64', 'make': 'i32', 'power2': 'i64', 'check': 'i64', '<top-level>': 'void'}
    functions = {}
    for match in re.finditer(r'; ([^\n]+)\ndefine void @(rh_fn_\d+)\(([^\n]*)\) \{\nentry:\n(.*?)\n\}', ir, re.S):
        name, symbol, args, body = match.groups()
        result = results[name]
        types = re.findall(r'load (ptr|i\d+), ptr %arg\d+', body)
        functions[symbol] = (result, types)
    assert len(functions) == 5
    def definition(m):
        symbol, args = m.groups()
        result, types = functions[symbol]
        newargs = ['i64 %sp'] + [f'{ty} %value_arg{k}' for k, ty in enumerate(types)]
        entry = ''
        if result != 'void':
            entry += f'  %out = alloca {result}\n'
        for k, ty in enumerate(types):
            entry += f'  %arg{k} = alloca {ty}\n  store {ty} %value_arg{k}, ptr %arg{k}\n'
        return f'define internal {result} @{symbol}({", ".join(newargs)}) {{\nentry:\n' + entry
    ir = re.sub(r'define void @(rh_fn_\d+)\(([^\n]*)\) \{\nentry:\n', definition, ir)
    # Unique named SSA temporaries avoid changing the original emitter IDs.
    serial = 0
    def call(m):
        nonlocal serial
        symbol, args = m.groups()
        result, types = functions[symbol]
        args = args.split(', ')
        dest = None
        if result != 'void':
            dest = args.pop(1).removeprefix('ptr ')
        before, newargs = '', [args[0]]
        for ty, arg in zip(types, args[1:]):
            serial += 1
            value = f'%abi_arg_{serial}'
            before += f'  {value} = load {ty}, {arg}\n'
            newargs.append(f'{ty} {value}')
        if dest is None:
            return before + f'  call void @{symbol}({", ".join(newargs)})'
        serial += 1
        value = f'%abi_call_{serial}'
        return before + f'  {value} = call {result} @{symbol}({", ".join(newargs)})\n  store {result} {value}, ptr {dest}'
    ir = re.sub(r'  call void @(rh_fn_\d+)\(([^\n]*)\)', call, ir)
    def returns(m):
        header, result, body = m.groups()
        if result == 'void':
            return m.group(0)
        def ret(_):
            nonlocal serial
            serial += 1
            return f'  %abi_result_{serial} = load {result}, ptr %out\n  ret {result} %abi_result_{serial}'
        return header + re.sub(r'  ret void', ret, body) + '\n}'
    return re.sub(r'(define internal (i32|i64|void) @rh_fn_\d+\([^\n]*\) \{\n)(.*?)\n}', returns, ir, flags=re.S)

def union_checks(ir):
    serial = 0
    def guard(m):
        nonlocal serial
        serial += 1
        value, tag, line = m.groups()
        k = serial
        return (f'  %tag_ok_{k} = icmp eq i32 {value}, {tag}\n'
                f'  br i1 %tag_ok_{k}, label %tag_good_{k}, label %tag_bad_{k}, !prof !0\n'
                f'tag_bad_{k}:\n  call void @rh_union_tag(i32 {value}, i32 {tag}, i32 {line})\n'
                f'  unreachable\ntag_good_{k}:')
    return re.sub(r'  call void @rh_union_tag\(i32 ([^,]+), i32 (\d+), i32 (\d+)\)', guard, ir)

for name, variant, transform in [('bintrees', 'scalar_abi', scalar_abi), ('calc', 'inline_tags', union_checks), ('sexp', 'inline_tags', union_checks)]:
    session = 'current_bintrees/results.json' if name == 'bintrees' else 'current/workloads_results.json'
    source = json.loads((ROOT / 'bench' / session).read_text())
    row = next(r for r in source['rows'] if r.get('workload', 'bintrees') == name and r['size'] == 'medium' and r['language'] == 'rhat')
    baseline = Path(row['executable'])
    assert sha(baseline) == row['executable_sha256']
    ll = work / f'{name}_{variant}.ll'
    ll.write_text(transform(Path(str(baseline) + '.ll').read_text()))
    exe = ll.with_suffix('.exe')
    cmd = [clang, '-Wno-override-module', '-std=c11', '-O2', '-rtlib=compiler-rt', str(ll), str(ROOT / 'runtime/rhat_runtime.c'), '-o', str(exe)]
    subprocess.run(cmd, check=True)
    data['builds'].append(cmd)
    variants = [('baseline', baseline), (variant, exe)]
    samples = {v: [] for v, _ in variants}
    warmups = {v: [] for v, _ in variants}
    for _, path in variants:
        registry.add(path)
    for iteration in range(5):
        for label, path in (variants if iteration % 2 == 0 else list(reversed(variants))):
            measured = registry.measure(path, lambda p: measure(p, 1800))
            assert measured['output'] == row['samples'][0]['output']
            (warmups if iteration < 2 else samples)[label].append(measured)
    for label, path in variants:
        data['rows'].append(dict(workload=name, variant=label, size='medium', executable=str(path), executable_sha256=sha(path), best_ms=min(s['ms'] for s in samples[label]), samples=samples[label], warmups=warmups[label]))
    print(name, {v: round(min(s['ms'] for s in samples[v]), 2) for v, _ in variants}, flush=True)
    data['executable_hashes'] = registry.hashes
    (out / 'targeted.json').write_text(json.dumps(data, indent=2) + '\n')
