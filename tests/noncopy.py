"""noncopy^ def^: an array owns the value, everything else holds a ref^."""

from harness import Suite

DEVICE = '''
let^c = import^std.collections
errordef^E { Bad }
noncopy^def^D {
    self^{ h:i64^, closed:bool^ = false^ }
    close = p^self^ {
        if^self^.closed {
            print^("closed twice", self^.h)
            return^
        }
        self^.closed := true^
        print^("close", self^.h)
    }
    dispose = p^self^ { self^.close() }
}
let^make = p^h:i64^ -> D { return^D{h = h} }
let^open = p^h:i64^, ok:bool^ -> D | E {
    if^!ok { return^E.Bad{} }
    return^make(h)
}
'''


def main():
    suite = Suite('noncopy')
    positive, negative = suite.positive, suite.negative

    positive('array_owns_map_refers', DEVICE + '''
var^files = D[grow^]{}
var^map:c.HashMap<string^, ref^D> = {}
files.push(D{h = 1})
files.push(make(2))
map.set("a", ref^files[0])
map.set("b", ref^files[1])
let^a = map.get("a")
if^a fits^ref^D {
    a.close()
    a.close()
}
for^i in^0 .. files.len { files[i].close() }
print^(files[0].closed, files[1].closed)
''', 'close 1\nclosed twice 1\nclosed twice 1\nclose 2\ntrue true\n')

    positive('temporaries_move', DEVICE + '''
def^Holder { self^{ d:D } }
var^a = make(1)
a := make(2)
var^h = Holder{d = make(3)}
var^files = D[grow^]{ make(4) }
files[0] := make(5)
print^(a.h, h.d.h, files[0].h)
print^(make(6).h)
var^fixed:D[2] = { make(7), make(8) }
print^(fixed[1].h)
print^(files.pop().h, files.len)
''', '2 3 5\n6\n8\n5 0\n')

    positive('with_and_try', DEVICE + '''
do^{
    with^a = try^open(1, true^)
    with^b = make(2)
    with^r = ref^b {
        a.close()
        print^(r.h)
    }
catch^:
    print^("failed")
}
do^{
    with^a = try^open(3, false^) {
        print^(a.h)
    }
catch^:
    print^("failed")
}
''', 'close 1\n2\nclose 2\nclosed twice 2\nclosed twice 1\nfailed\n')

    positive('union_in_place', DEVICE + '''
var^u = open(1, true^)
if^u fits^D {
    u.close()
}
if^u fits^D {
    u.close()
}
var^x = open(2, true^) catch^ make(3)
print^(x.h)
var^y = open(4, false^) catch^ make(5)
print^(y.h)
''', 'close 1\nclosed twice 1\n2\n5\n')

    positive('references', DEVICE + '''
let^shut = p^d:ref^D { d.close() }
var^a = make(1)
shut(ref^a)
let^r = ref^a
shut(r)
var^files = D[grow^]{}
files.push(make(2))
shut(ref^files[0])
let^view = files[..]
print^(view[0].closed, view.len)
def^Keeper { self^{ d:ref^D } }
var^keeper = Keeper{d = ref^files[0]}
var^other = keeper
other.d.close()
''', 'close 1\nclosed twice 1\nclose 2\ntrue 1\nclosed twice 2\n')

    positive('try_into_places', DEVICE + '''
var^files = D[grow^]{}
do^{
    files.push(try^open(1, true^))
    files[0] := try^open(2, true^)
    print^(files[0].h, files.len)
    files.push(try^open(3, false^))
    print^("not reached")
catch^:
    print^("failed", files.len)
}
''', '2 1\nfailed 1\n')

    positive('composition_and_generic', DEVICE + '''
noncopy^def^Base { self^{ n:i32^ = 1 } }
def^Derived : Base {}
def^Box<T> { self^{ item:T } }
var^d = Derived{}
var^b = Box<D>{item = make(9)}
print^(d.n, b.item.h)
''', '1 9\n')

    copied = "is noncopy^ and cannot be copied"
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
    negative('return_name', DEVICE + 'let^g = p^ -> D {\n    var^a = make(1)\n    return^a\n}\n', copied)
    negative('return_name_into_union', DEVICE + 'let^g = p^ -> D | E {\n    var^a = make(1)\n    return^a\n}\n', copied)
    negative('union_copied', DEVICE + 'var^u = open(1, true^)\nvar^v = u\n', copied)
    negative('narrowed_out', DEVICE + 'var^u = open(1, true^)\nif^u fits^D {\n    let^v:D = u\n}\n', copied)
    negative('slice_element_out', DEVICE + 'var^xs = D[grow^]{ make(1) }\nlet^view = xs[..]\nlet^a = view[0]\n', copied)
    negative('loop_binding', DEVICE + 'var^xs = D[grow^]{ make(1) }\nfor^x in^xs { print^(x.h) }\n', copied)
    negative('branches', DEVICE + 'var^a = make(1)\nvar^b = make(2)\nlet^x = if^true^: a el^: b;\n', copied)
    negative('derived_copied', DEVICE + 'noncopy^def^B { self^{ n:i32^ = 1 } }\ndef^S : B {}\nvar^s = S{}\nvar^t = s\n', copied)
    negative('generic_copied', DEVICE + 'def^Box<T> { self^{ item:T } }\nvar^b = Box<D>{item = make(1)}\nvar^o = b\n', copied)
    negative('with_name', DEVICE + 'var^a = make(1)\nwith^b = a {}\n', copied)

    by_value = "cannot be passed by value"
    negative('value_parameter', DEVICE + 'let^g = p^d:D { print^(d.h) }\ng(make(1))\n', by_value)
    negative('parameter_not_called', DEVICE + 'let^g = p^d:D { print^(d.h) }\n', by_value)
    negative('hash_map_value', DEVICE + 'var^m:c.HashMap<string^, D> = {}\nm.set("a", make(1))\n', by_value)
    negative('hash_map_declared', DEVICE + 'var^m:c.HashMap<string^, D> = {}\n', copied)
    negative('template_argument', DEVICE + 'let^g = template^p^d { print^(d.h) }\ng(make(1))\n', by_value)
    negative('external_argument', DEVICE + 'externC^g:p^d:D;\ng(make(1))\n', by_value)

    negative('slice_element_written', DEVICE + 'var^xs = D[grow^]{ make(1) }\nlet^view = xs[..]\nview[0].close()\n', 'read')
    negative('repeat', DEVICE + 'var^xs = D[grow^]{ make(1) repeat^2 }\n', 'would copy noncopy^')
    negative('fixed_repeat', DEVICE + 'var^xs:D[2] = { make(1) repeat^2 }\n', 'would copy noncopy^')
    negative('source', DEVICE + 'var^xs = D[grow^]{ make(1) }\nvar^ys = D[grow^]{ xs... }\n', 'would copy noncopy^')
    negative('append', DEVICE + 'var^xs = D[grow^]{ make(1) }\nvar^ys = D[grow^]{}\nys.append(xs[..])\n', 'would copy noncopy^')
    negative('only_def', 'noncopy^let^x = 1\n', 'noncopy^ applies only to a def^')
    negative('outlives_owner', DEVICE + '''
var^map:c.HashMap<string^, ref^D> = {}
do^{
    var^files = D[grow^]{ make(1) }
    map.set("a", ref^files[0])
}
''', 'reference would outlive its owner')

    suite.finish()


if __name__ == '__main__':
    main()
