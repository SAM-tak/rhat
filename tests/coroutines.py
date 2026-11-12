"""Coroutines, stage 1a: yield^, frames and their start, resume, done and result."""
from harness import Suite

SHOW = '''
let^ show = p^ v:i32^ | nil^ {
    if^ v fits^ i32^ { print^(v) el^: print^("nil") }
}
'''


def main():
    suite = Suite('coroutines')
    positive, negative, runtime_failure = suite.positive, suite.negative, suite.runtime_failure

    def resume_function(rir, ll):
        assert 'frame.make' in rir and 'frame.resume' in rir and 'yield' in rir, rir
        assert 'ptr noalias %frame' in ll and 'switch i32 %tag' in ll, ll

    positive('generator_turns', '''
let^ count = p^ n:i32^ {
    var^ i:i32^ = 0
    repeat^ while^ i < n {
        yield^ i
        i += 1
    }
    return^ i * 10
}
var^ c = count(3)
print^(c.start())
print^(c.resume())
print^(c.resume())
print^(c.done())
print^(c.resume())
print^(c.done(), c.result())
''', '0\n1\n2\nfalse\n30\ntrue 30\n', inspect=resume_function)

    def locals_stay_local(rir, ll):
        body = ll[ll.index('; fib'):]
        body = body[:body.index('\n}\n')]
        assert 'alloca' in body and 'getelementptr i8, ptr %frame' in body, body

    positive('values_live_across_yields', '''
let^ fib = p^ n:i32^ {
    var^ a:i64^ = 0
    var^ b:i64^ = 1
    var^ k:i32^ = 0
    repeat^ while^ k < n {
        if^ k % 2 = 0 { yield^ a el^: yield^ a + 0 }
        let^ t = a + b
        a := b
        b := t
        k += 1
    }
}
var^ c = fib(7)
var^ v = c.start()
repeat^ while^ !c.done() {
    if^ v fits^ i64^ { print^(v) }
    v := c.resume()
}
''', '0\n1\n1\n2\n3\n5\n8\n', inspect=locals_stay_local)

    positive('generator_without_result', SHOW + '''
let^ two = p^ { yield^ 1
    yield^ 2 }
var^ c = two()
show(c.start())
show(c.resume())
show(c.resume())
print^(c.done())
''', '1\n2\nnil\ntrue\n')

    positive('pure_generator_in_pure_function', '''
let^ evens = f^ n:i32^ {
    var^ i:i32^ = 0
    repeat^ while^ i < n {
        yield^ i * 2
        i += 1
    }
}
let^ total = f^ n:i32^ -> i32^ {
    var^ c = evens(n)
    var^ s:i32^ = 0
    var^ v = c.start()
    repeat^ while^ !c.done() {
        if^ v fits^ i32^ { s += v }
        v := c.resume()
    }
    return^ s
}
print^(total(4))
''', '12\n')

    positive('method_generator_and_fork', SHOW + '''
def^ Box {
    self^{ n:i32^ = 3 }
    walk = f^self^ {
        var^ i:i32^ = 0
        repeat^ while^ i < self^.n {
            yield^ i
            i += 1
        }
    }
}
var^ box = Box{}
var^ w = box.walk()
show(w.start())
var^ fork = w
show(w.resume())
show(w.resume())
show(fork.resume())
print^(w.done(), fork.done())
''', '0\n1\n2\n1\nfalse false\n')

    positive('borrowed_argument', '''
let^ each = p^ xs:i32^[..] {
    for^ x in^ xs { yield^ x }
}
var^ data = i32^[grow^]{4, 5}
var^ c = each(data[..])
let^ a = c.start()
let^ b = c.resume()
if^ a fits^ i32^ { if^ b fits^ i32^ { print^(a + b) } }
''', '9\n')

    positive('owned_scratch_ends_before_yield', '''
let^ sums = p^ {
    var^ total:i64^ = 0
    do^{
        var^ xs = i64^[grow^]{1, 2, 3}
        for^ x in^ xs { total += x }
    }
    yield^ total
}
var^ c = sums()
let^ v = c.start()
if^ v fits^ i64^ { print^(v) }
''', '6\n')

    positive('pure_function_makes_procedure_frame', '''
let^ g = p^ { yield^ 1 }
let^ make = f^ -> i32^ {
    var^ c = g()
    return^ 0
}
print^(make())
''', '0\n')

    positive('maker_is_a_function', '''
let^ py = p^ x:i32^ {
    yield^
    return^ "done"
}
let^ run = p^ {
    var^ c = py(1)
    print^(typeof^(py).signature^)
    print^(typeof^(c.start()).signature^)
}
run()
''', 'f^ i32^ -> py.ReturnType^;\nstring^ | nil^\n')

    positive('frame_named_by_overload', '''
let^ each = p^ x {
    yield^ x
}
let^ run = p^ {
    var^ c = each(3)
    print^(typeof^(c).signature^)
}
run()
''', 'overload^each(i32^).ReturnType^\n')

    positive('received_values', '''
let^ acc = p^ {
    var^ total:i32^ = 0
    repeat^ while^ total < 10 {
        var^ step:i32^ = yield^ total
        total += step
    }
    return^ total
}
var^ c = acc()
print^(c.start())
print^(c.resume(3))
print^(c.resume(4))
print^(c.resume(5))
print^(c.done(), c.result())
''', '0\n3\n7\n12\ntrue 12\n')
    negative('resume_needs_the_received_value', '''
let^ acc = p^ { var^ step:i32^ = yield^ 0 }
var^ c = acc()
let^ a = c.start()
let^ b = c.resume()
''', "this coroutine receives 'i32^'; resume it with one value")
    negative('resume_hands_nothing_in', '''
let^ g = p^ { yield^ 1 }
var^ c = g()
let^ a = c.start()
let^ b = c.resume(2)
''', "'resume' takes no arguments")
    negative('received_types_disagree', '''
let^ g = p^ {
    var^ a:i32^ = yield^ 0
    var^ b:i64^ = yield^ 1
}
var^ c = g()
''', "yield^ receives 'i64^' here but 'i32^' at another yield^")
    negative('start_hands_nothing_in', '''
let^ acc = p^ { var^ step:i32^ = yield^ 0 }
var^ c = acc()
let^ a = c.start(1)
''', "'start' takes no arguments")

    base = '''import^std.sys
let^ count = p^ n:i32^ -> std.sys.CoroutineNR<i32^, nil^> {
    var^ i:i32^ = 0
    repeat^ while^ i < n {
        yield^ i
        i += 1
    }
}
let^ twice = f^ n:i32^ {
    yield^ n
    yield^ n * 2
}
'''

    def frame_tables(rir, ll):
        assert ll.count('linkonce_odr constant [4 x ptr]') == 2, ll

    positive('base_reference_specializes', base + '''
let^ drive = p^ co:ref^std.sys.CoroutineNR<i32^, nil^> -> i32^ {
    var^ s:i32^ = 0
    var^ v = co.start()
    repeat^ while^ !co.done() {
        if^ v fits^ i32^ { s += v }
        v := co.resume()
    }
    return^ s
}
var^ a = count(4)
var^ b = twice(5)
print^(drive(ref^a), drive(ref^b))
''', '6 15\n')

    positive('base_dynamic_mixes_kinds', base + '''
var^ c = count(3)
var^ d = twice(7)
var^ all:std.sys.CoroutineNR<i32^, nil^>[dynamic^2] = {ref^c, ref^d}
for^ co in^ all {
    let^ first = co.start()
    if^ first fits^ i32^ { print^(first) }
}
let^ again = all[1].resume()
if^ again fits^ i32^ { print^(again, all[1].done()) }
''', '0\n7\n14 false\n', inspect=frame_tables)

    positive('base_dynamic_hands_values_in', '''import^std.sys
let^ acc = p^ -> std.sys.CoroutineR<i32^, i32^, i32^> {
    var^ total:i32^ = 0
    repeat^ while^ total < 10 {
        var^ step = yield^ total
        total += step
    }
    return^ total
}
var^ c = acc()
let^ d:dynamic^std.sys.CoroutineR<i32^, i32^, i32^> = ref^c
print^(d.start(), d.resume(4), d.resume(8), d.done(), d.result())
''', '0 4 12 true 12\n')

    negative('base_arguments_must_match', base + '''
let^ drive = p^ co:ref^std.sys.CoroutineNR<i64^, nil^> { let^ v = co.start() }
var^ a = count(4)
drive(ref^a)
''', "count.ReturnType^")
    negative('written_coroutine_must_match', '''import^std.sys
let^ g = p^ -> std.sys.CoroutineNR<i32^, nil^> { yield^ "a" }
var^ c = g()
''', "the coroutine this body makes is not the written 'std.sys.CoroutineNR<i32^,nil^>'")
    negative('base_resume_needs_value', '''import^std.sys
let^ acc = p^ -> std.sys.CoroutineR<i32^, i32^, nil^> { var^ step = yield^ 0 }
var^ c = acc()
let^ d:dynamic^std.sys.CoroutineR<i32^, i32^, nil^> = ref^c
let^ a = d.start()
let^ b = d.resume()
''', "'resume' expects")

    positive('pure_base_takes_pure_frames', base + '''
let^ drive = p^ co:ref^std.sys.PureCoroutineNR<i32^, nil^> -> i32^ {
    let^ v = co.start()
    if^ v fits^ i32^ { return^ v }
    return^ 0
}
var^ b = twice(5)
print^(drive(ref^b))
''', '5\n')
    negative('pure_base_refuses_procedure_frames', base + '''
let^ drive = p^ co:ref^std.sys.PureCoroutineNR<i32^, nil^> { let^ v = co.start() }
var^ a = count(4)
drive(ref^a)
''', "count.ReturnType^")
    positive('common_base_mixes_receiving', base + '''
let^ acc = p^ { var^ step:i32^ = yield^ 0 }
var^ a = count(1)
var^ r = acc()
var^ all:std.sys.Coroutine<nil^>[dynamic^2] = {ref^a, ref^r}
let^ x = a.start()
let^ y = a.resume()
print^(all[0].done(), all[1].done())
''', 'true false\n')

    positive('await_delegates', '''
let^ inner = p^ n:i32^ {
    yield^ n
    yield^ n + 1
    return^ n * 100
}
let^ outer = p^ {
    let^ a = await^ inner(1)
    yield^ a
    let^ b = await^ inner(5)
    return^ a + b
}
var^ c = outer()
var^ v = c.start()
repeat^ while^ !c.done() {
    print^(v)
    v := c.resume()
}
print^(c.result())
''', '1\n2\n100\n5\n6\n600\n')
    positive('await_passes_values_in', '''
let^ add = p^ {
    var^ total:i32^ = 0
    repeat^ while^ total < 10 {
        var^ step:i32^ = yield^ total
        total += step
    }
    return^ total
}
let^ outer = p^ {
    let^ t = await^ add()
    return^ t + 1
}
var^ c = outer()
print^(c.start(), c.resume(4), c.resume(7), c.done(), c.result())
''', '0 4 12 true 12\n')
    positive('await_two_levels', '''
let^ leaf = f^ n:i32^ { yield^ n
    return^ n }
let^ mid = f^ n:i32^ { return^ await^ leaf(n) + await^ leaf(n + 1) }
let^ top = f^ { return^ await^ mid(3) }
let^ sum = f^ -> i32^ {
    var^ c = top()
    var^ s:i32^ = 0
    var^ v = c.start()
    repeat^ while^ !c.done() {
        s += v
        v := c.resume()
    }
    return^ s + c.result()
}
print^(sum())
''', '14\n')
    negative('await_procedure_from_function', '''
let^ inner = p^ { yield^ 1 }
let^ outer = f^ { await^ inner() }
var^ c = outer()
''', "f^ cannot advance a p^ coroutine; await^ runs its body")
    negative('await_needs_coroutine', '''
let^ plain = f^ -> i32^ { return^ 1 }
let^ outer = p^ { let^ v = await^ plain() }
var^ c = outer()
''', "await^ needs a coroutine frame, found 'i32^'")
    positive('await_relends_locals', '''
let^ fill = p^ into:i64^[..], start:i64^ {
    var^ k:i64^ = 0
    repeat^ while^ k < into.len {
        yield^ k
        into[k] := start + k
        k += 1
    }
    return^ into.len
}
let^ peek = p^ x:ref^i32^ { yield^ i64^(*x)  return^ *x + 1 }
let^ outer = p^ base:i64^ {
    var^ buf = i64^[3]{0, 0, 0}
    let^ n = await^ fill(buf[..], base)
    var^ m:i32^ = 4
    let^ q = await^ peek(ref^m)
    return^ buf[0] + buf[1] + buf[2] + n + i64^(q)
}
let^ run = p^ {
    var^ pool:outer.ReturnType^[max^4] = {}
    var^ a = outer(10)
    let^ s = a.start()
    pool.push(a)
    pool.push(outer(100))
    let^ t = pool[1].start()
    let^ u = pool[0].resume()
    var^ b = pool[0]
    repeat^ while^ !b.done() { let^ w = b.resume() }
    repeat^ while^ !pool[0].done() { let^ w = pool[0].resume() }
    repeat^ while^ !pool[1].done() { let^ w = pool[1].resume() }
    print^(b.result(), pool[0].result(), pool[1].result())
}
run()
''', '41 41 311\n')
    for name, text in [
        ('await_lend_derived', '''
let^ keep = p^ into:i64^[..] {
    let^ part = into[0 .. 1]
    yield^ 0
    return^ part.len
}
let^ outer = p^ { var^ buf = i64^[3]{0, 0, 0}  let^ n = await^ keep(buf[..])  return^ n }
var^ o = outer()
'''),
        ('await_lend_struct', '''
def^ Hold { self^{ r:ref^ i64^ } }
let^ h = p^ x:Hold { yield^ 0  return^ 1 }
let^ outer = p^ { var^ v:i64^ = 1  let^ n = await^ h(Hold{r = ref^ v})  return^ n }
var^ o = outer()
'''),
        ('await_lend_passed_on', '''
let^ inner = p^ into:i64^[..] { yield^ 0  into[0] := 5  return^ i64^(1) }
let^ mid = p^ into:i64^[..] { let^ n = await^ inner(into)  return^ n }
let^ outer = p^ { var^ buf = i64^[3]{0, 0, 0}  let^ n = await^ mid(buf[..])  return^ buf[0] + n }
var^ o = outer()
'''),
    ]:
        negative(name, text, 'needs it to keep only its ref^ and slice parameters across suspensions')

    walkers = '''let^ count = p^ n:i32^ {
    var^ i:i32^ = 0
    repeat^ while^ i < n {
        yield^ i
        i += 1
    }
    return^ "end"
}
'''
    positive('for_walks_a_temporary_frame', walkers + '''
for^ x in^ count(3) { print^(x) }
''', '0\n1\n2\n')
    positive('for_advances_a_frame_variable', walkers + '''
var^ c = count(5)
for^ x in^ c { if^ x = 1 { break^ } }
let^ rest = c.resume()
if^ rest fits^ i32^ { print^("rest", rest) }
''', 'rest 2\n')
    positive('for_walks_a_yielding_iter', '''
def^ Box {
    self^{ n:i32^ = 3 }
    iter = f^self^ {
        var^ i:i32^ = 0
        repeat^ while^ i < self^.n {
            yield^ i * 10
            i += 1
        }
    }
}
var^ box = Box{}
for^ v in^ box { print^(v) }
''', '0\n10\n20\n')
    positive('for_does_not_stop_at_nil', '''
let^ maybe = p^ { yield^ nil^
    yield^ nil^ }
var^ k:i32^ = 0
for^ z in^ maybe() { k += 1 }
print^(k)
''', '2\n')
    negative('for_cannot_hand_values_in', '''
let^ acc = p^ { var^ step:i32^ = yield^ 0 }
for^ x in^ acc() { print^(x) }
''', "for^ cannot hand values in to a coroutine that receives 'i32^'")

    res = '''import^std.sys
def^ Res : std.sys.Disposable {
    self^{ name:i32^ = 0 }
    dispose = p^self^ { print^("dispose", self^.name) }
}
'''
    positive('dispose_what_is_live_where_dropped', res + '''
let^ g = p^ {
    var^ a = Res{name = 1}
    yield^ 10
    do^{
        var^ b = Res{name = 2}
        yield^ 20
    }
    yield^ 30
}
let^ run = p^ n:i32^ {
    var^ c = g()
    var^ v = c.start()
    var^ k:i32^ = 1
    repeat^ while^ k < n {
        v := c.resume()
        k += 1
    }
    if^ v fits^ i32^ { print^("drop at", v) el^: print^("drop at end") }
}
run(1)
run(2)
run(3)
run(4)
''', 'drop at 10\ndispose 1\ndrop at 20\ndispose 2\ndispose 1\ndispose 2\ndrop at 30\ndispose 1\n'
       'dispose 2\ndispose 1\ndrop at end\n')
    negative('disposable_frame_cannot_be_copied', res + '''
let^ g = p^ {
    var^ a = Res{name = 1}
    yield^ 10
}
let^ run = p^ {
    var^ c = g()
    let^ v = c.start()
    var^ copy = c
}
run()
''', 'Disposable')
    positive('dispose_frame_awaited_when_dropped', res + '''
let^ inner = p^ {
    var^ a = Res{name = 7}
    yield^ 1
    yield^ 2
}
let^ outer = p^ {
    await^ inner()
}
let^ run = p^ {
    var^ c = outer()
    let^ v = c.start()
    print^("drop")
}
run()
''', 'drop\ndispose 7\n')

    counter = '''def^ Counter {
    self^{ n:i32^ = 3 }
    drain = f^mutable^self^ {
        repeat^ while^ self^.n > 0 {
            self^.n -= 1
            yield^ self^.n
        }
    }
}
'''
    negative('mutable_self_frame_on_borrowed_storage', counter + '''
let^ first = f^ fr -> i32^ {
    var^ g = fr
    let^ v = g.start()
    if^ v fits^ i32^ { return^ v }
    return^ 0
}
var^ c = Counter{}
print^(first(c.drain()), c.n)
''', 'f^ cannot advance the coroutine of a mutable^self^ method on borrowed storage')
    positive('mutable_self_frame_on_local_storage', counter + '''
let^ local = f^ -> i32^ {
    var^ c = Counter{}
    var^ s:i32^ = 0
    for^ v in^ c.drain() { s += v }
    return^ s + c.n
}
print^(local())
''', '3\n')

    big = '''let^ big = p^ {
    var^ buf:u8^[8192] = {}
    yield^ 1
    print^(buf[0])
}
var^ c = big()
let^ v = c.start()
'''

    def large_frame_warns():
        path = suite.source('large_frame_warns', big)
        warned = suite.run([suite.compiler, 'check', path])
        assert "coroutine 'big' keeps a 8208-byte frame" in warned.stderr, warned.stderr
        quiet = suite.run([suite.compiler, 'check', path, '--frame-warning', '100000'])
        assert 'warning:' not in quiet.stderr, quiet.stderr
    suite.case('large_frame_warns', large_frame_warns)

    positive('union_parts_of_the_turn', '''
let^ show = p^ v:i32^ | nil^ { if^ v fits^ i32^ { print^(v) el^: print^("nil") } }
let^ g = p^ n:i32^ {
    yield^ "a"
    if^ n > 0 { return^ n }
    return^ nil^
}
let^ outer = p^ n:i32^ {
    let^ r = await^ g(n)
    show(r)
    return^ 0
}
let^ h = p^ {
    yield^ "x"
    yield^ nil^
    return^ 5
}
let^ run = p^ {
    var^ c = g(3)
    let^ a = c.start()
    let^ b = c.resume()
    show(c.result())
    var^ d = g(0)
    let^ x = d.start()
    let^ y = d.resume()
    show(d.result())
    var^ o = outer(7)
    let^ s = o.start()
    let^ t = o.resume()
    for^ v in^ h() {
        if^ v fits^ string^ { print^(v) el^: print^("nil") }
    }
}
run()
''', '3\nnil\n7\nx\nnil\n')

    positive('frames_in_fields_and_arrays', '''
let^ count = p^ n:i32^ {
    var^ i:i32^ = 0
    repeat^ while^ i < n { yield^ i i += 1 }
    return^ "done"
}
def^ Pair {self^{a:count.ReturnType^, b:count.ReturnType^}}
let^ run = p^ {
    var^ pr = Pair{a=count(2), b=count(3)}
    for^ v in^ pr.a { print^(v) }
    print^(pr.a.result())
    var^ arr = count.ReturnType^[max^4]{}
    arr.push(count(1))
    for^ v in^ arr[0] { print^(v) }
}
run()
''', '0\n1\ndone\n0\n')

    negative('written_result', '''
let^ g = p^ -> i32^ { yield^ 1
    return^ 2 }
var^ c = g()
''', "a yielding function's result is the coroutine it makes")
    negative('yield_value_is_not_received', '''
let^ g = p^ { let^ x = yield^ 1 }
var^ c = g()
''', 'binding cannot have void type')
    negative('owned_across_yield', '''
let^ g = p^ {
    var^ xs = i32^[grow^]{1, 2}
    yield^ 1
    print^(xs.len)
}
var^ c = g()
''', "'xs' owns data stack storage, which cannot live across yield^")
    negative('local_reference_across_yield', '''
let^ g = p^ {
    var^ n:i32^ = 1
    let^ r = ref^n
    yield^ 1
    print^(*r)
}
var^ c = g()
''', "'r' refers to a local of this coroutine")
    negative('pure_cannot_advance_procedure', '''
let^ g = p^ { yield^ 1 }
let^ h = f^ -> i32^ {
    var^ c = g()
    let^ v = c.start()
    return^ 0
}
print^(h())
''', "f^ cannot advance a p^ coroutine; 'start' runs its body")
    negative('frame_of_itself', '''
let^ g = p^ {
    var^ inner = g()
    yield^ 1
}
var^ c = g()
''', "coroutine 'g' cannot hold a frame of itself")
    negative('top_level_yield', 'yield^ 1\n', 'yield^ belongs inside a function body')
    negative('let_frame_cannot_advance', '''
let^ g = p^ { yield^ 1 }
let^ c = g()
let^ v = c.start()
''', "cannot modify let^ binding 'c'")
    negative('unknown_member', '''
let^ g = p^ { yield^ 1 }
var^ c = g()
let^ v = c.next()
''', "a coroutine frame has start, resume, done and result, not 'next'")

    runtime_failure('start_twice', '''
let^ g = p^ { yield^ 1 }
var^ c = g()
let^ a = c.start()
let^ b = c.start()
''', 'start of a coroutine that has already started')
    runtime_failure('resume_before_start', '''
let^ g = p^ { yield^ 1 }
var^ c = g()
let^ b = c.resume()
''', 'resume of a coroutine that has not started')
    runtime_failure('resume_after_finish', '''
let^ g = p^ { yield^ 1 }
var^ c = g()
let^ a = c.start()
let^ b = c.resume()
let^ x = c.resume()
''', 'resume of a finished coroutine')
    runtime_failure('result_before_finish', '''
let^ g = p^ { yield^ 1
    return^ 5 }
var^ c = g()
let^ a = c.start()
print^(c.result())
''', 'result of a coroutine that has not finished')
    suite.finish()


if __name__ == '__main__':
    main()
