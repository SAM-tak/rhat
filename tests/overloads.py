"""Callable intersections, disjoint overloads and operator dispatch."""

from harness import Suite


def main():
    suite = Suite('overload')
    run, positive, negative = suite.run, suite.positive, suite.negative
    work = suite.work

    positive('unicode_operator_aliases', '''def^V {
 self^{x:i32^=0,y:i32^=0}
 op^× =f^self^,other:ref^Self^ {self^.x*other.y-self^.y*other.x}
 op^⋅ =f^self^,other:ref^Self^ {self^.x*other.x+self^.y*other.y}
}
let^左=V{x=3,y=4}
let^右=V{x=1,y=2}
print^(左×右,左 cross^右,左⋅右,左 dot^右,2*(左⋅右)+1)
let^一=1 let^二=2
print^(一≦二,一≤二,二≧一,二≥一,一≠二,一=/二)
let^`×`=7
print^("× ⋅ ≦ ≤ ≧ ≥ ≠",`×`)
''', '2 2 11 11 23\ntrue true true true true true\n× ⋅ ≦ ≤ ≧ ≥ ≠ 7\n')

    numeric_overloads = '''let^min=f^a:f32^,b:f32^ {if^a<b:a el^:b;}
& f^a:i64^,b:i64^ {if^a<b:a el^:b;}
'''
    positive('overload_contextual_numeric_literals', numeric_overloads+'''
let^text="hello"
var^message:u8^[grow^]={}
message.append(text.bytes[0 .. min(text.bytes.len,160)])
let^alias=min
let^wide:i64^=200
let^real:f32^=200.0
print^(message.len,min(160,wide),alias(wide,160),min(real,160),min(160,real))
print^(min(wide,-160),min(-160,wide),min(wide,-9223372036854775808))
let^forward=f^n:i64^ {min(160,n)}
print^(forward(200))
let^tick=p^calls:ref^i64^ { *calls+=1 return^i64^(200) }
var^calls:i64^=0
print^(min(tick(ref^calls),160),calls)
let^pick=f^n:i8^ {"small"} & f^n:i64^ {"wide"}
let^signed=f^n:u64^ {"unsigned"} & f^n:i64^ {"signed"}
let^float_pick=f^n:f8^ {"f8"} & f^n:f16^ {"f16"} & f^n:f32^ {"f32"}
let^large=f^n:f32^ {"f32"} & f^n:f64^ {"f64"}
print^(pick(128),pick(-129),signed(-1),float_pick(65520.0),large(4e38))
''', '5 160 160 160.0 160.0\n-160 -160 -9223372036854775808\n160\n160 1\nwide wide signed f32 f64\n')
    positive('overload_contextual_method_literals', '''def^Choice {
 pick=f^self^,a:f32^,b:f32^ {if^a<b:a el^:b;}
 overload^pick=f^self^,a:i64^,b:i64^ {if^a<b:a el^:b;}
}
var^choice=Choice{}
let^wide:i64^=200
let^real:f32^=200.0
let^unbound=choice.pick
print^(choice.pick(wide,160),choice.pick(160,wide),choice.pick(real,160))
print^(unbound(ref^choice,160,wide))
''', '160 160 160.0\n160\n')
    for name, call, diagnostic in [
        ('both_literals', 'min(1,2)', 'ambiguous overload'),
        ('typed_width', 'min(i64^(1),i32^(2))', 'no matching overload'),
        ('bound_literal', 'min(i64^(1),small)', 'no matching overload'),
        ('integer_overflow', 'min(i64^(1),9223372036854775808)', 'no matching overload'),
        ('float_overflow', 'min(f32^(1.0),4e38)', 'no matching overload'),
    ]:
        negative('overload_numeric_'+name,
                 numeric_overloads+'let^small=2\nprint^('+call+')', diagnostic)
    positive('overload_declarations', '''overload^min=f^a:i64^,b:i64^ {if^a<b:a el^:b;}
let^min=f^a:f32^,b:f32^ {if^a<b:a el^:b;}
overload^min=f^a:string^,b:string^ ->i32^ {7}
& f^a:bool^ ->i32^ {8}
let^w:i64^=3
print^(min(w,2),min(1.5,2.5),min("a","b"),min(true^))
''', '2 1.5 7 8\n')

    positive('overload_expected_result', numeric_overloads+'''let^ math=import^std.math
let^wide:i64^=min(1,2)
let^real:f32^=min(1,2)
let^half=f^x:f32^ -> f32^ {x*0.5}
let^lens=f^ -> f32^ {return^ math.tan(0.5)}
print^(wide,real,half(math.sin(1.0)),lens())
''', '1 1.0 0.4207354784011841 0.5463024973869324\n')
    negative('overload_expected_result_no_narrowing', numeric_overloads+'''let^ math=import^std.math
let^x:f64^=0.5
let^y:f32^=math.tan(x)''', "call returns 'f64^', expected 'f32^'")
    negative('overload_numeric_literal_width_ambiguity',
             'let^pick=f^n:i8^ {1} & f^n:i64^ {2}\nprint^(pick(127))', 'ambiguous overload')

    vector = '''let^ math=import^std.math
def^ F2 {
 self^{x:f32^=0.0,y:f32^=0.0}
 op^+ =f^self^,other:ref^Self^ {F2{x=self^.x+other.x,y=self^.y+other.y}}
 op^- =f^self^,other:ref^Self^ {F2{x=self^.x-other.x,y=self^.y-other.y}}
 op^* =f^self^,scale:f32^ {F2{x=self^.x*scale,y=self^.y*scale}}
 op^cross^ =f^self^,other:ref^Self^ {self^.x*other.y-self^.y*other.x}
 op^dot^ =f^self^,other:ref^Self^ {self^.x*other.x+self^.y*other.y}
 length=f^self^ {math.sqrt(self^dot^self^)}
}
'''
    positive('operator_auto_borrow_vectors', vector+'''
let^a=F2{x=4.0,y=5.0}
let^b=F2{x=1.0,y=1.0}
var^source=a
let^read=ref^source
let^items={a,b}
let^make=f^ {F2{x=1.0,y=1.0}}
print^((a-b).length(),(a+b).x,(a*2.0).y)
print^(a dot^ b,read dot^ b,a dot^ read,read dot^ read,a cross^ b)
print^((F2{x=4.0,y=5.0}-make()).length(),(items[0]-items[1]).length())
print^((a cross^(a-b)) <= 0.0)
let^accept=f^v:F2 {v.length()}
let^move=f^v:F2 {accept((v-make())*2.0)}
print^(move(a))
''', '5.0 5.0 10.0\n9.0 9.0 41.0 41.0 -1.0\n5.0 5.0\nfalse\n10.0\n')

    positive('operator_auto_borrow_order_and_right_receiver', '''
def^ L {self^{n:i32^=0}}
def^ R {
 self^{n:i32^=0}
 op^+ =f^left:ref^L,self^ {left.n+self^.n}
 op^<=> =f^self^,other:ref^Self^ {self^.n-other.n}
}
let^left=p^calls:ref^i32^ ->L {*calls+=1 return^L{n=*calls}}
let^right=p^calls:ref^i32^ ->R {*calls+=1 return^R{n=*calls}}
var^calls=0
print^(left(ref^calls)+right(ref^calls),calls)
print^(right(ref^calls)<right(ref^calls)<right(ref^calls),calls)
print^(right(ref^calls)>right(ref^calls)>right(ref^calls),calls)
''', '3 2\ntrue 5\nfalse 7\n')

    negative('operator_borrow_overlap', '''def^ V {
 self^{n:i32^=0}
 op^+ =f^self^,other:Self^ {self^.n+other.n}
 overload^ op^+ =f^self^,other:ref^Self^ {self^.n+other.n}
}
''', 'overlapping overload')
    negative('operator_borrow_temporary_escape', '''def^ V {
 self^{n:i32^=0}
 op^:: =f^self^,other:ref^Self^ ->ref^Self^ {other}
}
let^v=V{}
let^escape=v::V{}
print^(escape.n)
''', 'reference would outlive')
    negative('operator_borrow_let_escape', '''def^ V {
 self^{n:i32^=0}
 op^:: =f^self^,other:ref^Self^ ->ref^Self^ {other}
}
var^a=V{}
let^b=V{}
let^escape=a::b
print^(escape.n)
''', 'reference would outlive')
    negative('operator_borrow_mutation', '''def^ V {
 self^{n:i32^=0}
 op^+ =f^self^,other:ref^Self^ ->i32^ {other.n:=1 return^self^.n}
}
var^a=V{}
var^b=V{}
print^(a+b)
''', 'f^ cannot mutate borrowed storage')
    negative('operator_borrow_mutable_method', '''def^ V {
 self^{n:i32^=0}
 change=f^mutable^self^ {self^.n+=1}
 op^+ =f^self^,other:ref^Self^ ->i32^ {other.change() return^self^.n}
}
var^a=V{}
var^b=V{}
print^(a+b)
''', 'f^ cannot mutate borrowed storage')

    positive('overload_types_and_effects', '''
let^ choose=f^ x:i32^ ->i32^ {x+1}
& f^ x:i64^ ->string^ {"wide"}
& f^ x:string^ ->i32^ {42}
& p^ x:bool^ {print^(x)}
& f^ x:i32^,y:i32^ ->i32^ {x+y}
let^ forward=f^ x:string^ {choose(x)}
var^ wide:i64^=1
print^(choose(i32^(2)),choose(wide),forward("s"),choose(3,4))
choose(true^)
''', '3 wide 42 7\ntrue\n')

    positive('callable_intersections_and_callbacks', '''
let^ choose=f^ x:i32^ {x+1}
& f^ x:string^ {42}
let^ alias:f^i32^->i32^; & f^string^->i32^; =choose
let^ reordered:f^string^->i32^; & f^i32^->i32^; =alias
let^ apply=f^ cb:f^i32^->i32^;,n:i32^ ->i32^ {cb(n)}
print^(alias(2),alias("s"),reordered(4),apply(choose,6))
print^(alias fits^ f^i32^->i32^; & f^string^->i32^;)
let^ factory=f^ ->f^i32^->i32^; {f^ n:i32^ ->i32^ {n*2}}
let^ twice=factory()
print^(twice(4))
do^{let^ local=f^ n:i32^ {n+3} print^(local(5))}
''', '3 42 5 7\ntrue\n8\n8\n')

    positive('mixed_external_and_function_overloads', '''
let^ choose=externC^ abs:f^ n:i32^ ->i32^;
& f^ text:string^ ->i32^ {42}
print^(choose(-3),choose("hi"))
''', '3 42\n')

    positive('generic_callable_contract', '''let^ identity=template^f^ value {value}
print^(identity("s"))
let^ copy:f^i32^->i32^;=identity
print^(copy(7))
''', 's\n7\n')

    positive('method_overloads_and_override', '''
def^ Base {
 self^{value:i32^=2}
 run=f^ self^,n:i32^ ->i32^ {self^.value+n}
 overload^ run=f^ self^,s:string^ ->i32^ {40}
 overload^ run=p^ self^,b:bool^ {self^.value+=1 print^(b)}
}
def^ Derived:Base {
 override^ run=f^ self^,n:i32^ ->i32^ {self^.value+n+10}
 overload^ run=f^ self^ ->i32^ {99}
}
var^ value=Derived{}
print^(value.run(1),value.run("s"),value.run())
value.run(true^)
let^ unbound=value.run
print^(unbound(ref^ value,2),unbound(ref^ value,"s"))
''', '13 40 99\ntrue\n15 40\n')

    positive('operators_and_comparison', '''
def^ V {
 self^{n:i32^=2}
 op^+ =f^ self^,rhs:i32^ ->V {V{n=self^.n+rhs}}
 overload^ op^+ =f^ self^,rhs:string^ ->i32^ {40}
 overload^ op^+ =f^ lhs:i32^,self^ ->i32^ {lhs+self^.n+10}
 op^- =f^ self^,rhs:V ->i32^ {self^.n-rhs.n}
 overload^ op^- =f^ self^ ->i32^ {-self^.n}
 op^<=>=f^ self^,rhs:V ->i64^ {i64^(self^.n)-i64^(rhs.n)}
}
var^ v,w=V{},V{n=3}
print^((v+3).n,v+"s",3+v,-v,v-w,v<w,v=w,v!=w,v<=>w)
v+=4
print^(v.n,w<v,v>w,v>=w,v<=w,1<=>2,2<=>2,3<=>2)
''', '5 40 15 -2 -1 true false true -1\n6 true true true false -1 0 1\n')

    self_dot = '''let^ math=import^std.math
def^ F2 {
 self^{x:f32^=3.0,y:f32^=4.0}
 op^dot^=f^ self^,rhs:F2 ->f32^ {self^.x*rhs.x+self^.y*rhs.y}
 length=f^ self^ ->f32^ {math.sqrt(self^ dot^ self^)}
}
var^ v=F2{}
print^(v.length())
'''
    negative('nested_operator_reference_diagnostic', self_dot,
             "operator 'dot^' has no matching overload for 'ref^ F2' and 'ref^ F2'")
    positive('operator_explicit_reference_operand',
             self_dot.replace('self^ dot^ self^', 'self^ dot^ *self^'), '5.0\n')

    positive('operator_evaluation_and_chains', '''
def^ V {
 self^{n:i32^=0}
 op^+ =f^ self^,rhs:V ->i32^ {self^.n+rhs.n}
 op^<=>=f^ self^,rhs:V ->i32^ {self^.n-rhs.n}
}
let^ make=p^ calls:ref^ i32^,n:i32^ ->V {*calls+=1 return^ V{n=n}}
var^ calls=0
print^(make(ref^ calls,1)+make(ref^ calls,2),calls)
print^(make(ref^ calls,1)<make(ref^ calls,2)<make(ref^ calls,3),calls)
print^(make(ref^ calls,3)<make(ref^ calls,2)<make(ref^ calls,1),calls)
''', '3 2\ntrue 5\nfalse 7\n')

    positive('operator_equality_precedence', '''
def^ V {
 self^{n:i32^=0}
 op^==f^ self^,rhs:V ->bool^ {true^}
 op^<=>=f^ self^,rhs:V ->i32^ {self^.n-rhs.n}
}
var^ a,b=V{n=1},V{n=2}
print^(a=b,a!=b,a<b)
'''.replace('op^==', 'op^= ='), 'true false true\n')

    positive('custom_operator_names_and_precedence', '''
def^ V {
 self^{n:i32^=2}
 op^//=f^ self^,rhs:i32^ ->i32^ {self^.n+rhs}
 op^**=f^ self^,rhs:V ->V {V{n=self^.n*10+rhs.n}}
 op^::=f^ self^,rhs:V ->V {V{n=self^.n*10+rhs.n}}
 op^cross^=f^ self^,rhs:i32^ ->i32^ {self^.n+rhs}
 op^dot^=f^ self^,rhs:i32^ ->i32^ {self^.n-rhs}
}
var^ a,b,c=V{n=1},V{n=2},V{n=3}
print^(a//2,(a**b**c).n,(a::b::c).n,a cross^ 2,a dot^ 2)
var^ n=9
var^ r=ref^ n
let^ rr:ref^^i32^=ref^ r;
print^(**rr)
'''.replace('op^//=', 'op^// =').replace('op^**=', 'op^** =').replace('op^::=', 'op^:: ='),
             '3 33 33 3 -1\n9\n')

    positive('operator_contextual_width', '''
def^ V {
 self^{n:i64^=1}
 op^+ =f^ self^,rhs:i64^ ->V {V{n=self^.n+rhs}}
 overload^ op^+ =f^ lhs:i64^,self^ ->V {V{n=lhs+self^.n}}
 op^==f^ self^,rhs:i64^ ->bool^ {self^.n=rhs}
}
var^ v=V{}
print^((v+2).n,(2+v).n,v=1)
v+=3
print^(v.n)
'''.replace('op^==', 'op^= ='), '3 3 true\n4\n')

    positive('operator_owned_result_destination', '''def^ Packet {
 self^{n:i32^=2}
 op^:: =f^ self^,rhs:i32^ ->i32^[grow^] {
  var^ result:i32^[grow^]={self^.n,rhs}
  return^ result
 }
}
var^ packet=Packet{}
var^ items=packet::3
print^(items.len,items[0],items[1])
let^ build=f^ n:i32^ ->i32^[grow^] {
 var^ local=Packet{n=n}
 return^ local::4
}
var^ other=build(8)
print^(other.len,other[0],other[1])
''', '2 2 3\n2 8 4\n')

    positive('operator_right_receiver_recursion', '''def^ V {
 self^{n:i32^=5}
 op^+ =f^ lhs:i32^,self^ ->i32^ {if^ lhs<=0: self^.n el^: this^(lhs-1)+1;}
}
var^ v=V{}
print^(3+v)
''', '8\n')

    (work/'lib').mkdir(exist_ok=True)
    (work/'lib'/'calls.rh').write_text('''module^ lib.calls
overload^ choose=f^ b:bool^ ->i32^ {9}
public^ let^ choose=f^ n:i32^ ->i32^ {n+1}
& f^ n:string^ ->i32^ {40}
public^ let^ inc=f^ n:i32^ ->i32^ {n+1}
public^ let^ twice=f^ n:i32^ ->i32^ {n*2}
public^ let^ small:i32^=2
public^ let^ apply=f^ cb:f^i32^->i32^;,n ->i32^ {cb(n)}
''',encoding='utf-8')
    positive('overload_modules', '''import^ lib.calls
print^(lib.calls.choose(2),lib.calls.choose("s"),lib.calls.choose(true^))
print^(lib.calls.apply(lib.calls.inc,2),lib.calls.apply(lib.calls.twice,2))
''', '3 40 9\n3 4\n')
    for value in ('lib.calls.small', '-lib.calls.small'):
        negative('overload_typed_constant_'+('negative' if value.startswith('-') else 'positive'),
                 numeric_overloads+'import^lib.calls\nprint^(min(i64^(1),'+value+'))',
                 'no matching overload')

    for name, text, diagnostic in [
        ('reopened_by_arity', 'let^ f=f^ x:i32^ {x} let^ f=f^ x:i32^,y:i32^ {x+y}', 'already declared'),
        ('reopened_by_type', 'let^ f=f^ x:i32^ {x} let^ f=f^ x:string^ {x}', 'already declared'),
        ('reopened_extern', 'externC^ abs:f^ x:i32^ ->i32^; let^ abs=f^ x:string^ {x}', 'already declared'),
        ('overlap_arguments', 'let^ f=f^ x:i32^ {x} & f^ x:i32^ {x+1}', 'overlapping overload'),
        ('overlap_result_only', 'let^ f=f^ x:i32^ ->i32^ {x} & f^ x:i32^ ->string^ {"s"}', 'overlapping overload'),
        ('overlap_effect_only', 'let^ f=f^ x:i32^ {x} & p^ x:i32^ {}', 'overlapping overload'),
        ('overlap_union', 'let^ f=f^ x:i32^|nil^ {1} & f^ x:i32^ {2}', 'overlapping overload'),
        ('overlap_base', 'def^ A {} def^ B:A {} let^ f=f^ x:A {1} & f^ x:B {2}', 'overlapping overload'),
        ('overlap_common_derived', 'def^ A {} def^ B {} def^ C:A,B {} let^ f=f^ x:A {1} & f^ x:B {2}', 'overlapping overload'),
        ('overlap_generic', 'let^ f=template^f^ x {x} & f^ x:i32^ {x}', 'overlapping overload'),
        ('no_runtime_union_dispatch', 'let^ f=f^ x:i32^ {1} & f^ x:string^ {2} let^ x:i32^|string^=1 print^(f(x))', 'no matching overload'),
        ('no_matching_arm', 'let^ f=f^ x:i32^ {1} & f^ x:string^ {2} print^(f(true^))', 'no matching overload'),
        ('pure_calls_procedure_arm', 'let^ f=f^ x:i32^ {1} & p^ x:string^ {} let^ bad=f^ {f("s")} bad()', 'f^ cannot call p^'),
        ('missing_overload_marker', 'def^ V {m=f^ self^,x:i32^ {1} m=f^ self^,x:string^ {2}}', 'requires overload^'),
        ('overload_nothing', 'def^ V {overload^ m=f^ self^ {1}}', 'overload^ has no'),
        ('overload_function_nothing', 'overload^ f=f^ x:i32^ {x}', 'overload^ has no existing function'),
        ('overload_function_overlap', 'let^ f=f^ x:i32^ {x} overload^ f=f^ y:i32^ {y}', 'overlapping overload'),
        ('overload_function_public', 'let^ f=f^ x:i32^ {x} public^ overload^ f=f^ x:bool^ {1}', 'visibility of their let^'),
        ('overload_function_let', 'let^ f=f^ x:i32^ {x} overload^ let^ f=f^ x:bool^ {1}', 'overload^ takes NAME ='),
        ('overload_function_value', 'let^ f=f^ x:i32^ {x} overload^ f=f^ x:bool^ {1}(true^)', 'not a value'),
        ('overload_function_local', 'let^ g=p^ { overload^ f=f^ x:i32^ {x} } g()', 'top-level functions only'),
        ('method_overlap', 'def^ V {m=f^ self^,x:i32^ {1} overload^ m=f^ self^,x:i32^ {2}}', 'overlapping overload'),
        ('operator_procedure', 'def^ V {op^+ =p^ self^,x:i32^ {}}', 'operator requires a pure f^'),
        ('operator_mutable', 'def^ V {op^+ =f^mutable^ self^,x:i32^ {1}}', 'operator requires a pure f^'),
        ('operator_no_self', 'def^ V {op^+ =f^ x:i32^,y:i32^ {1}}', 'operator requires a pure f^'),
        ('operator_two_self', 'def^ V {op^+ =f^ self^,self^ {1}}', 'operator requires a pure f^'),
        ('operator_bad_arity', 'def^ V {op^+ =f^ self^ {1}}', 'operator requires a pure f^'),
        ('operator_bad_symbol', 'def^ V {op^<=f^ self^,x:i32^ {1}}', 'invalid operator definition'),
        ('operator_wrong_equality', 'def^ V {op^= =f^ self^,x:i32^ {1}}', 'op^= must return bool^'),
        ('operator_wrong_order', 'def^ V {op^<=>=f^ self^,x:i32^ {true^}}', 'op^<=> must return a signed integer'),
        ('intersection_noncallable', 'let^ x:i32^ & string^=1', 'intersection'),
        ('generic_callable_bad_contract', 'let^ f=template^f^ x {"bad"} let^ g:f^i32^->i32^;=f', 'callable result'),
        ('callable_array_storage', 'let^ f=f^ n:i32^ {n} let^ items={f}', 'callable array storage'),
        ('callable_mutable_effect', 'def^ V {self^{n:i32^=0} m=f^mutable^ self^ ->i32^ {self^.n+=1 return^ self^.n}} var^ v=V{} let^ cb:f^ref^ V->i32^;=v.m', 'callable'),
        ('callable_bad_contract', 'let^ f=f^ n:i32^ {n} let^ g:f^i32^->string^;=f', 'callable'),
        ('callable_mutable_storage', 'let^ f=f^ n:i32^ {n} var^ g=f', 'require let^'),
        ('signature_runtime_field', 'def^ V {self^{cb:f^i32^->i32^;}}', 'statically selected implementation'),
    ]:
        negative(name, text, diagnostic)
    suite.finish()


if __name__ == '__main__':
    main()
