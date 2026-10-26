"""End-to-end module, separate object, cache and std.random contracts."""
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil

from harness import Suite


def main():
    suite = Suite('module integration')
    run, compiler, clang, work, repo = suite.run, suite.compiler, suite.clang, suite.work, suite.repo
    shutil.rmtree(work, ignore_errors=True)  # Cache tests need a cold start.
    work.mkdir(parents=True, exist_ok=True)

    def project(name, files):
        root = work / name
        for path, text in files.items():
            dest = root / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(text, encoding='utf-8')
        return root

    def positive(name, files, expected, extra=()):
        """Queue a project build at each level in suite.opts; return its root and future."""
        root = project(name, files)
        def body():
            for opt in suite.opts:
                exe = root / f'main{opt}.exe'
                run([compiler, 'build', root/'main.rh', '-o', exe, '--clang', clang, opt, *extra])
                actual = run([exe]).stdout
                assert actual == expected, (name, opt, actual, expected)
        return root, suite.case(name, body)

    def negative(name, files, diagnostic, extra=()):
        root = project(name, files)
        def body():
            p = run([compiler, 'check', root/'main.rh', *extra], False)
            assert diagnostic in p.stderr and re.search(r':\d+:\d+: error:', p.stderr), (name, p.stderr)
        suite.case(name, body)

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
    if^ x < 0 { return^ Error.Bad{} }
    if^ x = 0 { return^ nil^ }
    return^ x
}
'''
    root, imports_built = positive('imports_types_constants_errors', {
        'lib/example.rh': lib,
        'main.rh': '''import^ lib.example
let^ x=import^ lib.example
print(x.twice(21),lib.example.twice(10,11),x.answer,x.enabled,x.label)
let^ item=x.Item{value=42}
print(item.value)
let^ result=x.result(12)
assert(result fits^ i64^)
print(x.result(-1) fits^ x.Error.Bad,x.result(0) fits^ nil^,result)
'''}, '42 42 42 true module\n42\ntrue true 12\n')
    positive('module_owned_template_composition', {
        'lib/mem.rh': '''module^ lib.mem
public^ def^ Base { self^{ value:i32^=42 }; read=f^ self^ ->i32^ { self^.value } }
public^ let^ make=p^ ->grow^ i32^[] { return^ [20,22] }
public^ let^ total=f^ a:ref^ grow^ i32^[] ->i32^ { a[0]+a[1] }
public^ let^ read=template^f^ item { item.read() }
''', 'main.rh': '''import^ lib.mem
def^ Item : lib.mem.Base {}
var^ a=lib.mem.make()
print(lib.mem.total(ref^ a),lib.mem.read(Item{}))
'''}, '42 42\n')
    positive('module_error_propagation', {
        'lib/fallible.rh': '''module^ lib.fallible
public^ errordef^ Error {Bad,Missing}
public^ let^ read=f^ n:i32^ ->i32^|Error.Bad|Error.Missing {
 if^ n=0 {return^ Error.Bad{}}
 if^ n<0 {return^ Error.Missing{}}
 return^ n
}
public^ let^ wrap=template^f^ n {return^ try^ read(n)+1}
''', 'main.rh': '''let^ lib=import^ lib.fallible
print((lib.wrap(2) catch^ 0),lib.read(0) catch^ 12)
do^{print(try^ lib.wrap(-1)) catch^ lib.Error.Missing: print(20) catch^: print(30)}
'''}, '3 12\n20\n')
    positive('module_body_parameter_inference', {
        'lib/body.rh': """module^ lib.body
let^ typed=f^ x:i64^ {x+1}
public^ let^ twice=f^ x {typed(x)*2}
public^ let^ boolean=f^ x {if^ x: 42 el^: 0;}
public^ def^ Box {
 self^{value:i32^=1}
 add=p^ self^,delta {self^.value+=delta}
}
""", 'main.rh': """import^ lib.body
print(lib.body.twice(20),lib.body.boolean(true^))
var^ b=lib.body.Box{}
b.add(41)
print(b.value)
"""}, '42 42\n42\n')

    positive('module_local_call_inference', {
        'lib/inferred.rh': '''module^ lib.inferred
let^ twice=f^ x {x*2}
public^ let^ run=f^ x:i32^ {twice(x)}
public^ let^ generic=template^f^ x {twice(x)}
''', 'main.rh': '''import^ lib.inferred
print(lib.inferred.run(21),lib.inferred.generic(i64^(22)))
'''}, '42 44\n')
    positive('module_private_method_inference', {
        'lib/inferred.rh': '''module^ lib.inferred
