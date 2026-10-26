"""Local call-shape inference, per-instance results, contracts and ownership."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--filter', default='')
    args = parser.parse_args()
    compiler, clang = str(Path(args.compiler).resolve()), str(Path(args.clang).resolve())
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    passed = 0

    def run(command, good=True):
        p = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=90)
        assert (p.returncode == 0) == good, (command, p.stdout, p.stderr)
        return p

    def source(name, text):
        p = work / (name + '.rh')
        p.write_text(text, encoding='utf-8')
        return p

    def positive(name, text, expected, inspect=None):
        nonlocal passed
        if args.filter and args.filter not in name:
            return
        path = source(name, text)
        for opt in ('-O0', '-O2'):
            exe = work / (name + opt + '.exe')
            rir = exe.with_suffix('.rir')
            run([compiler, 'build', path, '-o', exe, '--clang', clang, opt, '--dump-ir', rir])
            assert run([exe]).stdout == expected, (name, opt, expected)
            if inspect:
                inspect(rir.read_text(), Path(str(exe)+'.ll').read_text())
        passed += 1
        print('PASS', name, flush=True)

    def negative(name, text, diagnostic):
        nonlocal passed
        if args.filter and args.filter not in name:
            return
        p = run([compiler, 'check', source(name, text)], False)
        assert diagnostic in p.stderr, (name, diagnostic, p.stderr)
        assert re.search(r':\d+:\d+: error:', p.stderr), p.stderr
        passed += 1
        print('PASS', name, flush=True)

    def instances(ir, ll):
        assert '-> i32^' in ir and '-> i64^' in ir and '-> string^' in ir
        assert 'malloc' not in ll

    positive('inference_identity', '''
let^ id=f^ x {x}
print(id(42),id(i64^(43)),id("hello"),id("hello").len)
''', '42 43 hello 5\n', instances)

    positive('inference_mixed_annotations', '''
let^ choose=f^ flag:bool^, a, b:i64^ {if^ flag: i64^(a) el^: b;}
print(choose(true^,12,99),choose(false^,u8^(7),42))
let^ show=p^ x {print(x)}
show(12) show("hello")
''', '12 42\n12\nhello\n')

    positive('inference_members', '''
def^ A {self^{value:i32^=7} read=f^ self^ {self^.value}}
def^ B {self^{value:i64^=9} read=f^ self^ {self^.value}}
let^ read=f^ item {item.read()}
print(read(A{}),read(B{}))
''', '7 9\n')

    positive('inference_owned_return', '''
let^ build=p^ n {
 var^ items:grow^ i32^[]=[]
 for^ i from^ 1 to^ n {items.push(i32^(i))}
 return^ items
}
var^ a=build(3) var^ b=build(i64^(2))
print(a.len,a[2],b.len,b[1])
''', '3 3 2 2\n')

    positive('inference_borrow', '''
let^ at=f^ items,index {ref^ items[index]}
let^ add=p^ items,value {items.push(value)}
var^ a:grow^ i32^[]=[1,2]
let^ ref=at(ref^ a,1)
add(ref^ a,3);
*ref:=42
print(a[1],a[2])
''', '42 3\n')

    positive('inference_recursion', '''
let^ factorial=f^ n {if^ n<2: 1 el^: n*this^(n-1);}
let^ even=f^ n {if^ n=0: true^ el^: odd(n-1);}
let^ odd=f^ n {if^ n=0: false^ el^: even(n-1);}
print(factorial(5),factorial(i64^(6)),even(10),odd(i64^(11)))
''', '120 720 true true\n')

    positive('inference_anonymous', '''
print(f^ x {x*2}(21),(p^ x {return^ x+1})(41))
print(f^ n {if^ n<2: 1 el^: n*this^(n-1);}(6))
''', '42 42\n720\n')

    positive('inference_overloads_and_forward_call', '''
print(twice(21),twice(20,1))
let^ twice=f^ x {x*2}
let^ twice=f^ x,y {(x+y)*2}
''', '42 42\n')

    positive('inference_methods', '''
def^ Box {
 self^{value:i32^=1}
 identity=f^ self^,x {x}
 add=p^ self^,delta {self^.value+=delta}
}
var^ b=Box{}
b.add(41)
print(b.value,b.identity("hello"),b.identity(7))
def^ Derived:Box {}
var^ derived=Derived{}
derived.add(2)
print(derived.value)
''', '42 hello 7\n3\n')

    positive('inference_nullable', '''
let^ value=f^ x {if^ x fits^ nil^ {return^ 0} return^ x}
let^ n:i64^|nil^=7
print(value(n),value(nil^),value(i32^(42)))
''', '7 0 42\n')

    positive('inference_errors', '''
errordef^ Error {Bad}
let^ read=f^ n:i32^ ->i32^|Error.Bad {if^ n<0: Error.Bad{} el^: n;}
let^ increment=f^ n {return^ try^ read(n)+1}
print(increment(41) catch^ 0,increment(-1) fits^ Error.Bad)
''', '42 true\n')

    positive('inference_template_still_deferred', '''
let^ unused=template^f^ x {x.missing()}
print(42)
''', '42\n')

    positive('body_unused_declarations', """
