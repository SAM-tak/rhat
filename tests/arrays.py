"""Runtime-sized owning arrays with a length fixed after construction."""
from harness import Suite


def main():
    suite = Suite('arrays')
    positive, negative = suite.positive, suite.negative
    positive('literal_typed_and_contextual', '''
var^a=i64^[max^10]{0,1,2,3}
var^b=u32^[max^10]{0,1,2,3}
var^c=i32^[grow^]{1,2}
var^d=i64^[flex^]{7,8}
let^e=u32^[2]{10,11}
let^f={10,11,12}
let^g=i32^[]{4,5,6}
let^h:i32^[]=i32^[]{1}
a.push(4) b.push(4) c.push(3) d.pop()
print^(a.len,b[4],c[2],d.len,e[1],f[2],g.len,h[0])
''', '5 4 3 1 11 12 3 1\n')
    positive('literal_reference_auto_borrow', '''
def^B {self^{n:i32^}}
var^a:B[4]={B{n=0},B{n=1},B{n=2},B{n=3}}
let^refs=B[ref^ 4]{a[0],a[1],a[2],a[3]}
let^typed=B[ref^ 4]{a[0],a[1],ref^a[2],ref^a[3]}
var^bounded=B[ref^max^10]{a[0],a[1]}
var^growing=B[ref^grow^]{a[2],ref^a[3]}
var^flexing=B[ref^flex^]{a[1]}
a[0].n:=42
bounded.push(ref^a[2]) growing.push(ref^a[0]) flexing.pop()
print^(refs[0].n,typed[0].n,typed[3].n,bounded.len,growing[2].n,flexing.len)
''', '42 42 3 3 42 0\n')
    positive('literal_references_are_not_borrowed_twice', '''
var^a:i32^[2]={7,8}
let^r=ref^a[0]
let^refs=i32^[ref^ 2]{r,ref^a[1]}
let^typed=i32^[ref^ 2]{r,ref^a[1]}
print^(*refs[0],*refs[1],*typed[0])
''', '7 8 7\n')
    positive('literal_dynamic_references_are_not_borrowed_twice', '''
def^Shape {abstract^value:f^self^ ->i32^;}
def^B:Shape {self^{} value=f^self^ ->i32^ {7}}
var^b=B{}
let^view:ref^dyn^Shape=ref^b
let^refs={view}
print^(refs[0].value())
''', '7\n')
    positive('literal_empty_and_contextual', '''
var^a:i64^[max^10]={}
var^b=u32^[grow^]{}
var^c:i64^[grow^]={0,1}
var^x:i32^=42
let^refs:i32^[ref^2]={ref^x,ref^x}
let^owned:i32^[]={}
b.push(7) a.push(9)
print^(a[0],b[0],c[1],*refs[1],owned.len)
''', '9 7 1 42 0\n')
    positive('literal_borrow_once_and_lifetime', '''
let^index=p^calls:ref^i64^ {*calls+=1 return^i64^(0)}
var^calls:i64^=0
var^a:i32^[1]={7}
let^refs=i32^[ref^ 2]{a[index(ref^calls)],ref^a[index(ref^calls)]}
print^(calls,*refs[0],*refs[1])
''', '2 7 7\n')
    positive('literal_nominal_and_nested_elements', '''
def^B {self^{n:i64^}}
let^records=B[max^10]{{n=7},{n=8}}
let^nested=i32^[2][max^3]{{1,2},{3,4}}
let^grid={{1,2},{3,4}}
print^(records[1].n,nested[1][0],grid[1][1])
''', '8 3 4\n')
    positive('literal_borrowed_parameter_return', '''
let^borrow=f^a:ref^i32^[2] {i32^[ref^ 2]{a[0],a[1]}}
var^a:i32^[2]={7,8}
let^refs=borrow(ref^a)
print^(*refs[0],*refs[1])
''', '7 8\n')
    for name,source,error in [
        ('brackets','let^a=[1,2]', 'array literals are written {a, b}'),
        ('bare_type','let^a=i32^[grow^]', 'needs {elements} or (arguments)'),
        ('over_capacity','let^a=i32^[max^2]{1,2,3}', 'holds at most 2'),
        ('borrow_literal','let^a=i32^[ref^max^10]{0,1,2}', 'cannot borrow a temporary'),
        ('borrow_temporary','def^B {} let^a=B[ref^ 1]{B{}}', 'cannot borrow a temporary'),
        ('borrow_let','let^x=1 let^a=i64^[ref^ 1]{x}', 'requires var^'),
        ('borrow_escape','let^bad=f^ {var^x=1 return^i32^[ref^ 1]{x}} let^r=bad()', 'cannot return a reference'),
        ('borrow_scope','var^x=1 var^refs:i32^[ref^max^3]={} do^{var^local=2 refs:=i32^[ref^max^3]{local}}', 'outlive'),
        ('wrong_kind','let^a:i32^[grow^]=i32^[max^3]{1,2}', 'cannot initialize'),
        ('wrong_owner','let^a:i32^[]=i64^[]{1}', 'cannot initialize'),
        ('wrong_type','var^x:i64^=7 let^a=i32^[ref^ 1]{x}', 'type'),
        ('wrong_count','let^a=i32^[2]{1}', 'requires exactly 2 elements'),
        ('slice_literal','var^x:i32^=1 let^a=i32^[ref^ ..]{x}', 'slice borrows existing storage'),
        ('borrow_prevents_shrink','var^a:i32^[flex^]={1,2} let^refs=i32^[ref^ 1]{a[0]} a.clear()',
         "while 'refs' may refer into it"),
        ('borrow_earlier_element','let^take=p^a:ref^i32^[flex^] {a.pop() return^i32^(0)} var^a:i32^[flex^]={1,2} let^refs=i32^[ref^ 2]{a[0],take(ref^a)}',
         'earlier operand may refer into it'),
    ]:
        negative('literal_'+name,source,error)
    positive('sized_repeat_and_elements', '''
let^make=f^n {i64^[]{7 repeat^ n}}
var^a=make(3)
a[1]:=42
var^empty:i64^[]={}
let^zero=i64^[]{9 repeat^ 0}
print^(a.len,a[0],a[1],a[2],empty.len,zero.len,a.bytes.len)
''', '3 7 42 7 0 0 24\n')
    positive('sized_copy_sources', '''
var^fixed:i32^[3]={1,2,3}
var^grow:i32^[grow^]={4,5}
var^bounded:i32^[max^8]={6,7}
let^a=i32^[](fixed)
let^b=i32^[](ref^grow)
let^c=i32^[](bounded[..])
let^d=i32^[]({8,9})
fixed[0]:=99 grow[0]:=99 bounded[0]:=99
let^e=i32^[](a)
print^(a[0],b[0],c[0],d[0],e.len,e[2])
''', '1 4 6 8 3 3\n')
    positive('sized_copy_return_independent', '''
let^make=f^n:i64^ {
 var^source:i64^[grow^]={n,n+1}
 return^i64^[](source[..])
}
var^a=make(41)
let^read=f^xs:ref^i64^[] {xs[0]+xs[1]}
print^(read(ref^a),a[1])
''', '83 42\n')
    positive('sized_reference_elements', '''
def^Node {self^{value:i32^=1}}
var^n=Node{}
let^make=f^n:ref^Node {Node[ref^]{n repeat^ 2}}
var^a=make(ref^n)
let^b=Node[ref^](a)
n.value:=42
print^(a[0].value,b[1].value,a.len)
''', '42 42 2\n')
    positive('sized_initial_evaluation_order', '''
let^count=p^calls:ref^i64^ {*calls+=1 return^i64^(3)}
let^initial=p^calls:ref^i64^ {*calls+=10 return^*calls}
var^calls:i64^=0
let^a=i64^[]{initial(ref^calls) repeat^ count(ref^calls)}
let^b=i64^[]{initial(ref^calls) repeat^ 0}
print^(calls,a.len,a[0],a[2],b.len)
''', '21 3 10 10 0\n')
    positive('sized_contextual_forms', '''
let^n:i64^=3
let^a:i32^[]={7 repeat^ n}
var^g:i64^[grow^]={0 repeat^ n}
var^f=i64^[flex^]{1 repeat^ 2}
let^c:i32^[]={a...}
let^d=i32^[]{a[1 ..]...}
var^e:i64^[grow^]={g...}
var^m:i32^[max^]={max^ 4}
var^k=i32^[max^]{max^n}
g.push(5) e.push(6) m.push(1) k.push(2)
print^(a[2],g.len,f.len,c[0],d.len,e.len,m[0],k[0])
''', '7 4 2 7 2 4 1 2\n')
    for name,source,error in [
        ('untyped','let^a={0 repeat^ 3}', 'array construction needs a type'),
        ('fixed','let^a:i32^[3]={0 repeat^ 3}', 'build owning arrays'),
        ('capped','var^a:i32^[max^]={1 repeat^ 2}', 'build owning arrays'),
        ('capacity_owner','let^a:i32^[]={max^ 3}', 'builds a T[max^]'),
        ('pair','let^a=i32^[](3,0)', 'write T[]{value repeat^ n}'),
        ('mixed','let^a:i32^[]={1, 2 repeat^ 3}', "expected '}'"),
        ('copy_mismatch','let^a:i32^[]={(i64^[]{1})...}', 'array or slice'),
    ]:
        negative('form_'+name,source,error)
    positive('sized_empty_typed_value', '''
var^a=i32^[]{}
var^b:i32^[]={1,2}
print^(a.len,b.len,b[1])
''', '0 2 2\n')
    positive('sized_struct_and_fixed_elements', '''
def^Pair {self^{a:i64^,b:i64^}}
let^pairs=Pair[]{Pair{a=7,b=8} repeat^ 2}
var^source:i64^[2][2]={{1,2},{3,4}}
let^arrays=i64^[2][](source)
print^(pairs[1].a,pairs[1].b,arrays[1][0])
''', '7 8 3\n')
    positive('sized_self_elements', '''
def^Node {self^{n:i32^=7}
 copies=f^self^ {Self^[]{*self^ repeat^ 2}}
}
var^node=Node{}
let^copies=node.copies()
print^(copies.len,copies[1].n)
''', '2 7\n')
    for method,args in [('push','1'),('pop',''),('clear',''),('truncate','0'),('append','a[..]')]:
        negative('sized_forbid_'+method, f'var^a=i32^[]{{1 repeat^ 2}} a.{method}({args})',
                 'fixed length after construction')
    negative('sized_forbid_reassign','var^a=i32^[]{1 repeat^ 2} a:=i32^[]{2 repeat^ 3}',
             'owned values cannot be reassigned')
    negative('sized_forbid_implicit_copy','let^a=i32^[]{1 repeat^ 2} let^b=a',
             'owned')
    negative('sized_forbid_by_value','let^read=f^a:i32^[] {a.len} let^a=i32^[]{1 repeat^ 2} print^(read(a))',
             'reference')
    negative('sized_forbid_owned_elements','let^a=i32^[grow^][]{}',
             'nonempty fixed-size')
    negative('sized_wrong_element','let^a=i32^[]({"x"})', 'array or slice')
    negative('sized_wrong_source','let^a=i32^[](42)', 'array or slice')
    negative('sized_wrong_count','let^n:i32^=2 let^a=i32^[]{1 repeat^ n}', 'array length must be i64^')
    negative('sized_wrong_initial','let^a=i32^[]{"x" repeat^ 2}', "initial value must be 'i32^'")
    negative('sized_constructor_arity','let^a=i32^[](1,2,3)', 'construction expects')
    negative('sized_no_grow_conversion','let^a:i32^[grow^]=i32^[]{1 repeat^ 2}', 'cannot initialize')
    negative('sized_reference_escape','let^bad=f^ {var^x=1 return^i32^[ref^]{ref^x repeat^ 2}} let^a=bad()',
             'cannot return a reference')
    negative('sized_copy_reference_escape','let^bad=f^ {var^x=1 let^a:i32^[ref^1]={ref^x} return^i32^[ref^](a)} let^b=bad()',
             'cannot return a reference')
    suite.runtime_failure('sized_negative_count','let^a=i32^[]{0 repeat^ -1}', 'data stack')
    suite.runtime_failure('sized_overflow_count','let^a=i64^[]{0 repeat^ 9223372036854775807}', 'data stack')
    suite.runtime_failure('sized_bounds','let^a=i32^[]{7 repeat^ 2} print^(a[2])', 'out of bounds')
    positive('capped_operations', '''
var^a=i32^[max^](4)
let^src={7,8}
a.push(1)
let^r=a.push(2)
a.append(src[..])
print^(a.len,a[0],*r,a[3],a[1 ..].len)
a.pop() a.truncate(1)
print^(a.len)
a.clear()
for^ i from^ 0 to^ 3 {a.push(i32^(i)*10)}
print^(a.len,a[3])
''', '4 1 2 8 3\n1\n4 30\n')
    positive('capped_reference_field_result_and_reuse', '''
def^W {self^{n:i32^, xs:i64^[max^]}}
let^fill=p^b:ref^i64^[max^] {b.push(9)}
let^build=f^c:i64^ -> i64^[max^] {var^r=i64^[max^](c) r.push(c) return^r}
var^w=W{n=1, xs=i64^[max^](3)}
fill(ref^w.xs) fill(ref^w.xs)
let^q=build(3)
let^copy=i64^[](q)
var^total:i64^=0
for^ k from^ 1 to^ 1000 {
    var^c=u8^[max^](10000000)
    c.push(u8^(1))
    total+=c.len
}
print^(w.xs.len,w.xs[1],q.len,q[0],copy[0],total)
''', '2 9 1 3 3 1000\n')
    negative('capped_literal', 'var^a:i32^[max^]={1,2}', 'is built with {max^ capacity}')
    negative('capped_empty_value', 'let^a=i32^[max^]{}', 'is built with')
    negative('capped_wrong_capacity', 'let^n:i32^=2 let^a=i32^[max^](n)',
             'array capacity must be i64^')
    negative('capped_constructor_arity', 'let^a=i32^[max^](1,2)', 'expects a capacity')
    negative('capped_no_grow_conversion', 'let^a:i32^[grow^]=i32^[max^](2)', 'cannot initialize')
    negative('capped_forbid_implicit_copy', 'let^a=i32^[max^](2) let^b=a', 'owned')
    suite.runtime_failure('capped_push_full', 'var^a=i32^[max^](1) a.push(1) a.push(2)',
                          'bounded array is full')
    suite.runtime_failure('capped_append_full', 'var^a=i32^[max^](2) let^s={1,2,3} a.append(s[..])',
                          'bounded array is full')
    suite.runtime_failure('capped_negative_capacity', 'let^a=i32^[max^](-1)', 'data stack')
    suite.runtime_failure('capped_overflow_capacity', 'let^a=i64^[max^](9223372036854775807)',
                          'data stack')
    suite.finish()


if __name__ == '__main__':
    main()
