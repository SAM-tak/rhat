"""std.collections and std.hash: HashMap, HashSet and interned string keys."""

from harness import Suite

PRELUDE = '''
let^c = import^std.collections
let^hash = import^std.hash
import^std.text
'''

MODEL = '''
let^RANGE:i64^ = KEYS
var^m:c.HashMap<i64^, i64^> = {}
var^model:i64^[KEYS] = {-1 repeat^ KEYS}
var^state:u64^ = 88172645463325252
var^count:i64^ = 0
var^peak:i64^ = 0
let^STEPS:i64^ = 200000
for^step in^0 .. STEPS {
    state := state bitxor^ (state bitshift^ 13)
    state := state bitxor^ (state bitshift^ -7)
    state := state bitxor^ (state bitshift^ 17)
    # The live key range swells and shrinks, so the table grows and empties repeatedly.
    let^filling = (step / 20000) % 2 = 0
    let^key = i64^(state bitshift^ -40) % RANGE
    let^add = ((state bitand^ 3) != 0) = filling
    if^add {
        if^model[key] < 0 { count += 1 }
        model[key] := step
        m.set(key, step)
    else^:
        let^removed = m.remove(key)
        assert^(removed = (model[key] >= 0))
        if^removed { count -= 1 }
        model[key] := -1
    }
    assert^(m.len() = count)
    if^count > peak { peak := count }
    if^step % 1024 = 0 {
        for^k in^0 .. RANGE {
            assert^((m.get(k) ?? -1) = model[k])
            assert^(m.has(k) = (model[k] >= 0))
        }
        let^items = m.items()
        for^i in^0 .. items.len { assert^(model[items[i].key] = items[i].value) }
    }
}
print^(peak * 2 > RANGE)
'''


