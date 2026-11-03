"""Generic nominal definitions, transparent type aliases and type constants."""
from harness import Suite


VECTOR = '''
def^ Vec2<T> {
 self^{x:T,y:T}
 ZERO=Self^{x=T.ZERO,y=T.ZERO}
 ONE=Self^{x=T.ONE,y=T.ONE}
 RIGHT=Self^{x=T.ONE,y=T.ZERO}
 op^+ =f^self^,b:Self^ ->Self^ {Self^{x=self^.x+b.x,y=self^.y+b.y}}
 dot=f^self^,b:Self^ ->T {self^.x*b.x+self^.y*b.y}
 make=f^x:T,y:T ->Self^ {Self^{x=x,y=y}}
}
'''


def main():
    suite = Suite('generics')
    positive, negative = suite.positive, suite.negative
    positive('vector_aliases_and_constants', VECTOR + '''
let^Float2=Vec2<f32^>
let^Int2=Vec2<i32^>
let^a=Float2{x=2.0,y=3.0}
let^b:Vec2<f32^>=a+Float2.ONE
let^c=Int2.ZERO+Int2.RIGHT
print^(b.x,b.y,a.dot(b),c.x,c.y)
''', '3.0 4.0 18.0 1 0\n')
    positive('alias_identity_and_chains', VECTOR + '''
let^A=Vec2<i32^>
let^B=A
let^Scalar=i32^
let^C=Vec2<Scalar>
let^take=f^v:C ->B {v}
let^v:A=take(B.ONE)
print^(v.x,v.y,Scalar.ZERO,Scalar.ONE)
''', '1 1 0 1\n')
    positive('nested_and_multiple_parameters', '''
def^Pair<A,B> {self^{left:A,right:B}}
def^Box<T> {self^{value:T}}
let^Nested=Box<Pair<i32^,f64^>>
let^n=Nested{value=Pair<i32^,f64^>{left=7,right=2.5}}
let^accept=f^v:Box<Pair<i32^,f64^>> ->i32^ {v.value.left}
print^(accept(n),n.value.right)
''', '7 2.5\n')
    positive('generic_composition_and_lexical_self', VECTOR + '''
def^Position:Vec2<f32^> {}
def^Velocity:Vec2<f32^> {}
let^p=Position{x=2.0,y=3.0}
let^v=Velocity{x=4.0,y=5.0}
let^z:Vec2<f32^>=Position.ZERO
print^(p.x,v.y,z.x,p.dot(p))
''', '2.0 5.0 0.0 13.0\n')
    positive('generic_base_parameter', '''
def^Base<T> {self^{value:T} get=f^self^ ->T {self^.value}}
def^Derived<T>:Base<T> {self^{extra:T}}
let^d=Derived<i64^>{value=7,extra=9}
print^(d.get(),d.extra)
''', '7 9\n')
    positive('generic_reference_recursion', '''
def^Node<T> {self^{next:ref^Self^|nil^=nil^,value:T}}
var^a=Node<i32^>{value=7}
var^b=Node<i32^>{next=ref^a,value=9}
let^next=b.next
if^next != nil^ {print^(next.value,b.value)}
''', '7 9\n')
    positive('generic_named_union_is_nominal', '''
def^Result<T> = T | nil^
let^r:Result<i32^>=7
if^r fits^i32^ {print^(r)}
''', '7\n')
    positive('generic_array_and_callable_arguments', '''
def^Box<T> {self^{value:T}}
let^B=Box<i32^[2]>
let^b=B{value=[4,5]}
print^(b.value[1])
''', '5\n')
    positive('specialization_keeps_nested_function_scope', '''
def^Box<T> {
 self^{value:T}
 copy=f^self^ ->Self^ {
  let^identity=f^value:T ->T {value}
  return^Self^{value=identity(self^.value)}
 }
}
let^a=Box<i32^>{value=7}.copy()
let^b=Box<f64^>{value=2.5}.copy()
print^(a.value,b.value)
''', '7 2.5\n')
    positive('type_constants_are_values', '''
def^V {
 self^{x:i32^,y:i32^}
 ZERO=Self^{x=i32^.ZERO,y=i32^.ZERO}
 TWO=Self^{x=i32^.ONE+i32^.ONE,y=ZERO.y}
}
var^a=V.TWO
a.x:=9
print^(a.x,V.TWO.x,V.ZERO.y)
''', '9 2 0\n')
    positive('private_constant_and_private_fields', '''
def^V {
 self^{private^x:i32^}
 private^VALUE=7
 ZERO=Self^{x=VALUE}
 get=f^self^ ->i32^ {self^.x}
}
let^a=V.ZERO
print^(a.get())
''', '7\n')
    positive('custom_scalar_constants', '''
def^Scalar {
 self^{value:i32^}
 ZERO=Self^{value=0}
 ONE=Self^{value=1}
}
def^Pair<T> {self^{x:T,y:T} ZERO=Self^{x=T.ZERO,y=T.ZERO}}
let^p=Pair<Scalar>.ZERO
print^(p.x.value,p.y.value)
''', '0 0\n')
    positive('self_construction_without_generics', '''
def^Box {self^{x:i32^} make=f^self^ ->Self^ {Self^{x=7}}}
let^b=Box{}.make()
print^(b.x)
''', '7\n')
    positive('numeric_alias_and_parameter_conversion', '''
def^Convert<T> {convert=f^self^,value:i32^ ->T {T(value)}}
let^Real=f32^
let^c=Convert<Real>{}
print^(Real(7),c.convert(9))
''', '7.0 9.0\n')
    positive('enum_alias_preserves_members', '''
enum^Direction:u8^ {Left,Right}
let^D=Direction
let^d:Direction=D.Right
print^(d=D.Right,d=Direction.Left)
''', 'true false\n')
    positive('owned_generic_values_and_loop_scopes', '''
def^Buffer<T> {
 self^{items:T[grow^]}
 fill=p^self^,value:T {
  for^i from^0 to^2 {self^.items.push(value)}
 }
}
let^make=f^ ->Buffer<i32^> {var^b=Buffer<i32^>{} return^b}
var^b=make()
b.fill(7)
print^(b.items.len,b.items[2])
''', '3 7\n')
    positive('canonical_union_type_arguments', '''
def^Box<T> {self^{value:T}}
let^a:Box<i32^|nil^>=Box<nil^|i32^>{value=7}
let^b:Box<i32^>=Box<i32^|i32^>{value=9}
let^value=a.value
if^value fits^i32^ {print^(value,b.value)}
''', '7 9\n')
    positive('callable_type_arguments', '''
def^Tag<T> {self^{value:i32^}}
let^a=Tag<f^i32^ ->i32^;>{value=7}
print^(a.value)
''', '7\n')
    positive('explicit_recursive_generic_application', '''
def^Node<T> {self^{value:T,next:ref^Node<T>|nil^=nil^}}
var^a=Node<i32^>{value=7}
var^b=Node<i32^>{value=9,next=ref^a}
print^(b.next?.value??0)
''', '7\n')
    for scalar in ('i8','u8','i16','u16','i32','u32','i64','u64','i128','u128',
                   'f8','f16','f32','f64'):
        positive('numeric_constants_' + scalar, f'''
let^z:{scalar}^={scalar}^.ZERO
let^o:{scalar}^={scalar}^.ONE
print^(z,o)
''', '0.0 1.0\n' if scalar.startswith(('f', 'bf')) else '0 1\n')

    positive('static_members', VECTOR + '''
def^Pos:Vec2<i32^>{}
def^M {
 self^{v:i64^}
 fact=f^n:i64^ ->i64^ {if^ n<2: 1 el^: n*this^(n-1);}
 hello=p^n:i64^ {print^("hi",n)}
 private^ secret=f^ ->i64^ {7}
 peek=f^ ->i64^ {Self^.secret()}
 pick=f^n:i64^ ->i64^ {n}
 overload^ pick=f^a:i64^,b:i64^ ->i64^ {a+b}
 twice=f^self^ ->i64^ {Self^.pick(self^.v,self^.v)}
}
let^Float2=Vec2<f32^>
let^a=Float2.make(1.0,2.0)
M.hello(3)
print^(a.dot(Vec2<f32^>.make(3.0,4.0)),Pos.make(1,2).y,M.fact(5),M.peek(),M.pick(2),M{v=4}.twice())
''', 'hi 3\n11.0 2 120 7 2 8\n')

    cases = [
        ('static_private', 'def^V{private^s=f^ ->i32^ {7}}\nprint^(V.s())', "private member 's'"),
        ('static_from_value', 'def^V{self^{x:i32^} s=f^ ->i32^ {7}}\nprint^(V{x=1}.s())', 'is a static member'),
        ('static_uncalled', 'def^V{s=f^ ->i32^ {7}}\nlet^g=V.s', "static member 's' must be called"),
        ('static_method_name', 'def^V{s=f^ ->i32^ {7} overload^s=f^self^,n:i32^ ->i32^ {n}}', 'cannot share a name'),
        ('static_abstract', 'def^V{abstract^s:f^ ->i32^;}', 'cannot be abstract^ or override^'),
        ('static_override', 'def^B{s=f^ ->i32^ {7}}\ndef^D:B{override^s=f^ ->i32^ {8}}', 'cannot be abstract^ or override^'),
        ('static_missing', 'def^V{}\nprint^(V.s())', 'has no constant or static member'),
        ('static_arity', 'def^V{s=f^ ->i32^ {7}}\nprint^(V.s(1))', 'expects 0 arguments'),
        ('missing_arguments', 'def^Box<T>{self^{v:T}}\nlet^B=Box', 'expects 1 type arguments'),
        ('extra_arguments', 'def^Box<T>{self^{v:T}}\nlet^B=Box<i32^,f32^>', 'expects 1 type arguments'),
        ('nongeneric_arguments', 'def^Box{}\nlet^B=Box<i32^>', 'expects 0 type arguments'),
        ('duplicate_parameter', 'def^Box<T,T>{}', 'duplicate type parameter'),
        ('different_specializations', 'def^Box<T>{self^{v:T}}\nlet^b:Box<i32^>=Box<f32^>{v=1.0}', 'nominal'),
        ('derived_types_differ', VECTOR + 'def^P:Vec2<i32^>{}\ndef^V:Vec2<i32^>{}\nlet^p:P=V{}', 'nominal'),
        ('derived_zero_is_base', VECTOR + 'def^P:Vec2<i32^>{}\nlet^p:P=P.ZERO', 'nominal'),
        ('missing_scalar_constant', 'def^S{}\ndef^Box<T>{ZERO=T.ZERO}\nlet^B=Box<S>', 'has no constant'),
        ('constant_cycle', 'def^V{A=Self^.B B=Self^.A}', 'cyclic type constant'),
        ('constant_function', 'let^f=f^ {7}\ndef^V{A=f()}', 'type constant requires'),
        ('constant_default_function', 'let^f=f^ {7}\ndef^V{self^{x:i32^=f()} A=Self^{}}', 'type constant cannot call'),
        ('constant_private', 'def^V{private^A=7}\nprint^(V.A)', 'private type constant'),
        ('constant_duplicate', 'def^V{A=0 A=1}', 'duplicate type constant'),
        ('constant_field_collision', 'def^V{self^{A:i32^} A=0}', 'name collision'),
        ('constant_method_collision', 'def^V{A=f^self^ {0} A=0}', 'name collision'),
        ('constant_owned', 'def^V{self^{items:i32^[grow^]} A=Self^{}}', 'cannot own a data stack region'),
        ('constant_capture', 'var^n=7\ndef^V{A=n}', 'top-level binding'),
        ('alias_cycle', 'def^Box<T>{self^{v:T}}\nlet^A=Box<A>', 'cyclic type alias'),
        ('alias_arguments', VECTOR + 'let^A=Vec2<i32^>\nlet^B=A<f32^>', 'does not accept type arguments'),
        ('self_call', 'def^V{A=Self^(0)}', 'type constant requires'),
        ('self_value', 'def^V{f=f^self^ {Self^}}', 'names a type, not a value'),
        ('self_arguments', 'def^V<T>{self^{v:Self^<T>}}\nlet^A=V<i32^>', 'Self^ does not accept'),
        ('type_parameter_application', 'def^V<T>{self^{v:T<i32^>}}\nlet^A=V<i32^>', 'does not accept type arguments'),
        ('callable_has_no_constant', 'def^V<T>{ZERO=T.ZERO}\nlet^A=V<f^i32^ ->i32^;>', 'has no constant'),
        ('generic_borrow_escape', 'def^Box<T>{self^{v:T}}\nlet^bad=f^ ->ref^Box<i32^>{var^b=Box<i32^>{v=7} return^ref^b}\nbad()', 'cannot return'),
        ('public_runtime_block', 'public^do^{}', 'public^ executable bindings'),
    ]
    for name, text, diagnostic in cases:
        negative(name, text, diagnostic)

    def module_case():
        root = suite.work / 'modules'
        root.mkdir(exist_ok=True)
        (root / 'vector.rh').write_text('module^vector\npublic^' + VECTOR.lstrip() +
                                      '\npublic^let^Float2=Vec2<f32^>\n', encoding='utf-8')
        (root / 'other.rh').write_text('''module^other
let^v=import^vector
public^let^F=v.Vec2<f32^>
public^let^read=f^p:F ->f32^ {p.x}
''', encoding='utf-8')
        source = root / 'main.rh'
        source.write_text('''let^v=import^vector
let^o=import^other
let^F=v.Float2
let^p:F=v.Vec2<f32^>.ONE
let^q:o.F=F.make(2.0,3.0)
print^(o.read(p),o.read(q),F.ZERO.y,v.Float2.make(4.0,5.0).y)
''', encoding='utf-8')
        if suite.args.checks_only:
            suite.run([suite.compiler, 'check', source])
        else:
            for opt in suite.opts:
                exe = root / ('main' + opt + '.exe')
                suite.run([suite.compiler, 'build', source, '-o', exe, '--clang', suite.clang, opt])
                assert suite.run([exe]).stdout == '1.0 2.0 0.0 5.0\n'
    suite.case('imported_templates_and_alias_identity', module_case)
    suite.finish()


if __name__ == '__main__':
    main()
