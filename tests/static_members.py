"""Task-owned mutable type members and their lifetime/effect checks."""
from harness import Suite


def main():
    suite = Suite('static members')
    positive, negative = suite.positive, suite.negative
    positive('explicit_static_declarations', '''
def^ A {
    let^zero=0
    let^step:f32^=0.2
    wide:i64^=7
    var^value:f32^=0.2
    let^read=f^ {def^.step}
}
A.value+=A.step
print^(A.zero,A.wide,typeof^(A.step).signature^,typeof^(A.wide).signature^,A.value=A.read()+A.step)
''', '0 7 f32^ i64^ true\n')
    positive('typed_generic_static_constants', '''
def^ Value<T> {let^ZERO:T=T.ZERO private^let^ONE:T=T.ONE get=f^ {def^.ONE}}
def^ Child:Value<i64^> {}
print^(typeof^(Value<i32^>.ZERO).signature^,typeof^(Child.ZERO).signature^,Child.get())
''', 'i32^ i64^ 1\n')
    positive('qualified_static_outer_name_collision', '''
var^aaa=100
let^step=7
def^ A {
    var^aaa:i32^=1
    let^step:f32^=0.2
    self^{}
    change=p^self^ {def^.aaa+=1 Self^.aaa+=2}
    read=f^self^ {def^.aaa+Self^.aaa}
    implicit=f^self^ {aaa}
    constant=f^self^ {def^.step=Self^.step}
}
var^a=A{}
a.change()
print^(aaa,step,A.aaa,a.read(),a.implicit(),a.constant())
''', '100 7 4 8 100 true\n')
    negative('unqualified_static_variable', 'def^ A {var^n=0 read=f^ {n}} A.read()',
             'requires qualification')
    negative('unqualified_static_constant', 'def^ A {let^n=7 read=f^ {n}} A.read()',
             'requires qualification')
    negative('unqualified_static_constant_member',
             'struct^ P {x:i32^=0} def^ A {let^origin=P{} read=f^ {origin.x}} A.read()',
             "qualified name 'origin.x'")
    negative('explicit_static_duplicate', 'def^ A {let^aaa=0 var^aaa:f32^=0.2}',
             'name collision')
    negative('typed_static_constant_invalid', 'def^ A {let^bad:i32^="wrong"}',
             'expected')
    negative('explicit_static_private', 'def^ A {private^let^n=7} print^(A.n)',
             'private type constant')
    positive('entry_state_record_layout', '''
struct^ Item {left:i64^=0, right:i64^=0}
var^items=Item[grow^]{}
var^count=0
def^ Observer {touch=p^ {count+=1}}
items.push(Item{left=10,right=20})
items.push(Item{left=30,right=40})
Observer.touch()
print^(items.len,items[0].left,items[0].right,items[1].left,items[1].right,count)
''', '2 10 20 30 40 1\n')
    positive('static_record_layout', '''
struct^ Item {name:u8^[max^8]={}, value:i64^=0}
def^ Store {
    var^items=Item[grow^]{}
    add=p^ {def^.items.push(Item{name={65,66},value=7})}
}
Store.add() Store.add()
print^(Store.items.len,Store.items[0].name.len,Store.items[0].name[1],Store.items[1].value)
''', '2 2 66 7\n')
    positive('entry_binding_methods', '''
var^base=2
var^count=base+1
let^increment=p^ {count+=1}
def^ A {
    self^{i=0}
    var^si=0
    increment=p^ {count+=1 def^.si+=1}
    step=p^self^ {count+=1 self^.i+=1 increment()}
    read=f^self^ {count+self^.i}
}
var^a=A{}
A.increment() a.step() increment()
print^(count,a.read(),A.si)
do^{var^count=99 a.step() print^(count)}
print^(count,a.read())
''', '7 8 1\n99\n9 11\n')
    positive('entry_binding_dispatch', '''
var^count=0
def^ Base {abstract^step:p^self^;}
def^ A:Base {override^step=p^self^ {count+=1}}
def^ Counter<T> {step=p^ {count+=1}}
var^a=A{}
let^b:ref^dyn^Base=ref^a
b.step() Counter<i32^>.step() Counter<i64^>.step()
print^(count)
''', '3\n')
    positive('entry_binding_inference', '''
let^next=f^n {n+1}
var^count=next(2)
let^argc=....len
def^ A {step=p^ {count+=1} get=f^ {count} args=f^ {argc}}
A.step()
print^(A.get(),A.args())
''', '4 0\n')
    positive('entry_binding_parallel', '''
let^pair=f^ {return^ 2,3}
var^a,b=pair()
var^c,d=4,5
def^ A {step=p^ {a+=1 b+=2 c+=3 d+=4}}
A.step()
print^(a,b,c,d)
''', '3 5 7 9\n')
    positive('entry_let_read', '''
let^n=7
let^items=i32^[2]{2,3}
let^show=f^ {n}
def^ A {
    self^{i=1}
    get=f^self^ {n+items[0]+self^.i}
    indirect=f^ {show()}
    view=f^ {items[..]}
}
print^(A{}.get(),A.indirect(),A.view()[1])
''', '10 7 3\n')
    positive('entry_reference_binding', '''
var^n=1
let^r=ref^n
def^ A {step=p^ {*r+=2}}
A.step()
print^(n)
''', '3\n')
    negative('entry_method_indirect_flex_borrow', '''
var^items=i32^[flex^]{1,2}
let^view=ref^items
def^ A {first=f^ {ref^view[0]}}
do^{let^r=A.first() items.clear() print^(*r)}
''', 'may refer into it')
    negative('entry_method_stored_flex_borrow', '''
var^items=i32^[flex^]{1,2}
let^r=ref^items[0]
def^ A {clear=p^ {items.clear()}}
A.clear()
print^(*r)
''', 'may refer into it')
    negative('entry_method_pure_write', '''
var^count=0
def^ A {self^{} bad=f^self^ {count+=1}}
A{}.bad()
''', 'f^ cannot mutate')
    negative('entry_static_pure_write', '''
var^count=0
def^ A {bad=f^ {count+=1}}
A.bad()
''', 'f^ cannot mutate')
    negative('entry_method_let_slice', '''
let^items=i32^[2]{1,2}
def^ A {view=f^ {items[..]}}
A.view()[0]:=3
''', 'read^')
    negative('entry_static_let_write', '''
let^n=1
def^ A {bad=p^ {n+=1}}
A.bad()
''', "cannot modify let^ binding 'n'")
    negative('entry_instance_let_write', '''
let^items=i32^[2]{1,2}
def^ A {self^{} bad=p^self^ {items[0]:=3}}
A{}.bad()
''', "cannot modify let^ binding 'items'")
    negative('entry_method_let_borrow', '''
let^items=i32^[2]{1,2}
def^ A {bad=p^ {let^r=ref^items print^r[0]}}
A.bad()
''', "requires var^, found let^ 'items'")
    negative('entry_method_let_mutable_self', '''
def^ B {self^{n=0} set=p^self^ {self^.n=1}}
let^b=B{}
def^ A {bad=p^ {b.set()}}
A.bad()
''', "cannot modify let^ binding 'b'")
    negative('entry_method_flex_borrow', '''
var^items=i32^[flex^]{1,2}
def^ A {clear=p^ {items.clear()}}
let^r=ref^items[1]
A.clear()
print^(*r)
''', "while 'r' may refer into it")
    negative('entry_method_export', '''
var^n=0
def^ A {get=f^ {n}}
exportC^let^get=f^ ->i32^ {A.get()}
''', 'exported function cannot access task-owned static variables')
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
    positive('static_read_borrow', '''
def^ A {var^n=0}
let^bad=p^r:ref^read^i32^ {A.n+=1 print^(*r)}
bad(ref^A.n)
''', '1\n')
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

    def entry_initialization_guard():
        source = suite.source('entry_initialization_guard', '''
def^ A {step=p^ {count+=1}}
A.step()
var^count=0
''')
        if suite.args.checks_only:
            suite.run([suite.compiler,'check',source])
        else:
            for opt in suite.opts:
                exe = suite.work/f'entry_initialization_guard{opt}.exe'
                suite.run([suite.compiler,'build',source,'-o',exe,'--clang',suite.clang,opt])
                result = suite.run([exe],False)
                assert 'static variable used before initialization' in result.stderr, result.stderr
    suite.case('entry_initialization_guard', entry_initialization_guard)
    suite.finish()


if __name__ == '__main__':
    main()
