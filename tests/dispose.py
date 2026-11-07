"""Disposable: scopes dispose values, and such values are never copied."""

from harness import Suite

DEVICE = '''
let^c = import^std.collections
errordef^E { Bad }
def^D : Disposable {
    self^{ h:i64^ }
    id = f^self^ -> i64^ { return^self^.h }
    dispose = p^self^ { print^("dispose", self^.h) }
}
let^make = p^h:i64^ -> D { return^D{h = h} }
let^open = p^h:i64^, ok:bool^ -> D | E {
    if^!ok { return^E.Bad{} }
    return^make(h)
}
let^fail = p^ -> nil^ | E { return^E.Bad{} }
'''


def main():
    suite = Suite('dispose')
    positive, negative = suite.positive, suite.negative

    positive('every_way_out', DEVICE + '''
do^{
    let^a = make(1)
    var^b = make(2)
    print^(a.h, b.h)
}
let^early = p^ -> i32^ {
    var^r = make(3)
    return^1
}
print^(early())
for^i from^0 to^1 {
    var^r = make(i64^(10 + i))
    if^i = 0 { next^ }
    break^
}
let^propagates = p^ -> nil^ | E {
    var^r = make(4)
    try^fail()
    print^("never")
    return^nil^
}
print^(propagates() fits^ E)
do^{
    var^outer = make(5)
    do^{
        var^inner = make(6)
    }
    print^("between")
}
do^{
    var^r = make(7)
    try^fail()
    print^("never")
catch^:
    print^("caught")
}
let^_^ = make(8)
print^("end")
''', '1 2\ndispose 2\ndispose 1\ndispose 3\n1\ndispose 10\ndispose 11\ndispose 4\ntrue\n'
     'dispose 6\nbetween\ndispose 5\ndispose 7\ncaught\nend\ndispose 8\n')

    positive('parts', DEVICE + '''
def^Holder : Disposable {
    self^{ a:D, n:i32^ = 0, b:D }
    dispose = p^self^ { print^("holder", self^.n) }
}
def^Plain { self^{ d:D } }
def^Hidden { self^{ private^d:D = D{h = 9} } }
do^{
    var^h = Holder{a = make(1), b = make(2)}
    var^p = Plain{d = make(3)}
    var^s = Hidden{}
}
print^("arrays")
do^{
    var^fixed:D[2] = { make(1), make(2) }
    var^grown = D[grow^]{ make(3), make(4) }
    var^bounded = D[max^2]{ make(5) }
    var^nested = Plain[grow^]{ Plain{d = make(6)} }
}
print^("unions")
do^{
    var^yes = open(1, true^)
    var^no = open(2, false^)
    var^maybe:D | nil^ = nil^
    var^there:D | nil^ = make(3)
}
print^("end")
''', 'dispose 9\ndispose 3\nholder 0\ndispose 2\ndispose 1\narrays\ndispose 6\ndispose 5\n'
     'dispose 4\ndispose 3\ndispose 2\ndispose 1\nunions\ndispose 3\ndispose 1\nend\n')

    positive('overwrite_and_shrink', DEVICE + '''
var^a = make(1)
print^("next")
a := make(2)
var^xs = D[grow^]{ make(3), make(4), make(5), make(6) }
xs[0] := make(7)
print^("truncate")
xs.truncate(2)
print^("pop")
let^last = xs.pop()
print^(last.h, xs.len)
print^("clear")
xs.clear()
print^(xs.len)
''', 'next\ndispose 1\ndispose 3\ntruncate\ndispose 6\ndispose 5\npop\n4 1\nclear\ndispose 7\n0\n'
     'dispose 4\ndispose 2\n')

    positive('return_moves', DEVICE + '''
let^ready = p^h:i64^, ok:bool^ -> D | E {
    var^d = make(h)
    if^!ok { try^fail() }
    return^d
}
let^either = p^a:bool^ -> D {
    var^x = make(1)
    var^y = make(2)
    if^a { return^x }
    return^y
}
do^{
    var^a = try^ready(1, true^)
    print^("got", a.h)
    var^b = try^ready(2, false^)
    print^("never")
catch^:
    print^("failed")
}
let^p = either(true^)
print^(p.h)
let^q = either(false^)
print^(q.h)
''', 'got 1\ndispose 2\ndispose 1\nfailed\ndispose 2\n1\ndispose 1\n2\ndispose 2\ndispose 1\n')

    positive('return_owned', DEVICE + '''
let^all = p^n:i64^, ok:bool^ -> D[grow^] | E {
    var^xs = D[grow^]{}
    for^i in^0 .. n { xs.push(make(i)) }
    if^!ok { try^fail() }
    return^xs
}
let^stops = p^ok:bool^ -> D[grow^] | E {
    var^xs = D[grow^]{ make(5) }
    if^!ok { return^E.Bad{} }
    return^xs
}
do^{
    let^xs = try^all(2, true^)
    print^(xs.len)
catch^:
    print^("never")
}
do^{
    let^xs = try^all(2, false^)
    print^("never")
catch^:
    print^("failed")
}
let^stopped = stops(false^)
print^(stopped fits^ E)
do^{
    let^kept = try^stops(true^)
    print^(kept.len)
catch^:
    print^("never")
}
''', '2\ndispose 1\ndispose 0\ndispose 1\ndispose 0\nfailed\ndispose 5\ntrue\n1\ndispose 5\n')

    positive('partial_construction', DEVICE + '''
def^Pair { self^{ a:D, b:D } }
do^{
    let^p = Pair{a = try^open(1, true^), b = try^open(2, false^)}
    print^("never")
catch^:
    print^("record")
}
do^{
    var^xs:D[2] = { try^open(3, true^), try^open(4, false^) }
    print^("never")
catch^:
    print^("fixed")
}
do^{
    var^xs = D[grow^]{ try^open(5, true^), try^open(6, false^) }
    print^("never")
catch^:
    print^("grown")
}
let^both = p^ -> (D, D) | E {
    return^try^open(7, true^), try^open(8, false^)
}
let^whole = both()
print^(whole fits^ E)
def^Outer { self^{ p:Pair, d:D } }
do^{
    let^o = Outer{p = Pair{a = try^open(9, true^), b = try^open(10, true^)}, d = try^open(11, false^)}
catch^:
    print^("whole part")
}
do^{
    let^o = Outer{p = Pair{a = try^open(12, true^), b = try^open(13, false^)}, d = make(14)}
catch^:
    print^("nested part")
}
''', 'dispose 1\nrecord\ndispose 3\nfixed\ndispose 5\ngrown\ndispose 7\ntrue\n'
     'dispose 10\ndispose 9\nwhole part\ndispose 12\nnested part\n')

    positive('containers', DEVICE + '''
var^files = D[grow^]{}
var^map:c.HashMap<string^, ref^D> = {}
files.push(D{h = 1})
files.push(try^open(2, true^))
map.set("a", ref^files[0])
map.set("b", ref^files[1])
let^a = map.get("a")
if^a fits^ref^D { print^(a.h) }
for^i in^0 .. files.len { print^(files[i].h) }
print^("end")
''', '1\n1\n2\nend\ndispose 2\ndispose 1\n')

    positive('union_in_place', DEVICE + '''
def^Counter : Disposable {
    self^{ n:i32^ = 0 }
    add = p^self^ { self^.n += 1 }
    dispose = p^self^ { print^("counter", self^.n) }
}
let^counter = p^ -> Counter | E { return^Counter{} }
var^u = counter()
if^u fits^Counter { u.add() }
if^u fits^Counter { u.add() }
var^x = open(2, true^) catch^ make(3)
print^(x.h)
var^y = open(4, false^) catch^ make(5)
print^(y.h)
''', '2\n5\ndispose 5\ndispose 2\ncounter 2\n')

    positive('match', DEVICE + '''
def^Counter : Disposable {
    self^{ n:i32^ = 0 }
    add = p^self^ { self^.n += 1 }
    dispose = p^self^ { print^("counter", self^.n) }
}
let^counter = p^ -> Counter | E { return^Counter{} }
var^u = counter()
for^u {
when^fits^Counter:
    u.add()
other^:
    print^("error")
}
print^("after")
for^open(2, true^) {
when^fits^D:
    print^("got", it^.h)
other^:
    print^("error")
}
print^("end")
''', 'after\ngot 2\ndispose 2\nend\ncounter 1\n')

    positive('return_narrowed', DEVICE + '''
let^g = p^ -> D {
    var^u = open(1, true^)
    if^u fits^D { return^u }
    return^make(2)
}
let^x = g()
print^(x.h)
''', '1\ndispose 1\n')

    positive('top_level_used_by_a_function', DEVICE + '''
var^g = make(1)
let^show = p^ { print^(g.h) }
show()
''', '1\ndispose 1\n')

    positive('references_and_views', DEVICE + '''
let^id = p^d:ref^D -> i64^ { return^d.h }
var^a = make(1)
print^(id(ref^a))
var^files = D[grow^]{ make(2) }
print^(id(ref^files[0]))
let^view = files[..]
print^(view[0].h, view.len)
def^Keeper { self^{ d:ref^D } }
var^keeper = Keeper{d = ref^files[0]}
var^other = keeper
print^(other.d.h)
''', '1\n2\n2 1\n2\ndispose 2\ndispose 1\n')

    positive('composition_and_generic', DEVICE + '''
def^Base : Disposable {
    self^{ n:i32^ = 1 }
    dispose = p^self^ { print^("base", self^.n) }
}
def^Derived : Base {}
def^Box<T> { self^{ item:T } }
def^Own<T> : Disposable {
    self^{ item:T }
    dispose = p^self^ { print^("own", self^.item) }
}
var^d = Derived{}
var^b = Box<D>{item = make(9)}
var^o = Own<i32^>{item = 5}
print^(d.n, b.item.h)
''', '1 9\nown 5\ndispose 9\nbase 1\n')

    positive('dispose_name_alone', '''
def^N {
    dispose = p^self^ { print^("called") }
}
var^n = N{}
var^copy = n
copy.dispose()
print^("end")
''', 'called\nend\n')

    positive('own_disposable_name', '''
def^Disposable { self^{ n:i32^ = 1 } }
def^N : Disposable {
    dispose = p^self^ { print^("never") }
}
var^n = N{}
var^copy = n
print^(copy.n)
''', '1\n')

    positive('made_in_a_function', DEVICE + '''
let^build = f^h:i64^ -> D { return^D{h = h} }
var^a = build(1)
''', 'dispose 1\n')

    copied = "is Disposable and cannot be copied"
    negative('let_from_name', DEVICE + 'var^a = make(1)\nlet^b = a\n', copied)
    negative('var_annotated', DEVICE + 'var^a = make(1)\nvar^b:D = a\n', copied)
    negative('assign_from_name', DEVICE + 'var^a = make(1)\nvar^b = make(2)\nb := a\n', copied)
    negative('field_from_name', DEVICE + 'def^H { self^{ d:D } }\nvar^a = make(1)\nvar^h = H{d = a}\n', copied)
    negative('push_from_name', DEVICE + 'var^a = make(1)\nvar^xs = D[grow^]{}\nxs.push(a)\n', copied)
    negative('literal_from_name', DEVICE + 'var^a = make(1)\nvar^xs = D[grow^]{ a }\n', copied)
    negative('fixed_literal_from_name', DEVICE + 'var^a = make(1)\nvar^xs:D[1] = { a }\n', copied)
    negative('element_out', DEVICE + 'var^xs = D[grow^]{ make(1) }\nlet^a = xs[0]\n', copied)
    negative('field_out', DEVICE + 'def^H { self^{ d:D } }\nvar^h = H{d = make(1)}\nlet^a = h.d\n', copied)
    negative('holder_copied', DEVICE + 'def^H { self^{ d:D } }\nvar^h = H{d = make(1)}\nvar^i = h\n', copied)
    negative('deref_out', DEVICE + 'var^a = make(1)\nlet^r = ref^a\nvar^b = *r\n', copied)
    negative('return_field', DEVICE + 'def^H { self^{ d:D } }\nlet^g = p^h:ref^H -> D {\n    return^h.d\n}\n', copied)
    negative('return_element', DEVICE + 'let^g = p^ -> D {\n    var^xs = D[grow^]{ make(1) }\n    return^xs[0]\n}\ng()\n', copied)
    negative('union_copied', DEVICE + 'var^u = open(1, true^)\nvar^v = u\n', copied)
    negative('narrowed_out', DEVICE + 'var^u = open(1, true^)\nif^u fits^D {\n    let^v:D = u\n}\n', copied)
    negative('slice_element_out', DEVICE + 'var^xs = D[grow^]{ make(1) }\nlet^view = xs[..]\nlet^a = view[0]\n', copied)
    negative('loop_binding', DEVICE + 'var^xs = D[grow^]{ make(1) }\nfor^x in^xs { print^(x.h) }\n', copied)
    negative('branches', DEVICE + 'var^a = make(1)\nvar^b = make(2)\nlet^x = if^true^: a el^: b;\n', copied)
    negative('derived_copied', 'def^B : Disposable { dispose = p^self^ {} }\ndef^S : B {}\nvar^s = S{}\nvar^t = s\n', copied)
    negative('generic_copied', DEVICE + 'def^Box<T> { self^{ item:T } }\nvar^b = Box<D>{item = make(1)}\nvar^o = b\n', copied)
    negative('hash_map_declared', DEVICE + 'var^m:c.HashMap<string^, D> = {}\n', copied)

    by_value = "cannot be passed by value"
    negative('value_parameter', DEVICE + 'let^g = p^d:D { print^(d.h) }\ng(make(1))\n', by_value)
    negative('parameter_not_called', DEVICE + 'let^g = p^d:D { print^(d.h) }\n', by_value)
    negative('hash_map_value', DEVICE + 'var^m:c.HashMap<string^, D> = {}\nm.set("a", make(1))\n', by_value)
    negative('template_argument', DEVICE + 'let^g = template^p^d { print^(d.h) }\ng(make(1))\n', by_value)
    negative('external_argument', DEVICE + 'externC^g:p^d:D;\ng(make(1))\n', by_value)

    temporary = "a Disposable temporary has nothing to dispose it"
    negative('discarded', DEVICE + 'make(1)\n', temporary)
    negative('discarded_union', DEVICE + 'open(1, true^)\n', temporary)
    negative('discarded_pop', DEVICE + 'var^xs = D[grow^]{ make(1) }\nxs.pop()\n', temporary)
    negative('temporary_field', DEVICE + 'print^(make(1).h)\n', temporary)
    negative('temporary_method', DEVICE + 'print^(make(1).id())\n', temporary)
    negative('temporary_fits', DEVICE + 'print^(open(1, true^) fits^ D)\n', temporary)
    negative('temporary_index', DEVICE + 'let^g = p^ -> D[2] { return^{ make(1), make(2) } }\nprint^(g()[0].h)\n', temporary)

    negative('tail_name', DEVICE + 'let^g = p^ -> D {\n    var^a = make(1)\n    a\n}\ng()\n', 'only calls can be expression statements')
    negative('let_calls_p', DEVICE + 'def^M : Disposable {\n add = p^self^ {}\n dispose = p^self^ {}\n}\nlet^m = M{}\nm.add()\n', "cannot modify let^ binding 'm'")
    negative('direct_call', DEVICE + 'var^a = make(1)\na.dispose()\n', 'it cannot be called directly')
    negative('direct_call_on_self', 'def^N : Disposable {\n dispose = p^self^ {}\n again = p^self^ { self^.dispose() }\n}\nvar^n = N{}\nn.again()\n', 'it cannot be called directly')
    negative('dispose_missing', 'def^N : Disposable {}\nvar^n = N{}\n', 'cannot construct abstract')
    negative('dispose_with_argument', 'def^N : Disposable { dispose = p^self^, x:i32^ {} }\nvar^n = N{}\n', 'dispose')
    negative('dispose_with_result', 'def^N : Disposable { dispose = p^self^ -> i32^ { return^1 } }\nvar^n = N{}\n', 'dispose')
    negative('needs_procedure', DEVICE + 'let^g = f^ -> i64^ {\n    let^d = D{h = 1}\n    return^d.h\n}\nprint^(g())\n', 'needs p^')
    negative('first_binding', DEVICE + 'for^i in^0 .. i64^(2) {\nfirst^:\n    var^d = make(i)\nmain^:\n    print^(i)\n}\n', 'cannot hold a Disposable')
    negative('assign_many', DEVICE + 'var^a = make(1)\nvar^b = make(2)\na, b := make(3), make(4)\n', 'one at a time')
    negative('slice_element_written', DEVICE + 'def^M : Disposable {\n self^{ n:i32^ = 0 }\n add = p^self^ { self^.n += 1 }\n dispose = p^self^ {}\n}\nvar^xs = M[grow^]{ M{} }\nlet^view = xs[..]\nview[0].add()\n', 'read')
    negative('repeat', DEVICE + 'var^xs = D[grow^]{ make(1) repeat^2 }\n', 'would copy Disposable')
    negative('fixed_repeat', DEVICE + 'var^xs:D[2] = { make(1) repeat^2 }\n', 'would copy Disposable')
    negative('source', DEVICE + 'var^xs = D[grow^]{ make(1) }\nvar^ys = D[grow^]{ xs... }\n', 'would copy Disposable')
    negative('append', DEVICE + 'var^xs = D[grow^]{ make(1) }\nvar^ys = D[grow^]{}\nys.append(xs[..])\n', 'would copy Disposable')
    negative('outlives_owner', DEVICE + '''
var^map:c.HashMap<string^, ref^D> = {}
do^{
    var^files = D[grow^]{ make(1) }
    map.set("a", ref^files[0])
}
''', 'reference would outlive its owner')
    refer = 'may refer into it'
    negative('pop_while_referred', DEVICE + 'var^xs = D[grow^]{ make(1) }\nlet^r = ref^xs[0]\nlet^d = xs.pop()\nprint^(r.h)\n', refer)
    negative('clear_while_mapped', DEVICE + 'var^xs = D[grow^]{ make(1) }\nvar^m:c.HashMap<string^, ref^D> = {}\nm.set("a", ref^xs[0])\nxs.clear()\n', refer)
    negative('truncate_while_referred', DEVICE + 'var^xs = D[grow^]{ make(1) }\nlet^r = ref^xs[0]\nxs.truncate(0)\nprint^(r.h)\n', refer)

    negative('with_is_gone', 'with^r = 1 {}\n', 'error')
    negative('noncopy_is_gone', 'noncopy^def^N {}\n', 'noncopy^')

    suite.finish()


if __name__ == '__main__':
    main()
