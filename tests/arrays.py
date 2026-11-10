"""Runtime-sized owning arrays with a length fixed after construction."""
from harness import Suite


def main():
    suite = Suite('arrays')
    positive, negative = suite.positive, suite.negative
    def invalid_rows_at_declaration(name, text, line, error):
        def check():
            path = suite.source(name,text)
            result = suite.run([suite.compiler,'check',path],False)
            assert f':{line}:' in result.stderr, result.stderr
            assert error in result.stderr, result.stderr
        suite.case(name,check)
    for name, declaration in (
            ('inferred','var^d=i32^[grow^[grow^]]{}'),
            ('annotated','var^d:i32^[grow^[grow^]]={}'),
            ('unsupported_size_constructor','var^d=i32^[[grow^]](8)'),
            ('capacity_constructor','var^d=i32^[max^[grow^]](8)')):
        error = ('parentheses are reserved for size/capacity' if name == 'unsupported_size_constructor'
                 else 'nonempty fixed-size')
        for use, suffix in (('unused',''), ('push','\nd.push({})'),
                            ('index','\nd.push({})\nd[0].push(1)')):
            invalid_rows_at_declaration('invalid_rows_declaration_' + name + '_' + use,
                                        declaration + suffix + '\n',1,error)
        invalid_rows_at_declaration('invalid_rows_declaration_function_' + name,
                                   'let^test=p^{\n' + declaration + '\nd.push({})\nd[0].push(1)\n}\ntest()\n',2,error)
    row_runtime = suite.work/'jagged_runtime.c'
    positive('jagged_initialize_eight_empty_rows', '''
var^d=i32^[max^8[grow^]]{}
repeat^8 {d.push({})}
d[0].push(0)
d[7].push(7)
print^(d.len,d[0][0],d[7][0])
''', '8 0 7\n')
    row_runtime.write_text('''
#define rh_print_integer rh_base_print_integer
#define rh_runtime_shutdown rh_base_shutdown
#include "''' + (suite.repo/'runtime/rhat_runtime.c').as_posix() + '''"
#undef rh_print_integer
#undef rh_runtime_shutdown
void rh_print_integer(const void *value, int32_t bits, int32_t uns) {
    uint64_t used = 0;
    for (uint64_t j=0; j<rh_context->count; j++) {
        used += (uint64_t)(rh_context->stacks[j].top-rh_context->stacks[j].base);
    }
    if (used != 4) rh_panic("owned row retry leaked storage",0);
    rh_base_print_integer(value,bits,uns);
}
void rh_runtime_shutdown(void) {
    for (uint64_t j=0; j<rh_context->count; j++) {
        if (rh_context->stacks[j].top != rh_context->stacks[j].base) {
            rh_panic("owned row scope leaked storage",0);
        }
    }
    rh_base_shutdown();
}
''', encoding='utf-8')
    positive('jagged_error_restores_row_stacks', '''
errordef^E {Bad}
let^fail=f^ -> i32^|E.Bad {E.Bad{}}
var^d=i32^[max^2[grow^]]{}
repeat^1000 {
    do^{d.push({1,2,try^fail()}) catch^: assert^(d.len=0)}
}
d.push({7})
print^(d[0][0])
''', '7\n', extra=['--runtime',row_runtime])
    positive('fixed_jagged_rows_and_views', '''
var^d = i32^[3 [grow^]]{{1},{},{7,8}}
d[0].push(2)
d[1].push(5)
var^at:i32^=2
let^row=d[at]
row.push(9)
var^sum:i32^=0
for^r in^d {for^n in^r {sum+=n}}
do^{var^view=d[1 ..] view[0].push(6)}
print^(d.len,d[0][1],d[1].len,row[2],sum)
''', '3 2 2 9 32\n')
    positive('fixed_jagged_empty_rows_and_parenthesized', '''
var^d=(i32^[grow^])[2]{}
d[0].push(4)
d[1].push(5)
print^(d.len,d[0][0],d[1][0])
''', '2 4 5\n')
    positive('fixed_default_scalar_and_nested_arrays', '''
var^a=i32^[3]{}
var^b=bool^[2]{}
var^c=f32^[2,3]{}
var^d=i32^[2 [3]]{}
a[1]:=7
d[1][2]:=9
print^(a[0],a[1],a[2],b[0],b[1],c[1,2],d[0][2],d[1][2])
''', '0 7 0 false false 0.0 0 9\n')
    positive('fixed_default_independent_owned_rows', '''
var^d=i32^[8 [grow^]]{}
d[0].push(7)
d[7].push(9)
var^deep=i32^[2 [2 [grow^]]]{}
deep[1][0].push(5)
var^bounded=i32^[max^8 [grow^]]{}
print^(d.len,d[0][0],d[1].len,d[7][0],deep[0][0].len,deep[1][0][0],bounded.len)
''', '8 7 0 9 0 5 0\n')
    positive('fixed_default_struct_fields_and_disposal', '''
import^std.sys
def^Row:std.sys.Disposable {
 self^{n=7, text="ok", values:i32^[grow^]}
 dispose=p^self^{print^(self^.n,self^.values.len)}
}
struct^Table {rows:Row[2]}
do^{
 var^table=Table{}
 table.rows[0].values.push(3)
 print^(table.rows[1].text,table.rows[1].values.len)
}
''', 'ok 0\n7 0\n7 1\n')
    positive('fixed_default_partial_construction_unwinds', '''
import^std.sys
errordef^E {Bad}
def^Counter {
 var^calls:i32^=0
 next=p^ -> i32^|E.Bad {
  calls+=1
  if^calls=2 {return^E.Bad{}}
  return^calls
 }
}
def^D:std.sys.Disposable {
 self^{n=try^Counter.next(), values:i32^[grow^]}
 dispose=p^self^{print^(self^.n)}
}
let^make=p^ -> D[3]|E.Bad {return^D[3]{}}
do^{var^d=try^make() catch^: print^(99)}
print^(Counter.calls)
''', '1\n99\n2\n')
    for name, text, error in (
            ('string', 'let^a=string^[2]{}', 'needs an initializer'),
            ('reference', 'let^a:i32^[ref^2]={}', 'needs an initializer'),
            ('union', 'let^a:(i32^|nil^)[2]={}', 'needs an initializer'),
            ('required_field', 'def^D {self^{s:string^}} let^a=D[2]{}', "field 's' needs an initializer")):
        negative('fixed_default_reject_' + name,text,error)
    positive('fixed_jagged_returns_and_nested_owners', '''
let^make=f^ -> i32^[2 [grow^]] {return^i32^[2 [grow^]]{{10},{20}}}
let^add=p^a:ref^i32^[2 [grow^]] {a[1].push(21)}
var^d=make()
add(ref^d)
var^deep=i32^[2 [2 [grow^]]]{{{1},{2}},{{3},{4}}}
deep[1][0].push(30)
var^mixed=i32^[max^2 [2 [grow^]]]{}
mixed.push({{5},{6}})
mixed[0][1].push(60)
var^reverse=i32^[2 [max^2 [grow^]]]{{{7}},{{8}}}
reverse[1].push({9})
print^(d[1][1],deep[1][0][1],mixed[0][1][1],reverse[1][1][0])
''', '21 30 60 9\n')
    positive('fixed_jagged_struct_owned_tail', '''
def^Row {self^{id:i32^, values:i32^[grow^]}}
struct^Table {rows:Row[2]}
var^t=Table{rows={{id=1,values={2}},{id=3,values={4}}}}
t.rows[1].values.push(5)
print^(t.rows[0].id,t.rows[1].values[1])
''', '1 5\n')
    positive('fixed_jagged_dispose_partial_construction', '''
import^std.sys
errordef^E {Bad}
def^D:std.sys.Disposable {
 self^{n:i32^}
 dispose=p^self^{print^(self^.n)}
}
let^fail=f^ -> i32^|E.Bad {E.Bad{}}
do^{
 var^d=D[2 [grow^]]{{D{n=1},D{n=2}},{D{n=3},D{n=try^fail()}}}
catch^: print^(99)
}
do^{var^d=D[2 [grow^]]{{D{n=4}},{D{n=5}}}}
''', '3\n2\n1\n99\n5\n4\n')
    positive('fixed_jagged_error_return_checkpoint', '''
errordef^E {Bad}
let^fail=f^ -> i32^|E.Bad {E.Bad{}}
let^make=p^ -> i32^[2 [grow^]] {
 return^i32^[2 [grow^]]{{1,2},{3,try^fail()}}
catch^:
 return^i32^[2 [grow^]]{{7},{}}
}
repeat^1000 {
 do^{var^d=i32^[2 [grow^]]{{1},{2,try^fail()}} catch^: assert^(1=1)}
}
var^d=make()
print^(d[0][0])
''', '7\n', extra=['--runtime',row_runtime])
    for name, text, error in (
            ('missing', 'var^d=i32^[2[grow^]]{{1}}', 'exactly 2 elements'),
            ('extra', 'var^d=i32^[2[grow^]]{{},{},{}}', 'exactly 2 elements'),
            ('copy', 'var^d=i32^[2[grow^]]{{},{}} let^copy=d', 'cannot be copied'),
            ('row_copy', 'var^r=i32^[grow^]{} var^d=i32^[1[grow^]]{r}', 'cannot be copied'),
            ('readonly', 'let^d=i32^[1[grow^]]{{}} d[0].push(1)', 'cannot modify'),
            ('row_escape', 'let^bad=p^ -> ref^i32^[grow^] {var^d=i32^[1[grow^]]{{}} return^d[0]} let^r=bad()', 'cannot return'),
            ('thread_transfer', 'let^c=import^std.channel var^q=try^c.Channel<i32^[2[grow^]]>.new(1,1024)', 'thread transfer of owning row arrays'),
            ('repeat_copy', 'var^d=i32^[2[grow^]]{{} repeat^2}', 'repeat^ cannot copy owned elements'),
            ('stack_overflow', 'var^d:i32^[1000000 [1000000 [grow^]]]={}', 'data stack count overflows')):
        negative('fixed_jagged_reject_' + name,text,error)
    positive('fixed_jagged_callee_shrink_without_borrow', '''
let^clear=p^a:ref^i32^[2[flex^]] {a[0].clear()}
var^d=i32^[2[flex^]]{{1},{2}}
clear(ref^d)
d[0].push(9)
print^(d[0][0],d[1][0])
''', '9 2\n')
    negative('fixed_jagged_reject_callee_flex_shrink', '''
let^clear=p^a:ref^i32^[2[flex^]] {a[0].clear()}
var^d=i32^[2[flex^]]{{1},{2}}
let^r=ref^d[0][0]
clear(ref^d)
print^(*r)
''', 'cannot shrink')
    negative('fixed_jagged_reject_callee_owned_row_shrink', '''
let^clear=p^a:ref^i32^[2[max^2[grow^]]] {a[0].clear()}
var^d=i32^[2[max^2[grow^]]]{{{1}},{}}
let^r=d[0][0]
clear(ref^d)
print^(r.len)
''', 'cannot shrink')
    for method,args in [('push','{}'),('pop',''),('clear',''),('truncate','0')]:
        negative('fixed_jagged_reject_' + method,
                 'var^d=i32^[2[grow^]]{{},{}} d.' + method + '(' + args + ')',
                 'method call requires a structure')
    positive('jagged_nested_owners_and_struct_rows', '''
def^Row {self^{id:i32^, values:i32^[grow^]}}
var^rows = Row[max^2]{{id=7,values={1,2}}}
rows.push({id=8,values={3}})
rows[1].values.push(4)
var^deep = i32^[max^2 [max^2 [grow^]]]{}
deep.push({{10},{20,21}})
deep[0][1].push(22)
deep.push({})
deep[1].push({30})
print^(rows[1].id,rows[1].values[1],deep[0][1][2],deep[1][0][0])
deep.clear()
''', '8 4 22 30\n')
    positive('jagged_disposable_rows', '''
import^std.sys
def^D:std.sys.Disposable {
    self^{n:i32^}
    dispose=p^self^{print^(self^.n)}
}
do^{
    var^d = D[max^2 [grow^]]{{D{n=1},D{n=2}},{D{n=3}}}
    d.pop()
    d.clear()
    d.push({D{n=4}})
}
''', '3\n2\n1\n4\n')
    positive('jagged_error_retry_and_return_checkpoint', '''
errordef^E {Bad}
let^fail = f^ -> i32^|E.Bad {E.Bad{}}
let^make = p^ -> i32^[max^2 [grow^]] {
    return^i32^[max^2 [grow^]]{{1},{2,try^fail()}}
catch^:
    return^i32^[max^2 [grow^]]{{7},{8,9}}
}
var^d = i32^[max^2 [grow^]]{}
repeat^1000 {
    do^{d.push({1,try^fail()}) catch^: assert^(d.len=0)}
}
d.push({5})
var^result = make()
print^(d.len,d[0][0],result[1][1])
''', '1 5 9\n')
    negative('jagged_reject_reentrant_construction', '''
let^make=p^d:ref^i32^[max^2 [grow^]] -> i32^[grow^] {
    d.push({1})
    return^i32^[grow^]{2}
}
var^d=i32^[max^2 [grow^]]{}
d.push(make(ref^d))
''', 'while constructing one of its owned rows')
    negative('jagged_reject_readonly_row', 'let^d=i32^[max^2[grow^]]{{1}}\nd[0].push(2)\n', 'cannot modify')
    negative('jagged_reject_thread_transfer', 'let^c=import^std.channel\nvar^q=try^c.Channel<i32^[max^2[grow^]]>.new(1,1024)\n', 'thread transfer of owning row arrays')
    negative('jagged_reject_stack_count_overflow', 'var^d:i32^[max^1000000 [max^1000000 [grow^]]]={}\n', 'data stack count overflows')
    negative('jagged_reject_metadata_size_overflow', '''
def^Huge {
    self^{
        a:u8^[65536,65536,65536,65535],
        b:u8^[65536,65536,65535],
        c:u8^[65536,65535],
        d:u8^[65232],
        rows:i32^[max^8 [grow^]]
    }
}
var^d:Huge[max^1]={}
''', 'array layout overflows')
    positive('jagged_owned_rows', '''
var^d = i32^[max^8 [grow^]]{}
d.push({1})
d.push({})
d.push({7,8,9})
d[0].push(2)
d[1].push(5)
let^row = d[0]
row.push(3)
d.push({10})
var^n:i32^ = 1
print^(d.len,row.len,d[n][0],d[2][2],d[3][0])
''', '4 3 5 9 10\n')
    positive('jagged_shrink_reuse_and_iteration', '''
var^d = i32^[max^3 [grow^]]{{1,2},{3}}
var^sum = 0
for^row in^d { for^n in^row { sum += n } }
d.pop()
d.push({4,5,6})
d.truncate(1)
d.clear()
for^i from^0 to^1000 { d.push({7,8}) d.clear() }
d.push({9})
print^(sum,d.len,d[0][0])
''', '6 1 9\n')
    positive('jagged_return_and_borrowed_mutation', '''
let^make = f^ -> i32^[max^3 [grow^]] {
    var^d = i32^[max^3 [grow^]]{{1},{2,3}}
    return^d
}
let^add = p^d:ref^i32^[max^3 [grow^]] { d.push({4,5,6}) }
var^d = make()
add(ref^d)
print^(d[0][0],d[1].len,d[2][2])
''', '1 2 6\n')
    for name, tail in (
            ('row_borrow', 'let^r=d[0]\nd.clear()\nprint^(r.len)'),
            ('element_borrow', 'let^r=d[0].push(1)\nd.clear()\nprint^(*r)'),
            ('callee', 'let^clear=p^a:ref^i32^[max^2[grow^]]{a.clear()}\nlet^r=d[0]\nclear(ref^d)\nprint^(r.len)')):
        negative('jagged_reject_' + name,
                 'var^d=i32^[max^2[grow^]]{{1}}\n' + tail + '\n', 'cannot shrink')
    negative('jagged_reject_copy', 'var^d=i32^[max^2[grow^]]{{1}}\nlet^copy=d\n', 'cannot be copied')
    negative('jagged_reject_row_escape', 'let^bad=p^ -> ref^i32^[grow^]{var^d=i32^[max^2[grow^]]{{1}} return^d[0]}\nlet^r=bad()\n', 'cannot return')
    suite.runtime_failure('jagged_capacity', 'var^d=i32^[max^1[grow^]]{}\nd.push({})\nd.push({})\n', 'bounded array is full')
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
            ('too_many', 'let^a=i32^[2,3]{{1,2,3},{4,5,6}}\nprint^(a[0,1,2])', 'indexing requires an array'),
            ('row_shape', 'let^a=i32^[2,3]{{1,2},{3,4,5}}', 'exactly 3 elements'),
            ('zero_dimension', 'var^a:i32^[2,0] = {}', 'array length out of range'),
            ('dynamic_dimension', 'var^n=2\nvar^a:i32^[n,3] = {}', 'constant integer expression'),
            ('max_dimension', 'var^a:i32^[max^2,3] = {}', 'require fixed lengths'),
            ('mixed_range', 'let^a=i32^[2,3]{{1,2,3},{4,5,6}}\nprint^(a[0,0..2])', 'requires integer indices'),
            ('wrapped_chain', 'let^a = (i32^[2][3,4])[1]{}', 'consecutive array type brackets')):
        negative('rectangular_reject_' + name, source + '\n', diagnostic)
    positive('array_dimension_syntax_is_equivalent', '''
var^m={{0 repeat^3} repeat^3}
m[1,1]:=7
m[2][0]:=9
var^other:i32^[3,3]=m
var^nested:i32^[3 [3]]=other
def^Box<T> {self^{value:T}}
var^box=Box<i32^[3,3]>{value=m}
var^alias:Box<i32^[3 [3]]>=box
let^row=m[1]
print^(m[1][1],m[2,0],nested[1,1],alias.value[2][0],row[1])
print^(typeof^(m).signature^)
''', '7 9 7 9 7\ni32^[3, 3]\n')
    positive('array_dimension_grow_and_jagged_indexing', '''
var^a=i32^[grow^,3]{{1,2,3}}
let^add=p^x:ref^i32^[grow^ [3]] {x.push({4,5,6})}
add(ref^a)
var^j=i32^[max^8 [grow^]]{{7},{8,9}}
j[1,1]:=10
var^copy={j...}
copy[0].push(11)
print^(a[1][2],a[1,2],j[1][1],copy[0,1])
print^(typeof^(a).signature^,typeof^(j).signature^)
''', '6 6 10 11\ni32^[grow^, 3] i32^[max^8 [grow^]]\n')
    positive('array_dimension_read_slice_display_roundtrip', '''
var^data=i32^[3]{1,2,3}
var^views:(read^i32^[..])[2]={data[..],data[..]}
let^same:i32^[2 [read^ ..]]=views
print^(same[1,2],typeof^(same).signature^)
''', '3 i32^[2 [read^ ..]]\n')
    positive('rectangular_grow_rows_and_operations', '''
var^a=i32^[grow^,3]{}
a.push({1,2,3})
a.push({4,5,6})
a[1,2]+=1
let^extra=i32^[2,3]{{7,8,9},{10,11,12}}
a.append(extra[..])
print^(a.len,a[0,1],a[1,2],a[3,2],a.bytes.len)
let^last=a.pop()
print^(last[2])
a.truncate(1)
print^(a.len,a[0,2])
a.clear()
a.push({})
print^(a.len,a[0,2])
''', '4 2 7 12 48\n12\n1 3\n1 0\n')
    positive('rectangular_grow_three_dimensions_and_inference', '''
var^a=i32^[grow^,2,3]{}
a.push({{1,2,3},{4,5,6}})
a[0,1,2]:=9
var^b=i32^[grow^,_^]{{10,11},{12,13}}
let^add=p^rows:ref^i32^[grow^,2] {rows.push({14,15})}
add(ref^b)
print^(a.len,a[0,1,2],b.len,b[2,1])
var^sum:i32^=0
for^row in^b {for^n in^row {sum+=n}}
print^(sum,b[..][1][0])
''', '1 9 3 15\n75 12\n')
    positive('rectangular_grow_return_and_generic_identity', '''
let^make=p^ -> i32^[grow^,2] {return^i32^[grow^,2]{{1,2}}}
def^Box<T> {self^{values:T}}
var^a=make()
var^box=Box<i32^[grow^,2]>{values={{3,4}}}
var^fixed=Box<i32^[2,2]>{values={{5,6},{7,8}}}
var^nested=Box<i32^[grow^ [2]]>{values={{9,10}}}
a.push({11,12})
print^(a[1,1],box.values[0,1],fixed.values[1,1],nested.values[0][1])
''', '12 4 8 10\n')
    for name, source, diagnostic in (
            ('inner_grow','var^a=i32^[3,grow^]{}','remaining dimensions require fixed lengths'),
            ('two_grow','var^a=i32^[grow^,grow^]{}','remaining dimensions require fixed lengths'),
            ('inner_flex','var^a=i32^[grow^,flex^]{}','remaining dimensions require fixed lengths'),
            ('outer_flex','var^a=i32^[flex^,3]{}','only grow^'),
            ('row_shape','var^a=i32^[grow^,3]{} a.push({1,2})','exactly 3 elements'),
            ('too_many','var^a=i32^[grow^,3]{} print^(a[0,1,2])','indexing requires an array'),
            ('copy','var^a=i32^[grow^,3]{} let^b=a','cannot be copied'),
            ('distinct_fixed','var^a=i32^[grow^,3]{} let^use=p^x:ref^i32^[2,3]{} use(ref^a)','requires nominal')):
        negative('rectangular_grow_reject_' + name,source,diagnostic)
    for name,index in (('row','1,0'),('column','0,3'),('negative','-1,0')):
        suite.runtime_failure('rectangular_grow_bounds_' + name,
            'var^a=i32^[grow^,3]{{1,2,3}} print^(a[' + index + '])', 'index out of bounds')
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
    negative('nested_unbounded_owning_rows_still_unsupported',
             'var^rows:i32^[grow^ [grow^]] = {}\n', 'nonempty fixed-size')
    negative('parenthesized_owning_rows_still_unsupported',
             'var^rows:(i32^[grow^])[grow^] = {}\n', 'nonempty fixed-size')
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
    for name, constructor in (
            ('owned', 'i32^[](src)'),
            ('references', 'i32^[ref^](src)'),
            ('bounded', 'i32^[max^8](src)'),
            ('grow', 'i32^[grow^](src)'),
            ('flex', 'i32^[flex^](src)'),
            ('parenthesized', '(i32^[2])[](src)')):
        negative('reject_parenthesized_copy_' + name,
                 'var^src:i32^[2]={1,2} let^copy=' + constructor,
                 'parentheses are reserved for size/capacity')
    positive('spread_infer_source_array_types', '''
var^mm=i32^[]{0 repeat^5}
var^copy={mm...}
var^fixed={1,2,3}
var^other={fixed...}
mm[0]:=9
fixed[0]:=8
print^(copy fits^i32^[],other fits^i32^[3],copy[0],other[0],copy.len)
''', 'true true 0 1 5\n')
    positive('spread_fixed_rectangular_and_inferred_shapes', '''
var^m=i32^[3,3]{}
m[2,0]:=1
var^a=i32^[3,3]{m...}
var^b=i32^[_^,_^]{m...}
var^c={m...}
m[2,0]:=9
a[0,0]:=7
print^(a[2,0],b[2,0],c[2,0],b[0,0],c fits^i32^[3,3])
var^cube=i32^[2,2,3]{}
cube[1,1,2]:=6
var^deep=i32^[_^,2,_^]{cube...}
print^(deep[1,1,2])
''', '1 1 1 0 true\n6\n')
    positive('spread_owned_rows_deep_copy', '''
var^cm=u8^[3 []]{{1,2},{3},{4,5,6}}
var^a=u8^[3 []]{cm...}
var^b=u8^[_^ []]{cm...}
var^c={cm...}
cm[0][0]:=9
a[1][0]:=8
print^(a[0][0],b[0][0],c[0][0],cm[1][0],b[1].len,c[2].len)
var^grow=i32^[2 [grow^]]{{1},{2,3}}
var^copy={grow...}
grow[0].push(4)
copy[1].push(5)
print^(copy[0].len,grow[1].len,copy[1][2])
''', '1 1 1 3 1 3\n1 2 5\n')
    positive('spread_bounded_and_nested_owned_rows', '''
var^a=i32^[max^3 [grow^]]{{1},{2,3}}
var^b={a...}
a.clear()
b[0].push(4)
var^deep=i32^[2 [2 [grow^]]]{{{1},{2}},{{3},{4}}}
var^copy={deep...}
deep[1][0].push(5)
copy[0][0].push(6)
print^(a.len,b.len,b[0][1],copy[1][0].len,deep[0][0].len,copy[0][0][1])
''', '0 2 4 1 1 6\n')
    positive('spread_runtime_source_to_fixed_array', '''
var^src=i32^[]{1,2,3}
var^a=i32^[3]{src...}
var^b=i32^[max^4]{src...}
var^c={src[..]...}
print^(a[2],b.len,c[1],c fits^i32^[])
''', '3 3 2 true\n')
    positive('spread_deep_copy_survives_source_scope', '''
let^make=p^ -> i32^[2 [grow^]] {
 var^src=i32^[2 [grow^]]{{1,2},{3}}
 return^{src...}
}
var^a=make()
let^copy=p^src:ref^read^i32^[2 [grow^]] -> i32^[2 [grow^]] {return^{src...}}
var^b=copy(ref^a)
a[0].push(4)
b[1].push(5)
print^(a[0][2],b[0].len,a[1].len,b[1][1])
''', '4 2 1 5\n')
    positive('spread_source_evaluated_once_and_element_borrows', '''
let^choose=p^n:ref^i32^, a:ref^i32^[3] -> ref^i32^[3] {*n+=1 return^a}
var^calls:i32^=0
var^a=i32^[3]{1,2,3}
var^b={choose(ref^calls,ref^a)...}
var^n:i32^=7
var^refs=i32^[ref^2]{ref^n,ref^n}
var^copy={refs...}
n:=9
print^(calls,b[2],*copy[0],*copy[1])
''', '1 3 9 9\n')
    positive('spread_convert_fixed_row_shape', '''
var^a=i32^[2,2,2]{{{1,2},{3,4}},{{5,6},{7,8}}}
var^b=i32^[grow^ [2 [2]]]{a...}
var^c=i32^[max^3 [2 [2]]]{a...}
print^(b.len,b[1][1][1],c.len,c[0][1][0])
''', '2 8 2 3\n')
    positive('spread_template_infers_each_source_shape', '''
let^copy=template^p^source {return^i32^[_^,_^]{source...}}
var^a=i32^[2,3]{}
var^b=i32^[3,2]{}
a[1,2]:=7
b[2,1]:=9
var^x=copy(ref^a)
var^y=copy(ref^b)
print^(x.len,x[1,2],y.len,y[2,1])
''', '2 7 3 9\n')
    for name,source,error in (
            ('fixed_length','var^a=i32^[2]{{1,2,3}...}','exactly 2 elements'),
            ('runtime_shape','var^a=i32^[]{1,2} var^b=i32^[_^]{a...}','runtime-sized source'),
            ('scalar','var^a={42...}','requires an array or slice'),
            ('borrow_escape','let^bad=p^ -> i32^[ref^2] {var^n:i32^=1 var^a=i32^[ref^2]{ref^n,ref^n} return^{a...}} let^b=bad()','cannot return'),
            ('disposable','import^std.sys def^D:std.sys.Disposable {dispose=p^self^{}} var^a=D[2]{} var^b={a...}','Disposable')):
        negative('spread_reject_' + name,source,error)
    suite.runtime_failure('spread_runtime_fixed_length_mismatch',
        'var^a=i32^[]{1,2} var^b=i32^[3]{a...}', 'assertion failed')
    positive('sized_copy_sources', '''
var^fixed:i32^[3]={1,2,3}
var^grow:i32^[grow^]={4,5}
var^bounded:i32^[max^8]={6,7}
let^a=i32^[]{fixed...}
let^b=i32^[]{(ref^grow)...}
let^c=i32^[]{bounded[..]...}
let^d=i32^[]{({8,9})...}
fixed[0]:=99 grow[0]:=99 bounded[0]:=99
let^e=i32^[]{a...}
print^(a[0],b[0],c[0],d[0],e.len,e[2])
''', '1 4 6 8 3 3\n')
    positive('sized_copy_return_independent', '''
let^make=f^n:i64^ {
 var^source:i64^[grow^]={n,n+1}
 return^i64^[]{source[..]...}
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
let^b=Node[ref^]{a...}
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
        ('fixed_count','let^a:i32^[3]={0 repeat^ 2}', 'built by repeat^ with the constant count 3'),
        ('capped','var^a:i32^[max^]={1 repeat^ 2}', 'repeat^ builds T[N], T[] and T[grow^]'),
        ('variable_length','var^n:i64^=2\nlet^a:i32^[n]={}', 'constant integer expression'),
        ('zero_length','let^N:i64^=1\nlet^a:i32^[N - 1]={}', 'out of range'),
        ('capacity_owner','let^a:i32^[]={max^ 3}', 'builds a T[max^]'),
        ('pair','let^a=i32^[](3,0)', 'parentheses are reserved for size/capacity'),
        ('mixed','let^a:i32^[]={1, 2 repeat^ 3}', "expected '}'"),
        ('copy_mismatch','let^a:i32^[]={(i64^[]{1})...}', 'cannot copy'),
    ]:
        negative('form_'+name,source,error)
    positive('repeat_max_nested_fixed', '''
var^a=u8^[2 [3]]{{0 repeat^max^} repeat^max^}
var^b:u8^[2 [3]]={{4 repeat^max^} repeat^max^}
var^c=u8^[2,3]{{5 repeat^max^} repeat^max^}
var^d=u8^[2 [3 [4]]]{{{6 repeat^max^} repeat^max^} repeat^max^}
a[0][1]:=9
print^(a.len,a[0].len,a[0][1],a[1][1],b[1][2],c[1,2],d[1][2][3])
''', '2 3 9 0 4 5 6\n')
    positive('repeat_max_context_and_single_evaluation', '''
let^value=p^calls:ref^i64^ ->u8^ {*calls+=1 return^u8^(7)}
let^make=f^ ->u8^[2 [3]] {return^{{8 repeat^max^} repeat^max^}}
def^Grid {self^{cells:u8^[2 [3]]={{3 repeat^max^} repeat^max^}}}
var^calls:i64^=0
var^a=u8^[2 [3]]{{value(ref^calls) repeat^max^} repeat^max^}
let^b=make()
let^g=Grid{}
print^(calls,a[1][2],b[0][2],g.cells[1][2])
''', '1 7 8 3\n')
    for name,source in (
            ('untyped', 'var^a={0 repeat^max^}'),
            ('grow', 'var^a=u8^[grow^]{0 repeat^max^}'),
            ('sized', 'var^a=u8^[]{0 repeat^max^}'),
            ('bounded', 'var^a=u8^[max^8]{0 repeat^max^}'),
            ('capped', 'var^a=u8^[max^]{0 repeat^max^}'),
            ('nested_grow', 'var^a=u8^[2[grow^]]{{0 repeat^max^},{}}')):
        negative('repeat_max_reject_' + name,source,'requires a fixed-length array type')
    negative('repeat_max_inner_count_mismatch',
             'var^a=u8^[2[3]]{{0 repeat^2} repeat^max^}', 'constant count 3')
    positive('inferred_lengths_fixed_and_rectangular', '''
var^a=i32^[_^]{0 repeat^5}
var^b=i32^[_^,_^]{{0 repeat^3} repeat^3}
var^c=i32^[2,_^]{{1,2,3},{4,5,6}}
var^d=i32^[_^,3]{{7 repeat^max^} repeat^2}
var^e:i32^[_^,_^]={{8,9},{10,11}}
a[4]:=12 b[2,2]:=13
print^(a.len,a[4],b.len,b[2,2],c[1,2],d[1,2],e[1,1])
''', '5 12 3 13 6 7 11\n')
    positive('inferred_lengths_nested_and_owned', '''
var^a=u8^[_^ [_^]]{{1,2},{3,4}}
var^b=i32^[_^ [grow^]]{{1},{2,3}}
b[0].push(4)
var^c=i32^[grow^ [_^]]{{5,6},{7,8}}
c.push({9,10})
var^d=u8^[_^,_^,_^]{{{0 repeat^4} repeat^3} repeat^2}
print^(a[1][1],b.len,b[0][1],c[2][1],d[1,2,3])
''', '4 2 4 10 0\n')
    positive('inferred_lengths_defaults_and_constants', '''
let^N:i64^=3
let^_:i64^=2
def^Grid {self^{cells:u8^[_^,_^]={{7 repeat^3} repeat^2}}}
let^make=f^ ->u8^[2,3] {return^u8^[_^,_^]{{8 repeat^3} repeat^2}}
var^a=i32^[_^]{9 repeat^N}
var^b=i32^[_]{10 repeat^max^}
let^g=Grid{}
let^v=make()
print^(a.len,b.len,g.cells[1,2],v[1,2])
''', '3 2 7 8\n')
    for name,source,error in (
            ('cycle', 'var^a=i32^[_^]{0 repeat^max^}', 'cannot infer array length _^'),
            ('empty', 'var^a=i32^[_^]{}', 'inferred array length out of range'),
            ('runtime', 'var^n:i64^=3 var^a=i32^[_^]{0 repeat^n}', 'cannot infer array length _^'),
            ('ragged', 'var^a=i32^[_^,_^]{{1,2},{3}}', 'inconsistent inferred array length'),
            ('nested_ragged', 'var^a=i32^[_^[_^]]{{1},{2,3}}', 'inconsistent inferred array length'),
            ('inner_cycle', 'var^a=i32^[_^,_^]{{0 repeat^max^} repeat^2}', 'cannot infer array length _^'),
            ('specified_mismatch', 'var^a=i32^[2,_^]{{1,2}}', 'exactly 2 elements'),
            ('oversize', 'var^a=i32^[_^]{0 repeat^1000001}', 'inferred array length out of range'),
            ('bounded', 'var^a=i32^[max^_^]{}', '_^ is only allowed for fixed array lengths'),
            ('parameter', 'let^f=p^a:ref^i32^[_^]{}', 'cannot infer array length _^')):
        negative('inferred_lengths_reject_' + name,source,error)
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
let^arrays=(i64^[2])[]{source...}
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
    negative('sized_wrong_element','let^a=i32^[]{({"x"})...}', 'cannot copy')
    negative('sized_wrong_source','let^a=i32^[]{42...}', 'array or slice')
    negative('sized_wrong_count','let^n:i32^=2 let^a=i32^[]{1 repeat^ n}', 'array length must be i64^')
    negative('sized_wrong_initial','let^a=i32^[]{"x" repeat^ 2}', "initial value must be 'i32^'")
    negative('sized_constructor_arity','let^a=i32^[](1,2,3)', 'parentheses are reserved for size/capacity')
    negative('sized_no_grow_conversion','let^a:i32^[grow^]=i32^[]{1 repeat^ 2}', 'cannot initialize')
    negative('sized_reference_escape','let^bad=f^ {var^x=1 return^i32^[ref^]{ref^x repeat^ 2}} let^a=bad()',
             'cannot return a reference')
    negative('sized_copy_reference_escape','let^bad=f^ {var^x=1 let^a:i32^[ref^1]={ref^x} return^i32^[ref^]{a...}} let^b=bad()',
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
let^copy=i64^[]{q...}
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
