"""Writable slices, and read^ slices under the rules of ref^ read^."""

from harness import Suite


def main():
    suite = Suite('slices')
    positive, negative = suite.positive, suite.negative

    positive('write_through_a_parameter', '''
let^fill = p^xs:i32^[..], n:i32^ {
    for^i in^0 .. xs.len { xs[i] := n }
}
var^a = i32^[grow^]{1, 2, 3}
fill(a[1 .. 3], 9)
print^(a[0], a[1], a[2])
var^f:i32^[2] = {1, 2}
fill(f[..], 7)
print^(f[0], f[1])
var^m = i32^[max^4]{1, 2}
fill(m[..], 5)
print^(m[0], m[1], m.len)
var^o = i32^[]{0 repeat^2}
fill(o[..], 3)
print^(o[0], o[1])
''', '1 9 9\n7 7\n5 5 2\n3 3\n')

    positive('handles_and_elements', '''
def^P {
    self^{ n:i32^ = 0 }
    add = p^self^ { self^.n += 1 }
}
var^a = P[grow^]{ P{}, P{} }
let^w = a[..]
w[0].add()
w[0].n += 5
w[1] := P{n = 9}
print^(a[0].n, a[1].n)
let^r = ref^w[1]
r.add()
print^(a[1].n)
var^v = a[.. 1]
v := a[1 ..]
v[0].n := 100
print^(a[0].n, a[1].n)
let^again = w[1 ..]
again[0].n := 7
print^(a[1].n)
''', '6 9\n10\n6 100\n7\n')

    positive('swap_and_sort', '''
let^sort = p^xs:i32^[..] {
    for^i in^1 .. xs.len {
        var^j = i
        repeat^while^j > 0 and^ xs[j - 1] > xs[j] {
            let^t = xs[j]
            xs[j] := xs[j - 1]
            xs[j - 1] := t
            j -= 1
        }
    }
}
var^a = i32^[grow^]{3, 1, 2}
sort(a[..])
print^(a[0], a[1], a[2])
''', '1 2 3\n')

    positive('writable_slices_may_overlap', '''
let^both = p^a:i32^[..], b:i32^[..] {
    a[0] := 1
    b[0] := 2
}
var^x = i32^[grow^]{0, 0, 0, 0}
both(x[.. 2], x[2 ..])
print^(x[0], x[2])
both(x[..], x[..])
print^(x[0])
''', '1 2\n2\n')

    positive('read_sources', '''
let^total = f^xs:i32^[..] -> i32^ {
    var^sum = 0
    for^x in^xs { sum += x }
    return^sum
}
let^shown = p^xs:read^i32^[..] -> i32^ { return^xs[0] }
let^length = p^s:read^u8^[..] -> i64^ { return^s.len }
let^fixed = i32^[grow^]{1, 2}
var^own = i32^[grow^]{3, 4}
print^(total(fixed[..]), total(own[..]), shown(fixed[..]), shown(own[..]))
print^(length("abc".bytes), length(fixed.bytes), length(own.bytes))
own.push(5)
print^(total(own[..]))
let^kept = fixed[..]
print^(total(kept), shown(kept), kept[1])
let^part = kept[1 ..]
print^(part[0])
''', '3 7 1 3\n3 8 8\n12\n3 1 2\n2\n')

    positive('let_of_a_var_is_unprotected', '''
var^a = i32^[grow^]{1}
let^w = a[..]
a.push(2)
print^(w.len, a.len)
''', '1 2\n')

    positive('read_borrow_ends_with_its_scope', '''
var^a = i32^[grow^]{1}
do^{
    let^r:read^i32^[..] = a[..]
    print^(r[0])
}
a.push(2)
let^shown = p^xs:read^i32^[..] -> i64^ { return^xs.len }
print^(shown(a[..]))
a.push(3)
print^(shown(a[..]))
''', '1\n2\n3\n')

    positive('fields_results_and_unions', '''
def^View { self^{ data:read^i32^[..] } }
def^Edit { self^{ data:i32^[..] } }
let^first = f^xs:i32^[..] -> read^i32^[..] { xs[.. 1] }
let^tail = p^xs:i32^[..] -> i32^[..] { return^xs[1 ..] }
let^maybe = f^xs:i32^[..] | nil^ -> i64^ {
    if^xs fits^nil^ { return^-1 }
    return^xs.len
}
var^a = i32^[grow^]{1, 2, 3}
do^{
    let^v = View{data = a[..]}
    print^(v.data[2], first(v.data).len)
}
var^e = Edit{data = a[..]}
e.data[0] := 8
print^(a[0])
tail(a[..])[0] := 7
print^(a[1])
print^(maybe(a[..]), maybe(nil^))
''', '3 1\n8\n7\n3 -1\n')

    positive('references_in_slices', '''
def^T { self^{ n:i32^ = 0 } }
let^point = p^xs:(ref^T)[..], r:ref^T { xs[0] := r }
var^a = T{n = 1}
var^b = T{n = 2}
var^refs = T[ref^grow^]{ ref^a }
point(refs[..], ref^b)
print^(refs[0].n)
''', '2\n')

    positive('variadic_is_read', '''
let^first = p^...:i32^ -> i32^ { return^...[0] }
print^(first(4, 5))
var^a = i32^[grow^]{6}
print^(first(a...))
''', '4\n6\n')

    positive('method_and_callable_types', '''
def^Sink {
    abstract^take:p^self^, data:read^u8^[..] -> i64^;
}
def^Count : Sink {
    take = p^self^, data:read^u8^[..] -> i64^ { return^data.len }
}
let^use = p^s:ref^dyn^Sink, data:read^u8^[..] -> i64^ { return^s.take(data) }
var^c = Count{}
print^(use(ref^c, "abc".bytes))
let^apply = f^g:f^i32^[..] -> i32^; -> i32^ {
    let^xs = i32^[]{1, 2}
    return^g(xs[..])
}
print^(apply(f^xs:i32^[..] -> i32^ { xs[1] }))
''', '3\n2\n')

    positive('disposable_elements', '''
import^std.sys

def^D : std.sys.Disposable {
    self^{ h:i64^ }
    set = p^self^, n:i64^ { self^.h := n }
    dispose = p^self^ { print^("dispose", self^.h) }
}
let^replace = p^xs:D[..] {
    xs[0] := D{h = 9}
    xs[1].set(8)
}
var^a = D[grow^]{ D{h = 1}, D{h = 2} }
replace(a[..])
print^(a[0].h, a[1].h)
''', 'dispose 1\n9 8\ndispose 8\ndispose 9\n')

    positive('loops', '''
var^rows:i32^[[2] 2] = { {1, 2}, {3, 4} }
for^row in^rows[..] {
    let^view = row[..]
    print^(view[1])
}
for^i in^0 .. rows.len {
    let^view = rows[i][..]
    view[0] := 0
}
print^(rows[0][0], rows[1][0])
''', '2\n4\n0 0\n')

    positive('top_level_bindings', '''
let^fixed = i32^[grow^]{1, 2}
var^own = i32^[grow^]{3, 4}
let^read_fixed = p^ -> i32^ {
    let^view = fixed[..]
    return^view[0]
}
let^write_own = p^ {
    let^view = own[..]
    view[0] := 9
}
write_own()
print^(read_fixed(), own[0])
''', '1 9\n')

    positive('containers', '''
let^c = import^std.collections
var^m:c.HashMap<string^, i32^> = {}
m.set("a", 1)
do^{
    let^items = m.items()
    print^(items.len, items[0].value)
}
m.set("b", 2)
print^(m.len())
var^total = 0
for^k, v in^m { total += v }
print^(total)
def^T { self^{ n:i32^ = 5 } }
var^t = T{}
var^refs:c.HashMap<string^, ref^T> = {}
refs.set("t", ref^t)
for^k, v in^refs { print^(k, v.n) }
''', '1 1\n2\n3\nt 5\n')

    written = 'cannot modify through a read^ reference'
    negative('write_read_parameter', 'let^g = p^xs:read^i32^[..] { xs[0] := 1 }\nvar^a = i32^[grow^]{1}\ng(a[..])\n', written)
    negative('write_read_local', 'var^a = i32^[grow^]{1}\nlet^r:read^i32^[..] = a[..]\nr[0] := 1\n', written)
    negative('write_let_array_view', 'let^a = i32^[grow^]{1}\nlet^s = a[..]\ns[0] := 1\n', written)
    negative('write_bytes', 'let^s = "abc".bytes\ns[0] := 1\n', written)
    negative('write_array_bytes', 'var^a = i32^[grow^]{1}\nlet^s = a.bytes\ns[0] := 1\n', written)
    negative('write_read_method', 'def^P {\n self^{ n:i32^ = 0 }\n add = p^self^ { self^.n += 1 }\n}\nlet^g = p^xs:read^P[..] { xs[0].add() }\nvar^a = P[grow^]{ P{} }\ng(a[..])\n', written)
    negative('write_read_field', 'def^P { self^{ n:i32^ = 0 } }\nlet^g = p^xs:read^P[..] { xs[0].n := 1 }\nvar^a = P[grow^]{ P{} }\ng(a[..])\n', written)
    negative('write_map_items', 'let^c = import^std.collections\nvar^m:c.HashMap<string^, i32^> = {}\nm.set("a", 1)\nlet^items = m.items()\nitems[0].value := 2\n', written)
    negative('write_in_f', 'let^g = f^xs:i32^[..] -> i32^ {\n xs[0] := 1\n return^0\n}\nvar^a = i32^[grow^]{1}\nprint^(g(a[..]))\n', written)
    negative('write_variadic', 'let^g = p^...:i32^ { ...[0] := 1 }\ng(1)\n', 'read')
    negative('write_read_top_level', 'let^fixed = i32^[grow^]{1}\nlet^g = p^ {\n let^view = fixed[..]\n view[0] := 1\n}\ng()\n', written)
    negative('writable_ref_through_read', 'def^T { self^{ n:i32^ = 0 } }\nlet^g = p^xs:read^(ref^T)[..] -> ref^T { return^xs[0] }\n', 'cannot copy writable references')

    needs_read = 'declare it read^ if it only reads'
    negative('literal_to_writable', 'let^g = p^xs:u8^[..] {}\ng("abc".bytes)\n', needs_read)
    negative('let_array_to_writable', 'let^g = p^xs:i32^[..] {}\nlet^a = i32^[grow^]{1}\ng(a[..])\n', 'its source is read-only')
    negative('read_to_writable', 'let^g = p^xs:i32^[..] {}\nlet^h = p^ys:read^i32^[..] { g(ys) }\nvar^a = i32^[grow^]{1}\nh(a[..])\n', needs_read)
    negative('read_result_to_writable', 'var^a = i32^[grow^]{1}\nlet^r:read^i32^[..] = a[..]\nvar^w:i32^[..] = r\n', 'read^')
    negative('let_array_to_writable_local', 'let^a = i32^[grow^]{1}\nvar^w:i32^[..] = a[..]\n', 'its source is read-only')
    negative('drop_read_in_result', 'let^g = f^xs:i32^[..] -> i32^[..] { xs }\n', 'read^')

    borrowed = 'cannot modify storage during a read^ borrow'
    negative('push_while_read', 'var^a = i32^[grow^]{1}\nlet^r:read^i32^[..] = a[..]\na.push(2)\nprint^(r[0])\n', borrowed)
    negative('element_while_read', 'var^a = i32^[grow^]{1}\nlet^r:read^i32^[..] = a[..]\na[0] := 2\nprint^(r[0])\n', borrowed)
    negative('write_view_while_read', 'var^a = i32^[grow^]{1}\nlet^w = a[..]\nlet^r:read^i32^[..] = a[..]\nw[0] := 2\nprint^(r[0])\n', borrowed)
    negative('map_set_while_items', 'let^c = import^std.collections\nvar^m:c.HashMap<string^, i32^> = {}\nlet^items = m.items()\nm.set("a", 1)\nprint^(items.len)\n', borrowed)
    negative('read_and_write_arguments', 'let^g = p^r:read^i32^[..], w:i32^[..] {}\nvar^a = i32^[grow^]{1, 2}\ng(a[..], a[..])\n', 'read^')
    negative('read_and_write_parts', 'let^g = p^r:read^i32^[..], w:i32^[..] {}\nvar^a = i32^[grow^]{1, 2}\ng(a[.. 1], a[1 ..])\n', 'read^')
    negative('read_and_mutating_receiver', 'def^B {\n self^{ xs:i32^[grow^] = {} }\n put = p^self^, data:read^i32^[..] {}\n}\nvar^b = B{}\nb.put(b.xs[..])\n', 'read^')

    negative('read_escapes', 'var^outer:read^i32^[..] | nil^ = nil^\nvar^a = i32^[grow^]{1}\ndo^{\n let^r:read^i32^[..] = a[..]\n outer := r\n}\n', 'cannot escape')
    negative('outlives_owner', 'var^outer:i32^[..] | nil^ = nil^\ndo^{\n var^a = i32^[grow^]{1}\n outer := a[..]\n}\n', 'would outlive its owner')
    negative('stored_reference_outlives', '''
def^T { self^{ n:i32^ = 0 } }
let^point = p^xs:(ref^T)[..], r:ref^T { xs[0] := r }
var^a = T{}
var^refs = T[ref^grow^]{ ref^a }
do^{
    var^b = T{}
    point(refs[..], ref^b)
}
''', 'outlive')
    negative('no_read_on_arrays', 'var^a:read^i32^[grow^] = {}\n', 'read^ here needs a slice')
    negative('no_read_on_plain', 'var^a:read^i32^ = 1\n', 'read^ here needs a slice')
    negative('no_push_through_a_slice', 'var^a = i32^[grow^]{1}\nlet^w = a[..]\nw.push(2)\n', 'push')
    negative('no_pop_through_a_slice', 'var^a = i32^[grow^]{1}\nlet^w = a[..]\nlet^x = w.pop()\n', 'pop')

    suite.finish()


if __name__ == '__main__':
    main()
