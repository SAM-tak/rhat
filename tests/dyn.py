"""ref^ dyn^ D: method tables, dynamic calls and their static checks."""
from harness import Suite


def main():
    suite = Suite('dyn')
    positive, negative = suite.positive, suite.negative

    shapes = '''def^ Shape {
  abstract^ area:f^ self^ -> f64^;
  abstract^ scale:p^ self^, k:f64^;
  describe = p^ self^ { print^("area", self^.area()) }
}
def^ Square : Shape {
  self^{ side:f64^ = 2.0 }
  area = f^ self^ -> f64^ { self^.side * self^.side }
  scale = p^ self^, k:f64^ { self^.side := self^.side * k }
}
def^ Circle : Shape {
  self^{ r:f64^ = 1.0 }
  area = f^ self^ -> f64^ { 3.0 * self^.r * self^.r }
  scale = p^ self^, k:f64^ { self^.r := self^.r * k }
  override^ describe = p^ self^ { print^("circle", self^.area()) }
}
'''

    def tables(rir, ll):
        assert 'dyn.call' in rir and 'dyn.make' in rir, rir
        assert ll.count('= linkonce_odr constant [3 x ptr]') == 2, ll

    positive('dyn_mixed_array', shapes + '''let^ total = f^ shapes:(ref^ dyn^ Shape)[..] -> f64^ {
  var^ sum:f64^ = 0.0
  for^ var^ i:i64^ = 0 while^ i < shapes.len next^ i += 1 { sum += shapes[i].area() }
  return^ sum
}
var^ a = Square{}
var^ b = Circle{}
var^ all:(ref^ dyn^ Shape)[2] = {ref^ a, ref^ b}
print^ total(all[..])
for^ var^ i:i64^ = 0 while^ i < 2 next^ i += 1 { all[i].describe() all[i].scale(2.0) }
print^ total(all[..])
let^ one:ref^ dyn^ Shape = ref^ a
one.describe()
''', '7.0\narea 4.0\ncircle 3.0\n28.0\narea 16.0\n', inspect=tables)

    positive('dynamic_alias_and_reference_depth', shapes + '''
let^Dynamic=dynamic^Shape
var^a=Square{}
var^one:Dynamic=ref^a
var^two:dynamic^^Shape=ref^one
let^three:dynamic^^^Shape=ref^two
let^legacy:ref^^dyn^Shape=two
let^readonly:dynamic^read^Shape=ref^a
print^(one.area(),(*two).area(),(**three).area(),(*legacy).area(),readonly.area())
''', '4.0 4.0 4.0 4.0 4.0\n')

    positive('dynamic_binds_before_array_suffix_and_ref', shapes + '''
let^total=f^items:dynamic^Shape[..] ->f64^ {
    var^sum:f64^=0.0
    for^var^i:i64^=0 while^i<items.len next^i+=1 {sum+=items[i].area()}
    return^sum
}
let^touch=p^items:ref^dynamic^Shape[2] {items[0].scale(2.0)}
let^show=p^items:ref^(dynamic^Shape)[2] {print^(items[0].area(),items[1].area())}
let^forward=p^items:ref^Shape[dynamic^2] {show(items)}
var^a=Square{} var^b=Circle{}
var^direct:dynamic^Shape[2]={ref^a,ref^b}
var^parenthesized:(dynamic^Shape)[2]=direct
var^attribute:Shape[dynamic^2]=direct
let^Array=dynamic^Shape[2]
var^aliased:Array=direct
print^(total(direct[..]),total(parenthesized[..]),total(attribute[..]),total(aliased[..]))
touch(ref^direct) forward(ref^direct)
var^one:dynamic^Shape=ref^a
let^multiple:dynamic^^Shape[2]={ref^one,ref^one}
var^nested:(dynamic^Shape[2])[2]={{ref^a,ref^b},{ref^b,ref^a}}
let^readonly:dynamic^read^Shape[2]={ref^a,ref^b}
print^((*multiple[0]).area(),nested[1][0].area(),readonly[0].area())
''', '7.0 7.0 7.0 7.0\n16.0 3.0\n16.0 3.0 16.0\n')

    positive('dynamic_prefix_array_constructors', shapes + '''
var^a=Square{} var^b=Circle{}
var^fixed=dynamic^Shape[2]{a,ref^b}
var^grow=dynamic^Shape[grow^]{a,ref^b}
var^flex=dynamic^Shape[flex^]{a,ref^b}
var^bound=dynamic^Shape[max^3]{a,ref^b}
var^sized=dynamic^Shape[]{a,ref^b}
var^copy=dynamic^Shape[]{fixed[..]...}
var^reserved=dynamic^Shape[max^](3)
reserved.push(ref^a) grow.push(ref^a) flex.pop() bound.push(ref^b)
print^(fixed[0].area(),grow.len,flex.len,bound.len,sized.len,copy[1].area(),reserved[0].area())
''', '4.0 3 1 3 2 3.0 4.0\n')

    positive('dynamic_generic_element_precedence', '''
def^Base<T> {abstract^get:f^self^ ->T;}
def^Item:Base<i32^> {get=f^self^ ->i32^ {7}}
let^first=f^items:dynamic^Base<i32^>[..] ->i32^ {items[0].get()}
var^item=Item{}
var^items:dynamic^Base<i32^>[2]={ref^item,ref^item}
print^(first(items[..]))
''', '7\n')
    positive('dynamic_typeof_element_precedence', shapes + '''
var^a=Square{}
let^run=p^ {
    var^local=Square{}
    var^items:dynamic^typeof^(local)[2]={ref^local,ref^local}
    print^(items[0].area())
}
run()
''', '4.0\n')
    negative('dynamic_explicit_array_operand', shapes + 'let^g=p^x:dynamic^(Shape[2]) {}',
             "dynamic^ needs a def^ type, found 'Shape[2]'")

    for attribute in ('ref^dyn^', 'dynamic^'):
        positive('dynamic_array_attributes_'+attribute.replace('^','_'), shapes + '''
let^total=f^items:Shape[ATTRIBUTE..] ->f64^ {
    var^sum:f64^=0.0
    for^var^i:i64^=0 while^i<items.len next^i+=1 {sum+=items[i].area()}
    return^sum
}
var^a=Square{}
var^b=Circle{}
var^fixed:Shape[ATTRIBUTE2]={ref^a,ref^b}
let^literal=Shape[ATTRIBUTE2]{a,ref^b}
var^growing=Shape[ATTRIBUTEgrow^]{a,ref^b}
var^flexing=Shape[ATTRIBUTEflex^]{a,ref^b}
var^bounded=Shape[ATTRIBUTEmax^3]{a,ref^b}
let^sized=Shape[ATTRIBUTE]{a,ref^b}
let^copy=Shape[ATTRIBUTE]{fixed[..]...}
var^reserved=Shape[ATTRIBUTEmax^](3)
growing.push(ref^a) flexing.pop() bounded.push(ref^a) reserved.push(ref^a)
print^(total(fixed[..]),total(literal[..]),total(growing[..]),total(flexing[..]),total(bounded[..]),total(sized[..]),total(copy[..]),total(reserved[..]))
'''.replace('ATTRIBUTE',attribute), '7.0 7.0 11.0 4.0 11.0 7.0 7.0 4.0\n', inspect=tables)

    positive('dynamic_array_reference_depth_and_read', shapes + '''
var^a=Square{}
var^one:dynamic^Shape=ref^a
let^two=Shape[dynamic^^2]{ref^one,ref^one}
let^old=Shape[ref^^dyn^2]{ref^one,ref^one}
let^read=Shape[dynamic^read^2]{a,ref^a}
let^oldread=Shape[ref^read^dyn^2]{a,ref^a}
print^((*two[0]).area(),(*old[1]).area(),read[0].area(),oldread[1].area())
''', '4.0 4.0 4.0 4.0\n')
    negative('dynamic_not_def', 'let^g=p^x:dynamic^i32^ {}', 'dynamic^ needs a def^ type')
    negative('dynamic_array_not_def', 'var^a:i32^[dynamic^2]={}', 'dynamic^ needs a def^ type')
    negative('dynamic_read_mutation', shapes + '''var^a=Square{}
let^view:dynamic^read^Shape=ref^a
view.scale(2.0)
''', 'read^')
    negative('dynamic_escape', shapes + '''let^make=f^ ->dynamic^Shape {
var^a=Square{} return^ref^a
}
''', 'cannot return a reference')

    positive('dyn_recursion_without_owned_values', '''def^ W { abstract^ go:p^ self^, n:i64^; }
def^ A : W {
  self^{ z:i32^ = 0 }
  go = p^ self^, n:i64^ { print^ n  let^ me:ref^ dyn^ W = self^  if^ n > 0 { me.go(n - 1) } }
}
var^ a = A{}
a.go(2)
''', '2\n1\n0\n')

    positive('dyn_unannotated_method_not_called', '''def^ N { name = f^ self^ { "base" } }
def^ M : N { }
var^ m = M{}
let^ s:ref^ dyn^ N = ref^ m
print^ m.name()
''', 'base\n')

    kinds = '''def^ Base { abstract^ name:f^ self^ -> string^; }
def^ D1 : Base { self^{ a:i32^ = 1 } name = f^ self^ -> string^ { "d1" } }
def^ D2 : Base { self^{ b:i32^ = 2 } name = f^ self^ -> string^ { "d2" } }
def^ D3 : D2 { self^{ c:i32^ = 3 } override^ name = f^ self^ -> string^ { "d3" } }
'''
    positive('dyn_downcast_exact_and_composing', kinds + '''let^ probe = p^ x:ref^ dyn^ Base {
  if^ x fits^ D2 { print^("exact D2", x.b) }
  if^ x fits^ dyn^ D2 { print^("composes D2", x.name()) }
  if^ x fits^ D1 { print^("exact D1", x.a) else^: print^("not D1", x.name()) }
}
var^ a = D1{}
var^ b = D2{}
var^ c = D3{}
probe(ref^ a)
probe(ref^ b)
probe(ref^ c)
''', 'exact D1 1\nexact D2 2\ncomposes D2 d2\nnot D1 d2\ncomposes D2 d3\nnot D1 d3\n')
    positive('dyn_downcast_match', kinds + '''let^ g = p^ x:ref^ dyn^ Base {
  for^ x { when^ fits^ D1: print^("one", x.a) when^ fits^ D2: print^("two", x.b) other^: print^("other", x.name()) }
}
var^ a = D1{}
var^ b = D2{}
var^ c = D3{}
g(ref^ a) g(ref^ b) g(ref^ c)
''', 'one 1\ntwo 2\nother d3\n')
    positive('dynamic_downcast_composing', kinds + '''let^probe=p^x:dynamic^Base {
    if^x fits^dynamic^D2 {print^(x.name())}
}
var^a=D1{} var^b=D2{} var^c=D3{}
probe(ref^a) probe(ref^b) probe(ref^c)
''', 'd2\nd3\n')
    negative('dyn_downcast_abstract', kinds + 'let^ g = p^ x:ref^ dyn^ Base { if^ x fits^ Base { } }', 'write fits^ dyn^ Base')
    negative('dyn_downcast_else_field', kinds + 'let^ g = p^ x:ref^ dyn^ Base { if^ x fits^ D1 { print^(x.a) else^: print^(x.a) } }',
             'field access requires')

    pre = shapes + 'var^ q = Square{}\n'
    negative('dyn_bare', 'let^ g = f^ s:dyn^ Shape { }', 'write ref^ dyn^ T')
    negative('dyn_unrelated', shapes + 'def^ O { self^{ y:i32^ = 0 } }\nvar^ o = O{}\nlet^ s:ref^ dyn^ Shape = ref^ o', 'cannot become')
    negative('dyn_missing_method', pre + 'let^ s:ref^ dyn^ Shape = ref^ q\nprint^(s.missing())', "has no method 'missing'")
    negative('dyn_field', pre + 'let^ s:ref^ dyn^ Shape = ref^ q\nprint^(s.side)', 'field access requires')
    negative('dyn_escape', shapes + 'let^ mk = f^ -> ref^ dyn^ Shape { var^ q = Square{} return^ ref^ q }', 'cannot return a reference')
    negative('dyn_purity', shapes + 'let^ g = f^ s:ref^ dyn^ Shape -> i32^ { s.describe() return^ 0 }', "f^ cannot call p^ 'describe'")
    negative('dyn_pure_mutation', '''def^ C { abstract^ bump:f^ mutable^ self^ -> i32^; }
def^ K : C { self^{ n:i32^ = 0 } bump = f^ mutable^ self^ -> i32^ { self^.n += 1 return^ self^.n } }
let^ g = f^ c:ref^ dyn^ C -> i32^ { c.bump() }''', 'f^ cannot mutate borrowed storage')
    positive('dyn_mutable_self', '''def^ C { abstract^ bump:f^ mutable^ self^ -> i32^; }
def^ K : C { self^{ n:i32^ = 0 } bump = f^ mutable^ self^ -> i32^ { self^.n += 1 return^ self^.n } }
let^ g = p^ c:ref^ dyn^ C -> i32^ { return^ c.bump() }
var^ k = K{}
print^(g(ref^ k), g(ref^ k))
''', '1 2\n')
    negative('dyn_unannotated_result', 'def^ N { name = f^ self^ { "base" } }\ndef^ M : N { }\nvar^ m = M{}\nlet^ s:ref^ dyn^ N = ref^ m\nprint^(s.name())',
             'its result needs a type annotation')
    negative('dyn_template', 'def^ T { pick = template^ f^ self^, v { v } }\ndef^ U : T { }\nvar^ u = U{}\nlet^ s:ref^ dyn^ T = ref^ u\nprint^(s.pick(1))',
             'not an ordinary self^ method')
    negative('dyn_overloaded', 'def^ V { put = f^ self^, x:i32^ -> i32^ { x } overload^ put = f^ self^, x:string^ -> i32^ { 0 } }\ndef^ X : V { }\nvar^ x = X{}\nlet^ s:ref^ dyn^ V = ref^ x\nprint^(s.put(1))',
             'it is overloaded')
    negative('dyn_recursion_holding_owned', '''def^ W { abstract^ go:p^ self^, n:i64^; }
def^ A : W {
  self^{ z:i32^ = 0 }
  go = p^ self^, n:i64^ { var^ buf:i64^[grow^] = {}  buf.push(n)  let^ me:ref^ dyn^ W = self^  if^ n > 0 { me.go(n - 1) } }
}
var^ a = A{}
a.go(2)''', "holds local owned value 'buf'")
    suite.finish()


if __name__ == '__main__':
    main()