public^ def^ Box {
 private^ identity=f^ self^,x {x}
 run=f^ self^,x:i32^ {self^.identity(x)}
}
''', 'main.rh': '''import^ lib.inferred
let^ box=lib.inferred.Box{}
print(box.run(42))
'''}, '42\n')
    negative('module_public_untyped', {
        'lib/inferred.rh': 'module^ lib.inferred\npublic^ let^ id=f^ x {x}',
        'main.rh': 'import^ lib.inferred\nprint(lib.inferred.id(42))'}, 'or explicit template^')
    negative('module_public_untyped_method', {
        'lib/inferred.rh': 'module^ lib.inferred\npublic^ def^ Box {id=f^ self^,x {x}}',
        'main.rh': 'import^ lib.inferred'}, 'or explicit template^')
    negative('module_public_inherited_untyped', {
        'lib/inferred.rh': 'module^ lib.inferred\ndef^ Base {id=f^ self^,x {x}} public^ def^ Box:Base {}',
        'main.rh': 'import^ lib.inferred'}, 'or explicit template^')
    positive('modules_nominal_distinct', {
        'lib/a.rh': 'module^ lib.a\npublic^ def^ Item { self^{ value:i32^=1 } }',
        'lib/b.rh': 'module^ lib.b\npublic^ def^ Item { self^{ value:i32^=2 } }',
        'main.rh': 'import^ lib.a\nimport^ lib.b\nprint(lib.a.Item{}.value,lib.b.Item{}.value)'}, '1 2\n')
    for name, text, diagnostic in [
        ('private_function', 'import^ lib.example\nprint(lib.example.hidden())', 'does not export'),
        ('private_type', 'import^ lib.example\nlet^ x:lib.example.Secret=lib.example.Secret{}', 'unknown or unavailable type'),
        ('unknown_module', 'import^ absent.module', 'cannot find module'),
        ('import_collision', 'def^ lib {}\nimport^ lib.example', 'conflicts'),
        ('removed_error_prefix_struct', 'def^ A {}\nlet^ x=error^A{}', 'error^ construction has been removed'),
        ('removed_error_prefix_variant', 'errordef^ E { Bad }\nlet^ x=error^E.Bad{}', 'error^ construction has been removed'),
        ('overload_duplicate_domain', 'let^ f=f^ x:i32^ { x }\nlet^ f=f^ x:i32^ { x }', 'duplicate function'),
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
    positive('block_may_shadow_import', {'main.rh':'import^ std.random\ndo^{ let^ std=42; print(std) }'}, '42\n')
    negative('module_local_owner_recursive', {'lib/a.rh': '''module^ lib.a
public^ let^ recur=p^ n:i32^ {
    var^ a:grow^ i32^[]=[n]
    if^ n > 0 { this^(n-1) }
    print(a[0])
}''', 'main.rh': 'import^ lib.a\nlib.a.recur(2)'}, 'recursive call')
    negative('module_borrow_escape', {'lib/a.rh': '''module^ lib.a
public^ let^ bad=p^ ->ref^ i32^ {
 var^ a:grow^ i32^[]=[1]; return^ ref^ a[0]
}''', 'main.rh': 'import^ lib.a'}, 'escape')
    positive('primitive_union_literals', {'main.rh': '''errordef^ Error { Bad }
def^ Int = i64^ | Error.Bad | nil^
def^ Real = f64^ | Error.Bad
let^ value=f^ ->Int { return^ 9223372036854775807 }
let^ real=f^ ->Real { return^ 1.25 }
let^ integer=value() let^ floating=real()
assert(integer fits^ i64^) assert(floating fits^ f64^)
print(integer,floating)
'''}, '9223372036854775807 1.25\n')
    negative('reserved_std_module', {'main.rh': 'module^ std.forged'}, 'reserved')
    negative('private_standard_intrinsic', {'main.rh': 'print(__sqrt^(4.0))'}, 'private standard-library')
    positive('module_recursive_external_borrow', {
        'lib/borrow.rh': '''module^ lib.borrow
public^ let^ fill=p^ a:ref^ grow^ i32^[], n:i32^ {
    if^ n > 0 { a.push(n); this^(a,n-1) }
}
''', 'main.rh':'import^ lib.borrow\nvar^ a:grow^ i32^[]=[]\nlib.borrow.fill(ref^ a,3)\nprint(a.len,a[0],a[2])'}, '3 3 1\n')
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
    if imports_built:
        imports_built.result()
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
    suite.mark('object_cache_identity_reuse_invalidation_and_single_ir')

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
    suite.mark('runtime_header_cache_invalidation')
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
        suite.mark('windows_read_locked_immutable_cache_inputs')

    random_source = '''import^ std.random
