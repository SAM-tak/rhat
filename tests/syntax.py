"""L^ syntax alignment, evaluation order, and ownership regressions."""
from harness import Suite


def main():
    suite = Suite('syntax')
    source, positive, negative = suite.source, suite.positive, suite.negative

    def versioned(rir, ll):
        assert '; range.guard result=' in ll

    positive('ref_keyword', '''
var^ n=7
var^ r:ref^ i32^=ref^n
var^ rr:ref^^ i32^=ref^r;
**rr:=9
let^ rrr:ref^^^ i32^=ref^rr
print(***rrr)
let^ read=f^ p:ref^ i32^ ->i32^ {*p}
var^ refs:(ref^ i32^)[2]=[ref^ n,r]
let^ all:ref^ (ref^ i32^)[2]=ref^ refs
print(read(ref^ n),*all[0],*refs[1],n*2)
def^ Box {self^{value:i32^=1} get=f^ self^ {self^.value}}
var^ box=Box{}
let^ b=ref^ box
b.value:=3
print(b.value,b.get())
var^ values:i32^[2]=[4,5]
let^ a=ref^ values
print(a[1],a.len)
let^ ref=11
print(ref)
''', '9\n9 9 9 18\n3 3\n5 2\n11\n')

    positive('contextual_records', '''
def^ Point {self^{x:i32^=10,y:i32^=20}}
def^ Outer {self^{point:Point}}
let^ show=p^ x:Point {print(x.x,x.y)}
show({x=1,y=2})
show({})
let^ a:Point={x=3}
show(a)
let^ nested:Outer={point={x=4,y=5}}
show(nested.point)
let^ make=f^ ->Point {return^ {x=6,y=7}}
let^ implicit=f^ ->Point {{x=8,y=9}}
show(make()) show(implicit())
var^ items:Point[2]=[{x=11},{y=12}]
show(items[0]) show(items[1])
let^ optional:Point|nil^={x=13,y=14}
print(optional?.x??0)
let^ flag=f^ x:Point ->bool^ {x.x>0}
if^ flag({x=1}) {print(99)}
let^ nestedFlag=f^ item:Outer {item.point.x>0}
if^ nestedFlag({point=Point{x=1}}) {print(88)}
''', '1 2\n10 20\n3 20\n4 5\n6 7\n8 9\n11 20\n10 12\n13\n99\n88\n')

    positive('do_blocks', '''
var^ x=1
do^{}
do^{var^ x=2 print(x) do^{var^ x=3 print(x)}}
print(x)
let^ early=p^ ->i32^ {do^{return^ 7}}
print(early())
repeat^ 1 {if^ true^ {do^{next^}} print(99)}
''', '2\n3\n1\n7\n')

    positive('contextual_owned_records', '''
def^ Bag {self^{tag:i32^=0,items:grow^ i32^[]}}
let^ make=f^ ->Bag {return^ {items=[1,2],tag=7}}
var^ bag=make()
print(bag.tag,bag.items[1])
let^ read=f^ b:ref^ Bag ->i32^ {b.tag}
print(read(ref^ bag))
''', '7 2\n7\n')

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

    positive('outer_this', '''
print(f^ n:i64^ {
 if^ n<2: 1 el^: n * f^ m:i64^ { this^^(m) }(n-1);
}(6))
let^ walk=f^ n:i32^ ->i32^ {
 if^ n=0 {return^ 0}
 return^ f^ m:i32^ ->i32^ {
   do^{ return^ f^ q:i32^ ->i32^ {this^^^(q-1)}(m) }
 }(n)+1
}
print(walk(5))
print(f^ n:i64^ {
 if^ n<2: 1 el^: f^ m:i64^ {if^ m=0: 0 el^: 1+this^(m-1);}(2)*this^(n-1);
}(4))
''', '720\n5\n8\n')

    positive('outer_this_owned', '''
let^ make=f^ n:i32^ ->grow^ i32^[] {
 if^ n=0 {return^ [7,8]}
 return^ f^ m:i32^ ->grow^ i32^[] {this^^(m-1)}(n)
}
var^ result=make(4)
print(result.len,result[1])
let^ fill=p^ a:ref^ grow^ i32^[],n:i32^ ->i64^ {
 if^ n=0 {return^ a.len}
 return^ p^ b:ref^ grow^ i32^[],m:i32^ ->i64^ {b.push(m) return^ this^^(b,m-1)}(a,n)
}
var^ values:grow^ i32^[]=[]
print(fill(ref^ values,3),values[0],values[2])
''', '2 8\n3 3 1\n')

    positive('outer_this_method', '''
def^ Counter {
 self^{base:i32^=3}
 sum=f^ self^,n:i32^ ->i32^ {
  if^ n=0 {return^ self^.base}
  return^ f^ obj:ref^ Counter,m:i32^ ->i32^ {this^^(obj,m-1)}(self^,n)+1
 }
 current=f^ self^,n:i32^ ->i32^ {if^ n=0: self^.base el^: 1+this^(n-1);}
}
def^ Derived:Counter {self^{marker:i32^=77}}
var^ value=Derived.new()
print(value.sum(4),value.current(2),value.marker)
def^ Mutable {
 self^{count:i32^=0}
 run=p^ self^,n:i32^ ->i32^ {
  if^ n=0 {return^ self^.count}
  self^.count+=1
  return^ p^ obj:ref^ Mutable,m:i32^ ->i32^ {return^ this^^(obj,m-1)}(self^,n)
 }
}
var^ counter=Mutable.new()
print(counter.run(4))
''', '7 5 77\n4\n')

    positive('assignments', '''
let^ index=p^ calls:ref^ i32^ ->i64^ { *calls+=1 return^ 0 }
let^ change=p^ a:ref^ i32^[2] ->i32^ { a[0]:=100 return^ 3 }
var^ calls=0
var^ a:i32^[2]=[10,20]
a[index(ref^ calls)] += change(ref^ a)
print(a[0],calls)
a[0],a[1] := a[1],a[0]
print(a[0],a[1])
var^ x,y=5,7
x,y += y,x
x-=2 x*=3 x/=2 x%=7
print(x,y)
do^{ let^ x,y=40,x print(x,y) }
var^ z:=9
print(z)
''', '13 1\n20 13\n1 12\n40 1\n9\n')

    positive('comparison_chains', '''
let^ tick=p^ calls:ref^ i32^ ->i32^ { *calls+=1 return^ *calls }
var^ calls=0
print(0 < tick(ref^ calls) < 2)
print(calls)
print(10 < tick(ref^ calls) < tick(ref^ calls))
print(calls)
print(1 <= 2 = 2 != 3)
print(1 ≦ 2 and^ 3 ≥ 2 and^ 2 ≠ 3 and^ 1 =/ 2)
''', 'true\n1\nfalse\n2\ntrue\ntrue\n')

    positive('parallel_owned_result', '''
let^ make=f^ ->grow^ i32^[] {
 var^ n,data:grow^ i32^[]=7,[1,2]
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
''', '10\n', inspect=versioned)

    positive('sections', '''
var^ calls=0
for^ var^ i=0 while^ i<3 next^ i+=1 {
prolog^: print(10)
pre^: var^ temp:grow^ i32^[]=[] temp.push(i) calls+=1
first^: var^ kept:grow^ i32^[]=[] kept.push(temp[0])
main^: kept.push(i) print(temp[0])
last^: print(i) print(kept.len)
epilog^: print(20)
}
print(calls)
repeat^ 0 { pre^: print(30) first^: print(99) last^: print(98) epilog^: print(40) }
repeat^ 3 { main^: var^ tmp:grow^ i32^[]=[] tmp.push(1) break^ last^: print(50) epilog^: print(60) }
repeat^ 2 { first^: let^ a,b=1,2 main^: print(a,b) last^: print(a+b) }
''', '10\n0\n1\n2\n2\n4\n20\n4\n30\n40\n50\n60\n1 2\n1 2\n3\n')

    positive('jumps_cleanup', '''
var^ hits=0
for^ var^ i=0 while^ i<3 next^ i+=1 {
main^:
 repeat^ 2 {
 main^: var^ owned:grow^ i32^[]=[] owned.push(i) hits+=1 continue^[2]
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
 repeat^ 2 { first^: var^ a:grow^ i32^[]=[] main^: a.push(42) return^ a[0] last^: print(95) epilog^: print(94) }
 return^ 0
}
print(early())
''', '2\n3\n1\n2\n42\n')

    positive('section_paths', '''
repeat^ 2 { print(1) epilogue^: print(2) }
repeat^ 0 { prologue^: print(3) premain^: print(4) epilogue^: print(5) }
repeat^ 1 {
main^:
 repeat^ 1 { pre^: break^^ first^: var^ a:grow^ i32^[]=[] main^: a.push(0) }
last^: print(6)
epilog^: print(7)
}
repeat^ 1 {
pre^: break^
first^: var^ a:grow^ i32^[]=[]
main^: a.push(99)
last^: print(98)
epilog^: print(8)
}
let^ early=p^ ->i32^ { repeat^ 1 { pre^: return^ 9 main^: print(97) } }
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
do^{ var^ x=2 do^{ var^ x=3 print($x,$$x,$$$x,$^x,$^^x) $^^x+=10 } }
print(x)
let^ select=f^ x:i32^ { do^{ let^ x:i64^=9 return^ $^x } }
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
var^ bytes:grow^ u8^[]=[] bytes.append("a\0b")
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
    source('outerthislib', '''module^ outerthislib
public^ let^ sum=template^f^ n {
 if^ n=0: 0 el^: n+template^f^ m {this^^(m)}(n-1);
}
''')
    positive('outer_this_module', '''import^ outerthislib
let^ wide:i64^=20
print(outerthislib.sum(10),outerthislib.sum(wide))
''', '55 210\n')

    cases = [
        ('ref_repeated_expression', 'var^ x=1 let^ r=ref^^ x', 'cannot borrow a temporary'),
        ('ref_repeated_expanded_expression', 'var^ x=1 let^ r=ref^ ref^ x', 'cannot borrow a temporary'),

        ('old_ref_expression', 'var^ x=1 let^ r=&x', 'expected an expression'),
        ('old_ref_type', 'let^ f=f^ x:&i32^ {*x}', 'expected a name'),
        ('old_double_ref_type', 'let^ f=f^ x:&&i32^ {**x}', 'expected a name'),

        ('outer_this_top', 'this^^()', 'at that nesting depth'),
        ('outer_this_depth', 'print(f^ ->i32^ {this^^()}())', 'at that nesting depth'),
        ('outer_this_sibling', 'let^ first=f^ {1} print(f^ ->i32^ {this^^()}())', 'at that nesting depth'),
        ('outer_this_arity', 'print(f^ n:i32^ ->i32^ {f^ {this^^()}()}(1))', 'expects 1 arguments'),
        ('outer_this_type', 'print(f^ n:i32^ ->i32^ {f^ {this^^("bad")}()}(1))', "requires nominal 'i32^'"),
        ('outer_this_capture', 'print(f^ n:i32^ ->i32^ {f^ {this^^(n-1)}()}(1))', "unknown value 'n'"),
        ('outer_this_effect', 'let^ run=p^ n:i32^ {f^ {this^^(0)}()} run(1)', 'f^ cannot call p^'),
        ('outer_this_local_owner', '''let^ run=f^ n:i32^ ->i32^ {
var^ scratch:grow^ i32^[]=[n]
if^ n=0 {return^ scratch[0]}
return^ f^ m:i32^ ->i32^ {this^^(m-1)}(n)
} print(run(3))''', 'holds local owned value'),
        ('outer_this_receiver', '''def^ A {run=f^ self^,n:i32^ ->i32^ {f^ {this^^(1,0)}()}}
var^ a=A.new() print(a.run(1))''', 'explicit'),
        ('outer_this_mutable_effect', '''def^ A {run=f^ mutable^ self^,n:i32^ ->i32^ {
f^ obj:ref^ A ->i32^ {this^^(obj,0)}(self^)
}} var^ a=A.new() print(a.run(1))''', 'cannot mutate borrowed storage'),
        ('old_else', 'if^ true^ {} else^ {}', 'else^'),
        ('old_while', 'while^ false^ {}', 'driving clause'),
        ('bare_record_no_context', 'let^ p={x=1}', 'requires a single nominal struct'),
        ('bare_block', '{let^ x=1}', 'standalone blocks require do^'),
        ('bare_record_empty_no_context', 'let^ p={}', 'requires a single nominal struct'),
        ('contextual_record_ambiguous', 'def^ A {self^{x:i32^}} def^ B {self^{y:i32^}} let^ a:A|B={x=1}', 'single nominal struct'),
        ('contextual_record_numeric', 'let^ x:i32^={}', 'single nominal struct'),
        ('contextual_record_unknown_field', 'def^ P {self^{x:i32^}} let^ p:P={y=1}', 'unknown field'),
        ('contextual_record_duplicate', 'def^ P {self^{x:i32^}} let^ p:P={x=1,x=2}', 'duplicate field'),
        ('contextual_record_private', 'def^ P {self^{private^ x:i32^=0}} let^ p:P={x=1}', 'private member'),
        ('contextual_record_untyped_template', 'let^ f=template^p^ x {} f({x=1})', 'single nominal struct'),
        ('contextual_record_error_union', 'errordef^ E {Bad} let^ bad:i32^|E.Bad={}', 'contextual construction requires'),
        ('contextual_record_borrow_escape', 'def^ Ref {self^{p:ref^ i32^}} let^ bad=f^ ->Ref {var^ n=1 return^ {p=ref^ n}} let^ v=bad()', 'cannot return a reference'),
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
        ('first_borrow_pre', 'repeat^ 1 {pre^: var^ x=1 first^: let^ p=ref^ x main^: print(*p)}', 'outlive'),
        ('scope_escape', 'let^ p=for^ var^ x=1 do^: ref^ x;', 'local owner'),
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

    suite.runtime_failure('dynamic_step', 'var^ step=0 for^ i from^ 1 to^ 0 step^ step {}', 'assert')
    suite.finish()


if __name__ == '__main__':
    main()
