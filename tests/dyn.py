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

    positive('dyn_mixed_array', shapes + '''let^ total = f^ shapes:(ref^ dyn^ Shape)[:] -> f64^ {
  var^ sum:f64^ = 0.0
  for^ var^ i:i64^ = 0 while^ i < shapes.len next^ i += 1 { sum += shapes[i].area() }
  return^ sum
}
var^ a = Square{}
var^ b = Circle{}
var^ all:(ref^ dyn^ Shape)[2] = [ref^ a, ref^ b]
print^ total(all[::])
for^ var^ i:i64^ = 0 while^ i < 2 next^ i += 1 { all[i].describe() all[i].scale(2.0) }
print^ total(all[::])
let^ one:ref^ dyn^ Shape = ref^ a
one.describe()
''', '7\narea 4\ncircle 3\n28\narea 16\n', inspect=tables)

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
  go = p^ self^, n:i64^ { var^ buf:grow^ i64^[] = []  buf.push(n)  let^ me:ref^ dyn^ W = self^  if^ n > 0 { me.go(n - 1) } }
}
var^ a = A{}
a.go(2)''', "holds local owned value 'buf'")
    suite.finish()


if __name__ == '__main__':
    main()
