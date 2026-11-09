"""Runtime-sized owning arrays with a length fixed after construction."""
from harness import Suite


def main():
    suite = Suite('arrays')
    positive, negative = suite.positive, suite.negative
    positive('rectangular_fixed_shapes', '''
var^a:i32^[2,3] = {{10,11,12},{20,21,22}}
let^b = i32^[2,3]{{30,31,32},{40,41,42}}
var^cube:i32^[2,2,2] = {{{1,2},{3,4}},{{5,6},{7,8}}}
a[1,2] += 7
cube[1,0,1] := 60
let^copy = a
print^(a.len,a[0,2],a[1,2],b[1,2],cube[1,0,1],copy[1,2])
''', '2 12 29 42 60 29\n')
    positive('rectangular_constants_generics_and_references', '''
let^WIDTH = 3
def^Grid<T> {self^{cells:T[2,WIDTH]}}
def^Box<T> {self^{value:T}}
let^get = f^a:ref^read^i32^[2,3] -> i32^ {a[1,2]}
var^grid = Grid<i32^>{cells={{1,2,3},{4,5,6}}}
let^r = ref^grid.cells[1,2];
*r := 9
let^box = Box<i32^[2,3]>{value={{11,12,13},{21,22,23}}}
let^nested = Box<(i32^[3])[2]>{value={{31,32,33},{41,42,43}}}
let^constructed = i32^[1+1,WIDTH]{{1,2,3},{4,5,6}}
let^outer = (i32^[2,3])[1]{constructed}
print^(get(ref^grid.cells),box.value[1,2],nested.value[1][2],outer[0][1,2])
''', '9 23 43 6\n')
    positive('rectangular_indices_evaluated_once_in_order', '''
let^step = p^n:ref^i64^ -> i64^ {
    let^before = *n;
    *n += 1
    return^before
}
let^get = p^a:ref^i32^[2,2], calls:ref^i64^ -> ref^i32^[2,2] {
    *calls += 1
    return^a
}
var^n:i64^ = 0
var^calls:i64^ = 0
var^a:i32^[2,2] = {{10,11},{20,21}}
get(ref^a,ref^calls)[step(ref^n),step(ref^n)] += 5
print^(calls,n,a[0,1])
''', '1 2 16\n')
    positive('rectangular_row_iteration_slices_and_layout', '''
var^a:i32^[2,3] = {{1,2,3},{4,5,6}}
var^sum = 0
for^row in^a { for^x in^row { sum += x } }
var^also = 0
inline^for^row in^a { inline^for^x in^row { also += x } }
let^rows = a[..]
let^range = 0..1
let^first = a[range]
rows[1][2] := 60
print^(sum,also,first[0][2],a[1,2],a.bytes.len)
''', '21 21 3 60 24\n')
    positive('rectangular_optional_indices_skip_when_nil', '''
let^index = p^calls:ref^i32^ -> i64^ { *calls += 1 return^0 }
var^a:i32^[2,3] = {{1,2,3},{4,5,6}}
var^r:ref^i32^[2,3]|nil^ = ref^a
var^calls = 0
print^(r?[index(ref^calls),index(ref^calls)]??0,calls)
r := nil^
print^(r?[index(ref^calls),index(ref^calls)]??7,calls)
''', '1 2\n7 2\n')
    for name, index in (('row','2,0'), ('column','0,3'), ('negative_row','-1,0'),
                        ('negative_column','0,-1')):
        suite.runtime_failure('rectangular_bounds_' + name,
                              'let^a = i32^[2,3]{{1,2,3},{4,5,6}}\nprint^(a[' + index + '])\n',
                              'index out of bounds')
    for name, source, diagnostic in (
            ('too_few', 'let^a=i32^[2,3]{{1,2,3},{4,5,6}}\nprint^(a[0])', 'requires 2 indices'),
            ('too_many', 'let^a=i32^[2,3]{{1,2,3},{4,5,6}}\nprint^(a[0,1,2])', 'requires 2 indices'),
            ('nested', 'let^a=i32^[2 [3]]{{1,2,3},{4,5,6}}\nprint^(a[0,1])', 'require a multidimensional array'),
            ('row_shape', 'let^a=i32^[2,3]{{1,2},{3,4,5}}', 'exactly 3 elements'),
            ('zero_dimension', 'var^a:i32^[2,0] = {}', 'array length out of range'),
            ('dynamic_dimension', 'var^n=2\nvar^a:i32^[n,3] = {}', 'constant integer expression'),
            ('grow_dimension', 'var^a:i32^[grow^,3] = {}', "expected ']'"),
            ('max_dimension', 'var^a:i32^[max^2,3] = {}', 'require fixed lengths'),
            ('mixed_range', 'let^a=i32^[2,3]{{1,2,3},{4,5,6}}\nprint^(a[0,0..2])', 'requires integer indices'),
            ('wrapped_chain', 'let^a = (i32^[2][3,4])[1]{}', 'consecutive array type brackets'),
            ('distinct_types', 'let^a=i32^[2,3]{{1,2,3},{4,5,6}}\nlet^b:(i32^[3])[2]=a', "expected '(i32^[3])[2]'")):
        negative('rectangular_reject_' + name, source + '\n', diagnostic)
    positive('nested_brackets_and_parenthesized_types', '''
var^a:i32^[2 [3]] = {{10,11,12},{20,21,22}}
var^b:(i32^[3])[2] = {{30,31,32},{40,41,42}}
let^c = i32^[2 [3]]{{50,51,52},{60,61,62}}
let^d = (i32^[3])[2]{{70,71,72},{80,81,82}}
var^rows:i32^[grow^ [3]] = {}
rows.push(a[0])
rows.push(b[1])
a[1][2] := 99
print^(a.len,a[0].len,a[1][2],b[1][2],c[1][2],d[1][2],rows[1][2])
''', '2 3 99 42 62 82 42\n')
    positive('nested_reference_and_slice_elements', '''
var^a:i32^[2] = {10,11}
var^b:i32^[2] = {20,21}
let^refs = i32^[ref^2 [2]]{ref^a,ref^b}
var^views:i32^[grow^ [..]] = {}
views.push(a[..])
views.push(b[..])
refs[0][1] := 12
views[1][0] := 22
let^more = (i32^[..])[2]{a[..],b[..]}
print^(a[1],b[0],more[0][1],more[1][0])
''', '12 22 12 22\n')
    positive('nested_generic_fields_and_constant_lengths', '''
let^WIDTH = 2
def^Grid<T> { self^{cells:T[2 [WIDTH + 1]]} }
var^grid = Grid<i32^>{cells={{10,11,12},{20,21,22}}}
let^copy = (i32^[WIDTH + 1])[2]{{30,31,32},{40,41,42}}
var^deep:i32^[2 [2 [2]]] = {{{1,2},{3,4}},{{5,6},{7,8}}}
print^(grid.cells[1][2],copy[1][2],deep[1][0][1])
''', '22 42 6\n')
    positive('nested_borrowed_growing_rows', '''
var^a:i32^[grow^] = {}
var^b:i32^[grow^] = {}
let^refs = (i32^[grow^])[ref^max^64]{ref^a,ref^b}
let^also = i32^[ref^max^64 [grow^]]{ref^a,ref^b}
refs[0].push(10)
also[1].push(20)
let^fixed = i32^[[2]]{{1,2},{3,4}}
print^(a[0],b[0],fixed[1][1])
''', '10 20 4\n')
    positive('nested_outer_first_lengths_and_parentheses', '''
let^ROWS = 1
let^COLS = 2
let^a = (i32^[ROWS+1 [COLS+1]])[1]{{{1,2,3},{4,5,6}}}
let^b = i32^[2,2 [3]]{{{1,2,3},{4,5,6}},{{7,8,9},{10,11,12}}}
let^c = i32^[2[(COLS+1)]]{{1,2,3},{4,5,6}}
let^indices = {1}
print^(a[0][1][2],b[1,0][2],c[indices[0]][2])
''', '6 9 6\n')
    for name,source in (
            ('annotation', 'var^a:i32^[2][3] = {}'),
            ('constructor', 'let^a = i32^[2][3]{}'),
            ('grow_constructor', 'let^a = i32^[grow^][max^64]{}'),
            ('parameter', 'let^f = p^x:i32^[2][3] {}'),
            ('return', 'let^f = f^ -> i32^[2][3] {}'),
            ('nested_header', 'var^a:i32^[4 [2][3]] = {}'),
            ('after_sugar', 'var^a:i32^[3 [2]][4] = {}'),
            ('wrapped_chain', 'let^a = (i32^[2][3])[]{}')):
        negative('nested_reject_chain_' + name, source + '\n', 'consecutive array type brackets')
    negative('nested_owning_rows_still_unsupported',
             'var^rows:i32^[max^64 [grow^]] = {}\n', 'nonempty fixed-size')
    negative('parenthesized_owning_rows_still_unsupported',
             'var^rows:(i32^[grow^])[max^64] = {}\n', 'nonempty fixed-size')
    for name, source in (
            ('annotation', 'var^a:i32^[[3] 2] = {}'),
            ('constructor', 'let^a = i32^[[3] 2]{}'),
            ('grow', 'var^a:i32^[[grow^] max^8] = {}'),
            ('parameter', 'let^f = p^a:i32^[[3] 2] {}')):
        negative('nested_reject_inner_first_' + name, source + '\n',
                 'nested array type must follow the outer policy')
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
let^nested=i32^[max^3 [2]]{{1,2},{3,4}}
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
        ('untyped_copy','let^b:i32^[]={1}\nlet^a={b...}', 'array construction needs a type'),
        ('fixed_count','let^a:i32^[3]={0 repeat^ 2}', 'built by repeat^ with the constant count 3'),
        ('capped','var^a:i32^[max^]={1 repeat^ 2}', 'repeat^ builds T[N], T[] and T[grow^]'),
        ('variable_length','var^n:i64^=2\nlet^a:i32^[n]={}', 'constant integer expression'),
        ('zero_length','let^N:i64^=1\nlet^a:i32^[N - 1]={}', 'out of range'),
        ('capacity_owner','let^a:i32^[]={max^ 3}', 'builds a T[max^]'),
        ('pair','let^a=i32^[](3,0)', 'write T[]{value repeat^ n}'),
        ('mixed','let^a:i32^[]={1, 2 repeat^ 3}', "expected '}'"),
        ('copy_mismatch','let^a:i32^[]={(i64^[]{1})...}', 'array or slice'),
    ]:
        negative('form_'+name,source,error)
    positive('constant_lengths', '''
let^W:i64^=8
let^H:i64^=W / 2
var^runtime:i64^=3
var^grid={0 repeat^ W * H}
var^table:i64^[W + 1]={7 repeat^ W + 1}
var^capped:u8^[max^H]={}
var^owned={1.5 repeat^ runtime}
let^last=f^ -> i64^ {
    let^local:i64^[H]={5 repeat^ H}
    return^ local[H - 1] + table[W]
}
grid[31]:=4 capped.push(1) owned[0]:=2.0
print^(grid.len,grid[31],table[8],capped.len,owned.len,last())
''', '32 4 7 1 3 12\n')
    positive('sized_empty_typed_value', '''
var^a=i32^[]{}
var^b:i32^[]={1,2}
print^(a.len,b.len,b[1])
''', '0 2 2\n')
    positive('sized_struct_and_fixed_elements', '''
def^Pair {self^{a:i64^,b:i64^}}
let^pairs=Pair[]{Pair{a=7,b=8} repeat^ 2}
var^source:(i64^[2])[2]={{1,2},{3,4}}
let^arrays=(i64^[2])[](source)
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
    negative('sized_forbid_owned_elements','let^a=(i32^[grow^])[]{}',
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
