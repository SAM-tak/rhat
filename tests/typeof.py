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

    cases = [
        ('typeof_value', 'let^x=1 print^(typeof^(x))', "'typeof^' names a type"),
        ('typeof_member', 'let^x=1 print^(typeof^(x).ZERO)', 'provides only signature^'),
        ('typeof_value_signature', 'let^x=1 print^(x.signature^)', 'write typeof^(value).signature^'),
        ('typeof_prefix_member', 'let^x=1 print^(typeof^ x.signature^)', 'write typeof^(value).signature^'),
        ('typeof_construct', 'def^ B {self^{n:i32^=0}} let^x=B{} let^y=typeof^(x){n=2}',
         'cannot construct values'),
        ('typeof_open_function', 'let^w=p^x {print^(x)} w(1) print^(typeof^(w).signature^)',
         'parameters left to call sites'),
        ('typeof_unknown', 'print^(typeof^(missing).signature^)', 'cannot determine the type'),
    ]
    for name, text, diagnostic in cases:
        negative(name, text, diagnostic)
    suite.finish()


if __name__ == '__main__':
    main()
