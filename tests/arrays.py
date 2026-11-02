"""Runtime-sized owning arrays with a length fixed after construction."""
from harness import Suite


def main():
    suite = Suite('arrays')
    positive, negative = suite.positive, suite.negative
    positive('sized_repeat_and_elements', '''
let^make=f^n {i64^[](n,7)}
var^a=make(3)
a[1]:=42
var^empty:i64^[]=[]
let^zero=i64^[](0,9)
print^(a.len,a[0],a[1],a[2],empty.len,zero.len,a.bytes.len)
''', '3 7 42 7 0 0 24\n')
    positive('sized_copy_sources', '''
var^fixed:i32^[3]=[1,2,3]
var^grow:i32^[grow^]=[4,5]
var^bounded:i32^[max^8]=[6,7]
let^a=i32^[](fixed)
let^b=i32^[](ref^grow)
let^c=i32^[](bounded[..])
let^d=i32^[]([8,9])
fixed[0]:=99 grow[0]:=99 bounded[0]:=99
let^e=i32^[](a)
print^(a[0],b[0],c[0],d[0],e.len,e[2])
''', '1 4 6 8 3 3\n')
    positive('sized_copy_return_independent', '''
let^make=f^n:i64^ {
 var^source:i64^[grow^]=[n,n+1]
 return^i64^[](source[..])
}
var^a=make(41)
let^read=f^xs:ref^i64^[] {xs[0]+xs[1]}
print^(read(ref^a),a[1])
''', '83 42\n')
    positive('sized_reference_elements', '''
def^Node {self^{value:i32^=1}}
var^n=Node{}
let^make=f^n:ref^Node {Node[ref^](2,n)}
var^a=make(ref^n)
let^b=Node[ref^](a)
n.value:=42
print^(a[0].value,b[1].value,a.len)
''', '42 42 2\n')
    positive('sized_initial_evaluation_order', '''
let^count=p^calls:ref^i64^ {*calls+=1 return^i64^(3)}
let^initial=p^calls:ref^i64^ {*calls+=10 return^*calls}
var^calls:i64^=0
let^a=i64^[](count(ref^calls),initial(ref^calls))
let^b=i64^[](0,initial(ref^calls))
print^(calls,a.len,a[0],a[2],b.len)
''', '21 3 11 11 0\n')
    positive('sized_empty_typed_value', '''
var^a=i32^[]
var^b:i32^[]=[1,2]
print^(a.len,b.len,b[1])
''', '0 2 2\n')
    positive('sized_struct_and_fixed_elements', '''
def^Pair {self^{a:i64^,b:i64^}}
let^pairs=Pair[](2,Pair{a=7,b=8})
var^source:i64^[2][2]=[[1,2],[3,4]]
let^arrays=i64^[2][](source)
print^(pairs[1].a,pairs[1].b,arrays[1][0])
''', '7 8 3\n')
    positive('sized_self_elements', '''
def^Node {self^{n:i32^=7}
 copies=f^self^ {Self^[](2,*self^)}
}
var^node=Node{}
let^copies=node.copies()
print^(copies.len,copies[1].n)
''', '2 7\n')
    for method,args in [('push','1'),('pop',''),('clear',''),('truncate','0'),('append','a[..]')]:
        negative('sized_forbid_'+method, f'var^a=i32^[](2,1) a.{method}({args})',
                 'fixed length after construction')
    negative('sized_forbid_reassign','var^a=i32^[](2,1) a:=i32^[](3,2)',
             'owned values cannot be reassigned')
    negative('sized_forbid_implicit_copy','let^a=i32^[](2,1) let^b=a',
             'owned')
    negative('sized_forbid_by_value','let^read=f^a:i32^[] {a.len} let^a=i32^[](2,1) print^(read(a))',
             'reference')
    negative('sized_forbid_owned_elements','let^a=i32^[grow^][]',
             'nonempty fixed-size')
    negative('sized_wrong_element','let^a=i32^[](["x"])', 'array or slice')
    negative('sized_wrong_source','let^a=i32^[](42)', 'array or slice')
    negative('sized_wrong_count','let^n:i32^=2 let^a=i32^[](n,1)', 'array length must be i64^')
    negative('sized_wrong_initial','let^a=i32^[](2,"x")', "initial value must be 'i32^'")
    negative('sized_constructor_arity','let^a=i32^[](1,2,3)', 'construction expects')
    negative('sized_no_grow_conversion','let^a:i32^[grow^]=i32^[](2,1)', 'cannot initialize')
    negative('sized_reference_escape','let^bad=f^ {var^x=1 return^i32^[ref^](2,ref^x)} let^a=bad()',
             'cannot return a reference')
    negative('sized_copy_reference_escape','let^bad=f^ {var^x=1 let^a:i32^[ref^1]=[ref^x] return^i32^[ref^](a)} let^b=bad()',
             'cannot return a reference')
    suite.runtime_failure('sized_negative_count','let^a=i32^[](-1,0)', 'data stack')
    suite.runtime_failure('sized_overflow_count','let^a=i64^[](9223372036854775807,0)', 'data stack')
    suite.runtime_failure('sized_bounds','let^a=i32^[](2,7) print^(a[2])', 'out of bounds')
    suite.finish()


if __name__ == '__main__':
    main()
