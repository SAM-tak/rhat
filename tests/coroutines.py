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
