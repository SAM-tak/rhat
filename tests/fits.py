"""Type conformance, branch narrowing, static specialization, and alias safety."""
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

    def run(command, success=True):
        p = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=90)
        assert (p.returncode == 0) == success, (command, p.stdout, p.stderr)
        return p

    def source(name, text):
        path = work / (name + '.rh')
        path.write_text(text, encoding='utf-8')
        return path

    def positive(name, text, expected, failure=False, inspect=None):
        nonlocal passed
        if args.filter and args.filter not in name:
            return
        path = source(name, text)
        for opt in ('-O0', '-O2'):
            exe = work / (name + opt + '.exe')
            rir = exe.with_suffix('.rir')
            run([compiler, 'build', path, '-o', exe, '--clang', clang, opt, '--dump-ir', rir])
            if inspect:
                inspect(rir.read_text(), Path(str(exe)+'.ll').read_text())
            p = run([exe], not failure)
            if failure:
                assert expected in p.stderr, (name, opt, p.stderr)
            else:
                assert p.stdout == expected, (name, opt, p.stdout, expected)
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

    def static_ir(rir, llvm):
        assert 'union.is ' not in rir and 'call void @rh_union_fail' not in llvm

    def narrowed_ir(rir, llvm):
        assert 'union.is ' in rir and 'proof=flow' in rir
        assert 'call void @rh_union_fail' not in llvm

    positive('fits_static', '''
print(1 fits^ i32^,1 fits^ string^,nil^ fits^ nil^)
def^ A {self^{value:i32^=7}}
def^ B {self^{value:i32^=7}}
let^ a=A.new()
print(a fits^ A,a fits^ B,a fits^ A|nil^)
let^ read=template^f^ x {if^ x fits^ A: x.value el^: 0;}
print(read(a),read(1))
let^ early=template^f^ x {if^ x fits^ A {return^ x.value} return^ 0}
print(early(a),early(1))
''', 'true false true\ntrue false true\n7 0\n7 0\n', inspect=static_ir)

    positive('fits_side_effects', '''
let^ make=p^ count:&i32^ ->i32^ {*count+=1 return^ 42}
var^ count=0
if^ make(&count) fits^ string^ {print("wrong") else^: print(count)}
print(make(&count) fits^ i32^,count)
print(if^ make(&count) fits^ i32^: count el^: 0;)
print(count)
''', '1\ntrue 2\n3\n3\n', inspect=static_ir)

    positive('fits_union_branches', '''
def^ A {self^{a:i32^=7}}
def^ B {self^{b:i32^=9}}
let^ read=p^ x:A|B|nil^ {
 if^ x fits^ A {print(x.a)
 elseif^ x fits^ B: print(x.b)
 else^: print(x fits^ nil^)}
}
read(A.new()) read(B.new()) read(nil^)
''', '7\n9\ntrue\n', inspect=narrowed_ir)

    positive('fits_numeric_union', '''
let^ twice=f^ x:i32^|i64^ ->i64^ {
 if^ x fits^ i32^: i64^(x+1) el^: x+2;
}
print(twice(3),twice(i64^(5)))
let^ isInt=f^ x:i32^|i64^|string^ {x fits^ i32^|i64^}
print(isInt(3),isInt(i64^(5)),isInt("x"))
''', '4 7\ntrue true false\n', inspect=narrowed_ir)

    positive('fits_named_sums', '''
errordef^ Error {Missing,Invalid}
def^ Result = i32^ | Error.Missing | Error.Invalid
let^ show=p^ value:Result {
 if^ value fits^ Error {print(value fits^ Error.Missing)
 else^: print(value+1)}
}
show(3) show(error^Error.Missing{}) show(error^Error.Invalid{})
''', '4\ntrue\nfalse\n')

    positive('fits_false_subset', '''
let^ test=p^ x:i32^|i64^|nil^ {
 if^ x fits^ i32^|nil^ {
   if^ x? {print(x+1) else^: let^ empty:nil^=x print(empty fits^ nil^)}
 else^: print(x+2)
 }
}
test(1) test(i64^(7)) test(nil^)
''', '2\n9\ntrue\n')

    positive('fits_logical', '''
def^ A {self^{a:i32^=7}}
let^ read=f^ x:A|nil^ {
 if^ !(x fits^ nil^) and^ x.a>0: x.a el^: 0;
}
let^ reverse=f^ x:A|nil^ {if^ x fits^ nil^ or^ x.a<0: 0 el^: x.a;}
print(read(A.new()),read(nil^),reverse(A.new()),reverse(nil^))
print(1 fits^ string^ and^ missing(),1 fits^ i32^ or^ missing())
''', '7 0 7 0\nfalse true\n')

    positive('fits_branch_mutations', '''
def^ A {self^{a:i32^=7}}
def^ B {self^{b:i32^=9}}
let^ read=p^ x:A|B {
 if^ x fits^ A {x:=B.new() print(x fits^ B)
 else^: print(x.b)}
}
read(A.new()) read(B.new())
''', 'true\n9\n')

    positive('fits_member_paths', '''
def^ A {self^{a:i32^=7}}
def^ B {self^{b:i32^=9}}
def^ H {self^{item:A|B|nil^=nil^}}
var^ h=H{item=A.new()}
if^ h.item fits^ A {print(h.item.a)}
var^ ref=&h
if^ ref.item fits^ A {print(ref.item.a)}
h.item:=B.new()
if^ h.item fits^ A {print(h.item.a)
elseif^ h.item fits^ B: print(h.item.b)
else^: print("none")}
''', '7\n7\n9\n')

    positive('fits_reference_and_arrays', '''
def^ A {self^{a:i32^=7}}
var^ a=A.new()
var^ ref:&A|nil^=&a
if^ ref fits^ &A {ref.a+=1 print(ref.a)}
var^ array:i32^[2]=[3,4]
let^ view:&i32^[2]|nil^=&array
if^ view fits^ &i32^[2] {print(view[1])}
print(array fits^ i32^[2],array fits^ i32^[3])
var^ owned:i32^[>..]=[]
print(owned fits^ i32^[>..])
''', '8\n4\ntrue false\ntrue\n')

    positive('fits_parent_reference_survives_field_write', '''
def^ A {self^{a:i32^=7}}
def^ B {self^{b:i32^=9}}
def^ H {self^{item:A|B=A.new(),extra:i32^=42}}
let^ change=p^ x:&H|nil^ {
 if^ x fits^ &H {
   if^ x.item fits^ A {x.item:=B.new()}
   return^ x.extra
 }
 return^ 0
}
var^ h=H.new()
print(change(&h),change(nil^),h.item fits^ B)
''', '42 0 true\n')

    positive('fits_composition', '''
def^ Base {self^{value:i32^=7}}
def^ Derived:Base {self^{extra:i32^=9}}
def^ Other {self^{value:i32^=7}}
let^ x=Derived.new()
print(x fits^ Base,x fits^ Other)
var^ choice:Derived|Other=x
if^ choice fits^ Base {print(choice.extra) else^: print(choice.value)}
''', 'true false\n9\n')

    positive('fits_chain', '''
let^ next=p^ count:&i32^ ->i32^ {*count+=1 return^ *count}
var^ count=0
print(0<next(&count) fits^ i32^<2,count)
print(0>next(&count) fits^ i32^<next(&count),count)
print(next(&count) fits^ string^<next(&count),count)
''', 'true 1\nfalse 2\nfalse 3\n')

    positive('fits_match', '''
def^ A {self^{a:i32^=7}}
def^ B {self^{b:i32^=9}}
let^ read=p^ x:A|B|nil^ {
 for^ x {
 when^ fits^ A: print(it^.a)
 when^ fits^ B: print(it^.b)
 other^: print(it^ fits^ nil^)
 }
}
read(A.new()) read(B.new()) read(nil^)
let^ compute=f^ x:A|B|nil^ {for^ x: when^ fits^ A: it^.a when^ fits^ B: it^.b other^: 0;}
print(compute(A.new()),compute(B.new()),compute(nil^))
let^ focus=f^ x:A|B {for^ let^ r=x: when^ fits^ A: r.a other^: r.b;}
print(focus(A.new()),focus(B.new()))
''', '7\n9\ntrue\n7 9 0\n7 9\n')

    positive('fits_loop', '''
var^ x:i32^|nil^=3
repeat^ while^ x fits^ i32^ {print(x) x:=nil^}
var^ y:i32^|string^=5
repeat^ until^ y fits^ string^ {print(y) y:="done"}
let^ run=template^p^ value {
 repeat^ while^ value fits^ string^ {print(value.len) break^}
}
run(1) run("abc")
''', '3\n5\n3\n')

    positive('fits_loop_sections', '''
var^ x:i32^|nil^=3
repeat^ while^ x fits^ i32^ {
 first^: let^ start=x+1
 main^: print(start,x) x:=nil^
 last^: print(x fits^ nil^)
 epilog^: print(x?)
}
''', '4 3\ntrue\nfalse\n')

    positive('fits_nil_else_proof', '''
let^ test=p^ x:i32^|nil^ {
 if^ x? {print(x fits^ i32^)
 else^: let^ empty:nil^=x print(empty fits^ nil^)}
}
test(1) test(nil^)
''', 'true\ntrue\n')

    prefix = 'def^ A {self^{a:i32^=7}}\ndef^ B {self^{b:i32^=9}}\n'
    cases = [
        ('fits_unknown_type','print(1 fits^ Missing)','unknown type'),
        ('fits_value_rhs','let^ value=1 print(1 fits^ value)','unknown type'),
        ('fits_void_rhs','print(1 fits^ void^)','requires a value type'),
        ('fits_void_lhs','let^ noop=p^ {} print(noop() fits^ nil^)','requires a value type'),
        ('fits_wrong_branch','var^ x:A|B=A.new() if^ x fits^ A {print(x.b)}','has no field'),
        ('fits_assignment_invalidates','var^ x:A|B=A.new() if^ x fits^ A {x:=B.new() print(x.a)}','field access requires'),
        ('fits_call_invalidates','let^ clear=p^ x:&(A|B) {*x:=B.new()} var^ x:A|B=A.new() if^ x fits^ A {clear(&x) print(x.a)}','field access requires'),
        ('fits_condition_invalidates','let^ clear=p^ x:&(A|B) ->bool^ {*x:=B.new() return^ true^} var^ x:A|B=A.new() if^ x fits^ A and^ clear(&x) {print(x.a)}','field access requires'),
        ('fits_path_assignment_invalidates','def^ H {self^{item:A|B=A.new()}} var^ h=H.new() if^ h.item fits^ A {h.item:=B.new() print(h.item.a)}','field access requires'),
        ('fits_path_alias_invalidates','def^ H {self^{item:A|B=A.new()}} let^ clear=p^ h:&H {h.item:=B.new()} var^ h=H.new() let^ ref=&h if^ ref.item fits^ A {clear(&h) print(ref.item.a)}','field access requires'),
        ('fits_path_borrow_invalidates','def^ H {self^{item:A|B=A.new()}} let^ clear=p^ x:&(A|B) {*x:=B.new()} var^ h=H.new() if^ h.item fits^ A {clear(&h.item) print(h.item.a)}','field access requires'),
        ('fits_method_invalidates','def^ H {self^{item:A|B=A.new()} clear=p^ self^ {self^.item:=B.new()}} var^ h=H.new() if^ h.item fits^ A {h.clear() print(h.item.a)}','field access requires'),
        ('fits_push_alias_invalidates','def^ H {self^{item:A|B=A.new()}} var^ a:H[>..]=[H.new()] let^ ref=&a[0] if^ ref.item fits^ A {a.clear() a.push(H{item=B.new()}) print(ref.item.a)}','field access requires'),
        ('fits_append_alias_invalidates','def^ H {self^{item:A|B=A.new()}} var^ a:H[>..]=[H.new()] var^ b:H[1]=[H{item=B.new()}] let^ ref=&a[0] if^ ref.item fits^ A {a.clear() a.append(b.slice(0,1)) print(ref.item.a)}','field access requires'),
        ('fits_match_original_invalidates','let^ change=p^ x:&(A|B) ->bool^ {*x:=B.new() return^ false^} var^ x:A|B=A.new() for^ x {when^ fits^ A: x:=B.new() print(x.a) other^: print(0)}','field access requires'),
        ('fits_index_not_narrowed','var^ items:(A|B)[1]=[A.new()] if^ items[0] fits^ A {print(items[0].a)}','field access requires'),
        ('fits_call_not_narrowed','let^ value=f^ ->A|B {A.new()} if^ value() fits^ A {print(value().a)}','field access requires'),
        ('fits_chain_not_narrowed','var^ x:A|nil^=A.new() if^ x fits^ A fits^ A {print(x.a)}','field access requires'),
        ('fits_loop_lost_fact','var^ x:A|B=A.new() if^ x fits^ A {repeat^ 2 {print(x.a) x:=B.new()}}','field access requires'),
        ('fits_path_loop_lost_fact','def^ H {self^{item:A|B=A.new()}} var^ h=H.new() if^ h.item fits^ A {repeat^ 2 {print(h.item.a) h.item:=B.new()}}','field access requires'),
        ('fits_loop_exit','var^ x:A|B=A.new() repeat^ while^ x fits^ A {break^} print(x.a)','field access requires'),
        ('fits_loop_epilog','var^ x:A|B=B.new() repeat^ while^ x fits^ A {main^: break^ epilog^: print(x.a)}','field access requires'),
        ('fits_borrow_projection','var^ x:A|B=A.new() if^ x fits^ A {let^ ref=&x.a}','cannot borrow a temporary'),
        ('fits_borrow_escape','let^ bad=f^ ->&A {var^ a=A.new() let^ x:&A|nil^=&a if^ x fits^ &A {return^ x} return^ &a} let^ value=bad()','cannot return a reference'),
    ]
    for name, text, diagnostic in cases:
        negative(name, prefix+text, diagnostic)
    print(f'{passed} fits cases passed; executable cases checked at -O0 and -O2.', flush=True)


if __name__ == '__main__':
    main()

