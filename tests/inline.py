"""Static iteration over fixed arrays and heterogeneous template arguments."""
from harness import Suite


def main():
    suite = Suite('inline')
    positive, negative = suite.positive, suite.negative
    show = '''let^write=p^x:i32^ {print^("int",x)}
& p^x:string^ {print^("text",x)}
& p^x:bool^ {print^("bool",x)}
let^show=template^p^... {
 inline^for^x in^... {write(x)}
}
'''
    positive('heterogeneous_dispatch', show+'show(1,"two",true^)',
             'int 1\ntext two\nbool true\n')
    positive('empty_pack', show+'show()', '')
    positive('empty_pack_skips_typecheck', '''let^show=template^p^... {
 inline^for^x in^... {x.noSuchMember()}
}
show()
''', '')
    positive('pack_length_and_index', '''let^pick=template^p^... {
 let^last=....len-1
 print^(....len,...[0],...[last])
}
pick(1,"two",true^)
''', '3 1 true\n')
    positive('forward_heterogeneous', show+'''let^forward=template^p^... {show(...)}
forward(1,"two",true^)
''', 'int 1\ntext two\nbool true\n')
    positive('forward_homogeneous', '''let^sum=f^...:i32^ ->i32^ {
 var^n=0
 for^i from^0 to^....len-1 {n+=...[i]}
 return^n
}
let^forward=template^f^... ->i32^ {sum(...)}
print^(forward(1,2,3),forward())
''', '6 0\n')
    positive('fixed_array', '''var^a:i32^[3]={1,2,3}
var^n=0
inline^for^x in^a {n+=x}
print^(n)
''', '6\n')
    positive('array_to_pack', show+'''var^a:i32^[3]={1,2,3}
show(a...)
''', 'int 1\nint 2\nint 3\n')
    positive('fixed_array_function', '''let^sum=f^a:i32^[3] ->i32^ {
 var^n=0
 inline^for^x in^a {n+=x}
 return^n
}
print^(sum(i32^[3]{1,2,3}))
''', '6\n')
    positive('break_next', '''var^a:i32^[5]={1,2,3,4,5}
var^n=0
inline^for^x in^a {
 if^x=2 {next^}
 if^x=4 {break^}
 n+=x
}
print^(n)
''', '4\n')
    positive('next_then_continue', show+'''let^skip=template^p^... {
 inline^for^x in^... {write(x) next^}
 print^("done")
}
skip(1,"two",true^)
''', 'int 1\ntext two\nbool true\ndone\n')
    positive('first_return_type', '''let^first=template^f^... {
 inline^for^x in^... {return^x}
}
print^(first("one",2),first(3,"four"))
''', 'one 3\n')
    positive('nested_arrays', '''var^a:i32^[2 [2]]={{1,2},{3,4}}
var^n=0
inline^for^row in^a {inline^for^x in^row {n+=x}}
print^(n)
''', '10\n')
    positive('single_evaluation', '''var^calls=0
let^make=p^ ->i32^[2] {calls+=1 return^i32^[2]{1,2}}
inline^for^x in^make() {print^(x)}
print^(calls)
''', '1\n2\n1\n')
    positive('references', '''def^C{self^{n:i32^=0}}
let^set=template^p^... {inline^for^x in^... {x.n+=1}}
var^a=C{}
var^b=C{}
set(ref^a,ref^b)
print^(a.n,b.n)
''', '1 1\n')
    positive('pack_evaluation_order', '''var^n=0
let^tick=p^ ->i32^ {n+=1 return^n}
let^show=template^p^... {inline^for^x in^... {print^(x)}}
show(tick(),tick(),tick())
print^(n)
''', '1\n2\n3\n3\n')
    positive('fixed_prefix', '''let^show=template^p^prefix:string^,... {
 inline^for^x in^... {print^(prefix,x)}
}
show("value",1,"two",true^)
''', 'value 1\nvalue two\nvalue true\n')
    positive('snapshot_array', '''var^a:i32^[2]={1,2}
inline^for^x in^a {a[1]:=9 print^(x)}
print^(a[1])
''', '1\n2\n9\n')
    positive('outer_jumps', '''var^a:i32^[2]={1,2}
inline^for^x in^a {
 inline^for^y in^a {print^(x,y) next^^}
}
inline^for^x in^a {
 inline^for^y in^a {print^(x,y) break^^}
}
print^("done")
''', '1 1\n2 1\n1 1\ndone\n')
    positive('runtime_nested_loop', '''var^a:i32^[2]={1,2}
inline^for^x in^a {repeat^2 {print^(x) break^}}
repeat^2 {inline^for^x in^a {print^(x) break^^}}
print^("done")
''', '1\n2\n1\ndone\n')
    positive('conditional_return', '''let^pick=template^f^n:i32^,... ->i32^ {
 inline^for^x in^... {if^x=n {return^x}}
 return^0
}
print^(pick(2,1,2,3),pick(4,1,2,3))
''', '2 0\n')
    positive('return_nullable', '''let^pick=template^f^take:bool^,... {
 inline^for^x in^... {if^take {return^x}}
 return^nil^
}
let^a=pick(true^,1,nil^)
let^b=pick(false^,1,nil^)
print^(a fits^i32^,b fits^nil^)
''', 'true true\n')
    positive('iteration_read_scope', '''def^C{self^{n:i32^=0}}
var^a=C{}
var^items:i32^[2]={1,2}
inline^for^x in^items {
 a.n+=x
 let^r:ref^read^C=ref^a
 print^(r.n)
}
a.n+=1
print^(a.n)
''', '1\n3\n4\n')
    positive('readonly_reference_pack', '''def^C{self^{n:i32^=7}}
let^show=template^p^... {inline^for^x in^... {print^(x.n)}}
var^a=C{}
do^{let^r:ref^read^C=ref^a show(r,r)}
a.n:=8
print^(a.n)
''', '7\n7\n8\n')
    positive('per_type_body_rewrite', '''let^show=template^p^... {
 inline^for^x in^... {print^(if^x fits^i32^: x+1 el^: x;)}
}
show(1,"two",3)
''', '2\ntwo\n4\n')
    positive('error_handlers', '''errordef^E{Bad}
let^work=f^x:i32^ ->i32^|E.Bad {if^x=2 {return^E.Bad{}} return^x}
var^a:i32^[3]={1,2,3}
inline^for^x in^a {print^(try^work(x)) catch^E.Bad: print^("bad")}
''', '1\nbad\n3\n')
    negative('untyped_requires_template', 'let^bad=p^... {}', 'requires template^')
    negative('return_mismatched_types', '''let^bad=template^f^take:bool^,... {
 inline^for^x in^... {if^take {return^x}}
 return^nil^
}
bad(true^,1,"two")
''', 'inferred types disagree')
    negative('dynamic_slice', '''var^a:i32^[2]={1,2}
inline^for^x in^a[..] {print^(x)}
''', 'requires a fixed array')
    negative('dynamic_grow', '''var^a:i32^[grow^]={1,2}
inline^for^x in^a {print^(x)}
''', 'requires a fixed array')
    negative('runtime_pack_index', '''let^bad=template^f^i:i32^,... {return^...[i]}
print^(bad(0,1,"two"))
''', 'compile-time integer constant')
    negative('pack_index_bounds', 'let^bad=template^f^... {return^...[2]} bad(1)', 'out of bounds')
    negative('pack_escape', 'let^bad=template^f^... {...} bad(1)', 'cannot be returned')
    negative('pack_binding', 'let^bad=template^p^... {let^x=...} bad(1)', 'cannot be bound')
    negative('pack_owned', '''let^bad=template^p^... {}
bad(i32^[grow^]{1})
''', 'cannot own storage')
    negative('iterator_readonly', '''var^a:i32^[2]={1,2}
inline^for^x in^a {x:=0}
''', 'cannot modify let^')
    negative('mismatched_forward', '''let^sum=f^...:i32^ ->i64^ {....len}
let^bad=template^f^... {sum(...)}
print^(bad(1,"two"))
''', "expected 'i32^'")
    negative('expansion_limit', '''var^a:i32^[4097]={0 repeat^4097}
inline^for^x in^a {print^(x)}
''', 'expansion limit')
    negative('pack_expansion_limit', '''let^show=template^p^... {}
var^a:i32^[4097]={0 repeat^4097}
show(a...)
''', 'exceeds 4096')
    negative('nested_expansion_limit', '''var^a:i32^[65]={0 repeat^65}
inline^for^x in^a {inline^for^y in^a {print^(x,y)}}
''', 'expansion limit')
    negative('mutable_constant_index', '''let^bad=template^p^... {
 var^i=0
 print^(...[i])
}
bad(1,"two")
''', 'compile-time integer constant')
    negative('pack_reference', '''let^bad=template^p^... {let^r=ref^...}
bad(1)
''', 'cannot take a reference')
    negative('readonly_pack_write', '''def^C{self^{n:i32^=0}}
let^bad=template^p^... {inline^for^x in^... {x.n:=1}}
var^a=C{}
let^r:ref^read^C=ref^a
bad(r)
''', 'read^')
    negative('iteration_read_escape', '''def^C{self^{n:i32^=0}}
var^a=C{}
var^out:ref^read^C|nil^=nil^
var^items:i32^[2]={1,2}
inline^for^x in^items {let^r:ref^read^C=ref^a out:=r}
''', 'protecting scope')
    negative('iteration_local_escape', '''var^out:ref^i32^|nil^=nil^
var^items:i32^[2]={1,2}
inline^for^x in^items {var^local=x out:=ref^local}
''', 'outlive its owner')

    def instantiation_trace():
        source = '''let^max=template^p^... {
 var^best=...[0]
 inline^for^x in^... {if^x>best {best:=x}}
 return^best
}
let^outer=template^p^... {return^max(...)}
print^(outer(2,""))
'''
        result = suite.run([suite.compiler, 'check', suite.source('instantiation_trace', source)], False)
        assert ':3:' in result.stderr and 'binary operands have different types' in result.stderr, result.stderr
        assert ':6:' in result.stderr and ':7:' in result.stderr, result.stderr
        assert result.stderr.count('note: in instantiation') == 2, result.stderr
    suite.case('instantiation_trace', instantiation_trace)
    suite.finish()


if __name__ == '__main__':
    main()
