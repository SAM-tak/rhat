"""flex^ arrays: shrinking returns storage, guarded by static reference checks."""
from harness import Suite


def main():
    suite = Suite('flex')
    positive, negative = suite.positive, suite.negative

    # Observe the runtime: shrinking a large flex^ must return its pages.
    wrapper = suite.work / 'flex_runtime.c'
    wrapper.write_text(f'''
#define rh_flex_trim rh_base_flex_trim
#define rh_runtime_shutdown rh_base_shutdown
#include "{(suite.repo / 'runtime/rhat_runtime.c').as_posix()}"
#undef rh_flex_trim
#undef rh_runtime_shutdown
static int trims;
static uint64_t lowest = UINT64_MAX;
void rh_flex_trim(RhStack *s, unsigned char *end) {{
  rh_base_flex_trim(s, end);
  trims++;
  if ((uint64_t)(s->committed - s->base) < lowest) lowest = (uint64_t)(s->committed - s->base);
}}
void rh_runtime_shutdown(void) {{
  printf("trimmed %d, kept at most 4 MiB %d\\n", trims > 0, lowest <= (UINT64_C(4) << 20));
  rh_base_shutdown();
}}
''', encoding='utf-8')
    positive('flex_returns_pages', '''
var^ q:flex^ i64^[] = []
for^ var^ round:i64^ = 0 while^ round < 2 next^ round += 1 {
  for^ var^ i:i64^ = 0 while^ i < 4000000 next^ i += 1 { q.push(i) }
  print^(q.len, q[q.len - 1])
  q.truncate(10)
  print^(q.len, q.pop(), q.len)
  q.clear()
}
''', '4000000 3999999\n10 9 9\n4000000 3999999\n10 9 9\ntrimmed 1, kept at most 4 MiB 1\n', extra=('--runtime', wrapper))

    positive('flex_operations', '''
var^ lit = flex^[1, 2, 3]
lit.push(4)
print^(lit.len, lit.pop(), lit.pop(), lit.len)
var^ empty = flex^ i32^[]
empty.push(7)
var^ more:flex^ i64^[] = [5, 6]
more.append(more[0 :: 2])
print^(empty[0], more.len, more[3])
def^ Box { self^{ n:i32^ = 0, items:flex^ i64^[] = [] } }
var^ b = Box{}
b.items.push(5)
let^ other = ref^ b.n
print^(b.items.pop(), *other)
var^ x:i64^ = 7
var^ rs:flex^ (ref^ i64^)[] = flex^[ref^ x]
let^ got = rs.pop()
rs.clear()
print^(*got)
''', '4 4 3 2\n7 4 6\n5 0\n7\n')

    prelude = '''var^ q:flex^ i64^[] = flex^[1, 2, 3]
let^ drop = p^ x:ref^ flex^ i64^[] { x.pop() }
let^ look = p^ x:ref^ flex^ i64^[] { print^(x.len) }
let^ outer = p^ x:ref^ flex^ i64^[] { drop(x) }
let^ two = p^ a:ref^ i64^, b:i64^ { print^(*a, b) }
'''
    positive('flex_scoped_and_unshrinking_calls', prelude + '''do^{ let^ r = ref^ q[0] print^(*r) }
print^(q.pop())
let^ r = ref^ q[0]
look(ref^ q)
print^(*r)
''', '1\n3\n2\n1\n')

    for name, text, diagnostic in [
        ('flex_reference', 'let^ r = ref^ q[0]\nq.pop()', "while 'r' may refer into it"),
        ('flex_slice', 'let^ s = q[0 :: 1]\nq.clear()', "while 's' may refer into it"),
        ('flex_push_result', 'let^ r = q.push(4)\nq.truncate(1)', "while 'r' may refer into it"),
        ('flex_earlier_argument', 'two(ref^ q[0], q.pop())', 'an earlier operand'),
        ('flex_literal_field', 'def^ H { self^{ r:ref^ i64^, n:i64^ = 0 } }\nvar^ h = H{ r = ref^ q[0], n = q.pop() }', 'an earlier operand'),
        ('flex_callee_shrinks', 'let^ r = ref^ q[0]\ndrop(ref^ q)', "while 'r' may refer into it"),
        ('flex_transitive_callee', 'let^ r = ref^ q[0]\nouter(ref^ q)', "while 'r' may refer into it"),
        ('flex_struct_tail', 'def^ Box { self^{ n:i64^ = 0, items:flex^ i64^[] = [] } }\nvar^ b = Box{}\nb.items.push(5)\nlet^ r = ref^ b.items[0]\nb.items.pop()',
         "while 'r' may refer into it"),
        ('flex_dyn', 'def^ S { abstract^ size:f^ self^ -> i64^; }\ndef^ T : S { self^{ xs:flex^ i64^[] = [] } size = f^ self^ -> i64^ { self^.xs.len } }\nvar^ t = T{}\nlet^ s:ref^ dyn^ S = ref^ t',
         'cannot be used through dyn^'),
        ('flex_grow_literal', 'var^ g:flex^ i64^[] = grow^[1]', "grow^ array literal cannot initialize 'flex^ i64^[]'"),
    ]:
        negative(name, prelude + text, diagnostic)
    suite.finish()


if __name__ == '__main__':
    main()
