"""Callable intersections, disjoint overloads and operator dispatch."""

from harness import Suite


def main():
    suite = Suite('overload')
    run, positive, negative = suite.run, suite.positive, suite.negative
    work = suite.work

    positive('overload_types_and_effects', '''
let^ choose=f^ x:i32^ ->i32^ {x+1}
overload^ let^ choose=f^ x:i64^ ->string^ {"wide"}
overload^ let^ choose=f^ x:string^ ->i32^ {42}
overload^ let^ choose=p^ x:bool^ {print^(x)}
overload^ let^ choose=f^ x:i32^,y:i32^ ->i32^ {x+y}
let^ forward=f^ x:string^ {choose(x)}
var^ wide:i64^=1
print^(choose(2),choose(wide),forward("s"),choose(3,4))
choose(true^)
''', '3 wide 42 7\ntrue\n')

    positive('callable_intersections_and_callbacks', '''
let^ choose=f^ x:i32^ {x+1}
overload^ let^ choose=f^ x:string^ {42}
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
 op^..=f^ self^,rhs:V ->V {V{n=self^.n*10+rhs.n}}
 op^cross^=f^ self^,rhs:i32^ ->i32^ {self^.n+rhs}
 op^dot^=f^ self^,rhs:i32^ ->i32^ {self^.n-rhs}
}
var^ a,b,c=V{n=1},V{n=2},V{n=3}
print^(a//2,(a**b**c).n,(a..b..c).n,a cross^ 2,a dot^ 2)
var^ n=9
var^ r=ref^ n
let^ rr:ref^^i32^=ref^ r;
print^(**rr)
'''.replace('op^//=', 'op^// =').replace('op^**=', 'op^** =').replace('op^..=', 'op^.. ='),
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
 op^.. =f^ self^,rhs:i32^ ->grow^ i32^[] {
  var^ result:grow^ i32^[]=[self^.n,rhs]
  return^ result
 }
}
var^ packet=Packet{}
var^ items=packet..3
print^(items.len,items[0],items[1])
let^ build=f^ n:i32^ ->grow^ i32^[] {
 var^ local=Packet{n=n}
 return^ local..4
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
public^ let^ choose=f^ n:i32^ ->i32^ {n+1}
public^ overload^ let^ choose=f^ n:string^ ->i32^ {40}
public^ let^ inc=f^ n:i32^ ->i32^ {n+1}
public^ let^ twice=f^ n:i32^ ->i32^ {n*2}
public^ let^ apply=f^ cb:f^i32^->i32^;,n ->i32^ {cb(n)}
''',encoding='utf-8')
    positive('overload_modules', '''import^ lib.calls
print^(lib.calls.choose(2),lib.calls.choose("s"))
print^(lib.calls.apply(lib.calls.inc,2),lib.calls.apply(lib.calls.twice,2))
''', '3 40\n3 4\n')

    for name, text, diagnostic in [
        ('overload_required', 'let^ f=f^ x:i32^ {x} let^ f=f^ x:string^ {0}', "adding a signature to 'f' requires overload^"),
        ('overload_required_extern', 'externC^ abs:f^ n:i32^ ->i32^;\nlet^ abs=f^ x:string^ ->i32^ {0}', "adding a signature to 'abs' requires overload^"),
        ('overload_first', 'overload^ let^ f=f^ x:i32^ {x}', "overload^ has no existing function 'f'"),
        ('overload_not_function', 'overload^ def^ D { self^{ n:i32^ = 0 } }', 'applies only to a function declaration'),
        ('overload_trailing', 'let^ f=f^ x:i32^ {x}\noverload^', 'applies only to a function declaration'),
        ('overlap_arguments', 'let^ f=f^ x:i32^ {x} overload^ let^ f=f^ x:i32^ {x+1}', 'overlapping overload'),
        ('overlap_result_only', 'let^ f=f^ x:i32^ ->i32^ {x} overload^ let^ f=f^ x:i32^ ->string^ {"s"}', 'overlapping overload'),
        ('overlap_effect_only', 'let^ f=f^ x:i32^ {x} overload^ let^ f=p^ x:i32^ {}', 'overlapping overload'),
        ('overlap_union', 'let^ f=f^ x:i32^|nil^ {1} overload^ let^ f=f^ x:i32^ {2}', 'overlapping overload'),
        ('overlap_base', 'def^ A {} def^ B:A {} let^ f=f^ x:A {1} overload^ let^ f=f^ x:B {2}', 'overlapping overload'),
        ('overlap_common_derived', 'def^ A {} def^ B {} def^ C:A,B {} let^ f=f^ x:A {1} overload^ let^ f=f^ x:B {2}', 'overlapping overload'),
        ('overlap_generic', 'let^ f=template^f^ x {x} overload^ let^ f=f^ x:i32^ {x}', 'overlapping overload'),
        ('no_runtime_union_dispatch', 'let^ f=f^ x:i32^ {1} overload^ let^ f=f^ x:string^ {2} let^ x:i32^|string^=1 print^(f(x))', 'no matching overload'),
        ('no_matching_arm', 'let^ f=f^ x:i32^ {1} overload^ let^ f=f^ x:string^ {2} print^(f(true^))', 'no matching overload'),
        ('pure_calls_procedure_arm', 'let^ f=f^ x:i32^ {1} overload^ let^ f=p^ x:string^ {} let^ bad=f^ {f("s")} bad()', 'f^ cannot call p^'),
        ('missing_overload_marker', 'def^ V {m=f^ self^,x:i32^ {1} m=f^ self^,x:string^ {2}}', 'requires overload^'),
        ('overload_nothing', 'def^ V {overload^ m=f^ self^ {1}}', 'overload^ has no'),
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
        ('callable_array_storage', 'let^ f=f^ n:i32^ {n} let^ items=[f]', 'callable array storage'),
        ('callable_mutable_effect', 'def^ V {self^{n:i32^=0} m=f^mutable^ self^ ->i32^ {self^.n+=1 return^ self^.n}} var^ v=V{} let^ cb:f^ref^ V->i32^;=v.m', 'callable'),
        ('callable_bad_contract', 'let^ f=f^ n:i32^ {n} let^ g:f^i32^->string^;=f', 'callable'),
        ('callable_mutable_storage', 'let^ f=f^ n:i32^ {n} var^ g=f', 'require let^'),
        ('callable_capture', 'let^ outer=f^ n:i32^ ->i32^ {let^ local=f^ ->i32^ {n} return^ local()} print^(outer(1))', "unknown value 'n'"),
        ('signature_runtime_field', 'def^ V {self^{cb:f^i32^->i32^;}}', 'statically selected implementation'),
    ]:
        negative(name, text, diagnostic)
    suite.finish()


if __name__ == '__main__':
    main()
