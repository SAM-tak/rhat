"""Inline nil unions, propagation, conditional writes, and borrow regressions."""

from harness import Suite


def main():
    suite = Suite('nil')
    positive, negative = suite.positive, suite.negative

    positive('nil_scalar', '''
let^ accept=f^ x:nil^|i64^ ->i64^|nil^ {return^ x}
var^ a:i64^|nil^=42
let^ b=accept(a)
print(a?,b??0)
a:=nil^
print(a?,a??9)
print(nil^?,0?,false^?,""?)
let^ f:bool^|nil^=false^
print(f?,f??true^)
let^ inferred=if^ false^: 7 el^: nil^;
print(inferred?)
''', 'true 42\nfalse 9\nfalse true true true\ntrue false\nfalse\n')

    positive('nil_wide_and_float', '''
var^ u:u128^|nil^=340282366920938463463374607431768211455
var^ i:i128^|nil^=-170141183460469231731687303715884105728
var^ f:f32^|nil^=1.5
var^ half:f16^|nil^=2.0
var^ tiny:f8^|nil^=3.0
print(u??0,i??0,f??0.0,half??0.0,tiny??0.0)
u:=nil^ f:=nil^
print(u??8,f??2.5)
''', '340282366920938463463374607431768211455 -170141183460469231731687303715884105728 1.5 2 3\n8 2.5\n')

    positive('nil_nominal_conversion', '''
def^ Named = nil^ | i32^
let^ a:i32^|nil^=12
let^ b:Named=a
let^ c:nil^|i32^=b
print((b fits^ i32^),c??0)
let^ empty:Named=nil^
let^ again:i32^|nil^=empty
print(again?,again??9)
def^ Ref = ref^ i32^ | nil^
var^ n=17
let^ ref:Ref=ref^ n
let^ anonymous:ref^ i32^|nil^=ref
var^ fallback=0
let^ selected=anonymous??ref^ fallback
print(*selected)
''', 'true 12\nfalse 9\n17\n')

    positive('nil_multiple_alternatives', '''
var^ x:i32^|i64^|nil^=i64^(99)
let^ a=x??i32^(0)
assert(a fits^ i64^)
print(a fits^ i64^,a)
x:=nil^
let^ b=x??i32^(7)
assert(b fits^ i32^)
print(b fits^ i32^,b)
if^ x? {print((x fits^ i64^)) else^: print(x?)}
''', 'true 99\ntrue 7\nfalse\n')

    positive('nil_chain', '''
def^ Child {self^{value:i32^=9}}
def^ Node {self^{child:Child,maybe:Child|nil^=nil^}
 read=f^ self^,x:i32^ ->i32^ {self^.child.value+x}
}
var^ yes:Node|nil^=Node.new()
var^ no:Node|nil^=nil^
print(yes?.child.value??0,no?.child.value??77)
print((yes?.child).value??0,(no?.child).value??88)
print(yes?.maybe?.value??10,(yes?.maybe)?)
print(yes?.read(3)??0,no?.read(3)??66)
let^ always=Node.new()
print(always?.child.value??0)
print(nil^?.missing?)
''', '9 77\n9 88\n10 false\n12 66\n9\nfalse\n')

    positive('nil_chain_lazy', '''
def^ Node {self^{values:i32^[2]=[5,6]} read=f^ self^,x:i32^ ->i32^ {x+1}}
let^ make=p^ calls:ref^ i32^,yes:bool^ ->Node|nil^ {*calls+=1 return^ if^ yes: Node.new() el^: nil^;}
let^ index=p^ calls:ref^ i32^ ->i64^ {*calls+=10 return^ 1}
let^ arg=p^ calls:ref^ i32^ ->i32^ {*calls+=100 return^ 7}
var^ calls=0
print(make(ref^ calls,false^)?.values[index(ref^ calls)]??0,calls)
print(make(ref^ calls,true^)?.values[index(ref^ calls)]??0,calls)
print(make(ref^ calls,false^)?.read(arg(ref^ calls))??0,calls)
print(make(ref^ calls,true^)?.read(arg(ref^ calls))??0,calls)
''', '0 1\n6 12\n0 13\n8 114\n')

    positive('nil_array', '''
let^ array:i64^[2]|nil^=[10,20]
print(array?.len??0,array?[1]??0)
var^ owner:grow^ i32^[]=[3,4]
var^ ref:ref^ grow^ i32^[]|nil^=ref^ owner
print(ref?[1]??0)
ref:=nil^
print(ref?[999999999]??7)
let^ view:i32^[:]|nil^=owner[:: 2]
print(view?[0]??0,view?.len??0)
let^ fallback:i64^[2]=[7,8]
let^ copy=array??fallback
print(copy[0],copy[1])
''', '2 20\n4\n7\n3 2\n10 20\n')

    positive('nil_strings', r'''
var^ text:string^|nil^="a\0b"
print(text?.len??0,text?[2]??u8^(0))
print((text?[:: 1])?.len??0)
text:=nil^
print(text??"fallback")
''', '3 98\n1\nfallback\n')

    positive('nil_mutable_reference', '''
def^ Counter {self^{n:i32^=0} bump=p^ self^,n:i32^ {self^.n+=n}}
let^ tick=p^ calls:ref^ i32^ ->i32^ {*calls+=1 return^ 3}
var^ counter=Counter.new()
var^ ref:ref^ Counter|nil^=ref^ counter
var^ calls=0
ref?.bump(tick(ref^ calls))
ref:=nil^
ref?.bump(tick(ref^ calls))
print(counter.n,calls)
''', '3 1\n')

    positive('nil_assignment', '''
let^ tick=p^ calls:ref^ i32^ ->i32^ {*calls+=1 return^ *calls}
var^ calls=0
var^ a:i32^|nil^=nil^
var^ b:i32^|nil^=10
a ?:= tick(ref^ calls)
b ?+= tick(ref^ calls)
print(a?,b??0,calls)
b ?:= nil^
b ?:= tick(ref^ calls)
print(b?,calls)
var^ plain=2
plain ?*= 3 plain ?-= 1 plain ?/= 5 plain ?%= 2
print(plain)
''', 'false 11 1\nfalse 1\n1\n')

    positive('nil_parallel_assignment', '''
let^ tick=p^ calls:ref^ i32^ ->i32^ {*calls+=1 return^ *calls}
var^ calls=0
var^ a:(i32^|nil^)[3]=[1,nil^,3]
a[0],a[1],a[2] ?+= tick(ref^ calls),tick(ref^ calls),tick(ref^ calls)
print(a[0]??0,a[1]??0,a[2]??0,calls)
a[0],a[2] ?:= a[2],a[0]
print(a[0]??0,a[2]??0)
var^ x:i32^|nil^=5
x,x ?+= 1,2
print(x??0)
''', '2 0 5 2\n5 2\n7\n')

    positive('nil_assignment_chained_place', '''
def^ Item {self^{x:i32^=5}}
let^ tick=p^ calls:ref^ i32^ ->i32^ {*calls+=1 return^ 4}
let^ index=p^ calls:ref^ i32^ ->i64^ {*calls+=10 return^ 0}
var^ calls=0 var^ item=Item.new()
var^ maybe:ref^ Item|nil^=ref^ item
maybe?.x ?+= tick(ref^ calls)
maybe:=nil^
maybe?.x ?+= tick(ref^ calls)
var^ a:(i32^|nil^)[2]=[1,nil^]
var^ ref:ref^ (i32^|nil^)[2]|nil^=ref^ a
ref?[index(ref^ calls)] ?:= tick(ref^ calls)
ref:=nil^
ref?[index(ref^ calls)] ?:= tick(ref^ calls)
print(item.x,a[0]??0,calls)
''', '9 4 12\n')

    positive('nil_assignment_rhs_changes_tag', '''
let^ change=p^ x:ref^ (i32^|nil^) ->i32^ {*x:=nil^ return^ 7}
var^ x:i32^|nil^=3
x ?+= change(ref^ x)
print(x??0)
''', '10\n')

    positive('nil_flow', '''
def^ Item {self^{n:i32^=7}}
let^ read=f^ x:Item|nil^ {if^ x? {return^ x.n} return^ 0}
let^ logical=f^ x:Item|nil^ {if^ x? and^ x.n>0: x.n el^: 0;}
let^ reverse=f^ x:Item|nil^ {if^ !(x?): 0 el^: x.n;}
print(read(Item.new()),read(nil^))
print(logical(Item.new()),logical(nil^),reverse(Item.new()),reverse(nil^))
var^ n:i32^|nil^=3
if^ n? {n+=1 n+=2 n?+=1 print(n)}
if^ n != nil^ {print(n)}
n:=nil^
if^ n = nil^ {print(n?)}
''', '7 0\n7 0 7 0\n7\n7\nfalse\n')

    positive('nil_flow_loop', '''
def^ Item {self^{n:i32^=7}}
let^ clear=p^ x:ref^ (Item|nil^) {*x:=nil^}
let^ run=p^ x:Item|nil^ {
 repeat^ while^ x? {print(x.n) clear(ref^ x)}
}
run(Item.new())
var^ x:Item|nil^=Item.new()
if^ x? {repeat^ 2 {print(x.n)}}
let^ exit=f^ a:Item|nil^ {
 repeat^ while^ a? {break^}
 return^ a
}
print(exit(nil^)? ,exit(Item.new())?.n??0)
''', '7\n7\n7\nfalse 7\n')

    positive('nil_coalesce_lazy', '''
let^ tick=p^ x:ref^ i32^ ->i32^ {*x+=1 return^ 12}
var^ calls=0
var^ x:i32^|nil^=3
print(x??tick(ref^ calls),calls)
x:=nil^
print(x??tick(ref^ calls),calls)
print(2+x??40)
print(nil^??17)
''', '3 0\n12 1\n42\n17\n')

    positive('nil_coalesce_reference', '''
var^ a=3 var^ b=8
var^ x:ref^ i32^|nil^=ref^ a
let^ present=x??ref^ b;
*present:=4
x:=nil^
let^ absent=x??ref^ b
print(a,*absent)
var^ cell:i32^|nil^=nil^
let^ ref=ref^ cell
print(ref?,(*ref)?)
''', '4 8\ntrue false\n')

    positive('nil_template', '''
def^ A {self^{value:i32^=7}}
def^ B {self^{value:i32^=9}}
let^ read=template^f^ x {if^ x?: x.value el^: 0;}
let^ a:A|nil^=A.new()
let^ b:B|nil^=nil^
print(read(a),read(b))
''', '7 0\n')

    positive('nil_bounds_present', '''
var^ a:i32^[2]=[1,2]
var^ ref:ref^ i32^[2]|nil^=ref^ a
print(ref?[-1]??0)
''', 'array index out of bounds', failure=True)

    prefix = 'def^ Item {self^{n:i32^=7}}\n'
    cases = [
        ('nil_wrong_arm', 'let^ x:i32^|nil^="bad"', 'not an alternative'),
        ('nil_owned_union', 'let^ x:grow^ i32^[]|nil^=nil^', 'non-owning fixed-size'),
        ('nil_void_union', 'let^ x:void^|nil^=nil^', 'void cannot'),
        ('nil_void_presence', 'let^ noop=p^ {} print(noop()?)', 'presence test requires'),
        ('nil_void_fallback', 'let^ noop=p^ {} let^ x:i32^|nil^=nil^ let^ y=x??noop()', 'value-producing fallback'),
        ('nil_chain_needs_next_guard', 'def^ A {self^{child:Item|nil^=nil^}} let^ x:A|nil^=A.new() print(x?.child.n??0)', 'nil-propagating chain'),
        ('nil_chain_binding_ends_guard', 'let^ x:Item|nil^=Item.new() let^ y=x?.n print(y.len)', 'field access requires'),
        ('nil_coalesce_nonnullable', 'print(1??2)', 'requires a nil alternative'),
        ('nil_assignment_immutable', 'let^ x:i32^|nil^=nil^ x ?:= 2', 'cannot modify let^'),
        ('nil_assignment_wrong_type', 'var^ x:i32^|nil^=1 x ?:= "bad"', 'not an alternative'),
        ('nil_discard_guard', '_^ ?:= 1', 'discard is not'),
        ('nil_mutable_copy', 'def^ C {self^{x:i32^=0} bump=p^ self^ {self^.x+=1}} var^ c:C|nil^=C.new() c?.bump()', 'optional reference'),
        ('nil_place_copy', 'var^ x:Item|nil^=Item.new() x?.n ?:= 3', 'optional reference'),
        ('nil_borrow_projection', 'var^ x:Item|nil^=Item.new() if^ x? {let^ ref=ref^ x.n}', 'cannot borrow a temporary'),
        ('nil_direct_mutation_invalidates', 'var^ x:Item|nil^=Item.new() if^ x? {x:=nil^ print(x.n)}', 'field access requires'),
        ('nil_call_invalidates', 'let^ clear=p^ x:ref^ (Item|nil^) {*x:=nil^} var^ x:Item|nil^=Item.new() if^ x? {clear(ref^ x) print(x.n)}', 'field access requires'),
        ('nil_alias_invalidates', 'let^ clear=p^ x:ref^ (Item|nil^) {*x:=nil^} var^ x:Item|nil^=Item.new() let^ alias=ref^ x if^ x? {clear(alias) print(x.n)}', 'field access requires'),
        ('nil_condition_mutates', 'let^ clear=p^ x:ref^ (Item|nil^) ->bool^ {*x:=nil^ return^ true^} var^ x:Item|nil^=Item.new() if^ x? and^ clear(ref^ x) {print(x.n)}', 'field access requires'),
        ('nil_branch_mutation', 'let^ clear=p^ x:ref^ (Item|nil^) {*x:=nil^} var^ x:Item|nil^=Item.new() if^ x? {if^ true^ {clear(ref^ x)} print(x.n)}', 'field access requires'),
        ('nil_loop_invariant', 'var^ x:Item|nil^=Item.new() if^ x? {repeat^ 2 {print(x.n) x:=nil^}}', 'field access requires'),
        ('nil_loop_exit', 'var^ x:Item|nil^=Item.new() repeat^ while^ x? {break^} print(x.n)', 'field access requires'),
        ('nil_borrow_return', 'let^ bad=f^ ->ref^ i32^|nil^ {var^ x=1 return^ ref^ x} let^ r=bad()', 'cannot return a reference'),
        ('nil_chain_borrow_return', 'let^ bad=f^ ->i32^[:]|nil^ {var^ a:grow^ i32^[]=[1] let^ ref:ref^ grow^ i32^[]|nil^=ref^ a return^ ref?[:: 1]} let^ r=bad()', 'cannot return a reference'),
        ('nil_coalesce_borrow_return', 'let^ bad=f^ ->ref^ i32^ {var^ a=1 var^ b=2 let^ x:ref^ i32^|nil^=ref^ a return^ x??ref^ b} let^ r=bad()', 'cannot return a reference'),
        ('nil_array_history', 'var^ a:grow^ (ref^ i32^|nil^)[]=[] do^{var^ local=1 a.push(ref^ local)}', 'outlive'),
        ('nil_guard_store_borrow', 'var^ outer:ref^ i32^|nil^=nil^ do^{var^ local=1 outer ?:= ref^ local}', 'outlive'),
        ('nil_compare_order', 'let^ x:i32^|nil^=1 print(x<nil^)', 'only equality'),
    ]
    for name, text, diagnostic in cases:
        negative(name, prefix + text, diagnostic)
    suite.finish()


if __name__ == '__main__':
    main()
