"""Compare versioned loops with their forced checked fallback at O0 and O2."""
import pathlib
import re


def exercise(run, source, compiler, clang, work, repo):
    cases = [
        ('fixed', 'var^ a:i32^[4]=[1,2,3,4]; var^ i:i64^=0; repeat^ while^ i < 4 { a[i] := a[i]+1; i := i+1 }; print(a[0],a[3])', '2 5\n', True, None),
        ('grow', 'var^ a:grow^ i32^[]=[1,2,3,4]; var^ i:i64^=0; var^ s:i32^=0; repeat^ while^ i < a.len { s := s+a[i]; i := i+1 }; print(s)', '10\n', True, None),
        ('string', 'let^ a="abc"; var^ i:i64^=0; var^ s:i32^=0; repeat^ while^ i < a.len { s := s+i32^(a[i]); i := i+1 }; print(s)', '294\n', True, None),
        ('slice', 'var^ a:grow^ i32^[]=[1,2,3,4]; let^ v=a[1 :: 3]; var^ i:i64^=0; var^ s:i32^=0; repeat^ while^ i < v.len { s := s+v[i]; i := i+1 }; print(s)', '5\n', True, None),
        ('nested', 'var^ a:i32^[6]=[0,1,2,3,4,5]; var^ y:i64^=0; var^ s:i32^=0; repeat^ while^ y < 2 { var^ x:i64^=0; repeat^ while^ x < 3 { s := s+a[y*3+x]; x := x+1 }; y := y+1 }; print(s)', '15\n', True, None),
        ('negative_scale', 'let^ a:i32^[4]=[1,2,3,4]; var^ i:i64^=0; var^ s:i32^=0; repeat^ while^ i < 4 { s := s+a[i*(-1)+3]; i := i+1 }; print(s)', '10\n', True, None),
        ('square', 'let^ a:i32^[5]=[1,2,3,4,5]; var^ i:i64^=-2; var^ s:i32^=0; repeat^ while^ i < 3 { s := s+a[i*i]; i := i+1 }; print(s)', '15\n', True, None),
        ('empty', 'let^ a:grow^ i32^[]=[]; var^ i:i64^=0; repeat^ while^ i < a.len { assert(a[i]=0); i := i+1 }; print(i)', '0\n', False, None),
        ('empty_pure', 'let^ a:grow^ i32^[]=[]; var^ i:i64^=0; var^ s:i32^=7; repeat^ while^ i < a.len { s := a[i]; i := i+1 }; print(s)', '7\n', True, None),
        ('near_max', 'let^ a:i32^[1]=[42]; var^ i:i64^=9223372036854775806; var^ s:i32^=0; repeat^ while^ i < 9223372036854775807 { s := a[i-9223372036854775806]; i := i+1 }; print(s)', '42\n', True, None),
        ('wrapped_add', 'let^ a:i32^[3]=[1,2,3]; var^ i:i64^=1; var^ s:i32^=0; repeat^ while^ i < 3 { s := s+a[i+9223372036854775807-9223372036854775807]; i := i+1 }; print(s)', '5\n', True, None),
        ('wrapped_mul', 'let^ a:i32^[1]=[42]; var^ i:i64^=2; var^ s:i32^=0; let^ k:i64^=4611686018427387904; repeat^ while^ i < 3 { s := a[i*k-(-9223372036854775808)]; i := i+1 }; print(s)', '42\n', True, None),
        ('upper_wrap', 'let^ a:i32^[1]=[42]; var^ i:i64^=0; var^ s:i32^=0; let^ n:i64^=-9223372036854775808; repeat^ while^ i < n-1 { s := a[i]; i := i+1 }; print(s)', '', True, 'array index out of bounds'),
        ('late_bounds', 'print(42); var^ a:i32^[2]=[0,0]; var^ i:i64^=0; repeat^ while^ i < 3 { a[i] := 7; i := i+1 }', '42\n', True, 'array index out of bounds'),
        ('negative_index', 'let^ a:i32^[2]=[1,2]; var^ i:i64^=-1; var^ s:i32^=0; repeat^ while^ i < 2 { s := s+a[i]; i := i+1 }', '', True, 'array index out of bounds'),
        ('late_division', 'print(42); var^ a:i32^[3]=[1,0,3]; var^ i:i64^=0; repeat^ while^ i < 4 { a[i] := 12/a[i]; i := i+1 }', '42\n', True, 'invalid integer division'),
        ('varying_bound', 'var^ a:i32^[4]=[1,2,3,4]; var^ n:i64^=4; var^ i:i64^=0; var^ s:i32^=0; repeat^ while^ i < n { s := s+a[i]; n := n-1; i := i+1 }; print(s)', '3\n', False, None),
        ('addressed_induction', 'let^ a:i32^[2]=[1,2]; var^ i:i64^=0; let^ ref=ref^ i; var^ s:i32^=0; repeat^ while^ i < 2 { s := s+a[i]; i := i+1 }; print(s,*ref)', '3 2\n', False, None),
        ('step_two', 'let^ a:i32^[4]=[1,2,3,4]; var^ i:i64^=0; var^ s:i32^=0; repeat^ while^ i < 4 { s := s+a[i]; i := i+2 }; print(s)', '4\n', False, None),
        ('shrink', 'var^ a:grow^ i32^[]=[1,2,3]; var^ i:i64^=0; repeat^ while^ i < 3 { a[i] := 7; a.clear(); i := i+1 }', '', False, 'array index out of bounds'),
        ('alias', '''let^ change=p^ a:ref^ grow^ i32^[], b:ref^ grow^ i32^[] {
            var^ i:i64^=1; repeat^ while^ i < a.len { b[i] := a[i-1]+1; i := i+1 }
        }; var^ a:grow^ i32^[]=[1,0,0,0]; change(ref^ a,ref^ a); print(a[3])''', '4\n', True, None),
        ('field', '''def^ Item { self^{ x:i32^ } }; var^ a:grow^ Item[]=[Item{x=1},Item{x=2}];
            var^ i:i64^=0; repeat^ while^ i < a.len { a[i].x := a[i].x+1; i := i+1 }; print(a[1].x)''', '3\n', True, None),
        ('byte_alias', '''let^ change=p^ a:ref^ grow^ u8^[], b:ref^ grow^ u8^[] {
            var^ i:i64^=1; repeat^ while^ i < a.len { b[i] := a[i-1]+1; i := i+1 }
        }; var^ a:grow^ u8^[]=[]; var^ k:i64^=0
        repeat^ while^ k < 64 { a.push(1); k := k+1 }; change(ref^ a,ref^ a); print(a[63])''', '64\n', True, None),
        ('slice_overlap', '''let^ change=p^ a:u8^[:], b:ref^ grow^ u8^[] {
            var^ i:i64^=1; repeat^ while^ i < a.len { b[i] := a[i-1]+1; i := i+1 }
        }; var^ a:grow^ u8^[]=[]; var^ k:i64^=0
        repeat^ while^ k < 64 { a.push(1); k := k+1 }; change(a[:: 64],ref^ a); print(a[63])''', '64\n', True, None),
        ('two_loops', '''var^ a:i32^[3]=[1,2,3]; var^ i:i64^=0
            repeat^ while^ i < 3 { a[i] := a[i]+1; i := i+1 }; i := 0
            repeat^ while^ i < 3 { a[i] := a[i]*2; i := i+1 }; print(a[0],a[2])''', '4 8\n', True, None),
    ]
    # A single runtime-parameterized function exercises signs and endpoint
    # combinations without letting constant folding replace the loop body.
    matrix = '''let^ sum=p^ a:ref^ grow^ i32^[], start:i64^, stop:i64^, scale:i64^, offset:i64^ -> i64^ {
        var^ i=start; var^ s:i64^=0
        repeat^ while^ i < stop { s := s+i64^(a[i*scale+offset]); i := i+1 }
        return^ s
    }; var^ a:grow^ i32^[]=[]; var^ k:i64^=0
    repeat^ while^ k < 64 { a.push(i32^(k+1)); k := k+1 }
    '''
    expected = ''
    for start in (-3, -1, 0, 2):
        for scale in (-3, -1, 0, 1, 2):
            for span in (1, 3):
                matrix += f'print(sum(ref^ a,{start},{start+span},{scale},20))\n'
                expected += str(sum(i*scale+21 for i in range(start, start+span))) + '\n'
    cases.append(('affine_matrix', matrix, expected, True, None))
    for name, text, output, versioned, error in cases:
        name = 'ranges_' + name
        path = source(name, text)
        for opt in ('-O0', '-O2'):
            exe = work / f'{name}_{opt[1:]}.exe'
            rir = exe.with_suffix('.rir')
            run([compiler, 'build', str(path), '-o', str(exe), '--clang', clang, '--dump-ir', str(rir), opt])
            llvm = pathlib.Path(str(exe) + '.ll').read_text()
            assert ('range.guard' in rir.read_text()) == versioned, name
            actual = run([str(exe)], 1 if error else 0)
            assert actual.stdout == output, (name, actual.stdout)
            if error:
                assert error in actual.stderr, (name, actual.stderr)
            flags = re.findall(r'; range.guard result=(%p\d+)', llvm)
            for flag in flags:
                llvm, count = re.subn(r'store i1 [^,]+, ptr ' + re.escape(flag) + r'\b',
                                      'store i1 false, ptr ' + flag, llvm)
                assert count == 1
            if versioned:
                assert flags and 'range=proven' in rir.read_text()
                checked = exe.with_suffix('.checked.ll')
                checked.write_text(llvm)
                other = exe.with_suffix('.checked.exe')
                run([clang, '-Wno-override-module', '-std=c11', opt, '-rtlib=compiler-rt', str(checked),
                     str(repo / 'runtime/rhat_runtime.c'), '-o', str(other)])
                fallback = run([str(other)], 1 if error else 0)
                assert (actual.stdout, actual.stderr) == (fallback.stdout, fallback.stderr), name
        print('PASS ' + name, flush=True)
    wrapper=work/'literal_capacity_runtime.c'
    wrapper.write_text('#define RH_STACK_BYTES 65536\n#include "'+
                       (repo/'runtime/rhat_runtime.c').as_posix()+'"\n')
    path=source('literal_append_capacity',
                'var^ a:grow^ u8^[]=[]; var^ i:i32^=0; repeat^ while^ i<64 { a.append("'+
                'a'*1024+'"); i := i+1 }; a.append("!")')
    for opt in ('-O0','-O2'):
        exe=work/('literal_append_capacity_'+opt[1:]+'.exe')
        run([compiler,'build',str(path),'-o',str(exe),'--clang',clang,'--runtime',str(wrapper),opt])
        result=run([str(exe)],1)
        assert 'data stack overflow' in result.stderr,result.stderr
        assert 'at line 1:' in result.stderr,result.stderr
    print('PASS literal_append_capacity',flush=True)
    return len(cases)+1