let^ flag=f^ x {if^ x: 1 el^: 0;}
let^ local=f^ x {let^ y:i64^=x return^ y}
let^ result=f^ x ->i16^ {x}
let^ number=f^ x {x+i64^(1)}
let^ reference=f^ x ->i32^ {*x}
def^ Box {self^{value:i32^} add=p^ self^,delta {self^.value+=delta}}
print(42)
""", '42\n')

    positive('body_transitive_context', """
let^ outer=f^ x {middle(x)}
let^ middle=f^ y {inner(y)}
let^ inner=f^ z:i64^ {z+1}
print(outer(41))
""", '42\n')

    positive('body_constructor_array_alias', """
def^ Box {self^{value:i64^}}
let^ boxed=f^ x {let^ alias=x return^ Box{value=alias}}
let^ array=f^ x ->i64^[2] {[x,x+1]}
let^ a=array(20)
print(boxed(42).value,a[0],a[1])
""", '42 20 21\n')

    positive('body_nominal_constraint_preserves_shape', """
def^ A {self^{value:i32^=42}}
def^ B {}
def^ U=A|B
let^ sink=p^ x:U {}
let^ read=p^ x {sink(x) return^ x.value}
print(read(A{}))
def^ Base {self^{base:i32^=1}}
def^ Derived:Base {self^{extra:i32^=43}}
let^ base=p^ x:Base {}
let^ extra=p^ x {base(x) return^ x.extra}
print(extra(Derived{}))
""", '42\n43\n')

    positive('body_type_dependent_suffix', """
def^ A {}
let^ integer=f^ n:i64^ {n}
let^ choose=f^ x,y {if^ x fits^ A {return^ 42} return^ integer(y)}
print(choose(A{},"skipped"),choose(0,i64^(43)))
errordef^ Error {Bad}
let^ propagate=f^ x,y {try^ x return^ integer(y)}
print(propagate(Error.Bad{},"skipped") fits^ Error.Bad)
""", '42 43\ntrue\n')

    positive('body_loop_and_shadow', """
let^ sink=p^ x:i64^ {}
let^ loop=p^ x {repeat^ while^ false^ {sink(x)}}
let^ shadow=p^ x {do^{let^ x:i32^=1 print(x)} sink(x)}
loop(42) shadow(43)
""", '1\n')

    positive('body_anonymous', """
let^ typed=f^ n:i64^ {n+1}
print((f^ x {typed(x)})(41))
""", '42\n')

    chain = '\n'.join(f'let^ step{n}=f^ x {{step{n+1}(x)}}' for n in range(160))
    chain += '\nlet^ step160=f^ x:i64^ {x+1}\nstep120(0) step80(0) step40(0) print(step0(41))'
    positive('body_long_forward_requirements', chain, '42\n')

    negative('body_conflict', 'let^ bad=f^ x {if^ x {return^ x+1} return^ 0}', 'conflicting body requirements')
    negative('body_ambiguous_width', 'let^ unused=f^ x {x+1}', 'without a call shape')
    negative('body_nominal_not_guessed', 'def^ A {self^{value:i32^}} let^ read=f^ x {x.value}', 'without a call shape')

    negative('inference_unused', 'let^ unused=f^ x {x}', 'without a call shape')
    negative('inference_unused_method', 'def^ A {read=f^ self^,x {x}}', 'without a call shape')
    negative('inference_missing_member', 'def^ A {} let^ read=f^ x {x.value} print(read(A{}))', 'has no field')
    negative('inference_missing_method', 'def^ A {} let^ read=f^ x {x.read()} print(read(A{}))', 'has no method')
    negative('inference_bad_shape', 'let^ add=f^ x,y {x+y} print(add(1,"hello"))', 'inferred types disagree')
    negative('inference_nominal_annotation', 'def^ A {} def^ B {} let^ use=p^ a:A,x {} let^ b=B{} use(b,1)', 'requires nominal')
    negative('inference_bare_record', 'let^ use=p^ x {} use({value=42})', 'single nominal struct')
    negative('inference_pure_mutation', 'let^ edit=f^ x {*x:=42} var^ n=0 edit(ref^ n)', 'f^ cannot mutate')
    negative('inference_borrow_escape', 'let^ bad=f^ x {var^ local=x return^ ref^ local} let^ ref=bad(1)', 'cannot return a reference')
    negative('inference_owned_argument', 'let^ use=p^ x {} var^ a:grow^ i32^[]=[] use(a)', 'pass owned values by reference')
    negative('inference_recursive_owner', 'let^ call=p^ n {var^ a:grow^ i32^[]=[n] if^ n>0 {this^(n-1)} print(a.len)} call(3)', 'holds local owned value')
    negative('inference_ungrounded_recursion', 'let^ forever=f^ x {this^(x)} print(forever(1))', 'cannot infer return type')
    negative('inference_abstract_contract', 'def^ A {abstract^ read:f^ self^,x ->i32^;}', 'requires a type annotation')
    print(f'{passed} inference cases passed; executable cases checked at -O0 and -O2.', flush=True)


if __name__ == '__main__':
    main()
