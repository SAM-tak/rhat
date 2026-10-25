"""End-to-end module, separate object, cache and std.random contracts."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()
    compiler, clang = str(Path(args.compiler).resolve()), str(Path(args.clang).resolve())
    work = Path(args.work).resolve()
    repo = Path(__file__).resolve().parents[1]
    work.mkdir(parents=True, exist_ok=True)
    passed = 0

    def run(command, good=True):
        p = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=90)
        assert (p.returncode == 0) == good, (command, p.stdout, p.stderr)
        return p

    def project(name, files):
        root = work / name
        for path, text in files.items():
            dest = root / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(text, encoding='utf-8')
        return root

    def positive(name, files, expected, extra=()):
        nonlocal passed
        root = project(name, files)
        for opt in ('-O0', '-O2'):
            exe = root / f'main{opt}.exe'
            run([compiler, 'build', root/'main.rh', '-o', exe, '--clang', clang, opt, *extra])
            actual = run([exe]).stdout
            assert actual == expected, (name, opt, actual, expected)
        passed += 1
        print('PASS', name, flush=True)
        return root

    def negative(name, files, diagnostic, extra=()):
        nonlocal passed
        root = project(name, files)
        p = run([compiler, 'check', root/'main.rh', *extra], False)
        assert diagnostic in p.stderr and re.search(r':\d+:\d+: error:', p.stderr), (name, p.stderr)
        passed += 1
        print('PASS', name, flush=True)

    lib = '''module^ lib.example
public^ def^ Item { self^{ value:i64^ } }
public^ let^ twice=f^ x:i64^ -> i64^ { x*2 }
public^ let^ twice=f^ x:i64^, y:i64^ -> i64^ { (x+y)*2 }
let^ hidden=f^ ->i64^ { 99 }
public^ let^ answer:i64^ = 40+2
public^ let^ enabled:bool^ = true^
public^ let^ label:string^ = "module"
public^ errordef^ Error { Bad, Worse }
public^ def^ Result = i64^ | Error.Bad | nil^
public^ let^ result=f^ x:i64^ ->Result {
    if^ x < 0 { return^ error^Error.Bad{} }
    if^ x = 0 { return^ nil^ }
    return^ x
}
'''
    root = positive('imports_types_constants_errors', {
        'lib/example.rh': lib,
        'main.rh': '''import^ lib.example
let^ x=import^ lib.example
print(x.twice(21),lib.example.twice(10,11),x.answer,x.enabled,x.label)
let^ item=x.Item{value=42}
print(item.value)
print(x.result(-1).is(x.Error.Bad),x.result(0).is(nil^),x.result(12).get(i64^))
'''}, '42 42 42 true module\n42\ntrue true 12\n')
    positive('module_owned_template_composition', {
        'lib/mem.rh': '''module^ lib.mem
public^ def^ Base { self^{ value:i32^=42 }; read=f^ self^ ->i32^ { self^.value } }
public^ let^ make=p^ ->i32^[>..] { return^ [20,22] }
public^ let^ total=f^ a:&i32^[>..] ->i32^ { a[0]+a[1] }
public^ let^ read=template^f^ item { item.read() }
''', 'main.rh': '''import^ lib.mem
def^ Item : lib.mem.Base {}
var^ a=lib.mem.make()
print(lib.mem.total(&a),lib.mem.read(Item{}))
'''}, '42 42\n')
    positive('modules_nominal_distinct', {
        'lib/a.rh': 'module^ lib.a\npublic^ def^ Item { self^{ value:i32^=1 } }',
        'lib/b.rh': 'module^ lib.b\npublic^ def^ Item { self^{ value:i32^=2 } }',
        'main.rh': 'import^ lib.a\nimport^ lib.b\nprint(lib.a.Item{}.value,lib.b.Item{}.value)'}, '1 2\n')
    for name, text, diagnostic in [
        ('private_function', 'import^ lib.example\nprint(lib.example.hidden())', 'does not export'),
        ('private_type', 'import^ lib.example\nlet^ x:lib.example.Secret=lib.example.Secret{}', 'unknown or unavailable type'),
        ('unknown_module', 'import^ absent.module', 'cannot find module'),
        ('import_collision', 'def^ lib {}\nimport^ lib.example', 'conflicts'),
        ('error_constructor_wrong_type', 'def^ A {}\nlet^ x=error^A{}', 'errordef^ variant'),
        ('error_constructor_required', 'errordef^ E { Bad }\nlet^ x=E.Bad{}', 'error^ constructor'),
        ('overload_same_arity', 'let^ f=f^ x:i32^ { x }\nlet^ f=f^ x:i64^ { x }', 'duplicate function'),
        ('module_not_first', 'print(1)\nmodule^ late.name', 'error:'),
    ]:
        negative(name, {'lib/example.rh': lib, 'main.rh': text}, diagnostic)
    negative('nominal_cross_module_rejection', {
        'lib/a.rh': 'module^ lib.a\npublic^ def^ Item { self^{ value:i32^=1 } }',
        'lib/b.rh': 'module^ lib.b\npublic^ def^ Item { self^{ value:i32^=2 } }',
        'main.rh': 'import^ lib.a\nimport^ lib.b\nlet^ a:lib.a.Item=lib.b.Item{}'}, 'nominal')
    negative('module_name_mismatch', {'lib/a.rh': 'module^ wrong.name', 'main.rh': 'import^ lib.a'}, 'must declare')
    negative('module_declaration_required', {'lib/a.rh': 'public^ let^ a=f^ { 1 }', 'main.rh': 'import^ lib.a'}, 'must declare')
    negative('module_cycle', {'lib/a.rh': 'module^ lib.a\nimport^ lib.b',
                             'lib/b.rh': 'module^ lib.b\nimport^ lib.a', 'main.rh': 'import^ lib.a'}, 'cyclic import')
    negative('module_execution', {'lib/a.rh': 'module^ lib.a\nprint(1)', 'main.rh': 'import^ lib.a'}, 'only declarations')
    negative('module_mutable_global', {'lib/a.rh': 'module^ lib.a\nvar^ a=1', 'main.rh': 'import^ lib.a'}, 'only declarations')
    negative('module_constant_call', {'lib/a.rh': 'module^ lib.a\nlet^ x=i64^(1)', 'main.rh': 'import^ lib.a'}, 'literal operations')
    negative('module_constant_invalid_unused', {'lib/a.rh': 'module^ lib.a\nlet^ x:i32^="bad"', 'main.rh': 'import^ lib.a'}, "expected 'i32^'")
    negative('module_nontransitive_import', {'lib/a.rh': 'module^ lib.a\nimport^ lib.b',
        'lib/b.rh': 'module^ lib.b\npublic^ let^ value=f^ { 1 }',
        'main.rh': 'import^ lib.a\nprint(lib.b.value())'}, 'unknown')
    for name, use in [
        ('hidden_type_alias_bypass', 'let^ x:lib.b.Secret=lib.b.Secret{}'),
        ('hidden_type_namespace_bypass', 'def^ lib {}\nlet^ x:lib.b.Secret=lib.b.Secret{}'),
        ('hidden_constructor_alias_bypass', 'let^ x=lib.b.Secret{}'),
        ('hidden_composition_bypass', 'def^ X:lib.b.Secret {}'),
        ('hidden_union_bypass', 'def^ X=lib.b.Secret | i64^'),
    ]:
        negative(name, {'lib/a.rh': 'module^ lib.a\nimport^ lib.b',
            'lib/b.rh': 'module^ lib.b\ndef^ Secret {}',
            'main.rh': 'let^ a=import^ lib.a\n'+use}, 'unknown')
    negative('top_level_import_binding_collision', {'main.rh':'import^ std.random\nlet^ std=1'}, 'conflicts')
    negative('alias_import_binding_collision', {'main.rh':'let^ rng=import^ std.random\nvar^ rng=1'}, 'conflicts')
    negative('child_module_export_collision', {
        'lib/a.rh':'module^ lib.a\npublic^ let^ child=f^ { 1 }',
        'lib/a/child.rh':'module^ lib.a.child',
        'main.rh':'import^ lib.a\nimport^ lib.a.child'}, 'conflicts')
    positive('block_may_shadow_import', {'main.rh':'import^ std.random\n{ let^ std=42; print(std) }'}, '42\n')
    negative('module_local_owner_recursive', {'lib/a.rh': '''module^ lib.a
public^ let^ recur=p^ n:i32^ {
    var^ a:i32^[>..]=[n]
    if^ n > 0 { this^(n-1) }
    print(a[0])
}''', 'main.rh': 'import^ lib.a\nlib.a.recur(2)'}, 'recursive call')
    negative('module_borrow_escape', {'lib/a.rh': '''module^ lib.a
public^ let^ bad=p^ ->&i32^ {
 var^ a:i32^[>..]=[1]; return^ &a[0]
}''', 'main.rh': 'import^ lib.a'}, 'escape')
    positive('primitive_union_literals', {'main.rh': '''errordef^ Error { Bad }
def^ Int = i64^ | Error.Bad | nil^
def^ Real = f64^ | Error.Bad
let^ value=f^ ->Int { return^ 9223372036854775807 }
let^ real=f^ ->Real { return^ 1.25 }
print(value().get(i64^),real().get(f64^))
'''}, '9223372036854775807 1.25\n')
    negative('reserved_std_module', {'main.rh': 'module^ std.forged'}, 'reserved')
    negative('private_standard_intrinsic', {'main.rh': 'print(__sqrt^(4.0))'}, 'private standard-library')
    positive('module_recursive_external_borrow', {
        'lib/borrow.rh': '''module^ lib.borrow
public^ let^ fill=p^ a:&i32^[>..], n:i32^ {
    if^ n > 0 { a.push(n); this^(a,n-1) }
}
''', 'main.rh':'import^ lib.borrow\nvar^ a:i32^[>..]=[]\nlib.borrow.fill(&a,3)\nprint(a.len,a[0],a[2])'}, '3 3 1\n')
    external = project('external_root', {'lib/value.rh': 'module^ lib.value\npublic^ let^ n=f^ { 42 }'})
    positive('module_search_path', {'main.rh': 'import^ lib.value\nprint(lib.value.n())'}, '42\n', ['--module-path', external])

    # Compare both compilation paths, and validate actual per-module cache reuse.
    exe = root/'cached.exe'
    cache = root/'cache with spaces'
    command = [compiler, 'build', root/'main.rh', '-o', exe, '--clang', clang,
               '-O2', '--cache-dir', cache, '--verbose']
    first = run(command).stderr
    assert 'compile: lib.example' in first
    objects = {p.name: (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest()) for p in cache.glob('*.obj')}
    second = run(command).stderr
    assert 'compile:' not in second and 'cache hit: lib.example' in second
    assert objects == {p.name: (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest()) for p in cache.glob('*.obj')}
    assert run([exe]).stdout == run([root/'main-O2.exe']).stdout
    private_change = (root/'lib/example.rh').read_text().replace('{ 99 }', '{ 100 }')
    (root/'lib/example.rh').write_text(private_change)
    log = run(command).stderr
    assert 'compile: lib.example' in log and 'cache hit: <entry>' in log
    assert 'compile: lib.example' in run(command+['--no-cache']).stderr
    changed = (root/'lib/example.rh').read_text().replace('x*2', 'x*3')
    (root/'lib/example.rh').write_text(changed)
    assert 'compile: lib.example' in run(command).stderr
    assert run([exe]).stdout.startswith('63 42')
    layout_change = (root/'lib/example.rh').read_text().replace('value:i64^', 'value:i32^')
    (root/'lib/example.rh').write_text(layout_change)
    log = run(command).stderr
    assert 'compile: <entry>' in log and 'compile: lib.example' in log
    with_template = layout_change+'\npublic^ let^ id=template^f^ item { item }\n'
    (root/'lib/example.rh').write_text(with_template)
    main_path = root/'main.rh'
    main_text = main_path.read_text()
    main_path.write_text(main_text+'\nprint(x.id(42))\n')
    assert 'compile: lib.example' in run(command).stderr
    main_path.write_text(main_text+'\nprint(x.id(i64^(42)))\n')
    assert 'compile: lib.example' in run(command).stderr
    # Corruption must be detected even when a matching cache stamp exists.
    current_object = max(cache.glob('*.obj'), key=lambda p: p.stat().st_mtime_ns)
    current_object.write_bytes(b'broken object')
    assert 'compile:' in run(command).stderr
    assert current_object.read_bytes() != b'broken object'
    assert 'compile: lib.example' in run([('-O0' if x == '-O2' else x) for x in command]).stderr
    # SHA-256 cache input names must match an independent implementation.
    for path in cache.glob('*.ll'):
        assert path.stem == hashlib.sha256(path.read_bytes()).hexdigest()
    ll = root/'single.ll'
    run([compiler, 'emit', root/'main.rh', '-o', ll])
    run([clang, '-Wno-override-module', '-O2', '-rtlib=compiler-rt', ll,
         repo/'runtime/rhat_runtime.c', '-o', root/'single.exe'])
    assert run([root/'single.exe']).stdout == run([exe]).stdout
    p = run([compiler, 'build', root/'main.rh', '-o', root/'lib/example.rh'], False)
    assert 'overwrite' in p.stderr
    passed += 1
    print('PASS object_cache_identity_reuse_invalidation_and_single_ir', flush=True)

    native = project('native_header_cache', {
        'main.rh':'print(42)', 'value.h':'#define HEADER_VALUE 1\n',
        'wrapper.c': '#define rh_print_newline original_newline\n'
            +f'#include "{(repo/"runtime/rhat_runtime.c").as_posix()}"\n'
            +'#undef rh_print_newline\n#include "value.h"\n'
            +'void rh_print_newline(void) { printf("%d\\n", HEADER_VALUE); }\n'})
    command = [compiler,'build',native/'main.rh','-o',native/'header.exe',
               '--runtime',native/'wrapper.c','--clang',clang,'-O2','--verbose']
    run(command)
    assert run([native/'header.exe']).stdout=='421\n'
    assert 'cache hit: runtime' in run(command).stderr
    (native/'value.h').write_text('#define HEADER_VALUE 2\n')
    assert 'compile: runtime' in run(command).stderr
    assert run([native/'header.exe']).stdout=='422\n'
    passed += 1
    print('PASS runtime_header_cache_invalidation',flush=True)
    if os.name=='nt':
        import ctypes
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        create=kernel.CreateFileW
        create.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,
                         ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
        create.restype=ctypes.c_void_p
        close=kernel.CloseHandle
        close.argtypes=[ctypes.c_void_p]
        close.restype=ctypes.c_int
        held=[]
        try:
            for path in list((native/'.rhat-cache').glob('*.i'))+list((native/'.rhat-cache').glob('*.ll')):
                handle=create(str(path),0x80000000,3,None,3,0,None)
                assert handle!=ctypes.c_void_p(-1).value, ctypes.get_last_error()
                held.append(handle)
            assert 'cache hit: runtime' in run(command).stderr
            assert run([native/'header.exe']).stdout=='422\n'
        finally:
            for handle in held:
                close(handle)
        passed += 1
        print('PASS windows_read_locked_immutable_cache_inputs',flush=True)

    random_source = '''import^ std.random
var^ r=std.random.new(12345)
print(r.save())
var^ i:i32^=0
repeat^ while^ i < 8 { print(r.next()); i:=i+1 }
let^ saved=r.save()
let^ a=r.rangeint(-100,100).get(i64^)
assert(r.load(saved).is(nil^))
assert(r.rangeint(-100,100).get(i64^)=a)
let^ before=r.save()
assert(r.load(0).is(std.random.Error.InvalidState))
assert(r.rangeint(5,4).is(std.random.Error.InvalidArgument))
assert(r.uniform(1.0,-1.0).is(std.random.Error.InvalidArgument))
assert(r.normal(0.0,-1.0).is(std.random.Error.InvalidArgument))
assert(r.rangeint(7,7).get(i64^)=7)
assert(r.uniform(2.0,2.0).get(f64^)=2.0)
assert(r.normal(3.0,0.0).get(f64^)=3.0)
let^ nan=0.0/0.0
let^ infinity=1.0/0.0
assert(r.uniform(nan,1.0).is(std.random.Error.InvalidArgument))
assert(r.normal(0.0,infinity).is(std.random.Error.InvalidArgument))
assert(before=r.save())
assert(r.rangeint(-9223372036854775807-1,9223372036854775807).is(i64^))
let^ u=r.uniform(-1.7976931348623157e308,1.7976931348623157e308).get(f64^)
assert(u >= -1.7976931348623157e308 and^ u < 1.7976931348623157e308)
let^ n=r.normal(0.0,1.0).get(f64^)
assert(n=n)
let^ automatic=std.random.new()
assert(automatic.is(std.random.Random))
print("ok")
'''
    random_root = project('random_contract', {'main.rh': random_source})
    mask = (1 << 64)-1
    state = 12345 ^ 0x9E3779B97F4A7C15
    expected = [state-(1<<64) if state >> 63 else state]
    for _ in range(8):
        state ^= state >> 12
        state ^= (state << 25) & mask
        state ^= state >> 27
        expected.append((((state*0x2545F4914F6CDD1D)&mask) >> 11)/(1<<53))
    for opt in ('-O0', '-O2'):
        exe = random_root/f'random{opt}.exe'
        run([compiler, 'build', random_root/'main.rh', '-o', exe, '--clang', clang, opt])
        output = run([exe]).stdout.splitlines()
        assert int(output[0]) == expected[0]
        assert list(map(float, output[1:9])) == expected[1:]
        assert output[-1] == 'ok'
        # Native failure injection exercises generated error returns, not a fake R^ implementation.
        ll = random_root/'single.ll'
        run([compiler, 'emit', random_root/'main.rh', '-o', ll])
        # Use a focused caller: the success-only assertion above deliberately differs.
        fail_source = random_root/'fail.rh'
        fail_source.write_text('import^ std.random\nassert(std.random.new().is(std.random.Error.SeedUnavailable))\nprint(42)')
        run([compiler, 'emit', fail_source, '-o', ll])
        command = [clang, '-Wno-override-module', opt, '-rtlib=compiler-rt', '-DRH_TEST_SEED_FAILURE',
                   ll, repo/'runtime/rhat_runtime.c', repo/'runtime/rhat_random.c', '-o', random_root/'failure.exe']
        if os.name == 'nt': command += ['-lbcrypt']
        else: command += ['-lm']
        run(command)
        assert run([random_root/'failure.exe']).stdout == '42\n'
    passed += 1
    print('PASS std_random_lhat_oracle_state_ranges_math_and_seed_failure', flush=True)
    # Integer rejection, sign crossing, full-width range and distribution replay.
    state = 12345 ^ 0x9E3779B97F4A7C15
    def raw_draw():
        nonlocal state
        state ^= state >> 12
        state ^= (state << 25) & mask
        state ^= state >> 27
        return (state*0x2545F4914F6CDD1D)&mask
    statements = ['import^ std.random', 'var^ r=std.random.new(12345)']
    integer_expected = []
    for lo, hi in [(-100,100), (-(1<<63),0), (-(1<<63),(1<<63)-1), ((1<<63)-3,(1<<63)-1)]:
        for _ in range(24):
            span = (hi-lo+1)&mask
            bits = raw_draw()
            if span:
                threshold = ((-span)&mask)%span
                while bits < threshold:
                    bits = raw_draw()
                bits %= span
            integer_expected.append(lo+bits)
            statements.append(f'print(r.rangeint({lo},{hi}).get(i64^))')
    normal_expected = []
    for _ in range(8):
        u = (raw_draw() >> 11)/(1<<53)
        v = (raw_draw() >> 11)/(1<<53)
        normal_expected.append(2.0+3.0*(math.sqrt(-2.0*math.log(1.0-u))*math.cos(6.283185307179586476925286766559*v)))
        statements.append('print(r.normal(2.0,3.0).get(f64^))')
    statements += ['r.reseed(12345)', 'assert(r.save()=-7046029254386365396)',
                   'var^ copy=r', 'assert(r.next()=copy.next())',
                   'var^ zero=std.random.new(0)', 'assert(zero.save()!=0)',
                   'var^ fallback=std.random.new(-7046029254386353131)',
                   'assert(fallback.save()=2685821657736338717)']
    oracle_root = project('random_integer_normal_oracle', {'main.rh':'\n'.join(statements)})
    for opt in ('-O0','-O2'):
        exe=oracle_root/f'oracle{opt}.exe'
        run([compiler,'build',oracle_root/'main.rh','-o',exe,'--clang',clang,opt])
        output=run([exe]).stdout.splitlines()
        assert list(map(int,output[:96]))==integer_expected
        assert all(math.isclose(float(x), y, rel_tol=2e-14, abs_tol=2e-14)
                   for x,y in zip(output[96:],normal_expected)) and len(output)==104
    passed += 1
    print('PASS std_random_integer_rejection_and_normal_oracles',flush=True)

    optional_root = positive('module_optional_types', {
        'lib/value.rh': '''module^ lib.value
public^ def^ Item {self^{value:i32^=42}}
public^ def^ Result = Item | nil^
public^ let^ maybe=f^ yes:bool^ ->Item|nil^ {if^ yes: Item.new() el^: nil^;}
public^ let^ read=f^ item:nil^|Item ->i32^ {item?.value??0}
public^ let^ templateRead=template^f^ item {item?.value??0}
public^ let^ borrow=f^ value:&i32^ ->&i32^|nil^ {value}
''',
        'main.rh': '''let^ lib=import^ lib.value
let^ yes=lib.maybe(true^)
let^ no:lib.Item|nil^=lib.maybe(false^)
let^ named:lib.Result=yes
let^ anonymous:nil^|lib.Item=named
print(lib.read(yes),lib.read(no),anonymous?.value??0)
print(lib.templateRead(yes),lib.templateRead(no))
var^ number=7
let^ ref=lib.borrow(&number)
print(*(ref??&number))
''',
    }, '42 0 42\n42 0\n7\n')
    optional_ir = optional_root/'single.ll'
    run([compiler, 'emit', optional_root/'main.rh', '-o', optional_ir])
    run([clang, '-Wno-override-module', '-O2', optional_ir,
         repo/'runtime/rhat_runtime.c', '-o', optional_root/'single.exe'])
    assert run([optional_root/'single.exe']).stdout == '42 0 42\n42 0\n7\n'
    passed += 1
    print('PASS optional_union_single_ir_matches_modules', flush=True)

    negative('module_optional_nominal_identity', {
        'lib/a.rh': 'module^ lib.a\npublic^ def^ Item {self^{value:i32^}}',
        'lib/b.rh': 'module^ lib.b\npublic^ def^ Item {self^{value:i32^}}',
        'main.rh': '''import^ lib.a
import^ lib.b
let^ item:lib.a.Item|nil^=lib.b.Item.new()
''',
    }, 'not an alternative')

    # Recreate the install layout, add a marker to prove that packaged resources
    # take precedence over the build-time source-tree fallback.
    package=work/'package'
    packed=package/'bin'/Path(compiler).name
    packed.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(compiler,packed)
    share=package/'share/rhat'
    shutil.copytree(repo/'stdlib',share/'stdlib',dirs_exist_ok=True)
    for runtime in ('rhat_runtime.c','rhat_random.c'):
        shutil.copy2(repo/'runtime'/runtime,share/runtime)
    installed_random=share/'stdlib/std/random.rh'
    installed_random.write_text(installed_random.read_text()+'\npublic^ let^ portableMarker:i32^=42\n')
    portable=project('packaged_resources',{'main.rh':'import^ std.random\nprint(std.random.portableMarker)\nvar^ r=std.random.new(12345)\nprint(r.save())'})
    for opt in ('-O0','-O2'):
        exe=portable/f'portable{opt}.exe'
        run([packed,'build',portable/'main.rh','-o',exe,'--clang',clang,opt])
        assert run([exe]).stdout=='42\n-7046029254386365396\n'
    passed += 1
    print('PASS installed_standard_library_and_runtime_resources',flush=True)
    print(f'{passed} module integration cases passed (positive cases at -O0 and -O2)', flush=True)


if __name__ == '__main__':
    main()
