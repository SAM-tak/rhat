"""Task-owned mutable type members and their lifetime/effect checks."""
from harness import Suite


def main():
    suite = Suite('static members')
    positive, negative = suite.positive, suite.negative
    positive('static_basic', '''
def^ A {
    self^{i=0}
    ssi=7
    var^si=0
    increment=p^ {def^.si+=1}
    step=p^self^ {def^.si+=self^.i}
    read=f^ {def^.si}
}
A.increment()
var^a=A{i=3}
a.step()
let^inc=p^ {A.si+=2}
inc()
print^(A.si,A.read(),A.ssi)
''', '6 6 7\n')
    positive('static_initialization', '''
let^make=p^ {print^("init") return^ 3}
def^ A {var^n=make() var^zero:i64^ var^empty:i32^[grow^]}
print^(A.n,A.zero,A.empty.len)
A.n+=1
print^(A.n)
''', 'init\n3 0 0\n4\n')
    positive('static_generic_and_inherited', '''
def^ Counter<T> {var^n=T.ZERO add=p^ {def^.n+=T.ONE}}
let^I=Counter<i32^>
let^J=Counter<i64^>
def^ Child:I {}
I.add() J.add() J.add()
Child.n+=4
print^(I.n,J.n,Child.n)
''', '5 2 5\n')
    positive('static_inherited_shadow', '''
def^ Base {var^n=1}
def^ Child:Base {n=7}
print^(Base.n,Child.n)
''', '1 7\n')
    positive('static_owned_and_reference_result', '''
def^ A {
    var^items=i32^[grow^]{1,2}
    var^other=i32^[grow^]{8}
    add=p^ {def^.items.push(3)}
    first=f^ ->ref^i32^ {ref^def^.items[0]}
}
A.add()
let^r=A.first();
*r:=9
print^(A.items.len,A.items[0],A.other[0])
''', '3 9 8\n')
    positive('static_dynamic_dispatch', '''
def^ Base {abstract^add:p^self^;}
def^ A:Base {var^n=0 override^add=p^self^ {def^.n+=1}}
var^a=A{}
let^b:ref^dyn^Base=ref^a
b.add()
print^(A.n)
''', '1\n')
    positive('static_function_value', '''
def^ A {var^n=2 inc=p^ {def^.n+=1}}
let^inc=p^ {A.inc()}
inc() inc()
print^(A.n)
''', '4\n')
    positive('static_reference_transfer', '''
def^ A {var^n=1 var^r:ref^i32^=ref^def^.n}
let^set=p^r:ref^i32^ {A.r:=r}
set(ref^A.n)
print^(*A.r)
''', '1\n')
    negative('static_pure_write', 'def^ A {var^n=0 bad=f^ {def^.n+=1}} A.bad()',
             'f^ cannot mutate')
    negative('static_private', 'def^ A {private^var^n=0} print^(A.n)',
             'private static variable')
    negative('static_duplicate', 'def^ A {var^n=0 var^n=1}', 'duplicate static variable')
    negative('static_collision', 'def^ A {var^n=0 n=1}', 'name collision')
    negative('static_missing_type', 'def^ A {var^n}', 'requires a type annotation or an initializer')
    negative('static_cyclic_type', 'def^ A {var^n=def^.n}', 'cyclic static variable type')
    negative('static_constant_read', 'def^ A {var^n=1 N=def^.n}', 'type constant requires')
    negative('static_reference_escape', '''
def^ A {var^n=1 var^r:ref^i32^=ref^def^.n}
let^set=p^r:ref^i32^ {A.r:=r}
var^n=4
set(ref^n)
''', 'reference would outlive its owner')
    negative('static_export', 'def^ A {var^n=0 get=f^ {def^.n}} exportC^let^get=f^ ->i32^ {A.get()}',
             'exported function cannot access task-owned static variables')
    negative('static_read_borrow', '''
def^ A {var^n=0}
let^bad=p^r:ref^read^i32^ {A.n+=1 print^(*r)}
bad(ref^A.n)
''', 'read^ borrow')
    negative('static_flex_borrow', '''
def^ A {var^items=i32^[flex^]{1,2} clear=p^ {def^.items.clear()}}
let^r=ref^A.items[0]
A.clear()
print^(*r)
''', 'cannot shrink flex^ storage')

    def imported():
        root = suite.work/'imported'
        root.mkdir(parents=True, exist_ok=True)
        (root/'counter.rh').write_text('''module^counter
public^def^ Counter<T> {
    var^n=T.ZERO
    inc=p^ {def^.n+=T.ONE}
    bump=p^self^ {def^.n+=T.ONE}
}
''', encoding='utf-8')
        source = root/'main.rh'
        source.write_text('''let^c=import^counter
let^C=c.Counter<i32^>
C.inc()
var^v=C{}
v.bump()
C.n+=5
print^(C.n)
''', encoding='utf-8')
        if suite.args.checks_only:
            suite.run([suite.compiler,'check',source])
        else:
            for opt in suite.opts:
                exe = root/f'main{opt}.exe'
                suite.run([suite.compiler,'build',source,'-o',exe,'--clang',suite.clang,opt])
                assert suite.run([exe]).stdout == '7\n'
    suite.case('static_imported_generic', imported)

    def initialization_guard():
        source = suite.source('static_initialization_guard', '''
def^ A {var^a:i32^=def^.b var^b=1}
print^(A.a)
''')
        if suite.args.checks_only:
            suite.run([suite.compiler,'check',source])
        else:
            for opt in suite.opts:
                exe = suite.work/f'static_initialization_guard{opt}.exe'
                suite.run([suite.compiler,'build',source,'-o',exe,'--clang',suite.clang,opt])
                result = suite.run([exe],False)
                assert 'static variable used before initialization' in result.stderr, result.stderr
    suite.case('static_initialization_guard', initialization_guard)
    suite.finish()


if __name__ == '__main__':
    main()
