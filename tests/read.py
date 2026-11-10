"""Read-path capabilities, aliased mutations and independent lifetime checks."""
from harness import Suite


def main():
    suite = Suite('read')
    positive, negative = suite.positive, suite.negative
    pre = '''def^Cell {
 self^{x:i32^=1}
 change=p^self^ {self^.x+=1}
 get=f^self^ ->i32^ {self^.x}
}
let^view=f^x:ref^Cell ->ref^read^Cell {x}
let^inspect=p^x:ref^read^Cell {print^(x.x)}
var^a=Cell{}
'''
    positive('lexical_release', pre+'''do^{
 let^r=view(ref^a)
 print^(r.x,r.get())
}
a.change()
print^(a.x)
''', '1 1\n2\n')
    positive('argument_call_only', pre+'''inspect(ref^a)
a.change()
print^(a.x)
''', '1\n2\n')
    positive('read_input_and_distinct_output', pre+'''let^copy=p^x:ref^read^Cell,y:ref^Cell {y.x:=x.x}
var^b=Cell{x=9}
copy(ref^a,ref^b)
a.change()
print^(a.x,b.x)
''', '2 1\n')
    positive('unrelated_storage', pre+'''var^b=Cell{}
do^{let^r=view(ref^a) b.change() print^(r.x,b.x)}
''', '1 2\n')
    positive('forward_return', pre+'''let^forward=f^x:ref^read^Cell ->ref^read^Cell {x}
do^{let^r=forward(view(ref^a)) print^(r.x)}
a.change()
''', '1\n')
    positive('copy_value_is_mutable', pre+'''do^{
 let^r=view(ref^a)
 var^copy=*r
 copy.change()
 print^(copy.x,r.x)
}
''', '2 1\n')
    positive('readonly_field', pre+'''def^Holder{self^{r:ref^read^Cell}}
do^{let^h=Holder{r=ref^a} print^(h.r.x)}
a.change()
''', '1\n')
    positive('let_read_argument', pre+'''let^fixed=Cell{}
inspect(ref^fixed)
''', '1\n')
    positive('let_read_binding', pre+'''let^fixed=Cell{}
let^r:ref^read^Cell=ref^fixed
print^(r.x)
''', '1\n')
    positive('readonly_array', '''var^a:i32^[2]={1,2}
do^{let^r:ref^read^i32^[2]=ref^a print^(r[0],r[1])}
a[0]:=3
print^(a[0])
''', '1 2\n3\n')
    positive('readonly_reference_array', pre+'''do^{
 let^rs:Cell[ref^read^1]={ref^a}
 print^(rs[0].x)
}
a.change()
''', '1\n')
    positive('nested_calls', pre+'''let^peek=f^x:ref^read^Cell ->i32^ {x.x}
do^{let^r=view(ref^a) print^(peek(r))}
a.change()
''', '1\n')
    positive('pure_read_argument', pre+'''let^peek=f^x:ref^read^Cell {x.x}
print^(peek(ref^a))
a.change()
''', '1\n')
    positive('readonly_reborrow', pre+'''do^{
 let^r=view(ref^a)
 let^s=ref^*r
 print^(s.x)
}
a.change()
''', '1\n')
    positive('readonly_method_result', '''def^Box{
 self^{x:i32^=7}
 at=f^self^ {ref^self^.x}
}
var^a=Box{}
do^{let^r:ref^read^Box=ref^a let^s=r.at() print^(*s)}
a.x:=8
''', '7\n')
    positive('direct_return_chain', pre+'''let^forward=f^x:ref^Cell ->ref^read^Cell {view(x)}
do^{let^r=forward(ref^a) print^(r.x)}
a.change()
''', '1\n')
    positive('optional_conversion', pre+'''do^{let^r:ref^read^Cell|nil^=ref^a print^(r?.x ?? 0)}
a.change()
''', '1\n')
    positive('loop_release', pre+'''for^ var^i=0 while^i<2 next^i+=1 {
 let^r=view(ref^a)
 print^(r.x)
}
a.change()
''', '1\n1\n')
    for name, body, message in [
        ('field_write', 'let^r=view(ref^a) r.x:=0', 'read^'),
        ('method_write', 'let^r=view(ref^a) r.change()', 'read^'),
        ('no_downgrade', 'let^r=view(ref^a) let^w:ref^Cell=r', "expected 'ref^ Cell'"),
        ('readonly_argument_write', 'let^bad=p^x:ref^read^Cell {x.x:=0} bad(ref^a)', 'read^'),
        ('reborrow_write', 'let^r=view(ref^a) let^s=ref^*r s.x:=0', 'read^'),
    ]:
        negative(name, pre+body, message)
    for name, body, output in [
        ('alias_write', 'let^r=view(ref^a) a.x:=0', ''),
        ('alias_method', 'let^r=view(ref^a) a.change()', ''),
        ('last_use_alias_change', 'let^r=view(ref^a) print^(r.x) a.change()', '1\n'),
        ('discarded_result', 'view(ref^a) a.change()', ''),
        ('mutable_argument', 'let^set=p^x:ref^Cell {x.x:=0} let^r=view(ref^a) set(ref^a)', ''),
        ('overlapping_arguments', 'let^both=p^x:ref^read^Cell,y:ref^Cell {} both(ref^a,ref^a)', ''),
        ('reverse_overlapping_arguments', 'let^both=p^y:ref^Cell,x:ref^read^Cell {} both(ref^a,ref^a)', ''),
        ('later_argument', 'let^tick=p^ {a.change() return^1} let^take=p^x:ref^read^Cell,n:i32^ {} take(ref^a,tick())', ''),
        ('outer_store', 'var^out:ref^read^Cell|nil^=nil^ do^{let^r=view(ref^a) out:=r}', ''),
        ('aggregate_escape', 'def^H{self^{r:ref^read^Cell}} var^out:H|nil^=nil^ do^{out:=H{r=ref^a}}', ''),
        ('reference_copy', 'let^r=view(ref^a) let^s=r a.change()', ''),
        ('optional_alias', 'let^r:ref^read^Cell|nil^=ref^a a.change()', ''),
        ('nested_field_alias', 'def^H{self^{r:ref^Cell}} var^h=H{r=ref^a} let^r:ref^read^H=ref^h a.change()', ''),
        ('local_read_return', 'let^bad=f^x:ref^Cell ->ref^read^Cell {let^r=view(x) return^r} bad(ref^a)', ''),
        ('inherited_inner_escape', 'var^out:ref^read^Cell|nil^=nil^ do^{let^r=view(ref^a) let^id=f^x:ref^read^Cell {x} out:=id(r)}', ''),
        ('aggregate_argument_alias', 'def^H{self^{r:ref^Cell}} let^bad=p^h:H {h.r.change()} let^r=view(ref^a) bad(H{r=ref^a})', ''),
    ]:
        positive(name, pre+body, output)
    positive('array_alias_write', '''var^a:i32^[2]={1,2}
let^r:ref^read^i32^[2]=ref^a
a[0]:=3
''', '')
    negative('array_write', '''var^a:i32^[2]={1,2}
let^r:ref^read^i32^[2]=ref^a
r[0]:=3
''', 'read^')
    positive('array_shrink', '''var^a:i32^[flex^]={1,2}
let^r:ref^read^i32^[flex^]=ref^a
a.pop()
''', '')
    positive('reference_array_alias', pre+'''let^rs:Cell[ref^read^1]={ref^a}
a.change()
''', '')
    dynamic = '''def^I{
 abstract^get:f^self^ ->i32^;
 abstract^change:p^self^;
}
def^C:I{
 self^{x:i32^=3}
 get=f^self^ ->i32^ {self^.x}
 change=p^self^ {self^.x+=1}
}
var^a=C{}
'''
    positive('dyn_read_and_narrow', dynamic+'''do^{
 let^r:ref^read^dyn^I=ref^a
 print^(r.get())
 if^r fits^C {print^(r.x)}
}
a.change()
''', '3\n3\n')
    positive('dyn_argument_call_only', dynamic+'''let^see=p^x:ref^read^dyn^I {print^(x.get())}
see(ref^a)
a.change()
print^(a.x)
''', '3\n4\n')
    positive('optional_dyn_read', dynamic+'''do^{
 let^source:ref^read^dyn^I=ref^a
 let^r:ref^read^dyn^I|nil^=source
 if^r? {print^(r.get())}
}
a.change()
''', '3\n')
    negative('dyn_writable_result', '''def^I{abstract^at:f^self^ ->ref^i32^;}
def^C:I{self^{x:i32^=1} at=f^self^ ->ref^i32^ {ref^self^.x}}
var^a=C{}
let^r:ref^read^dyn^I=ref^a
let^w=r.at()
''', 'read^')
    positive('dyn_argument_overlap', dynamic+'''let^both=p^x:ref^read^dyn^I,y:ref^C {}
both(ref^a,ref^a)
''', '')
    positive('dyn_later_argument', dynamic+'''let^tick=p^ {a.change() return^1}
let^both=p^x:ref^read^dyn^I,n:i32^ {}
both(ref^a,tick())
''', '')
    negative('dyn_change', dynamic+'''let^r:ref^read^dyn^I=ref^a
r.change()
''', 'read^')
    positive('dyn_alias_change', dynamic+'''let^r:ref^read^dyn^I=ref^a
a.change()
''', '')
    negative('dyn_narrow_change', dynamic+'''let^r:ref^read^dyn^I=ref^a
if^r fits^C {r.x:=0}
''', 'read^')
    negative('dyn_downgrade', dynamic+'''let^r:ref^read^dyn^I=ref^a
let^w:ref^dyn^I=r
''', 'read^')
    positive('readonly_ffi_argument', '''externC^abs:f^n:i32^ ->i32^;
externC^memcmp:f^a:ref^read^i32^,b:ref^read^i32^,n:u64^ ->i32^;
var^a:i32^=7
var^b:i32^=7
print^(memcmp(ref^a,ref^b,4))
a:=8
''', '0\n')
    positive('readonly_generic_holder', pre+'''def^Holder<T>{self^{r:ref^read^T}}
do^{let^h=Holder<Cell>{r=ref^a} print^(h.r.x)}
a.change()
''', '1\n')
    positive('generic_holder_alias', pre+'''def^Holder<T>{self^{r:ref^read^T}}
let^h=Holder<Cell>{r=ref^a}
a.change()
''', '')
    negative('method_reference_downgrade', '''def^Box{
 self^{x:i32^=7}
 at=f^self^ {ref^self^.x}
}
var^a=Box{}
let^r:ref^read^Box=ref^a
let^s:ref^i32^=r.at()
print^(*s)
''', 'read^')
    positive('merged_read_roots', pre+'''var^b=Cell{x=2}
let^choose=f^x:ref^read^Cell,y:ref^read^Cell,first:bool^ ->ref^read^Cell {
 if^first {return^x}
 return^y
}
do^{let^r=choose(ref^a,ref^b,false^) print^(r.x)}
a.change()
b.change()
''', '2\n')
    positive('merged_root_alias_change', pre+'''var^b=Cell{}
let^choose=f^x:ref^read^Cell,y:ref^read^Cell,first:bool^ ->ref^read^Cell {
 if^first {return^x}
 return^y
}
let^r=choose(ref^a,ref^b,true^)
b.change()
''', '')
    positive('mutable_self_alias', pre+'''def^M{
 self^{n:i32^=0}
 step=f^mutable^self^ {self^.n+=1 return^self^.n}
}
var^m=M{}
let^r:ref^read^M=ref^m
m.step()
''', '')
    negative('readonly_subtype_parameter', '''def^Base{self^{n:i32^=0}}
def^Derived:Base{}
let^bad=p^x:ref^read^Base {x.n:=1}
var^x=Derived{}
bad(ref^x)
''', 'read^')
    positive('escape_through_procedure', pre+'''var^out:ref^read^Cell|nil^=nil^
let^save=p^x:ref^read^Cell {out:=x}
do^{let^r=view(ref^a) save(r)}
''', '')
    positive('optional_reference_promotion', pre+'''var^source:ref^Cell|nil^=ref^a
do^{let^r:ref^read^Cell|nil^=source print^(r?.x ?? 0)}
a.change()
''', '1\n')
    positive('optional_promotion_alias_change', pre+'''var^source:ref^Cell|nil^=ref^a
let^r:ref^read^Cell|nil^=source
a.change()
''', '')
    negative('optional_reference_downgrade', pre+'''let^r:ref^read^Cell|nil^=ref^a
let^w:ref^Cell|nil^=r
''', 'not an alternative')
    positive('recursive_read_forward', pre+'''let^walk=f^x:ref^read^Cell,n:i32^ ->ref^read^Cell {
 if^n=0 {return^x}
 return^this^(x,n-1)
}
do^{let^r=walk(ref^a,3) print^(r.x)}
a.change()
''', '1\n')
    internal = pre+'''let^inner=p^x:ref^Cell,y:ref^Cell {
 let^r:ref^read^Cell=x
 y.x:=2
 print^(r.x)
}
'''
    positive('internal_borrow_distinct_arguments', internal+'''var^b=Cell{}
inner(ref^a,ref^b)
a.change()
''', '1\n')
    positive('internal_borrow_aliased_arguments', internal+'''inner(ref^a,ref^a)
''', '2\n')
    positive('transitive_internal_borrow', internal+'''let^wrap=p^x:ref^Cell,y:ref^Cell {inner(x,y)}
wrap(ref^a,ref^a)
''', '2\n')
    positive('recursive_internal_borrow', internal+'''let^wrap=p^x:ref^Cell,y:ref^Cell,n:i32^ {
 if^n=0 {inner(x,y) return^}
 this^(x,y,n-1)
}
wrap(ref^a,ref^a,2)
''', '2\n')
    positive('internal_borrow_via_alias_argument', internal+'''let^w=ref^a
inner(ref^a,w)
''', '2\n')
    positive('readonly_slice_ffi_write', '''externC^memset:p^out:u8^[..],c:i32^,n:u64^;
var^a:u8^[2]={1,2}
let^r:ref^read^u8^[2]=ref^a
memset(a[..],0,2)
''', '')
    dyn_internal = '''def^I{abstract^run:p^self^,x:ref^i32^;}
def^C:I{
 self^{x:i32^=1}
 run=p^self^,x:ref^i32^ {
  let^r:ref^read^i32^=x
  self^.x:=2
  print^(*r)
 }
}
var^a=C{}
let^d:ref^dyn^I=ref^a
'''
    positive('dyn_internal_alias', dyn_internal+'d.run(ref^a.x)', '2\n')
    positive('dyn_internal_distinct', dyn_internal+'''var^b:i32^=7
d.run(ref^b)
''', '7\n')
    negative('readonly_reference_field_copy', pre+'''def^Link{self^{child:ref^Cell}}
let^bad=p^x:ref^read^Link {let^w=x.child w.change()}
var^link=Link{child=ref^a}
bad(ref^link)
''', 'read^')
    positive('readonly_reference_field_conversion', pre+'''def^Link{self^{child:ref^Cell}}
let^look=p^x:ref^read^Link {let^r:ref^read^Cell=x.child print^(r.x)}
var^link=Link{child=ref^a}
look(ref^link)
a.change()
''', '1\n')
    let_hole = '''
def^T {
    self^{ n:i32^ = 0, xs:i32^[grow^] = {} }
    me = f^self^ -> ref^T { self^ }
    view = f^self^ -> i32^[..] { self^.xs[..] }
    number = f^self^ -> ref^i32^ { ref^self^.n }
    add = p^self^ { self^.n += 1 }
    get = f^self^ -> i32^ { self^.n }
}
def^Holder {
    self^{ target:ref^T }
    held = f^self^ -> ref^T { self^.target }
}
def^Sink { abstract^take:p^self^, t:ref^T; }
def^Adder : Sink { take = p^self^, t:ref^T { t.add() } }
let^id = f^t:ref^T -> ref^T { t }
let^bump = p^t:ref^T { t.add() }
'''
    let_error = "cannot modify let^ binding"
    for name, body in (
        ('let_result_method', 'let^a = T{}\nlet^r = a.me()\nr.add()\n'),
        ('let_result_field', 'let^a = T{}\nlet^r = a.me()\nr.n := 5\n'),
        ('let_result_slice', 'let^a = T{xs = {1}}\nlet^v = a.view()\nv[0] := 9\n'),
        ('let_result_number', 'let^a = T{}\nlet^p = a.number();\n*p := 9\n'),
        ('let_result_chain', 'let^a = T{}\na.me().add()\n'),
        ('let_result_chain_slice', 'let^a = T{xs = {1}}\na.view()[0] := 2\n'),
        ('let_result_twice', 'let^a = T{}\nlet^r = a.me().me()\nr.add()\n'),
        ('let_result_in_var', 'let^a = T{}\nvar^r = a.me()\nr.add()\n'),
        ('let_result_in_function', 'let^g = p^ {\n let^a = T{}\n let^r = a.me()\n r.add()\n}\ng()\n'),
        ('let_result_nested_field', 'def^W { self^{ t:T = T{} } }\nlet^w = W{}\nlet^r = w.t.me()\nr.add()\n'),
        ('let_result_copied_slice', 'let^a = T{xs = {1}}\nlet^v = a.view()\nvar^w = v\nw[0] := 9\n'),
        ('let_result_through_field', 'let^a = T{}\nlet^h = Holder{target = a.me()}\nh.target.add()\n'),
        ('let_result_through_array', 'let^a = T{}\nvar^rs = T[ref^grow^]{ a.me() }\nrs[0].add()\n'),
        ('let_result_through_function', 'let^a = T{}\nlet^r = id(a.me())\nr.add()\n'),
        ('let_result_resliced', 'let^a = T{xs = {1}}\nlet^r = a.me()\nlet^w = r.xs[..]\nw[0] := 2\n'),
        ('let_result_as_argument', 'let^a = T{}\nbump(a.me())\n'),
        ('let_result_as_dyn_argument', 'let^a = T{}\nvar^s = Adder{}\nlet^d:ref^dyn^Sink = ref^s\nd.take(a.me())\n'),
        ('let_result_top_level', 'let^a = T{}\nlet^g = p^ {\n let^r = a.me()\n r.add()\n}\ng()\n'),
    ):
        negative(name, let_hole + body, let_error)
    negative('let_result_borrowed_field', let_hole + 'let^a = T{}\nlet^r = a.me()\nvar^q = ref^r.n\n', 'borrowing storage requires var^')
    for name, body, output in (
        ('let_result_reads', 'let^a = T{n = 1, xs = {2}}\nlet^r = a.me()\nprint^(r.n, r.get(), a.view()[0], *a.number())\n', '1 1 2 1\n'),
        ('let_result_resliced_reads', 'let^a = T{xs = {1, 2}}\nlet^r = a.me()\nlet^w = r.xs[1 ..]\nprint^(w[0])\n', '2\n'),
        ('var_result_writes', 'var^a = T{xs = {1}}\nlet^r = a.me()\nr.add()\nlet^v = a.view()\nv[0] := 9\nprint^(a.n, a.xs[0])\n', '1 9\n'),
        ('let_holding_a_reference_writes', 'var^t = T{}\nlet^h = Holder{target = ref^t}\nh.target.add()\nlet^r = h.held()\nr.add()\nprint^(t.n)\n', '2\n'),
        ('let_reference_writes', 'var^t = T{}\nlet^r = ref^t\nr.add()\nbump(r)\nprint^(t.n)\n', '2\n'),
        ('let_slice_writes', 'var^t = i32^[grow^]{1}\nlet^w = t[..]\nw[0] := 2\nprint^(t[0])\n', '2\n'),
        ('let_array_of_references_writes', 'var^t = T{}\nlet^rs:T[ref^ 1] = {ref^t}\nrs[0].add()\nlet^gs = T[ref^grow^]{ ref^t }\ngs[0].add()\nlet^r = rs[0]\nr.add()\nprint^(t.n)\n', '3\n'),
        ('let_array_of_slices_writes', 'var^x = i32^[grow^]{1}\nlet^vs:(i32^[..])[1] = {x[..]}\nvs[0][0] := 2\nprint^(x[0])\n', '2\n'),
        ('let_in_a_loop', 'var^t = T{}\nfor^i in^0 .. 2 {\n let^r = ref^t\n r.add()\n}\nprint^(t.n)\n', '2\n'),
        ('let_shadowed_by_var', 'let^a = T{}\ndo^{\n var^a = T{}\n a.add()\n print^(a.n)\n}\n', '1\n'),
    ):
        positive(name, let_hole + body, output)
    positive('alias_observes_mutation', pre+'''let^r=view(ref^a)
let^s=r
a.change()
print^(r.x,s.x)
''', '2 2\n')
    positive('aliased_read_and_write', pre+'''let^update=p^r:ref^read^Cell,w:ref^Cell {
 print^(r.x)
 w.change()
 print^(r.x)
}
update(ref^a,ref^a)
''', '1\n2\n')
    positive('later_argument_observed', pre+'''let^tick=p^ {a.change() return^0}
let^show=p^r:ref^read^Cell,n:i32^ {print^(r.x,n)}
show(ref^a,tick())
''', '2 0\n')
    positive('read_return_local_conversion', pre+'''let^forward=f^x:ref^Cell ->ref^read^Cell {
 let^r:ref^read^Cell=x
 return^r
}
let^r=forward(ref^a)
a.change()
print^(r.x)
''', '2\n')
    positive('read_store_and_return_flow', pre+'''def^Holder{self^{r:ref^read^Cell|nil^=nil^}}
let^save=p^h:ref^Holder,r:ref^read^Cell {h.r:=r}
var^h=Holder{}
do^{let^r=view(ref^a) save(ref^h,r)}
a.change()
print^(h.r?.x ?? 0)
''', '2\n')
    negative('read_local_owner_escape', '''let^bad=p^ ->ref^read^i32^ {
 var^n:i32^=1
 let^r:ref^read^i32^=ref^n
 return^r
}
bad()
''', 'cannot return a reference')
    negative('read_store_short_owner', pre+'''def^Holder{self^{r:ref^read^Cell|nil^=nil^}}
let^save=p^h:ref^Holder,r:ref^read^Cell {h.r:=r}
var^h=Holder{}
do^{var^local=Cell{} save(ref^h,ref^local)}
''', 'outlive')
    positive('scratch_read_and_append', '''def^F2{self^{x:i32^=0,y:i32^=0}}
let^walk=p^poly:read^F2[..],scratch:ref^F2[grow^],depth:i32^ {
 let^mark=scratch.len
 if^depth>0 {
  for^p in^poly {scratch.push(F2{x=p.x+1,y=p.y+1})}
  this^(scratch[mark ..],scratch,depth-1)
 }
 print^(poly[0].x,poly[0].y)
 scratch.truncate(mark)
}
let^bounds=F2[1]{F2{x=1,y=2}}
var^scratch=F2[grow^]{}
walk(bounds[..],ref^scratch,2)
print^(bounds[0].x,scratch.len)
''', '3 4\n2 3\n1 2\n1 0\n')
    positive('grow_retained_read_slice', '''var^a=i32^[grow^]{1,2}
let^r:read^i32^[..]=a[..]
a.truncate(1)
print^(r.len,r[1])
a.push(9)
print^(r[1])
''', '2 2\n9\n')
    negative('read_flex_slice_shrink', '''var^a=i32^[flex^]{1,2}
let^r:read^i32^[..]=a[..]
a.clear()
print^(r[0])
''', 'cannot shrink flex^ storage')
    negative('read_flex_later_operand', '''let^clear=p^a:ref^i32^[flex^] ->i32^ {a.clear() return^0}
let^use=p^r:read^i32^[..],n:i32^ {print^(r[0])}
var^a=i32^[flex^]{1,2}
use(a[..],clear(ref^a))
''', 'earlier operand')
    variants = '''def^A{self^{n:i32^=7}}
def^B{self^{s:string^="hi"}}
def^H{self^{u:A|B=A{}}}
'''
    negative('read_alias_invalidates_variant', variants+'''var^h=H{}
let^r:ref^read^H=ref^h
if^r.u fits^A {h.u:=B{} print^(r.u.n)}
''', 'field access requires')
    negative('read_parameter_alias_invalidates_variant', variants+'''let^bad=p^r:ref^read^H,w:ref^H {
 if^r.u fits^A {w.u:=B{} print^(r.u.n)}
}
var^h=H{}
bad(ref^h,ref^h)
''', 'field access requires')
    positive('read_variant_recheck', variants+'''var^h=H{}
let^r:ref^read^H=ref^h
if^r.u fits^A {
 let^copy=r.u
 h.u:=B{}
 print^(copy.n)
 if^r.u fits^B {print^(r.u.s)}
}
''', '7\nhi\n')
    reference_holder = '''def^H{self^{r:ref^i32^}}
let^set=p^h:ref^H,r:ref^i32^ {h.r:=r}
'''
    positive('read_holder_reload', reference_holder+'''var^a:i32^=1
var^b:i32^=2
var^h=H{r=ref^a}
let^r:ref^read^H=ref^h
let^old:ref^read^i32^=r.r
set(ref^h,ref^b)
let^now:ref^read^i32^=r.r
print^(*old,*now)
''', '1 2\n')
    negative('read_holder_short_reload', reference_holder+'''var^a:i32^=1
var^h=H{r=ref^a}
let^r:ref^read^H=ref^h
do^{var^b:i32^=2 set(ref^h,ref^b)}
''', 'outlive')
    disposable = '''import^std.sys
def^D:std.sys.Disposable{self^{n:i32^=7} dispose=p^self^{}}
'''
    negative('read_disposable_union_path', disposable+'''def^H{self^{u:D|nil^=D{}}}
var^h=H{}
let^r:ref^read^H=ref^h
if^r.u fits^D {r.u.n:=9}
''', 'read^')
    negative('read_disposable_union_payload', disposable+'''var^u:D|nil^=D{}
let^w=ref^u
if^u fits^D {
 let^r:ref^read^i32^=ref^u.n;
 *w:=nil^
 print^(*r)
}
''', 'dispose elements')
    negative('read_disposable_union_callee', disposable+'''let^clear=p^w:ref^(D|nil^) {*w:=nil^}
var^u:D|nil^=D{}
if^u fits^D {
 let^r:ref^read^i32^=ref^u.n
 clear(ref^u)
 print^(*r)
}
''', 'dispose elements')
    negative('read_disposable_array_clear', disposable+'''var^a=D[grow^]{D{}}
let^r:ref^read^D=ref^a[0]
a.clear()
print^(r.n)
''', 'dispose elements')
    negative('read_disposable_array_callee', disposable+'''let^clear=p^a:ref^D[grow^] {a.clear()}
var^a=D[grow^]{D{}}
let^r:ref^read^D=ref^a[0]
clear(ref^a)
print^(r.n)
''', 'dispose elements')
    negative('read_union_holder_replacement', disposable+'''def^H{self^{u:D|nil^=D{}}}
var^h=H{}
if^h.u fits^D {
 let^r:ref^read^i32^=ref^h.u.n
 h:=H{}
 print^(*r)
}
''', 'dispose elements')
    negative('read_parameter_payload_invalidation', disposable+'''def^H{self^{u:D|nil^=D{}}}
let^bad=p^r:ref^read^H,w:ref^H {
 if^r.u fits^D {
  let^v:ref^read^i32^=ref^r.u.n
  w.u:=nil^
  print^(*v)
 }
}
var^h=H{}
bad(ref^h,ref^h)
''', 'dispose elements')
    negative('read_parameter_flex_invalidation', '''let^bad=p^r:read^i32^[..],w:ref^i32^[flex^] {
 w.clear()
 print^(r[0])
}
var^a=i32^[flex^]{1}
bad(a[..],ref^a)
''', 'cannot shrink flex^ storage')
    negative('read_reference_field_dynamic_conversion', '''def^I{abstract^change:p^self^;}
def^C:I{self^{n:i32^=1} change=p^self^ {self^.n:=2}}
def^H{self^{r:ref^C}}
var^a=C{}
var^h=H{r=ref^a}
let^r:ref^read^H=ref^h
let^w:ref^dyn^I=r.r
w.change()
''', 'read^')
    positive('many_read_parameters', 'let^peek=p^' + ','.join(
        f'x{i}:ref^read^i32^' for i in range(65)) +
        ' {print^(*x64)}\nvar^a:i32^=7\npeek(' + ','.join(['ref^a']*65) + ')\n', '7\n')
    suite.finish()


if __name__ == '__main__':
    main()
