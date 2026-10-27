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

    negative('ffi_struct_string_field', 'def^ P { self^{s:string^=""} }\nexternC^ f:f^ p:P ->i32^;', "parameter 'p' has no C shape")
    negative('ffi_struct_reference_field', 'var^ n:i32^=0\ndef^ P { self^{r:ref^ i32^} }\nexternC^ f:f^ p:P ->i32^;', "parameter 'p' has no C shape")
    negative('ffi_struct_empty', 'def^ P { }\nexternC^ f:f^ p:P ->i32^;', "parameter 'p' has no C shape")
    negative('ffi_array_value', 'externC^ f:f^ a:i32^[4] ->i32^;', "parameter 'a' has no C shape")
    negative('ffi_wide_integer', 'externC^ f:f^ x:i128^;', 'has no C shape')
    negative('ffi_reference_result', 'externC^ f:f^ ->ref^ i32^;', 'C scalar, a flat def^, or omitted')
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