var^ r=std.random.new(12345)
print(r.save())
var^ i:i32^=0
repeat^ while^ i < 8 { print(r.next()); i:=i+1 }
let^ saved=r.save()
let^ a=(r.rangeint(-100,100) catch^ 0)
assert((r.load(saved) fits^ nil^))
assert((r.rangeint(-100,100) catch^ 0)=a)
let^ before=r.save()
assert((r.load(0) fits^ std.random.Error.InvalidState))
assert((r.rangeint(5,4) fits^ std.random.Error.InvalidArgument))
assert((r.uniform(1.0,-1.0) fits^ std.random.Error.InvalidArgument))
assert((r.normal(0.0,-1.0) fits^ std.random.Error.InvalidArgument))
assert((r.rangeint(7,7) catch^ 0)=7)
assert((r.uniform(2.0,2.0) catch^ 0.0)=2.0)
assert((r.normal(3.0,0.0) catch^ 0.0)=3.0)
let^ nan=0.0/0.0
let^ infinity=1.0/0.0
assert((r.uniform(nan,1.0) fits^ std.random.Error.InvalidArgument))
assert((r.normal(0.0,infinity) fits^ std.random.Error.InvalidArgument))
assert(before=r.save())
assert((r.rangeint(-9223372036854775807-1,9223372036854775807) fits^ i64^))
let^ u=(r.uniform(-1.7976931348623157e308,1.7976931348623157e308) catch^ 0.0)
assert(u >= -1.7976931348623157e308 and^ u < 1.7976931348623157e308)
let^ n=(r.normal(0.0,1.0) catch^ 0.0)
assert(n=n)
let^ automatic=std.random.new()
assert((automatic fits^ std.random.Random))
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
    for opt in suite.opts:
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
        fail_source.write_text('import^ std.random\nassert((std.random.new() fits^ std.random.Error.SeedUnavailable))\nprint(42)')
        run([compiler, 'emit', fail_source, '-o', ll])
        command = [clang, '-Wno-override-module', opt, '-rtlib=compiler-rt', '-DRH_TEST_SEED_FAILURE',
                   ll, repo/'runtime/rhat_runtime.c', repo/'runtime/rhat_random.c', '-o', random_root/'failure.exe']
        if os.name == 'nt': command += ['-lbcrypt']
        else: command += ['-lm']
        run(command)
        assert run([random_root/'failure.exe']).stdout == '42\n'
    suite.mark('std_random_lhat_oracle_state_ranges_math_and_seed_failure')
    # Integer rejection, sign crossing, full-width range and distribution replay.
    state = 12345 ^ 0x9E3779B97F4A7C15
    def raw_draw():
        nonlocal state
        state ^= state >> 12
        state ^= (state << 25) & mask
        state ^= state >> 27
        return (state*0x2545F4914F6CDD1D)&mask
    statements = ['import^ std.random',
                  'let^ expect_int=f^ value:std.random.IntResult ->i64^ {assert(value fits^ i64^) return^ value}',
                  'let^ expect_real=f^ value:std.random.RealResult ->f64^ {assert(value fits^ f64^) return^ value}',
                  'var^ r=std.random.new(12345)']
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
            statements.append(f'print(expect_int(r.rangeint({lo},{hi})))')
    normal_expected = []
    for _ in range(8):
        u = (raw_draw() >> 11)/(1<<53)
        v = (raw_draw() >> 11)/(1<<53)
        normal_expected.append(2.0+3.0*(math.sqrt(-2.0*math.log(1.0-u))*math.cos(6.283185307179586476925286766559*v)))
        statements.append('print(expect_real(r.normal(2.0,3.0)))')
    statements += ['r.reseed(12345)', 'assert(r.save()=-7046029254386365396)',
                   'var^ copy=r', 'assert(r.next()=copy.next())',
                   'var^ zero=std.random.new(0)', 'assert(zero.save()!=0)',
                   'var^ fallback=std.random.new(-7046029254386353131)',
                   'assert(fallback.save()=2685821657736338717)']
    oracle_root = project('random_integer_normal_oracle', {'main.rh':'\n'.join(statements)})
    for opt in suite.opts:
        exe=oracle_root/f'oracle{opt}.exe'
        run([compiler,'build',oracle_root/'main.rh','-o',exe,'--clang',clang,opt])
        output=run([exe]).stdout.splitlines()
        assert list(map(int,output[:96]))==integer_expected
        assert all(math.isclose(float(x), y, rel_tol=2e-14, abs_tol=2e-14)
                   for x,y in zip(output[96:],normal_expected)) and len(output)==104
    suite.mark('std_random_integer_rejection_and_normal_oracles')

    optional_root, _ = positive('module_optional_types', {
        'lib/value.rh': '''module^ lib.value
public^ def^ Item {self^{value:i32^=42}}
public^ def^ Result = Item | nil^
public^ let^ maybe=f^ yes:bool^ ->Item|nil^ {if^ yes: Item.new() el^: nil^;}
public^ let^ read=f^ item:nil^|Item ->i32^ {item?.value??0}
public^ let^ templateRead=template^f^ item {item?.value??0}
public^ let^ borrow=f^ value:ref^ i32^ ->ref^ i32^|nil^ {value}
''',
        'main.rh': '''let^ lib=import^ lib.value
let^ yes=lib.maybe(true^)
let^ no:lib.Item|nil^=lib.maybe(false^)
let^ named:lib.Result=yes
let^ anonymous:nil^|lib.Item=named
print(lib.read(yes),lib.read(no),anonymous?.value??0)
print(lib.templateRead(yes),lib.templateRead(no))
var^ number=7
let^ ref=lib.borrow(ref^ number)
print(*(ref??ref^ number))
''',
    }, '42 0 42\n42 0\n7\n')
    optional_ir = optional_root/'single.ll'
    run([compiler, 'emit', optional_root/'main.rh', '-o', optional_ir])
    run([clang, '-Wno-override-module', '-O2', optional_ir,
         repo/'runtime/rhat_runtime.c', '-o', optional_root/'single.exe'])
    assert run([optional_root/'single.exe']).stdout == '42 0 42\n42 0\n7\n'
    suite.mark('optional_union_single_ir_matches_modules')

    positive('module_fits_types_and_specializations', {
        'lib/value.rh': '''module^ lib.value
public^ def^ Item {self^{value:i32^=42}}
public^ let^ read=f^ item:Item|nil^ {if^ item fits^ Item: item.value el^: 0;}
public^ let^ structural=template^f^ item {if^ item fits^ Item: item.value el^: 0;}
public^ let^ staticAnswer=1 fits^ i32^
''',
        'main.rh': '''let^ lib=import^ lib.value
let^ yes:lib.Item|nil^=lib.Item.new()
print(yes fits^ lib.Item,lib.read(yes),lib.read(nil^))
print(lib.structural(lib.Item.new()),lib.structural(0))
print(lib.staticAnswer)
let^ random=import^ std.random
var^ rng=random.new(12345)
print(rng fits^ random.Random,rng.load(0) fits^ random.Error.InvalidState)
''',
    }, 'true 42 0\n42 0\ntrue\ntrue true\n')

    negative('module_fits_private_type', {
        'lib/value.rh': 'module^ lib.value\ndef^ Hidden {self^{value:i32^}}',
        'main.rh': 'let^ lib=import^ lib.value\nprint(1 fits^ lib.Hidden)',
    }, 'unknown or unavailable type')

    positive('module_contextual_construction', {
        'lib/point.rh': '''module^ lib.point
public^ def^ Point {self^{x:i32^=1,y:i32^=2}}
public^ let^ sum=f^ point:Point {point.x+point.y}
public^ let^ make=f^ ->Point {return^ {x=3,y=4}}
def^ Hidden {self^{value:i32^=9}}
public^ def^ Box {self^{implicit:Hidden,explicit:Hidden={value=10}}}
public^ let^ total=f^ box:Box {box.implicit.value+box.explicit.value}
''',
        'main.rh': '''let^ lib=import^ lib.point
print(lib.sum({x=20,y=22}),lib.sum({}))
let^ point:lib.Point={x=5}
print(point.x,point.y,lib.sum(lib.make()))
print(lib.total({}))
''',
    }, '42 3\n5 2 7\n19\n')

    negative('module_contextual_private_type', {
        'lib/hidden.rh': '''module^ lib.hidden
def^ Hidden {self^{value:i32^}}
public^ let^ read=f^ item:Hidden {item.value}
''',
        'main.rh': 'import^ lib.hidden\nprint(lib.hidden.read({value=42}))',
    }, 'requires a public type')

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
    for opt in suite.opts:
        exe=portable/f'portable{opt}.exe'
        run([packed,'build',portable/'main.rh','-o',exe,'--clang',clang,opt])
        assert run([exe]).stdout=='42\n-7046029254386365396\n'
    suite.mark('installed_standard_library_and_runtime_resources')
    suite.finish()


if __name__ == '__main__':
    main()
