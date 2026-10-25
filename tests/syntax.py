"""L^ syntax alignment, evaluation order, and ownership regressions."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--filter', default='')
    parser.add_argument('--checks-only', action='store_true')
    args = parser.parse_args()
    compiler, clang = str(Path(args.compiler).resolve()), str(Path(args.clang).resolve())
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    passed = 0

    def run(command, success=True):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=90)
        assert (result.returncode == 0) == success, (command, result.stdout, result.stderr)
        return result

    def source(name, text):
        path = work / (name + '.rh')
        path.write_text(text, encoding='utf-8')
        return path

    def positive(name, text, expected, versioned=False):
        nonlocal passed
        if args.checks_only or (args.filter and args.filter not in name): return
        path = source(name, text)
        for opt in ('-O0', '-O2'):
            exe = work / (name + opt + '.exe')
            run([compiler, 'build', path, '-o', exe, '--clang', clang, opt])
            actual = run([exe]).stdout
            assert actual == expected, (name, opt, actual, expected)
            if versioned:
                assert '; range.guard result=' in Path(str(exe)+'.ll').read_text(), name
        passed += 1
        print('PASS', name, flush=True)

    def negative(name, text, diagnostic):
        nonlocal passed
        if args.filter and args.filter not in name: return
        result = run([compiler, 'check', source(name, text)], False)
        assert diagnostic in result.stderr, (name, diagnostic, result.stderr)
        passed += 1
        print('PASS', name, flush=True)

    positive('branches', '''
let^ choose=f^ x:i32^ {
 if^ x=0 { return^ 10
 elseif^ x=1: return^ 11
 else^if^ x=2: return^ 12
 elsif^ x=3: return^ 13
 elif^ x=4: return^ 14
 ei^ x=5: return^ 15
 el^ x=6: return^ 16
 else^: return^ 17
 }
}
for^ i from^ 0 to^ 7 { print(choose(i)) }
print(if^ false^: 1 elseif^ true^: 2 else^: 3;)
''', '10\n11\n12\n13\n14\n15\n16\n17\n2\n')

    positive('void_return_clauses', '''
let^ exit=p^ choice:bool^ { if^ choice { return^ else^: print(1) } }
exit(true^) exit(false^)
let^ early=p^ { repeat^ 1 { pre^: return^ main^: print(99) epilog^: print(98) } }
early()
''', '1\n')
    positive('return_eof', 'return^', '')

    positive('assignments', '''
let^ index=p^ calls:&i32^ ->i64^ { *calls+=1 return^ 0 }
let^ change=p^ a:&i32^[2] ->i32^ { a[0]:=100 return^ 3 }
var^ calls=0
var^ a:i32^[2]=[10,20]
a[index(&calls)] += change(&a)
print(a[0],calls)
a[0],a[1] := a[1],a[0]
print(a[0],a[1])
var^ x,y=5,7
x,y += y,x
x-=2 x*=3 x/=2 x%=7
print(x,y)
{ let^ x,y=40,x print(x,y) }
var^ z:=9
print(z)
''', '13 1\n20 13\n1 12\n40 1\n9\n')

    positive('comparison_chains', '''
let^ tick=p^ calls:&i32^ ->i32^ { *calls+=1 return^ *calls }
var^ calls=0
print(0 < tick(&calls) < 2)
print(calls)
print(10 < tick(&calls) < tick(&calls))
print(calls)
print(1 <= 2 = 2 != 3)
print(1 ≦ 2 and^ 3 ≥ 2 and^ 2 ≠ 3 and^ 1 =/ 2)
''', 'true\n1\nfalse\n2\ntrue\ntrue\n')

    positive('parallel_owned_result', '''
let^ make=f^ ->i32^[>..] {
 var^ n,data:i32^[>..]=7,[1,2]
 data.push(n)
 return^ data
}
var^ values=make()
let^ _^,answer=make(),42
print(values.len,values[2],answer)
''', '3 7 42\n')

    positive('loops', '''
var^ total=0
for^ var^ i=0 while^ i<5 next^ i+=1 { if^ i=2 { next^ } total+=i }
print(total)
for^ i from^ 2 to^ 8 step^ 3 { print(i) }
for^ i:i8^ from^ 126 to^ 127 { print(i) }
for^ i:u8^ from^ 2 downto^ 0 { print(i) }
for^ 4 to^ 5 { print(it^) }
repeat^ 3 { total+=1 }
print(total)
repeat^ { repeat^ { break^^ } }
var^ n=0
repeat^ until^ n=3 { n+=1 }
repeat^ while^ n<4 { n+=1 }
print(n)
var^ a=0 var^ b=0
for^ a:=0 while^ a<2 next^ a+=1,b+=2 { }
print(a,b)
for^ var^ x=0 for^ var^ y=0 while^ x<2 next^ x+=1,y+=1 { print(x,y) }
for^ var^ c=0 for^ var^ d=0 while^ c<3 next^ c,d+=1,2 { print(c,d) }
for^ var^ e=0 while^ e<1 next^ e+=1,print(e) {}
''', '8\n2\n5\n8\n126\n127\n2\n1\n0\n4\n5\n11\n4\n2 4\n0 0\n1 1\n0 0\n1 2\n2 4\n1\n')

    positive('counted_extrema', '''
for^ i:u128^ from^ 340282366920938463463374607431768211454 to^ 340282366920938463463374607431768211455 { print(i) }
for^ i:i128^ from^ -170141183460469231731687303715884105728 to^ -170141183460469231731687303715884105727 { print(i) }
for^ i:i8^ from^ -127 to^ 127 step^ 127 { print(i) }
for^ i from^ 9 to^ 1 { print(99) }
for^ i from^ 1 downto^ 9 { print(98) }
''', '340282366920938463463374607431768211454\n340282366920938463463374607431768211455\n-170141183460469231731687303715884105728\n-170141183460469231731687303715884105727\n-127\n0\n127\n')

    positive('for_range_proof', '''
var^ values:i32^[4]=[1,2,3,4]
var^ total=0
for^ var^ i:i64^=0 while^ i<4 next^ i+=1 {total+=values[i]}
print(total)
''', '10\n', versioned=True)

    positive('sections', '''
var^ calls=0
for^ var^ i=0 while^ i<3 next^ i+=1 {
prolog^: print(10)
pre^: var^ temp:i32^[>..]=[] temp.push(i) calls+=1
first^: var^ kept:i32^[>..]=[] kept.push(temp[0])
main^: kept.push(i) print(temp[0])
last^: print(i) print(kept.len)
epilog^: print(20)
}
print(calls)
repeat^ 0 { pre^: print(30) first^: print(99) last^: print(98) epilog^: print(40) }
repeat^ 3 { main^: var^ tmp:i32^[>..]=[] tmp.push(1) break^ last^: print(50) epilog^: print(60) }
repeat^ 2 { first^: let^ a,b=1,2 main^: print(a,b) last^: print(a+b) }
''', '10\n0\n1\n2\n2\n4\n20\n4\n30\n40\n50\n60\n1 2\n1 2\n3\n')

    positive('jumps_cleanup', '''
var^ hits=0
for^ var^ i=0 while^ i<3 next^ i+=1 {
main^:
 repeat^ 2 {
 main^: var^ owned:i32^[>..]=[] owned.push(i) hits+=1 continue^[2]
 last^: print(99)
 epilog^: print(98)
 }
last^: print(i)
epilog^: print(hits)
}
repeat^ 2 {
main^: repeat^ 2 { main^: break^[2] last^: print(97) epilog^: print(96) }
last^: print(1)
epilog^: print(2)
}
let^ early=p^ ->i32^ {
 repeat^ 2 { first^: var^ a:i32^[>..]=[] main^: a.push(42) return^ a[0] last^: print(95) epilog^: print(94) }
 return^ 0
}
print(early())
''', '2\n3\n1\n2\n42\n')

    positive('section_paths', '''
repeat^ 2 { print(1) epilogue^: print(2) }
repeat^ 0 { prologue^: print(3) premain^: print(4) epilogue^: print(5) }
repeat^ 1 {
main^:
 repeat^ 1 { pre^: break^^ first^: var^ a:i32^[>..]=[] main^: a.push(0) }
last^: print(6)
epilog^: print(7)
}
repeat^ 1 {
pre^: break^
first^: var^ a:i32^[>..]=[]
main^: a.push(99)
last^: print(98)
epilog^: print(8)
}
let^ early=p^ ->i32^ { repeat^ 1 { pre^: return^ 9 main^: print(97) } return^ 0 }
print(early())
repeat^ 1 {first^: let^ _^,_^=1,2 main^: print(10)}
let^ limit:i64^=2
for^ i from^ 1 to^ limit { print(i) }
var^ negative=-2
for^ i from^ 0 to^ 2 step^ -negative { print(i) }
''', '1\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n1\n2\n0\n2\n')

    positive('value_matches', '''
let^ classify=f^ x:i32^ { for^ x: when^ 0: 10 when^ 1 to^ 3,7: 20 other^: 30; }
print(classify(0),classify(2),classify(7),classify(9))
for^ 4 { when^ 0,1: print(99) when^ 2 to^ 5: print(it^) other^: print(98) }
for^ let^ value=2 { when^ 2: print(value) }
print(for^ false^: when^ true^: 1 when^ false^: 2;)
print(for^ let^ n=4 if^ n>2: n*2 el^: 0;)
print(for^ let^ n=5 do^: n+1;)
for^ let^ n=2 if^ n>1 { print(n) else^: print(0) }
''', '10 20 20 30\n4\n2\n2\n8\n6\n2\n')

    positive('lexical_scopes', '''
var^ x=1
{ var^ x=2 { var^ x=3 print($x,$$x,$$$x,$^x,$^^x) $^^x+=10 } }
print(x)
let^ select=f^ x:i32^ { { let^ x:i64^=9 return^ $^x } }
print(select(42))
for^ 1 to^ 2 { for^ 3 to^ 4 { print(it^,it^^) } }
let^ _^=1 let^ _^=2 let^ _^,y,_^=3,4,5
_^,x := 0,y
print(x)
let^ ignored=p^ _^:i32^,_^:i64^ { print(42) }
ignored(1,2)
''', '1 2 3 2 1\n11\n42\n3 1\n4 1\n3 2\n4 2\n4\n42\n')

    positive('hidden_names', '''
let^ `@counter1`=99
for^ i from^ 0 to^ 1 { print(`@counter1`) }
let^ `@match2`=88
print(for^ 1: when^ 1: `@match2` other^: 0;)
''', '99\n99\n88\n')

    positive('literals', r'''
let^ 変数=0xFF+0o10+0b11+1_000
let^ `not a name`=7 let^ `a``b`=8
print(変数,`not a name`,`a``b`)
let^ s="A\0B\x43\u{1f600}"
print(s.len,s[1],s[2])
var^ bytes:u8^[>..]=[] bytes.append("a\0b")
print(bytes.len,bytes[1],bytes[2])
print('a''b')
print("x
z")
let^ raw="""a\b
print(raw)
print(1.2_5e+1)
print("or^","==","=/")
''', "1266 7 8\n8 0 66\n3 0 98\na'b\nx\nz\na\\b\n12.5\nor^ == =/\n")

    source('syntaxlib', '''module^ syntaxlib
