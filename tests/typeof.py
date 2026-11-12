"""typeof^ static types and the Type.signature^ spelling of each specialization."""

from harness import Suite


def main():
    suite = Suite('typeof')
    positive, negative = suite.positive, suite.negative

    positive('typeof_specializations', '''
let^write=p^x {print^(typeof^(x).signature^,x)}
write(1) write("s") write(true^) write(2.5)
''', 'i32^ 1\nstring^ s\nbool^ true\nf64^ 2.5\n')

    positive('typeof_unevaluated', '''
let^side=p^ ->i32^ {print^("evaluated") return^ 1}
print^(typeof^(side()).signature^)
''', 'i32^\n')

    positive('typeof_static_shapes', '''
let^text=import^ std.text
def^ Shape {abstract^ area:f^ self^ ->f64^;}
def^ Sq : Shape {self^{s:f64^=1.0} area=f^ self^ ->f64^ {self^.s}}
let^twice=f^ n:i32^ ->i32^ {n*2}
let^pick=f^ n:i32^ ->i32^ {n}
& f^ s:string^ ->i32^ {0}
let^u:i32^|string^=3
var^sq=Sq{}
let^d:ref^ dyn^ Shape=ref^ sq
let^arr=i32^[]{1,2,3}
print^(typeof^(u).signature^)
print^(typeof^(d).signature^)
print^(typeof^(ref^ sq).signature^)
print^(typeof^(arr).signature^)
print^(typeof^(twice).signature^)
print^(typeof^(pick).signature^)
print^(typeof^(text.Builder{}).signature^)
''', 'i32^ | string^\nref^ dyn^ Shape\nref^ Sq\ni32^[]\nf^ i32^ -> i32^;\n'
         'f^ i32^ -> i32^; & f^ string^ -> i32^;\nstd.text.Builder\n')

    positive('typeof_type_spellings', '''
def^ Vec2<T> {self^{x:T,y:T} element=f^ self^ ->string^ {T.signature^}}
def^ Box {self^{n:i32^=0} kind=f^ self^ ->string^ {Self^.signature^}}
enum^ Mode {A, B}
let^Float2=Vec2<f32^>
print^(Float2.signature^,i32^.signature^,Mode.signature^,Box{}.kind())
print^(Float2{x=1.0,y=2.0}.element(),typeof^(Mode.A).signature^)
''', 'Vec2<f32^> i32^ Mode Box\nf32^ Mode\n')

    # The spelling reads back as an annotation of the same type.
    positive('typeof_reads_back', '''
let^pick=f^ n:i32^ ->i32^ {n}
& f^ s:string^ ->i32^ {0}
let^h:f^ i32^ -> i32^; & f^ string^ -> i32^; = pick
let^u:i32^ | string^ = 1
print^(typeof^(h).signature^ = typeof^(pick).signature^,typeof^(u).signature^)
''', 'true i32^ | string^\n')

    positive('typeof_prefix_operand', '''
def^ P {self^{n:i64^=1}}
let^p=P{}
print^((typeof^ p.n).signature^,typeof^(p).signature^)
''', 'i64^ P\n')

    positive('typeof_annotations', '''
let^copy=p^ x {
    var^ y:typeof^(x)=x
    let^ arr:typeof^(x)[2]={x,x}
    var^ maybe:typeof^(x)|nil^=nil^
    print^(typeof^(y).signature^,typeof^(arr).signature^,typeof^(maybe).signature^)
}
copy(1) copy(2.5)
let^u:i32^|string^=3
print^(u fits^ typeof^(u))
''', 'i32^ i32^[2] i32^ | nil^\nf64^ f64^[2] f64^ | nil^\ntrue\n')

    positive('typeof_comparison', '''
let^same=f^ a,b ->bool^ {typeof^(a)=typeof^(b)}
print^(same(1,2),same(1,"x"),typeof^(1)!=i32^,typeof^(1)=i32^)
''', 'true false false true\n')

    # A decided comparison prunes the arms that do not type-check for this specialization.
    positive('typeof_comparison_prunes', '''
def^ A {self^{a:i32^=7}}
let^show=template^ p^ x {
    if^ typeof^(x)=string^ {
        print^("text",x.bytes.len)
    el^ typeof^(x)=A:
        print^("A",x.a)
    else^:
        print^("other",x+1)
    }
}
let^plain=p^ x {if^ typeof^(x)!=string^ {print^(x+1) else^: print^(x.bytes.len)}}
show("hello") show(A{}) show(41) plain("hey") plain(1)
''', 'text 5\nA 7\nother 42\n3\n2\n')

    # A type bound in a body is an alias: no storage, scoped like a value's name.
    positive('local_type_alias', '''
def^ P {self^{x:i32^=1}}
errordef^ E {Eof,Bad}
let^T=nil^ | E.Eof
let^g=f^ ->i32^ {
    let^I=i32^
    let^Q=P
    let^U=E.Bad | nil^
    var^v:I=3
    var^p=Q{x=4}
    var^q:Q=P{}
    var^u:U=E.Bad{}
    let^n=if^ u fits^ E.Bad: 10 el^: 20;
    return^ v+I(2)+I.ONE+p.x+q.x+n
}
var^t:T=nil^
var^same:nil^ | E.Eof=nil^
print^(g(),typeof^(t)=typeof^(same))
let^w=p^ ->nil^|E {return^ E.Bad{}}
do^{
    let^C=E.Bad
    try^ w()
catch^ E.Bad:
    print^("bad")
catch^:
    print^("other")
}
''', '21 true\nbad\n')

    positive('local_type_catch', '''
errordef^ E {Eof,Bad}
let^w=p^ ->nil^|E {return^ E.Bad{}}
let^g=p^ {
    let^C=E.Bad
    do^{
        try^ w()
    catch^ C:
        print^("bad")
    catch^:
        print^("other")
    }
}
g()
''', 'bad\n')

    positive('local_type_typeof', '''
def^ K {self^{a:i32^=1} name=f^ self^ ->string^ {"K"}}
let^kind=template^ f^ x ->string^ {
    let^t=typeof^ x
    let^same=t
    if^ same=string^ {return^ "string"}
    if^ i32^=t {return^ "i32"}
    return^ t.signature^
}
let^maybe=template^ p^ x {
    let^t=typeof^(x) | nil^
    var^y:t=nil^
    y:=x
    print^(t.signature^,y?)
}
print^(kind("s"),kind(1),kind(K{}),kind(2.5))
maybe(1) maybe("a")
''', 'string i32 K f64^\ni32^ | nil^ true\nstring^ | nil^ true\n')

    # The type is fixed where it is bound, and per specialization.
    positive('local_type_fixed_at_binding', '''
var^x:i32^ | nil^=3
let^t=typeof^ x
if^ x? {
    print^(typeof^(x)=i32^,t=i32^,t.signature^)
    var^b:t=nil^
    print^(b?)
}
let^shadow=template^ p^ x {
    let^t=typeof^(x)
    if^ x=x {
        let^x="s"
        print^(t=string^,typeof^(x)=string^)
    }
}
shadow(1)
let^nested=template^ f^ x,n:i32^ ->string^ {
    let^t=typeof^ x
    if^ n>0 {let^_^=nested("a",n-1)}
    return^ t.signature^
}
print^(nested(1,1))
''', 'true false i32^ | nil^\nfalse\nfalse true\ni32^\n')

    positive('match_on_type', '''
def^ K {self^{a:i32^=1} name=f^ self^ ->string^ {"K"}}
let^of=template^ f^ x ->string^ {
    for^ typeof^(x):
    when^ string^: x
    when^ bool^: if^ x: "yes" el^: "no";
    when^ i32^,i64^: "int"
    other^: x.name();
}
let^show=template^ p^ x {
    for^ typeof^ x {
    when^ string^:
        print^("text",x.bytes.len)
    other^:
        print^("other",it^.signature^)
    }
}
print^(of("abc"),of(true^),of(1),of(i64^(1)),of(K{}))
show("abc") show(1)
''', 'abc yes int int K\ntext 3\nother i32^\n')

    positive('return_type_annotations', '''
let^parse=f^ s:string^ {
    if^ s = "" { return^ nil^ }
    return^ 5
}
let^show=p^ v:parse.ReturnType^ { if^ v fits^ i32^ { print^(v) el^: print^("nil") } }
def^ Holder {self^{v:parse.ReturnType^}}
let^twice=f^ s:string^ ->parse.ReturnType^ { parse(s) }
let^g:parse.ReturnType^=parse("x")
def^ Node {self^{n:i32^=2} get=f^ self^ {self^.n * 2}}
let^h=Holder{v=parse("")}
print^(typeof^(h.v).signature^)
show(twice("a")) show(g) show(h.v)
let^m:Node.get.ReturnType^=Node{}.get()
print^(m)
''', 'i32^ | nil^\n5\n5\nnil\n4\n')

    positive('return_type_selected', '''
let^a=f^ x:i32^ ->i32^ {x}
overload^a=f^ x:string^ ->string^ {x}
let^id=f^ x {x}
let^v:overload^a(string^).ReturnType^="s"
let^w:overload^id(f64^).ReturnType^=1.5
print^(v, w, id(2))
''', 's 1.5 2\n')

    cases = [
        ('local_type_scope', 'let^g=f^ ->i32^ {\n    do^{ let^t=i32^ }\n    var^v:t=3\n    return^ v\n}\nprint^(g())', "unknown type 't'"),
        ('local_type_var', 'var^x=1\nvar^t=typeof^ x', "'typeof^' names a type"),
        ('local_type_arguments', 'let^g=f^ {\n    let^t=i32^\n    var^v:t<i32^>=3\n}\ng()', 'does not accept type arguments'),
        ('local_type_construct', 'let^g=template^ p^ x {\n    let^t=typeof^ x\n    var^v=t{}\n}\ng(1)', 'cannot construct values'),
        ('local_type_member', 'let^g=template^ p^ x {\n    let^t=typeof^ x\n    print^(t.ZERO)\n}\ng(1)', 'provides only signature^'),
        ('match_on_type_cover', 'let^g=template^ f^ x ->i32^ {\n    for^ typeof^ x:\n    when^ i32^: 1;\n}\nprint^(g(1))', 'requires other^:'),
        ('match_on_value_type_arm', 'let^g=f^ x:i32^ ->i32^ {\n    for^ x:\n    when^ i64^: 1\n    other^: 2;\n}\nprint^(g(1))', "unknown value 'i64^'"),
        ('typeof_value', 'let^x=1 print^(typeof^(x))', "'typeof^' names a type"),
        ('typeof_member', 'let^x=1 print^(typeof^(x).ZERO)', 'provides only signature^'),
        ('typeof_value_signature', 'let^x=1 print^(x.signature^)', 'write typeof^(value).signature^'),
        ('typeof_prefix_member', 'let^x=1 print^(typeof^ x.signature^)', 'write typeof^(value).signature^'),
        ('typeof_construct', 'def^ B {self^{n:i32^=0}} let^x=B{} let^y=typeof^(x){n=2}',
         'cannot construct values'),
        ('typeof_open_function', 'let^w=p^x {print^(x)} w(1) print^(typeof^(w).signature^)',
         'parameters left to call sites'),
        ('typeof_unknown', 'print^(typeof^(missing).signature^)', 'cannot determine the type'),
        ('typeof_parameter', 'let^f=f^ a,b:typeof^(a) {a} print^(f(1,2))', 'inside a function body'),
        ('typeof_result', 'let^f=f^ a ->typeof^(a) {a} print^(f(1))', 'inside a function body'),
        ('typeof_field', 'def^ B {self^{n:typeof^(1)=0}}', 'inside a function body'),
        ('typeof_annotation_mismatch', 'let^x=1 var^y:typeof^(x)="s"', "expected 'i32^'"),
        ('typeof_compare_value', 'let^x=1 print^(typeof^(x)=x)', 'needs a type on both sides'),
        ('return_type_overloaded',
         'let^a=f^ x:i32^ ->i32^ {x} overload^a=f^ x:string^ ->string^ {x} let^v:a.ReturnType^=1',
         "'a' is overloaded; write overload^a(types).ReturnType^"),
        ('return_type_untyped', 'let^id=f^ x {x} let^v:id.ReturnType^=1 print^(id(1))',
         "'id' has a parameter without a type; write overload^id(types).ReturnType^"),
        ('return_type_no_arm', 'let^a=f^ x:i32^ ->i32^ {x} let^v:overload^a(string^).ReturnType^=1',
         "no matching overload for 'a'"),
        ('return_type_void', 'let^n=p^ {print^(1)} let^v:n.ReturnType^=1',
         "'n' returns nothing; it has no ReturnType^"),
        ('return_type_missing', 'let^v:missing.ReturnType^=1',
         "'missing' names no function for ReturnType^"),
        ('return_type_cycle', 'def^ Node {self^{v:make.ReturnType^}} let^make=f^ -> Node { Node{} }',
         "cyclic def^ composition at 'Node'"),
    ]
    for name, text, diagnostic in cases:
        negative(name, text, diagnostic)
    suite.finish()


if __name__ == '__main__':
    main()
