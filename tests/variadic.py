"""Variadic parameters ...:T, forwarding with expr..., and top-level command arguments."""
from harness import Suite


def main():
    suite = Suite('variadic')
    positive, negative = suite.positive, suite.negative

    positive('variadic_collect_and_forward', '''
let^ sum = f^ ...:i64^ -> i64^ {
    var^ total:i64^ = 0
    for^ var^ i:i64^ = 0 while^ i < ....len next^ i += 1 { total += ...[i] }
    return^ total
}
let^ label = f^ name:string^, ...:i64^ -> i64^ { sum(...) + ....len }
let^ twice = f^ xs:i64^[:] -> i64^ { sum(xs...) * 2 }
let^ depth = f^ n:i64^, ...:i64^ -> i64^ { if^ n = 0 { return^ sum(...) } return^ this^(n - 1, ...) }
var^ values:i64^[3] = [1, 2, 3]
print^(sum(), sum(5), sum(1, 2, 3), label("x", 10, 20), twice(values[0 :: 3]), sum(values...), depth(3, 4, 5))
''', '0 5 6 32 12 6 9\n')

    positive('variadic_values_methods_and_references', '''
let^ count = f^ ...:string^ -> i64^ { ....len }
let^ alias = count
let^ generic = template^ f^ ...:i32^ -> i64^ { ....len }
def^ Box {
    self^{ k:i64^ = 10 }
    add = f^ self^, ...:i64^ -> i64^ { self^.k + ....len }
}
let^ total = f^ ...:ref^ i32^ -> i32^ {
    var^ t:i32^ = 0
    for^ var^ i:i64^ = 0 while^ i < ....len next^ i += 1 { t += *...[i] }
    return^ t
}
let^ id = f^ ...:i32^ -> i32^[:] { return^ ... }
var^ box = Box{}
var^ a:i32^ = 2
var^ b:i32^ = 5
print^(alias("a", "b", "c"), generic(1, 2), box.add(1, 2), total(ref^ a, ref^ b), id(4, 8)[1])
''', '3 2 12 7 8\n')

    def arguments():
        suite.source('variadic_command_arguments', '''
print^(....len)
for^ var^ i:i64^ = 0 while^ i < ....len next^ i += 1 { print^(...[i], ...[i].bytes.len) }
''')
        for opt in suite.opts:
            exe, _ = suite.build('variadic_command_arguments', opt)
            actual = suite.run([exe, 'alpha', 'β gamma', '']).stdout
            assert actual == '3\nalpha 5\nβ gamma 8\n 0\n', (opt, actual)
            assert suite.run([exe]).stdout == '0\n', opt
    if not suite.args.checks_only:
        suite.case('variadic_command_arguments', arguments)

    negative('variadic_not_last', 'let^ s = f^ ...:i32^, x:i32^ { }', 'must be last')
    negative('variadic_untyped', 'let^ s = f^ ... { }', 'needs an element type')
    negative('variadic_overload', 'let^ s = f^ ...:i32^ -> i64^ { ....len }\nlet^ s = f^ x:string^ -> i64^ { 0 }', 'cannot be overloaded')
    negative('variadic_missing_fixed', 'let^ s = f^ x:i32^, ...:i32^ -> i64^ { ....len }\nprint^(s())', 'expects at least 1 arguments')
    negative('variadic_element_type', 'let^ s = f^ ...:i32^ -> i64^ { ....len }\nprint^(s(1, "x"))', "expected 'i32^'")
    negative('variadic_mixed_forward', 'let^ s = f^ ...:i32^ -> i64^ { ....len }\nvar^ a:i32^[1] = [1]\nprint^(s(1, a...))', 'only variadic argument')
    negative('variadic_forward_plain', 'let^ g = f^ x:i32^ -> i32^ { x }\nvar^ a:i32^[1] = [1]\nprint^(g(a...))', 'forwards only into a variadic parameter')
    negative('variadic_escape_binding', 'let^ id = f^ ...:i32^ -> i32^[:] { return^ ... }\nlet^ s = id(1, 2)\nprint^(s[0])', 'outlive its owner')
    negative('variadic_escape_return', 'let^ id = f^ ...:i32^ -> i32^[:] { return^ ... }\nlet^ wrap = f^ -> i32^[:] { return^ id(1, 2) }\nprint^(wrap()[0])', 'cannot return a reference')
    negative('variadic_owned_elements', 'let^ s = f^ ...:grow^ i32^[] -> i64^ { ....len }\nprint^(s(grow^ i32^[]))', 'cannot own storage')
    negative('variadic_extern', 'externC^ printf:p^ format:string^, ...:i32^ ->i32^;', 'variadic')
    suite.finish()


if __name__ == '__main__':
    main()