public^ let^ left,right=20,22
public^ let^ select=template^f^ value { for^ value: when^ 1: left+right other^: 0; }
''')
    positive('module_syntax', 'import^ syntaxlib\nprint(syntaxlib.select(1))', '42\n')
    source('discardlib', 'module^ discardlib\nlet^ _^=print(1)')

    cases = [
        ('old_else', 'if^ true^ {} else^ {}', 'else^'),
        ('old_while', 'while^ false^ {}', 'driving clause'),
        ('bare_record', 'def^ P {self^{x:i32^}} let^ p:P={x=1}', 'bare braces'),
        ('let_colon_equal', 'let^ x:=1', "expected '='"),
        ('immutable_compound', 'let^ x=1 x+=1', 'cannot modify let^'),
        ('readonly_focus', 'for^ i from^ 1 to^ 2 {i+=1}', 'cannot modify let^'),
        ('missing_match_default', 'let^ x=for^ 1: when^ 1: 2;', 'requires other^:'),
        ('missing_if_default', 'let^ x=if^ true^: 1;', 'requires a final'),
        ('jump_outside', 'break^', 'jump depth'),
        ('jump_too_far', 'repeat^ { next^^ }', 'jump depth'),
        ('zero_jump_depth', 'repeat^ { break^[0] }', 'must be positive'),
        ('double_jump_depth', 'repeat^ { break^^[2] }', 'not both'),
        ('zero_step', 'for^ i from^ 1 to^ 2 step^ 0 {}', 'must be positive'),
        ('negative_step', 'for^ i from^ 1 to^ 2 step^ -1 {}', 'must be positive'),
        ('float_count', 'repeat^ 1.5 {}', 'integer type'),
        ('counted_update', 'for^ i from^ 1 to^ 2 next^ i+=1 {}', 'explicit update'),
        ('update_jump', 'for^ var^ i=0 while^ i<2 next^ next^ {}', 'assignment or call'),
        ('missing_driver', 'for^ var^ i=0 { print(i) }', 'driving clause'),
        ('focusless_for', 'for^ while^ false^ {}', 'requires a focus'),
        ('immutable_loop_focus', 'for^ let^ i=0 while^ i<2 {}', 'requires var^'),
        ('double_driver', 'repeat^ 2 while^ false^ {}', 'another driving clause'),
        ('section_order', 'repeat^ 1 {main^: print(1) first^: print(2)}', 'sections must follow'),
        ('section_duplicate', 'repeat^ 1 {main^: main^:}', 'sections must follow'),
        ('section_no_body', 'repeat^ 1 {prolog^: print(1)}', 'requires pre^'),
        ('first_zero_use', 'repeat^ 0 {first^: let^ x=1 epilog^: print(x)}', 'not definitely initialized'),
        ('first_borrow_pre', 'repeat^ 1 {pre^: var^ x=1 first^: let^ p=&x main^: print(*p)}', 'outlive'),
        ('scope_escape', 'let^ p=for^ var^ x=1 do^: &x;', 'local owner'),
        ('scope_too_far', 'let^ x=1 print($^x)', 'outside'),
        ('scope_space', 'let^ x=1 print($ x)', 'adjacent'),
        ('discard_read', 'let^ _^=1 print(_^)', 'cannot be read'),
        ('discard_compound', '_^+=1', 'compound assignment'),
        ('duplicate_parallel', 'let^ x,x=1,2', 'duplicate binding'),
        ('parallel_type_collision', 'def^ P {} let^ P,x=1,2', 'conflicts with def^'),
        ('parallel_import_collision', 'import^ std.random let^ std,x=1,2', 'conflicts with import'),
        ('parallel_count', 'var^ x,y=1', 'does not match'),
        ('duplicate_first', 'repeat^ 1 {first^: let^ x=1 let^ x=2 main^:}', 'duplicate binding'),
        ('prolog_self_jump', 'repeat^ 1 {prolog^: break^ main^:}', 'own loop'),
        ('epilog_self_jump', 'repeat^ 1 {main^: epilog^: next^}', 'own loop'),
        ('first_partial_jump', 'repeat^ 1 {first^: break^ let^ x=1 main^:}', 'before all first^'),
        ('elseif_default', 'if^ false^ { elseif^: print(1) }', 'requires a condition'),
        ('declared_hat', 'let^ custom^=1', 'cannot be declared'),
        ('c_equality', 'print(1==1)', 'C-style'),
        ('c_and', 'print(true^ && false^)', "expected ')'"),
        ('empty_quote', 'let^ ``=1', 'empty or unterminated'),
        ('quote_newline', 'let^ `a\nb`=1', 'newline'),
        ('quote_control', 'let^ `\x01counter1`=1', 'control character'),
        ('module_discard_call', 'import^ discardlib', 'scalar literal operations'),
        ('bad_base', 'print(0b102)', 'numeric literal'),
        ('bad_separator', 'print(1__0)', 'separator'),
        ('bad_unicode', r'print("\u{d800}")', 'Unicode scalar'),
        ('bad_escape', r'print("\x0q")', 'two digits'),
        ('call_newline', 'print\n(1)', 'unknown value'),
    ]
    for name, text, diagnostic in cases:
        negative(name, text, diagnostic)

    for opt in (() if args.checks_only or (args.filter and args.filter not in 'dynamic_step') else ('-O0', '-O2')):
        path = source('dynamic_step', 'var^ step=0 for^ i from^ 1 to^ 0 step^ step {}')
        exe = work / ('dynamic_step' + opt + '.exe')
        run([compiler, 'build', path, '-o', exe, '--clang', clang, opt])
        assert 'assert' in run([exe], False).stderr
    if not args.checks_only and (not args.filter or args.filter in 'dynamic_step'): passed += 1
    print(f'{passed} syntax cases passed; executable cases checked at -O0 and -O2.', flush=True)


if __name__ == '__main__':
    main()
