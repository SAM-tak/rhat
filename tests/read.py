"""Read-reference capabilities, lexical protection and call-boundary escape checks."""
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
        ('alias_write', 'let^r=view(ref^a) a.x:=0', 'read^ borrow'),
        ('alias_method', 'let^r=view(ref^a) a.change()', 'read^ borrow'),
        ('last_use_still_protected', 'let^r=view(ref^a) print^(r.x) a.change()', 'read^ borrow'),
        ('discarded_result', 'view(ref^a) a.change()', 'read^ borrow'),
        ('no_downgrade', 'let^r=view(ref^a) let^w:ref^Cell=r', "expected 'ref^ Cell'"),
        ('mutable_argument', 'let^set=p^x:ref^Cell {x.x:=0} let^r=view(ref^a) set(ref^a)', 'read^ borrow'),
        ('readonly_argument_write', 'let^bad=p^x:ref^read^Cell {x.x:=0} bad(ref^a)', 'read^'),
        ('overlapping_arguments', 'let^both=p^x:ref^read^Cell,y:ref^Cell {} both(ref^a,ref^a)', 'read^ borrow'),
        ('reverse_overlapping_arguments', 'let^both=p^y:ref^Cell,x:ref^read^Cell {} both(ref^a,ref^a)', 'read^ borrow'),
        ('later_argument', 'let^tick=p^ {a.change() return^1} let^take=p^x:ref^read^Cell,n:i32^ {} take(ref^a,tick())', 'read^ borrow'),
        ('outer_store', 'var^out:ref^read^Cell|nil^=nil^ do^{let^r=view(ref^a) out:=r}', 'protecting scope'),
        ('aggregate_escape', 'def^H{self^{r:ref^read^Cell}} var^out:H|nil^=nil^ do^{out:=H{r=ref^a}}', 'protecting scope'),
        ('reference_copy', 'let^r=view(ref^a) let^s=r a.change()', 'read^ borrow'),
        ('reborrow_write', 'let^r=view(ref^a) let^s=ref^*r s.x:=0', 'read^'),
        ('optional_alias', 'let^r:ref^read^Cell|nil^=ref^a a.change()', 'read^ borrow'),
        ('nested_field_alias', 'def^H{self^{r:ref^Cell}} var^h=H{r=ref^a} let^r:ref^read^H=ref^h a.change()', 'read^ borrow'),
        ('local_borrow_return', 'let^bad=f^x:ref^Cell ->ref^read^Cell {let^r=view(x) return^r} bad(ref^a)', 'protecting scope'),
        ('inherited_inner_escape', 'var^out:ref^read^Cell|nil^=nil^ do^{let^r=view(ref^a) let^id=f^x:ref^read^Cell {x} out:=id(r)}', 'protecting scope'),
        ('aggregate_argument_alias', 'def^H{self^{r:ref^Cell}} let^bad=p^h:H {h.r.change()} let^r=view(ref^a) bad(H{r=ref^a})', 'read^ borrow'),
    ]:
        negative(name, pre+body, message)
    negative('array_alias_write', '''var^a:i32^[2]={1,2}
let^r:ref^read^i32^[2]=ref^a
a[0]:=3
''', 'read^ borrow')
    negative('array_write', '''var^a:i32^[2]={1,2}
let^r:ref^read^i32^[2]=ref^a
r[0]:=3
''', 'read^')
    negative('array_shrink', '''var^a:i32^[flex^]={1,2}
let^r:ref^read^i32^[flex^]=ref^a
a.pop()
''', 'read^ borrow')
    negative('reference_array_alias', pre+'''let^rs:Cell[ref^read^1]={ref^a}
a.change()
''', 'read^ borrow')
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
    negative('dyn_argument_overlap', dynamic+'''let^both=p^x:ref^read^dyn^I,y:ref^C {}
both(ref^a,ref^a)
''', 'read^ borrow')
    negative('dyn_later_argument', dynamic+'''let^tick=p^ {a.change() return^1}
let^both=p^x:ref^read^dyn^I,n:i32^ {}
both(ref^a,tick())
''', 'read^ borrow')
    negative('dyn_change', dynamic+'''let^r:ref^read^dyn^I=ref^a
r.change()
''', 'read^')
    negative('dyn_alias_change', dynamic+'''let^r:ref^read^dyn^I=ref^a
a.change()
''', 'read^ borrow')
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
    negative('generic_holder_alias', pre+'''def^Holder<T>{self^{r:ref^read^T}}
let^h=Holder<Cell>{r=ref^a}
a.change()
''', 'read^ borrow')
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
    negative('merged_root_protection', pre+'''var^b=Cell{}
let^choose=f^x:ref^read^Cell,y:ref^read^Cell,first:bool^ ->ref^read^Cell {
 if^first {return^x}
 return^y
}
let^r=choose(ref^a,ref^b,true^)
b.change()
''', 'read^ borrow')
    negative('mutable_self_alias', pre+'''def^M{
 self^{n:i32^=0}
 step=f^mutable^self^ {self^.n+=1 return^self^.n}
}
var^m=M{}
let^r:ref^read^M=ref^m
m.step()
''', 'read^ borrow')
    negative('readonly_subtype_parameter', '''def^Base{self^{n:i32^=0}}
def^Derived:Base{}
let^bad=p^x:ref^read^Base {x.n:=1}
var^x=Derived{}
bad(ref^x)
''', 'read^')
    negative('escape_through_procedure', pre+'''var^out:ref^read^Cell|nil^=nil^
let^save=p^x:ref^read^Cell {out:=x}
do^{let^r=view(ref^a) save(r)}
''', 'reference')
    positive('optional_reference_promotion', pre+'''var^source:ref^Cell|nil^=ref^a
do^{let^r:ref^read^Cell|nil^=source print^(r?.x ?? 0)}
a.change()
''', '1\n')
    negative('optional_promotion_protects', pre+'''var^source:ref^Cell|nil^=ref^a
let^r:ref^read^Cell|nil^=source
a.change()
''', 'read^ borrow')
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
    negative('internal_borrow_aliased_arguments', internal+'''inner(ref^a,ref^a)
''', 'read^')
    negative('transitive_internal_borrow', internal+'''let^wrap=p^x:ref^Cell,y:ref^Cell {inner(x,y)}
wrap(ref^a,ref^a)
''', 'read^')
    negative('recursive_internal_borrow', internal+'''let^wrap=p^x:ref^Cell,y:ref^Cell,n:i32^ {
 if^n=0 {inner(x,y) return^}
 this^(x,y,n-1)
}
wrap(ref^a,ref^a,2)
''', 'read^')
    negative('internal_borrow_via_alias_argument', internal+'''let^w=ref^a
inner(ref^a,w)
''', 'read^')
    negative('readonly_slice_ffi_write', '''externC^memset:p^out:u8^[..],c:i32^,n:u64^;
var^a:u8^[2]={1,2}
let^r:ref^read^u8^[2]=ref^a
memset(a[..],0,2)
''', 'read^')
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
    negative('dyn_internal_alias', dyn_internal+'d.run(ref^a.x)', 'read^')
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
    suite.finish()


if __name__ == '__main__':
    main()
