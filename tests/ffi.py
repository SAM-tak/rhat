"""externC^ declarations: C shapes, symbols, effects and --link-lib."""
import os

from harness import Suite


def main():
    suite = Suite('ffi')
    positive, negative = suite.positive, suite.negative

    def declarations(rir, ll):
        assert 'declare double @hypot(double, double) nounwind memory(none)' in ll, ll
        assert 'declare double @frexp(double, ptr) nounwind\n' in ll, ll
        assert ll.count('declare i32 @abs(') == 1, ll

    positive('ffi_libc_and_libm', '''
externC^ hypot:f^ x:f64^, y:f64^ ->f64^;
externC^ frexp:p^ x:f64^, exponent:ref^ i32^ ->f64^;
externC^ strlen:f^ s:string^ ->u64^;
externC^ memset:p^ s:u8^[:], c:i32^, n:u64^;
externC^ abs:f^ n:i32^ ->i32^;
let^ magnitude=f^ n:i32^ ->i32^ { abs(n) }
var^ exponent:i32^=0
let^ mantissa=frexp(8.0, ref^ exponent)
var^ bytes:u8^[4]=[1,2,3,4]
memset(bytes[1 :: 3], 7, 2)
print^(hypot(3.0,4.0), mantissa, exponent, strlen("hello"), abs(-5), magnitude(-6))
print^(bytes[0], bytes[1], bytes[2], bytes[3])
''', '5 0.5 4 5 5 6\n1 7 7 4\n', inspect=declarations)

    positive('ffi_procedure_output_order', '''
externC^ puts:p^ s:string^ ->i32^;
print^(1)
puts("from C")
print^(2)
''', '1\nfrom C\n2\n')

    positive('ffi_symbol_alias_overloads', '''
let^ root = externC^ sqrtf:f^ x:f32^ ->f32^;
overload^ let^ root = externC^ sqrt:f^ x:f64^ ->f64^;
externC^ sqrt:f^ x:f64^ ->f64^;
print^(root(f32^(2.25)), root(6.25), sqrt(16.0))
''', '1.5 2.5 4\n')

    if os.name == 'nt':
        linked = 'externC^ GetCurrentProcessId:p^ ->u32^;\nprint^(GetCurrentProcessId() > 0)\n'
        library = 'kernel32'
    else:
        linked = 'externC^ cbrt:f^ x:f64^ ->f64^;\nprint^(cbrt(27.0) = 3.0)\n'
        library = 'm'
    positive('ffi_link_library', linked, 'true\n', extra=('--link-lib', library))

    # def^ values cross by value through generated C shims.
    structs = suite.work / 'ffi_structs.c'
    structs.write_text(f'''#include "{(suite.repo / 'runtime/rhat_runtime.c').as_posix()}"
typedef struct {{ double x, y; }} Point;
typedef struct {{ int32_t a; int8_t b; int64_t c; }} Mixed;
typedef struct {{ int32_t lo, hi; }} Pair;
typedef struct {{ int64_t v[4]; }} Big;
typedef struct {{ Pair p; bool ok; uint16_t w[3]; }} Nest;
Point ffi_add(Point a, Point b) {{ Point r = {{a.x + b.x, a.y + b.y}}; return r; }}
Mixed ffi_mix(Mixed m, int32_t k) {{ m.a += k; m.b = (int8_t)(m.b * 2); m.c -= k; return m; }}
int64_t ffi_pair_sum(Pair p) {{ return (int64_t)p.lo + p.hi; }}
Big ffi_big(Big b) {{ for (int i = 0; i < 4; i++) b.v[i] *= 10; return b; }}
void ffi_scale(Point *p, double k) {{ p->x *= k; p->y *= k; }}
double ffi_sum(const Point *ps, uint64_t n) {{ double s = 0; for (uint64_t i = 0; i < n; i++) s += ps[i].x + ps[i].y; return s; }}
Nest ffi_nest(Nest n) {{ n.p.lo++; n.ok = !n.ok; n.w[2] = (uint16_t)(n.w[0] + n.w[1]); return n; }}
''', encoding='utf-8')
    positive('ffi_struct_values', '''
def^ Point { self^{ x:f64^ = 0.0, y:f64^ = 0.0 } }
def^ Mixed { self^{ a:i32^ = 0, b:i8^ = 0, c:i64^ = 0 } }
def^ Pair { self^{ lo:i32^ = 0, hi:i32^ = 0 } }
def^ Big { self^{ v:i64^[4] = [1, 2, 3, 4] } }
def^ Nest { self^{ p:Pair = Pair{}, ok:bool^ = false^, w:u16^[3] = [1, 2, 0] } }
def^ LLDiv { self^{ quot:i64^ = 0, rem:i64^ = 0 } }
externC^ ffi_add:f^ a:Point, b:Point -> Point;
externC^ ffi_mix:f^ m:Mixed, k:i32^ -> Mixed;
externC^ ffi_pair_sum:f^ p:Pair -> i64^;
externC^ ffi_big:f^ b:Big -> Big;
externC^ ffi_scale:p^ p:ref^ Point, k:f64^;
externC^ ffi_sum:f^ ps:Point[:], n:u64^ -> f64^;
externC^ ffi_nest:f^ n:Nest -> Nest;
externC^ lldiv:f^ n:i64^, d:i64^ -> LLDiv;
let^ s = ffi_add(Point{x = 1.0, y = 2.0}, Point{x = 10.0, y = 20.0})
let^ m = ffi_mix(Mixed{a = 1, b = 3, c = 100}, 5)
let^ b = ffi_big(Big{})
var^ p = Point{x = 2.0, y = 3.0}
ffi_scale(ref^ p, 2.0)
var^ ps:Point[2] = [Point{x = 1.0, y = 1.0}, Point{x = 2.0, y = 3.0}]
let^ n = ffi_nest(Nest{})
let^ q = lldiv(17, 5)
print^(s.x, s.y, m.a, m.b, m.c, ffi_pair_sum(Pair{lo = 40, hi = 2}))
print^(b.v[0], b.v[3], p.x, p.y, ffi_sum(ps[::], 2), n.p.lo, n.ok, n.w[2], q.quot, q.rem)
''', '11 22 6 6 95 42\n10 40 4 6 7 1 true 3 3 2\n', extra=('--runtime', structs))

    # includeC^: a header's static inline functions, its struct and opaque types.
    (suite.work / 'ffi_lib.h').write_text('''#include <stdbool.h>
#include <stdint.h>
typedef struct { union { int64_t i; double r; } as; int32_t tag; } FfiValue;
typedef struct FfiBox FfiBox;
static inline FfiValue ffi_int(int64_t i) { FfiValue v; v.as.i = i; v.tag = 1; return v; }
static inline FfiValue ffi_real(double r) { FfiValue v; v.as.r = r; v.tag = 2; return v; }
static inline int64_t ffi_twice(int64_t n) { return 2 * n; }
static inline double ffi_as_real(FfiValue v) { return v.tag == 1 ? (double)v.as.i : v.as.r; }
FfiBox *ffi_box_new(int64_t n);
FfiBox *ffi_box_find(int64_t n);
int64_t ffi_box_get(const FfiBox *b);
int64_t ffi_box_or(const FfiBox *b, int64_t fallback);
void ffi_box_free(FfiBox *b);
''', encoding='utf-8')
    boxes = suite.work / 'ffi_boxes.c'
    boxes.write_text(f'''#include "{(suite.repo / 'runtime/rhat_runtime.c').as_posix()}"
#include "ffi_lib.h"
struct FfiBox {{ int64_t n; }};
FfiBox *ffi_box_new(int64_t n) {{ FfiBox *b = malloc(sizeof *b); b->n = n; return b; }}
FfiBox *ffi_box_find(int64_t n) {{ return n ? ffi_box_new(n) : NULL; }}
int64_t ffi_box_get(const FfiBox *b) {{ return b->n; }}
int64_t ffi_box_or(const FfiBox *b, int64_t fallback) {{ return b ? b->n : fallback; }}
void ffi_box_free(FfiBox *b) {{ free(b); }}
''', encoding='utf-8')
    header = 'includeC^ "ffi_lib.h"\n'
    value = header + 'externC^ def^ FfiValue { self^{ as:u64^ = 0, tag:i32^ = 0 } }\nexternC^ ffi_int:f^ i:i64^ -> FfiValue;\n'
    positive('ffi_header_inline_and_opaque', header + '''externC^ def^ FfiBox;
externC^ def^ FfiValue { self^{ as:u64^ = 0, tag:i32^ = 0 } }
externC^ ffi_int:f^ i:i64^ -> FfiValue;
externC^ ffi_real:f^ r:f64^ -> FfiValue;
externC^ ffi_twice:f^ n:i64^ -> i64^;
externC^ ffi_as_real:f^ v:FfiValue -> f64^;
externC^ ffi_box_new:p^ n:i64^ -> ref^ FfiBox;
externC^ ffi_box_find:p^ n:i64^ -> ref^ FfiBox | nil^;
externC^ ffi_box_get:f^ b:ref^ FfiBox -> i64^;
externC^ ffi_box_or:f^ b:ref^ FfiBox | nil^, fallback:i64^ -> i64^;
externC^ ffi_box_free:p^ b:ref^ FfiBox;
let^ v = ffi_int(21)
print^(v.tag, ffi_twice(21), ffi_as_real(v), ffi_as_real(ffi_real(2.5)))
let^ b = ffi_box_new(7)
let^ found = ffi_box_find(9)
let^ missing = ffi_box_find(0)
print^(ffi_box_get(b), found fits^ nil^, missing fits^ nil^, ffi_box_or(found, -1), ffi_box_or(missing, -1), ffi_box_or(nil^, -2))
if^ found fits^ ref^ FfiBox { print^(ffi_box_get(found)) ffi_box_free(found) }
ffi_box_free(b)
''', '1 42 21 2.5\n7 false true 9 -1 -2\n9\n', extra=('--runtime', boxes))

    def rejected_by_c(name, text, diagnostic):
        """The C compiler checks shims against the header's declarations."""
        def body():
            path = suite.source(name, text)
            p = suite.run([suite.compiler, 'build', path, '-o', suite.work / (name + '.exe'),
                           '--clang', suite.clang, '--runtime', boxes], False)
            assert diagnostic in p.stderr, (name, p.stderr)
        if not suite.args.checks_only:
            suite.case(name, body)
    rejected_by_c('ffi_header_field_name', value.replace('tag:i32^', 'kind:i32^').replace('ffi_int(1)', '') + 'print^(ffi_int(1).kind)',
                  "no member named 'kind'")
    rejected_by_c('ffi_header_field_size', value.replace('tag:i32^', 'tag:i64^') + 'print^(ffi_int(1).tag)',
                  'R^ layout of FfiValue.tag differs from C')
    rejected_by_c('ffi_header_pointer_for_integer', header + 'externC^ ffi_twice:f^ n:ref^ i64^ -> i64^;\nvar^ x:i64^ = 1\nprint^(ffi_twice(ref^ x))',
                  'incompatible pointer to integer conversion')
    rejected_by_c('ffi_header_undeclared', header + 'externC^ ffi_nothing:f^ n:i64^ -> i64^;\nprint^(ffi_nothing(1))', 'ffi_nothing')
    negative('ffi_header_plain_def', header + 'def^ P { self^{ x:i32^ = 0 } }\nexternC^ ffi_p:f^ p:P -> i32^;', 'declare it with externC^ def^')
    negative('ffi_opaque_value', header + 'externC^ def^ FfiBox;\nvar^ boxed:FfiBox = FfiBox{}', 'opaque; use it only through ref^')
    negative('ffi_include_public', 'public^ includeC^ "ffi_lib.h"', 'cannot be public^')

    negative('ffi_struct_string_field', 'def^ P { self^{s:string^=""} }\nexternC^ f:f^ p:P ->i32^;', "parameter 'p' has no C shape")
    negative('ffi_struct_reference_field', 'var^ n:i32^=0\ndef^ P { self^{r:ref^ i32^} }\nexternC^ f:f^ p:P ->i32^;', "parameter 'p' has no C shape")
    negative('ffi_struct_empty', 'def^ P { }\nexternC^ f:f^ p:P ->i32^;', "parameter 'p' has no C shape")
    negative('ffi_array_value', 'externC^ f:f^ a:i32^[4] ->i32^;', "parameter 'a' has no C shape")
    negative('ffi_wide_integer', 'externC^ f:f^ x:i128^;', 'has no C shape')
    negative('ffi_string_result', 'externC^ f:f^ ->string^;', 'a C pointer, or omitted')
    negative('ffi_unannotated', 'externC^ f:f^ x ->i32^;', 'requires a type annotation')
    negative('ffi_template', 'externC^ f:template^ f^ x:i32^;', 'cannot be template^')
    negative('ffi_body', 'externC^ f:f^ x:i32^ { x }', "expected ';'")
    negative('ffi_main_symbol', 'externC^ main:p^ ->i32^;', 'usable C identifier')
    negative('ffi_runtime_symbol', 'externC^ rh_random_seed:f^ state:ref^ u64^ ->i64^;', 'reserved for the runtime')
    negative('ffi_conflicting_symbol', 'externC^ abs:f^ n:i32^ ->i32^;\noverload^ externC^ abs:f^ n:i64^ ->i64^;', 'another signature')
    negative('ffi_alias_conflict', 'let^ a = externC^ abs:f^ n:i32^ ->i32^;\nlet^ b = externC^ abs:f^ n:i64^ ->i64^;', 'another signature')
    negative('ffi_local', 'let^ g=p^ { let^ h = externC^ abs:f^ n:i32^ ->i32^; }', 'only allowed at the top level')
    negative('ffi_effect', 'externC^ puts:p^ s:string^ ->i32^;\nlet^ g=f^ ->i32^ { puts("x") }\nprint^(g())', "f^ cannot call p^ 'puts'")
    suite.finish()


if __name__ == '__main__':
    main()