def main():
    suite = Suite('collections')
    positive, negative = suite.positive, suite.negative

    positive('map_integers', PRELUDE + '''
var^m:c.HashMap<i32^, i32^> = {}
print^(m.len(), m.has(1), m.get(1) ?? -1, m.remove(1))
for^i in^0 .. 1000 { m.set(i, i * 2) }
m.set(7, 70)
print^(m.len(), m.get(7) ?? -1, m.get(999) ?? -1, m.get(1000) ?? -1, m.has(999), m.has(1000))
m.clear()
print^(m.len(), m.has(7))
m.set(7, 1)
print^(m.len(), m.get(7) ?? -1)
''', '0 false -1 false\n1000 70 1998 -1 true false\n0 false\n1 1\n')

    # Without growth the buckets stay few and this takes far too long to finish.
    positive('map_large', PRELUDE + '''
let^N:i64^ = 500000
var^m:c.HashMap<i64^, i64^> = {}
for^i in^0 .. N { m.set(i * 7, i) }
var^sum:i64^ = 0
for^i in^0 .. N { sum += m.get(i * 7) ?? 0 }
print^(m.len(), sum)
''', '500000 124999750000\n')

    positive('map_key_types', PRELUDE + '''
var^a:c.HashMap<u8^, i32^> = {}
var^b:c.HashMap<i64^, i32^> = {}
var^d:c.HashMap<char^, i32^> = {}
var^e:c.HashMap<bool^, i32^> = {}
a.set(u8^(255), 1)
b.set(-1, 2)
d.set(char^"あ", 3)
e.set(true^, 4)
e.set(false^, 5)
print^(a.get(u8^(255)) ?? 0, b.get(-1) ?? 0, d.get(char^"あ") ?? 0, e.get(true^) ?? 0, e.get(false^) ?? 0)
''', '1 2 3 4 5\n')

    positive('map_user_key', PRELUDE + '''
def^Point {
    self^{ x:i32^, y:i32^ }
    op^= = f^self^, other:Point -> bool^ { self^.x = other.x and^ self^.y = other.y }
    hash = f^self^ -> u64^ { hash.combine(hash.of(self^.x), hash.of(self^.y)) }
}
var^m:c.HashMap<Point, string^> = {}
m.set(Point{x = 1, y = 2}, "a")
m.set(Point{x = 2, y = 1}, "b")
print^(m.get(Point{x = 1, y = 2}) ?? "?", m.get(Point{x = 2, y = 1}) ?? "?", m.has(Point{x = 3, y = 3}))
''', 'a b false\n')

    positive('map_string_views', PRELUDE + '''
var^text = std.text.Builder{}
do^{
    var^pool = std.text.Builder{}
    var^m:c.HashMap<string^, i32^> = {}
    m.set(text.add("outer"), 1)
    m.set(pool.add("inner"), 2)
    m.set("literal", 3)
    print^(m.get("outer") ?? 0, m.get("inner") ?? 0, m.get("literal") ?? 0)
    print^(m.remove("outer"), m.len(), m.get("inner") ?? 0, m.get("literal") ?? 0)
}
''', '1 2 3\ntrue 2 2 3\n')

    positive('map_lookup_with_a_temporary_key', PRELUDE + '''
let^find = p^m:ref^ c.HashMap<string^, string^>, a:string^, b:string^ -> string^ | nil^ {
    var^key = std.text.Builder{}
    key.append(a)
    key.append(b)
    return^m.get(key.text())
}
var^m:c.HashMap<string^, string^> = {}
m.set("ab", "found")
print^(find(ref^ m, "a", "b") ?? "none", find(ref^ m, "x", "y") ?? "none")
''', 'found none\n')

    positive('map_reference_survives_growth', PRELUDE + '''
var^m:c.HashMap<i32^, i32^> = {}
m.set(0, 10)
let^value = m.get_mut(0)
if^value fits^ ref^ i32^ {
    for^i in^1 .. 1000 { m.set(i, i) }
    *value := 11
}
print^(m.get(0) ?? -1, m.get_mut(5000) fits^ nil^)
''', '11 true\n')

    positive('map_items_in_insertion_order', PRELUDE + '''
var^m:c.HashMap<string^, i32^> = {}
m.set("a", 1)
m.set("b", 2)
m.set("c", 3)
let^items = m.items()
for^i in^0 .. items.len { print^(items[i].key, items[i].value) }
''', 'a 1\nb 2\nc 3\n')

    # The live key count swells and shrinks, crossing bucket boundaries up and down:
    # the small range reaches the first few, including an empty table.
    for keys in (4, 512):
        positive(f'map_matches_a_model_{keys}', PRELUDE + MODEL.replace('KEYS', str(keys)), 'true\n')

    positive('set_operations', PRELUDE + '''
var^s:c.HashSet<i32^> = {}
print^(s.add(1), s.add(1), s.add(2), s.len(), s.has(1), s.has(3))
print^(s.remove(1), s.remove(1), s.len(), s.has(1))
let^items = s.items()
print^(items.len, items[0].key)
s.clear()
print^(s.len())
''', 'true false true 2 true false\ntrue false 1 false\n1 2\n0\n')

    positive('intern_keeps_one_copy', PRELUDE + '''
var^pool = std.text.Builder{}
var^keys:c.HashSet<string^> = {}
var^counts:c.HashMap<string^, i32^> = {}
let^words = {"the", "cat", "the", "dog", "the", "cat"}
for^i in^0 .. words.len {
    let^word = c.intern(ref^ keys, ref^ pool, words[i])
    counts.set(word, (counts.get(word) ?? 0) + 1)
}
print^(pool.text(), keys.len(), counts.len())
print^(counts.get("the") ?? 0, counts.get("cat") ?? 0, counts.get("dog") ?? 0)
''', 'thecatdog 3 3\n3 2 1\n')

    positive('builder_add_views_the_added_part', PRELUDE + '''
var^b = std.text.Builder{}
let^one = b.add("あ")
let^two = b.add("bc")
print^(one, two, b.text(), one.bytes.len)
''', 'あ bc あbc 3\n')

    negative('map_key_outlives_its_text', PRELUDE + '''
var^m:c.HashMap<string^, i32^> = {}
do^{
    var^b = std.text.Builder{}
    m.set(b.add("x"), 1)
}
''', 'reference would outlive its owner')
    negative('map_value_outlives_its_text', PRELUDE + '''
var^m:c.HashMap<i32^, string^> = {}
do^{
    var^b = std.text.Builder{}
    m.set(1, b.add("x"))
}
''', 'reference would outlive its owner')
    negative('intern_key_outlives_its_pool', PRELUDE + '''
var^keys:c.HashSet<string^> = {}
do^{
    var^pool = std.text.Builder{}
    c.intern(ref^ keys, ref^ pool, "x")
}
''', 'reference would outlive its owner')
    negative('float_keys_have_no_hash', PRELUDE + '''
var^m:c.HashMap<f64^, i32^> = {}
m.set(1.0, 1)
''', 'hash')
    negative('key_without_equality', PRELUDE + '''
def^K {
    self^{ n:i32^ }
    hash = f^self^ -> u64^ { hash.of(self^.n) }
}
var^m:c.HashMap<K, i32^> = {}
m.set(K{n = 1}, 1)
''', "operator '=' has no matching overload")

    suite.finish()


if __name__ == '__main__':
    main()
