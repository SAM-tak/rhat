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
externC^ memset:p^ s:u8^[..], c:i32^, n:u64^;
externC^ abs:f^ n:i32^ ->i32^;
let^ magnitude=f^ n:i32^ ->i32^ { abs(n) }
var^ exponent:i32^=0
let^ mantissa=frexp(8.0, ref^ exponent)
var^ bytes:u8^[4]={1,2,3,4}
memset(bytes[1 .. 3], 7, 2)
print^(hypot(3.0,4.0), mantissa, exponent, strlen("hello"), abs(-5), magnitude(-6))
print^(bytes[0], bytes[1], bytes[2], bytes[3])
''', '5.0 0.5 4 5 5 6\n1 7 7 4\n', inspect=declarations)

    positive('ffi_procedure_output_order', '''
externC^ puts:p^ s:string^ ->i32^;
print^(1)
puts("from C")
print^(2)
''', '1\nfrom C\n2\n')

    positive('ffi_symbol_alias_overloads', '''
let^ root = externC^ sqrtf:f^ x:f32^ ->f32^;
& externC^ sqrt:f^ x:f64^ ->f64^;
externC^ sqrt:f^ x:f64^ ->f64^;
print^(root(f32^(2.25)), root(f64^(6.25)), sqrt(16.0))
''', '1.5 2.5 4.0\n')

    if os.name == 'nt':
        linked = 'externC^ GetCurrentProcessId:p^ ->u32^;\nprint^(GetCurrentProcessId() > 0)\n'
        library = 'kernel32'
    else:
        linked = 'externC^ cbrt:f^ x:f64^ ->f64^;\nprint^(cbrt(27.0) > 2.999 and^ cbrt(27.0) < 3.001)\n'
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
struct^ Point { x:f64^ = 0.0, y:f64^ = 0.0 }
def^ Mixed { self^{ a:i32^ = 0, b:i8^ = 0, c:i64^ = 0 } }
def^ Pair { self^{ lo:i32^ = 0, hi:i32^ = 0 } }
def^ Big { self^{ v:i64^[4] = {1, 2, 3, 4} } }
def^ Nest { self^{ p:Pair = Pair{}, ok:bool^ = false^, w:u16^[3] = {1, 2, 0} } }
def^ LLDiv { self^{ quot:i64^ = 0, rem:i64^ = 0 } }
externC^ ffi_add:f^ a:Point, b:Point -> Point;
externC^ ffi_mix:f^ m:Mixed, k:i32^ -> Mixed;
externC^ ffi_pair_sum:f^ p:Pair -> i64^;
externC^ ffi_big:f^ b:Big -> Big;
externC^ ffi_scale:p^ p:ref^ Point, k:f64^;
externC^ ffi_sum:f^ ps:Point[..], n:u64^ -> f64^;
externC^ ffi_nest:f^ n:Nest -> Nest;
externC^ lldiv:f^ n:i64^, d:i64^ -> LLDiv;
let^ s = ffi_add(Point{x = 1.0, y = 2.0}, Point{x = 10.0, y = 20.0})
let^ m = ffi_mix(Mixed{a = 1, b = 3, c = 100}, 5)
let^ b = ffi_big(Big{})
var^ p = Point{x = 2.0, y = 3.0}
ffi_scale(ref^ p, 2.0)
var^ ps:Point[2] = {Point{x = 1.0, y = 1.0}, Point{x = 2.0, y = 3.0}}
let^ n = ffi_nest(Nest{})
let^ q = lldiv(17, 5)
print^(s.x, s.y, m.a, m.b, m.c, ffi_pair_sum(Pair{lo = 40, hi = 2}))
print^(b.v[0], b.v[3], p.x, p.y, ffi_sum(ps[..], 2), n.p.lo, n.ok, n.w[2], q.quot, q.rem)
''', '11.0 22.0 6 6 95 42\n10 40 4.0 6.0 7.0 1 true 3 3 2\n', extra=('--runtime', structs))

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
int64_t ffi_apply(int64_t (*fn)(int64_t), int64_t x);
typedef struct { FfiBox *box; int64_t n; } FfiPair;
#define FFI_ANSWER 42
enum { FFI_RED = 3, FFI_BLUE = 7 };
FfiPair *ffi_pair_new(int64_t n);
int64_t ffi_pair_sum(const FfiPair *p);
static inline const char *ffi_text(int32_t k) { return k == 0 ? "h\\xc3\\xa9llo" : k == 1 ? "\\xff" : (const char *)0; }
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
int64_t ffi_apply(int64_t (*fn)(int64_t), int64_t x) {{ return fn ? fn(x) : -1; }}
FfiPair *ffi_pair_new(int64_t n) {{ FfiPair *p = malloc(sizeof *p); p->box = ffi_box_new(n); p->n = 1; return p; }}
int64_t ffi_pair_sum(const FfiPair *p) {{ return p->n + (p->box ? p->box->n * 10 : 0); }}
''', encoding='utf-8')
    header = 'includeC^ "ffi_lib.h"\n'
    value = header + 'externC^ def^ FfiValue { self^{ as:u64^ = 0, tag:i32^ = 0 } }\nexternC^ ffi_int:f^ i:i64^ -> FfiValue;\n'
    positive('ffi_header_inline_and_opaque', header + '''externC^ def^ FfiBox;
externC^ struct^ FfiValue { as:u64^ = 0, tag:i32^ = 0 }
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
''', '1 42 21.0 2.5\n7 false true 9 -1 -2\n9\n', extra=('--runtime', boxes))

    # A reference to an opaque C type is C's object, not a borrow: a job holding one moves to a
    # pool worker. A reference to a C struct R^ can also hold is still a borrow.
    positive('ffi_foreign_ref_task', header + '''let^ task = import^ std.task
externC^ def^ FfiBox;
externC^ ffi_box_new:p^ n:i64^ -> ref^ FfiBox;
externC^ ffi_box_get:f^ b:ref^ FfiBox -> i64^;
let^ job = p^ b:ref^ FfiBox {
    yield^ i64^(0)
    return^ ffi_box_get(b)
}
let^ run = p^ {
    do^ {
        var^ pool = try^ task.Pool.new(1)
        var^ t = try^ pool.async(job(ffi_box_new(7)))
        print^((try^ t.wait()).value)
    catch^: print^("failed")
    }
}
run()
''', '7\n', extra=('--runtime', boxes))
    negative('ffi_struct_ref_task', value + '''let^ task = import^ std.task
let^ job = p^ v:ref^ FfiValue {
    yield^ i64^(0)
    return^ v.tag
}
let^ run = p^ {
    do^ {
        var^ v = FfiValue{}
        var^ pool = try^ task.Pool.new(1)
        var^ t = try^ pool.async(job(ref^ v))
    catch^: print^("failed")
    }
}
run()
''', "thread transfer refuses")
    # A C struct may point at C's objects: a ref^ T | nil^ field to an opaque C type is laid out
    # as one pointer, NULL for nil^, read and written as R^'s union.
    pair = header + '''externC^ def^ FfiBox;
externC^ ffi_box_new:p^ n:i64^ -> ref^ FfiBox;
externC^ ffi_box_get:f^ b:ref^ FfiBox -> i64^;
externC^ def^ FfiPair { self^{ box:ref^ FfiBox | nil^ = nil^, n:i64^ = 0 } }
externC^ ffi_pair_new:p^ n:i64^ -> ref^ FfiPair;
externC^ ffi_pair_sum:f^ p:ref^ FfiPair -> i64^;
'''
    positive('ffi_c_pointer_field', pair + '''let^ p = ffi_pair_new(7)
let^ b = p.box
if^ b fits^ ref^ FfiBox { print^(ffi_box_get(b), p.n) }
p.box := nil^
print^(p.box fits^ nil^, ffi_pair_sum(p))
p.box := ffi_box_new(4)
print^(ffi_pair_sum(p))
var^ local = FfiPair{n = 5}
print^(local.box fits^ nil^, ffi_pair_sum(ref^ local))
var^ other = FfiPair{box = ffi_box_new(2), n = 1}
print^(ffi_pair_sum(ref^ other))
''', '7 1\ntrue 1\n41\ntrue 5\n21\n', extra=('--runtime', boxes))
    negative('ffi_c_pointer_field_borrow', pair + '''var^ local = FfiPair{n = 5}
let^ r = ref^ local.box
''', 'a C pointer field cannot be borrowed')

    # externC^ NAME:T; reads a C constant -- a macro or an enum value -- through the header.
    positive('ffi_c_constant', header + '''externC^ FFI_ANSWER:i64^;
externC^ FFI_RED:i32^;
externC^ FFI_BLUE:i32^;
let^ pick = f^ k:i32^ -> i64^ { if^ k = FFI_RED: 1 el^: 2; }
print^(FFI_ANSWER, FFI_RED + FFI_BLUE, pick(FFI_RED), pick(FFI_BLUE))
''', '42 10 1 2\n')
    negative('ffi_c_constant_no_header', 'externC^ ANSWER:i64^;\nprint^(ANSWER)\n',
             "externC^ constant 'ANSWER' needs a module with includeC^")
    # Builder.append_c copies C's NUL-terminated text; NULL adds nothing, and text that is not
    # UTF-8 is refused whole.
    positive('ffi_append_c', header + '''let^ text = import^ std.text
externC^ ffi_text:f^ k:i32^ -> ref^ read^ u8^ | nil^;
var^ b = text.Builder{}
let^ a = b.append_c(ffi_text(0))
let^ bad = b.append_c(ffi_text(1))
let^ none = b.append_c(ffi_text(2))
print^(b.text(), a fits^ nil^, bad fits^ text.Error.Invalid, none fits^ nil^)
''', 'héllo true true true\n')

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

    # exportC^: C calls back into R^ during an externC^ call.
    callbacks = suite.work / 'ffi_callbacks.c'
    callbacks.write_text(f'''#include "{(suite.repo / 'runtime/rhat_runtime.c').as_posix()}"
typedef struct {{ double x, y; }} Point;
typedef struct Box Box;
int64_t r_twice(int64_t x);
Point r_mid(Point a, Point b);
int64_t r_count(int64_t n);
int64_t r_hold(int64_t n);
bool r_flag(bool b);
Box *r_pick(Box *a, Box *b);
int64_t ffi_call_twice(int64_t x) {{ return r_twice(x) + 1; }}
double ffi_call_mid(void) {{ Point a = {{0, 0}}, b = {{4, 6}}; Point m = r_mid(a, b); return m.x + m.y; }}
int64_t ffi_recurse(int64_t n) {{ return r_count(n); }}
int64_t ffi_hold(int64_t n) {{ return r_hold(n); }}
int32_t ffi_flag(void) {{ return r_flag(true) ? 1 : 2; }}
int32_t ffi_pick(void) {{ static int x; Box *b = (Box *)&x; return (r_pick(NULL, b) == b) + 2 * (r_pick(b, NULL) == NULL); }}
''', encoding='utf-8')
    exports = '''def^ Point { self^{ x:f64^ = 0.0, y:f64^ = 0.0 } }
externC^ def^ Box;
externC^ ffi_call_twice:f^ x:i64^ -> i64^;
externC^ ffi_call_mid:f^ -> f64^;
externC^ ffi_recurse:p^ n:i64^ -> i64^;
externC^ ffi_hold:p^ n:i64^ -> i64^;
externC^ ffi_flag:f^ -> i32^;
externC^ ffi_pick:p^ -> i32^;
exportC^ let^ r_twice = f^ x:i64^ -> i64^ { x * 2 }
exportC^ let^ r_mid = f^ a:Point, b:Point -> Point { Point{x = (a.x + b.x) / 2.0, y = (a.y + b.y) / 2.0} }
exportC^ let^ r_count = p^ n:i64^ -> i64^ { if^ n = 0 { return^ 0 } return^ ffi_recurse(n - 1) + 1 }
exportC^ let^ r_hold = p^ n:i64^ -> i64^ {
  var^ buf:i64^[grow^] = {}
  buf.push(n)
  if^ n = 0 { return^ 0 }
  return^ ffi_hold(n - 1) + buf[0]
}
exportC^ let^ r_flag = f^ b:bool^ -> bool^ { !b }
exportC^ let^ r_pick = f^ a:ref^ Box | nil^, b:ref^ Box | nil^ -> ref^ Box | nil^ { if^ a fits^ nil^ { return^ b } return^ nil^ }
'''
    positive('ffi_export_callbacks', exports + 'print^(ffi_call_twice(20), ffi_call_mid(), ffi_recurse(1000), ffi_flag(), ffi_pick(), ffi_hold(5))\n',
             '41 5.0 1000 2 3 15\n', extra=('--runtime', callbacks))
    suite.runtime_failure('ffi_export_nesting_limit', exports + 'print^(ffi_hold(100))\n',
                          'C callbacks nest deeper than the data stacks allow', extra=('--runtime', callbacks))
    positive('ffi_enum', '''enum^ Sign { Minus = -1, Zero, Plus }
externC^ abs:f^ s:Sign -> Sign;
print^(abs(Sign.Minus), abs(Sign.Plus), abs(Sign.Zero))
''', 'Plus Plus Zero\n')
    negative('ffi_enum_lhat', header + 'enum^ M { A }\nexportL^ let^ g = f^ m:M -> i32^ { 0 }', "exportL^ parameter 'm' needs")
    negative('ffi_export_slice', 'exportC^ let^ g = f^ xs:i32^[..] -> i32^ { 0 }', "exportC^ parameter 'xs' needs a C type annotation")
    negative('ffi_export_unannotated', 'exportC^ let^ g = f^ x -> i32^ { 0 }', "exportC^ parameter 'x' needs a C type annotation")
    negative('ffi_export_result', 'exportC^ let^ g = f^ x:i32^ { x }', 'needs a result type annotation')
    negative('ffi_export_template', 'exportC^ let^ g = template^ f^ x:i32^ -> i32^ { x }', 'cannot be template^')
    negative('ffi_export_duplicate', 'externC^ g:f^ x:i32^ -> i32^;\nexportC^ let^ g = f^ x:i32^ -> i32^ { x }', 'already declared')
    negative('ffi_export_not_function', 'exportC^ def^ D { self^{ n:i32^ = 0 } }', 'only to a let^ function')
    negative('ffi_export_extern', 'exportC^ let^ g = externC^ abs:f^ n:i32^ -> i32^;', 'only to a let^ function')
    # exportL^: a mock lhat.h records the registrations and calls the wrappers.
    mock = suite.work / 'mock_lhat'
    mock.mkdir(exist_ok=True)
    (mock / 'lhat.h').write_text('''#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int64_t LhatInteger;
typedef enum { LHAT_VALUE_NIL, LHAT_VALUE_BOOL, LHAT_VALUE_INTEGER, LHAT_VALUE_REAL, LHAT_VALUE_OBJECT, LHAT_VALUE_HOSTVALUE } LhatValueTag;
typedef struct { int kind; } LhatObject;
typedef struct { LhatObject header; size_t length; char text[]; } LhatString;
typedef struct { union { bool boolean; LhatInteger integer; double real; LhatObject *object; } as; LhatValueTag tag; } LhatValue;
typedef struct LhatProgram LhatProgram;
typedef struct LhatMachine LhatMachine;
typedef void (*LhatHostFn)(LhatMachine *, void *, const LhatValue *, size_t, LhatValue *, int *);
static inline LhatValue lhat_nil(void) { LhatValue v; v.tag = LHAT_VALUE_NIL; return v; }
static inline LhatValue lhat_bool(bool b) { LhatValue v; v.as.boolean = b; v.tag = LHAT_VALUE_BOOL; return v; }
static inline LhatValue lhat_integer(LhatInteger i) { LhatValue v; v.as.integer = i; v.tag = LHAT_VALUE_INTEGER; return v; }
static inline LhatValue lhat_real(double r) { LhatValue v; v.as.real = r; v.tag = LHAT_VALUE_REAL; return v; }
static inline LhatObject *lhat_as_object(LhatValue v) { return v.as.object; }
static inline double lhat_number_as_real(LhatValue v) { return v.tag == LHAT_VALUE_INTEGER ? (double)v.as.integer : v.as.real; }
static inline bool lhat_number_as_whole(LhatValue v, LhatInteger *out)
{
    if (v.tag == LHAT_VALUE_INTEGER) { *out = v.as.integer; return true; }
    if (v.as.real == (double)(LhatInteger)v.as.real) { *out = (LhatInteger)v.as.real; return true; }
    return false;
}
bool lhat_register_func(LhatProgram *program, const char *module, const char *name, const char *signature, LhatHostFn call, void *context);
bool lhat_machine_make_string(LhatMachine *machine, const char *text, size_t length, LhatValue *out);
typedef enum { LHAT_HVFIELD_F32, LHAT_HVFIELD_F64, LHAT_HVFIELD_I8, LHAT_HVFIELD_I16, LHAT_HVFIELD_I32,
               LHAT_HVFIELD_I64, LHAT_HVFIELD_U8, LHAT_HVFIELD_U16, LHAT_HVFIELD_U32 } LhatHostValueFieldKind;
typedef struct LhatHostValueTag { const char *name; size_t size; } LhatHostValueTag;
const LhatHostValueTag *lhat_register_hostvalue_type(LhatProgram *program, const char *module, const char *name, size_t size);
bool lhat_register_hostvalue_field(LhatProgram *program, const char *module, const char *type, const char *field, size_t offset, LhatHostValueFieldKind kind);
bool lhat_register_hostvalue_member(LhatProgram *program, const char *module, const char *type, const char *name, const char *signature, LhatHostFn call, void *context);
void *lhat_hostvalue_data(LhatValue argument, const LhatHostValueTag *tag);
bool lhat_make_hostvalue(LhatMachine *machine, const LhatHostValueTag *tag, const void *bytes, LhatValue *out);
typedef struct LhatHostDataTag { const char *name; } LhatHostDataTag;
const LhatHostDataTag *lhat_register_hostdata_type(LhatProgram *program, const char *module, const char *name);
bool lhat_register_member(LhatProgram *program, const char *module, const char *type, const char *name, const char *signature, LhatHostFn call, void *context);
bool lhat_machine_make_hostdata(LhatMachine *machine, const LhatHostDataTag *tag, void *pointer, LhatValue *out);
void *lhat_hostdata_pointer(LhatValue value, const LhatHostDataTag *tag);
typedef struct LhatErrorKind { const char *name; } LhatErrorKind;
bool lhat_register_error_kind(LhatProgram *program, const char *module, const char *name, const char *const *variant_names, size_t variant_count, const LhatErrorKind **out_group, const LhatErrorKind **out_variants);
bool lhat_machine_make_error(LhatMachine *machine, const LhatErrorKind *kind, const char *message, LhatValue cause, LhatValue *out);
const LhatProgram *lhat_machine_program(const LhatMachine *machine);
const LhatErrorKind *lhat_lookup_error_kind(const LhatProgram *program, const char *module, const char *name, const char *variant);
LhatProgram *mock_program(void);
void mock_run(int32_t mode);
void mock_values(int32_t mode);
void mock_multi(void);
void mock_data(LhatValue v, bool (*wrap)(LhatMachine *, void *, LhatValue *));
void mock_fail(void);
void mock_any(void);
typedef struct LhatRuntimeType LhatRuntimeType;
typedef void (*LhatHostHoldFn)(void *pointer, void *context);
typedef const LhatRuntimeType *(*LhatHostTypeArgumentFn)(const void *pointer, size_t index, void *context);
bool lhat_register_hostdata_shared(LhatProgram *program, const char *module, const char *name, LhatHostHoldFn retain, LhatHostHoldFn let_go, void *context);
bool lhat_register_hostdata_type_arguments(LhatProgram *program, const char *module, const char *name, LhatHostTypeArgumentFn read, void *context);
void mock_contract(void *pointer);
LhatRuntimeType *mock_type(void);
''', encoding='utf-8')
    host = suite.work / 'mock_lhat.c'
    host.write_text(f'''#include "{(suite.repo / 'runtime/rhat_runtime.c').as_posix()}"
#include "lhat.h"
struct LhatProgram {{ int n; const char *names[16]; LhatHostFn fns[16]; }};
static LhatProgram program;
LhatProgram *mock_program(void) {{ return &program; }}
bool lhat_register_func(LhatProgram *p, const char *module, const char *name, const char *signature, LhatHostFn call, void *context)
{{
    (void)context;
    printf("%s.%s %s\\n", module, name, signature);
    p->names[p->n] = name;
    p->fns[p->n++] = call;
    return true;
}}
bool lhat_machine_make_string(LhatMachine *machine, const char *text, size_t length, LhatValue *out)
{{
    (void)machine;
    LhatString *s = malloc(sizeof *s + length + 1);
    s->length = length;
    memcpy(s->text, text, length);
    s->text[length] = 0;
    out->as.object = &s->header;
    out->tag = LHAT_VALUE_OBJECT;
    return true;
}}
static LhatValue call(const char *name, LhatValue *arguments, size_t count)
{{
    LhatValue answer = lhat_nil();
    int answers = 0;
    for (int j = 0; j < program.n; j++) {{
        if (!strcmp(program.names[j], name)) {{
            program.fns[j](NULL, NULL, arguments, count, &answer, &answers);
        }}
    }}
    return answers ? answer : lhat_nil();
}}
typedef struct {{ const LhatHostValueTag *tag; double bytes[31]; }} MockValue;
const LhatHostValueTag *lhat_register_hostvalue_type(LhatProgram *p, const char *module, const char *name, size_t size)
{{
    (void)p;
    LhatHostValueTag *t = malloc(sizeof *t);
    t->name = name;
    t->size = size;
    printf("%s.%s %zu bytes\\n", module, name, size);
    return t;
}}
bool lhat_register_hostvalue_field(LhatProgram *p, const char *module, const char *type, const char *field, size_t offset, LhatHostValueFieldKind kind)
{{
    (void)p;
    printf("%s.%s.%s at %zu as %d\\n", module, type, field, offset, (int)kind);
    return true;
}}
bool lhat_register_hostvalue_member(LhatProgram *p, const char *module, const char *type, const char *name, const char *signature, LhatHostFn call, void *context)
{{
    (void)context;
    printf("%s.%s.%s %s\\n", module, type, name, signature);
    p->names[p->n] = name;
    p->fns[p->n++] = call;
    return true;
}}
void *lhat_hostvalue_data(LhatValue v, const LhatHostValueTag *tag)
{{
    return v.tag == LHAT_VALUE_HOSTVALUE && ((MockValue *)v.as.object)->tag == tag ? ((MockValue *)v.as.object)->bytes : NULL;
}}
bool lhat_make_hostvalue(LhatMachine *machine, const LhatHostValueTag *tag, const void *bytes, LhatValue *out)
{{
    (void)machine;
    MockValue *m = malloc(sizeof *m);
    m->tag = tag;
    memcpy(m->bytes, bytes, tag->size);
    out->as.object = (LhatObject *)m;
    out->tag = LHAT_VALUE_HOSTVALUE;
    return true;
}}
typedef struct {{ const LhatHostDataTag *tag; void *pointer; }} MockData;
const LhatHostDataTag *lhat_register_hostdata_type(LhatProgram *p, const char *module, const char *name)
{{
    (void)p;
    LhatHostDataTag *t = malloc(sizeof *t);
    t->name = name;
    printf("%s.%s data\\n", module, name);
    return t;
}}
bool lhat_register_member(LhatProgram *p, const char *module, const char *type, const char *name, const char *signature, LhatHostFn call, void *context)
{{
    return lhat_register_hostvalue_member(p, module, type, name, signature, call, context);
}}
bool lhat_machine_make_hostdata(LhatMachine *machine, const LhatHostDataTag *tag, void *pointer, LhatValue *out)
{{
    (void)machine;
    MockData *m = malloc(sizeof *m);
    m->tag = tag;
    m->pointer = pointer;
    out->as.object = (LhatObject *)m;
    out->tag = LHAT_VALUE_OBJECT;
    return true;
}}
void *lhat_hostdata_pointer(LhatValue v, const LhatHostDataTag *tag)
{{
    return v.tag == LHAT_VALUE_OBJECT && ((MockData *)v.as.object)->tag == tag ? ((MockData *)v.as.object)->pointer : NULL;
}}
bool lhat_register_error_kind(LhatProgram *p, const char *module, const char *name, const char *const *names, size_t count, const LhatErrorKind **group, const LhatErrorKind **variants)
{{
    (void)p;
    (void)group;
    printf("%s.%s errors", module, name);
    for (size_t j = 0; j < count; j++) {{
        LhatErrorKind *k = malloc(sizeof *k);
        k->name = names[j];
        variants[j] = k;
        printf(" %s", names[j]);
    }}
    printf("\\n");
    return true;
}}
typedef struct {{ const LhatErrorKind *kind; const char *message; }} MockError;
bool lhat_machine_make_error(LhatMachine *machine, const LhatErrorKind *kind, const char *message, LhatValue cause, LhatValue *out)
{{
    (void)machine;
    (void)cause;
    MockError *e = malloc(sizeof *e);
    e->kind = kind;
    e->message = message;
    out->as.object = (LhatObject *)e;
    out->tag = LHAT_VALUE_OBJECT;
    return true;
}}
const LhatProgram *lhat_machine_program(const LhatMachine *machine)
{{
    (void)machine;
    return &program;
}}
const LhatErrorKind *lhat_lookup_error_kind(const LhatProgram *p, const char *module, const char *name, const char *variant)
{{
    (void)p;
    LhatErrorKind *k = malloc(sizeof *k);
    char *text = malloc(strlen(module) + strlen(name) + strlen(variant) + 3);
    sprintf(text, "%s.%s.%s", module, name, variant);
    k->name = text;
    return k;
}}
void mock_fail(void)
{{
    for (int j = 0; j < program.n; j++) {{
        for (LhatInteger k = 0; k < 3; k++) {{
            LhatValue a = lhat_integer(k), answers[2];
            int count = 0;
            if (strcmp(program.names[j], "check") && strcmp(program.names[j], "split") &&
                strcmp(program.names[j], "spare")) {{
                continue;
            }}
            program.fns[j](NULL, NULL, &a, 1, answers, &count);
            if (count == 1 && answers[0].tag == LHAT_VALUE_OBJECT) {{
                MockError *e = (MockError *)answers[0].as.object;
                printf("%s %s: %s\\n", program.names[j], e->kind->name, e->message);
            }}
            else {{
                printf("%s %d %lld\\n", program.names[j], count, (long long)answers[0].as.integer);
            }}
        }}
    }}
}}
static LhatHostHoldFn retained, released;
static LhatHostTypeArgumentFn argued;
bool lhat_register_hostdata_shared(LhatProgram *p, const char *module, const char *name, LhatHostHoldFn retain, LhatHostHoldFn let_go, void *context)
{{
    (void)p;
    (void)context;
    printf("%s.%s shared\\n", module, name);
    retained = retain;
    released = let_go;
    return true;
}}
bool lhat_register_hostdata_type_arguments(LhatProgram *p, const char *module, const char *name, LhatHostTypeArgumentFn read, void *context)
{{
    (void)p;
    (void)context;
    printf("%s.%s type arguments\\n", module, name);
    argued = read;
    return true;
}}
LhatRuntimeType *mock_type(void) {{ return (LhatRuntimeType *)&program; }}
void mock_contract(void *pointer)
{{
    retained(pointer, NULL);
    retained(pointer, NULL);
    released(pointer, NULL);
    printf("%d %d\\n", argued(pointer, 0, NULL) == NULL, argued(pointer, 1, NULL) == NULL);
}}
static LhatValue text(const char *t) {{ LhatValue v; lhat_machine_make_string(NULL, t, strlen(t), &v); return v; }}
void mock_run(int32_t mode)
{{
    LhatValue sum[2] = {{lhat_integer(40), lhat_real(mode ? 1.5 : 2.0)}};
    LhatValue five = lhat_integer(5), yes = lhat_bool(true), name = text("from L^"), hi = text("hi");
    LhatInteger added = call("add", sum, 2).as.integer;
    double halved = call("half", &five, 1).as.real;
    const char *greeted = ((LhatString *)lhat_as_object(call("greet", &name, 1)))->text;
    bool flipped = call("flip", &yes, 1).as.boolean;
    printf("%lld %g %s %d\\n", (long long)added, halved, greeted, flipped);
    fflush(stdout);
    call("shout", &hi, 1);
}}
void mock_any(void)
{{
    LhatValue n = lhat_integer(42), s = text("hello"), seven = lhat_integer(7);
    LhatValue a = call("echo", &n, 1), b = call("echo", &s, 1), k = call("kind", &s, 1);
    LhatValue m = call("named", &seven, 1);
    printf("%lld %s %lld %s\\n", (long long)a.as.integer, ((LhatString *)lhat_as_object(b))->text,
           (long long)k.as.integer, ((LhatString *)lhat_as_object(m))->text);
}}
void mock_values(int32_t mode)
{{
    LhatValue xyz[3] = {{lhat_integer(1), lhat_integer(2), lhat_integer(3)}};
    LhatValue ones[3] = {{lhat_integer(1), lhat_integer(1), lhat_integer(1)}};
    LhatValue pair[2] = {{call("vec", xyz, 3), call("vec", ones, 3)}};
    LhatValue scale[2] = {{call("sum", pair, 2), lhat_real(2)}};
    LhatValue v = call("scaled", scale, 2);
    const float *f = lhat_hostvalue_data(v, ((MockValue *)v.as.object)->tag);
    printf("%g %g %g %g\\n", f[0], f[1], f[2], call("length", mode ? xyz : &v, 1).as.real);
}}
void mock_multi(void)
{{
    LhatValue arguments[2] = {{lhat_integer(17), lhat_integer(5)}}, answers[3];
    int count = 0;
    for (int j = 0; j < program.n; j++) {{
        if (!strcmp(program.names[j], "divmod")) {{
            program.fns[j](NULL, NULL, arguments, 2, answers, &count);
        }}
    }}
    printf("%d %lld %lld %s\\n", count, (long long)answers[0].as.integer, (long long)answers[1].as.integer,
           ((LhatString *)lhat_as_object(answers[2]))->text);
}}
void mock_data(LhatValue v, bool (*wrap)(LhatMachine *, void *, LhatValue *))
{{
    LhatValue bump[2] = {{v, lhat_integer(41)}}, both[2] = {{v, v}}, none[2] = {{v, lhat_nil()}}, again;
    long long bumped = (long long)call("bump", bump, 2).as.integer;
    LhatValue same = call("same", both, 2), missing = call("same", none, 2);
    void *pointer = ((MockData *)v.as.object)->pointer;
    wrap(NULL, pointer, &again);
    printf("%lld %d %d %d\\n", bumped, same.as.object == v.as.object, missing.tag == LHAT_VALUE_NIL,
           again.as.object == v.as.object);
    LhatValue picks[2][2] = {{{{v, lhat_integer(3)}}, {{v, lhat_integer(0)}}}}, answers[2][2];
    int counts[2] = {{0, 0}};
    for (int j = 0; j < program.n; j++) {{
        for (int k = 0; k < 2 && !strcmp(program.names[j], "pick"); k++) {{
            program.fns[j](NULL, NULL, picks[k], 2, answers[k], &counts[k]);
        }}
    }}
    printf("%d %d %lld %d %d\\n", counts[0], answers[0][0].as.object == v.as.object,
           (long long)answers[0][1].as.integer, counts[1], answers[1][0].tag == LHAT_VALUE_NIL);
    LhatValue finds[3][2] = {{{{v, lhat_integer(1)}}, {{v, lhat_integer(0)}}, {{v, lhat_integer(-1)}}}}, found[3];
    for (int j = 0; j < program.n; j++) {{
        for (int k = 0; k < 3 && !strcmp(program.names[j], "find"); k++) {{
            program.fns[j](NULL, NULL, finds[k], 2, &found[k], &counts[0]);
        }}
    }}
    printf("%d %d %s\\n", found[0].as.object == v.as.object, found[1].tag == LHAT_VALUE_NIL,
           ((MockError *)found[2].as.object)->kind->name);
    fflush(stdout);
    for (int j = 0; j < program.n; j++) {{
        if (!strcmp(program.names[j], "dispose")) {{
            program.fns[j](NULL, NULL, &v, 1, NULL, NULL);
        }}
    }}
    wrap(NULL, pointer, &again);
    printf("%d\\n", again.as.object != v.as.object);
}}
''', encoding='utf-8')
    lhat = '''includeC^ "lhat.h"
externC^ def^ LhatProgram;
externC^ mock_program:p^ -> ref^ LhatProgram;
externC^ mock_run:p^ mode:i32^;
externC^ rhat_lhat_register:p^ program:ref^ LhatProgram, module:string^ -> bool^;
exportL^ let^ add = f^ a:i64^, b:i32^ -> i64^ { a + i64^(b) }
exportL^ let^ half = f^ x:f64^ -> f64^ { x / 2.0 }
exportL^ let^ greet = f^ name:string^ -> string^ { name }
exportL^ let^ flip = f^ b:bool^ -> bool^ { !b }
exportL^ let^ shout = p^ text:string^ { print^("R^ says:", text) }
print^(rhat_lhat_register(mock_program(), "rhat"))
'''
    lhat_extra = ('--runtime', host, '--c-include', mock)
    positive('ffi_export_lhat', lhat + 'mock_run(0)\n',
             'rhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'true\n42 2.5 from L^ 0\nR^ says: hi\n', extra=lhat_extra)
    multi = lhat.replace('externC^ mock_run:p^ mode:i32^;', 'externC^ mock_multi:p^;') + \
        'exportL^ let^ divmod = f^ a:i64^, b:i64^ -> (i64^, i64^, string^) { return^ a / b, a % b, "ok" }\n'
    positive('ffi_export_lhat_multiple', multi + 'mock_multi()\n',
             'rhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'rhat.divmod f^number^, number^ -> number^, number^, string^;\ntrue\n3 3 2 ok\n', extra=lhat_extra)
    negative('ffi_export_lhat_multiple_text_nil', header + 'exportL^ let^ m = f^ -> (i64^, string^ | nil^) { return^ 1, nil^ }',
             'exportL^ result must be')
    negative('ffi_extern_multiple', 'externC^ div:f^ a:i32^, b:i32^ -> (i32^, i32^);\nlet^ q, r = div(7, 2)',
             'externC^ result must be')
    # Any L^ value passes through as LhatValue (any^); the calling machine comes first.
    anything = lhat.replace('externC^ mock_run:p^ mode:i32^;', '''externC^ mock_any:p^;
externC^ def^ LhatMachine;
externC^ def^ LhatValue { self^{ as:u64^ = 0, tag:i32^ = 0 } }
externC^ lhat_machine_make_string:p^ machine:ref^ LhatMachine, text:string^, length:u64^, out:ref^ LhatValue -> bool^;''') + '''exportL^ let^ echo = f^ v:LhatValue -> LhatValue { v }
exportL^ let^ kind = f^ v:LhatValue -> i32^ { v.tag }
exportL^ let^ named = p^ machine:ref^ LhatMachine, n:i64^ -> LhatValue {
  var^ out = LhatValue{}
  let^ made = lhat_machine_make_string(machine, "made", 4, ref^ out)
  return^ out
}
'''
    positive('ffi_export_lhat_any', anything + 'mock_any()\n',
             'rhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'rhat.echo f^any^ -> any^;\nrhat.kind f^any^ -> number^;\nrhat.named p^number^ -> any^;\n'
             'true\n42 hello 4 made\n', extra=lhat_extra)
    # exportL^"..." writes the L^ signature itself, for computed types R^'s types cannot say.
    positive('ffi_export_lhat_signature', anything + '''exportL^"p^c^{->*->*} -> std.task.Task<ARG0.ResultType>;" let^ wrap = p^ job:LhatValue -> LhatValue { return^ job }
mock_any()
''', 'rhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'rhat.echo f^any^ -> any^;\nrhat.kind f^any^ -> number^;\nrhat.named p^number^ -> any^;\n'
             'rhat.wrap p^c^{->*->*} -> std.task.Task<ARG0.ResultType>;\n'
             'true\n42 hello 4 made\n', extra=lhat_extra)
    # A module's exportL^ names drop the R^ module: L^ registers them under its own. One L^ name
    # can take several arms, a member its written signature, and a host data type the contracts
    # that share it across machines and give its type arguments.
    (suite.work / 'lhost').mkdir(parents=True, exist_ok=True)
    (suite.work / 'lhost' / 'shared.rh').write_text('''module^ lhost.shared
includeC^ "lhat.h"
externC^ def^ LhatRuntimeType;
externC^ mock_type:f^ -> ref^ read^ LhatRuntimeType;
public^ exportL^ ref^ def^ Job {
  self^{ n:i64^ = 0 }
  exportL^"f^self^ -> number^|nil^" count = f^ self^ -> i64^ { self^.n }
  retain = p^ self^ { self^.n += 1 }
  let_go = p^ self^ { self^.n -= 1 }
  type_argument = f^ self^, index:u64^ -> ref^ read^ LhatRuntimeType | nil^ {
    if^ index = 0 { return^ mock_type() }
    return^ nil^
  }
}
exportL^ let^ peek = f^ j:ref^ Job -> i64^ { j.n }
exportL^"start" "p^ -> number^" let^ start_default = p^ -> i64^ { return^ 2 }
exportL^"start" "p^number^ -> number^" let^ start_with = p^ n:i64^ -> i64^ { return^ n }
''', encoding='utf-8')
    contract = lhat.replace('externC^ mock_run:p^ mode:i32^;', '''let^ shared = import^lhost.shared
externC^ mock_contract:p^ j:ref^ shared.Job;''') + '''var^ j = shared.Job{n = 1}
mock_contract(ref^ j)
print^(j.n)
'''
    positive('ffi_export_lhat_contract', contract,
             'rhat.Job data\nrhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'rhat.peek f^rhat.Job -> number^;\nrhat.start p^ -> number^;\nrhat.start p^number^ -> number^;\n'
             'rhat.Job.count f^self^ -> number^|nil^;\nrhat.Job shared\nrhat.Job type arguments\n'
             'true\n0 1\n2\n', extra=lhat_extra)
    negative('ffi_export_lhat_machine_first', header + '''externC^ def^ LhatMachine;
exportL^ let^ late = f^ n:i64^, m:ref^ LhatMachine -> i64^ { n }''',
             'exportL^ takes the calling ref^ LhatMachine as its first parameter')
    # Each R^ module's exportL^ declarations also register on their own, under an L^ module the
    # host names.
    own = lhat.replace('print^(rhat_lhat_register(mock_program(), "rhat"))\n', '''externC^ rhat_lhat_register_main:p^ program:ref^ LhatProgram, module:string^ -> bool^;
print^(rhat_lhat_register_main(mock_program(), "mine"))
''')
    positive('ffi_export_lhat_per_module', own,
             'mine.add f^number^, number^ -> number^;\nmine.half f^number^ -> number^;\n'
             'mine.greet f^string^ -> string^;\nmine.flip f^bool^ -> bool^;\nmine.shout p^string^;\ntrue\n',
             extra=lhat_extra)
    suite.runtime_failure('ffi_export_lhat_whole', lhat + 'mock_run(1)\n', 'exportL^ add needs a whole number', extra=lhat_extra)
    values = lhat.replace('externC^ mock_run:p^ mode:i32^;', 'externC^ mock_values:p^ mode:i32^;') + '''exportL^ def^ Vec3 {
  self^{ x:f32^ = 0.0, y:f32^ = 0.0, z:f32^ = 0.0, on:bool^ = false^ }
  length = f^ self^ -> f64^ { f64^(self^.x * self^.x + self^.y * self^.y + self^.z * self^.z) }
  scaled = f^ self^, k:f64^ -> Vec3 { Vec3{x = self^.x * f32^(k), y = self^.y * f32^(k), z = self^.z * f32^(k)} }
}
exportL^ let^ vec = f^ x:f64^, y:f64^, z:f64^ -> Vec3 { Vec3{x = f32^(x), y = f32^(y), z = f32^(z)} }
exportL^ let^ sum = f^ a:Vec3, b:Vec3 -> Vec3 { Vec3{x = a.x + b.x, y = a.y + b.y, z = a.z + b.z} }
'''
    positive('ffi_export_lhat_value', values + 'mock_values(0)\n', 
             'rhat.Vec3 16 bytes\nrhat.Vec3.x at 0 as 0\nrhat.Vec3.y at 4 as 0\nrhat.Vec3.z at 8 as 0\n'
             'rhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'rhat.vec f^number^, number^, number^ -> rhat.Vec3;\nrhat.sum f^rhat.Vec3, rhat.Vec3 -> rhat.Vec3;\n'
             'rhat.Vec3.length f^self^ -> number^;\nrhat.Vec3.scaled f^self^, number^ -> rhat.Vec3;\n'
             'true\n4 6 8 116\n', extra=lhat_extra)
    suite.runtime_failure('ffi_export_lhat_value_type', values + 'mock_values(1)\n', 'exportL^ received another host value type', extra=lhat_extra)
    negative('ffi_export_lhat_value_flat', header + 'exportL^ def^ S { self^{ s:string^ = "" } }', "exportL^ def^ 'S' needs a flat def^")
    negative('ffi_export_lhat_struct_flat', header + 'exportL^ struct^ S { s:string^ = "" }',
             "exportL^ def^ 'S' needs a flat def^")
    negative('ffi_export_lhat_value_mutable', header + 'exportL^ def^ C { self^{ n:i32^ = 0 }\n  bump = f^ mutable^ self^ -> i32^ { self^.n += 1 return^ self^.n } }', "cannot change self^")
    data = lhat.replace('externC^ mock_run:p^ mode:i32^;', '''externC^ def^ LhatMachine;
externC^ def^ LhatValue { self^{ as:u64^ = 0, tag:i32^ = 0 } }
externC^ rhat_lhat_wrap_Counter:p^ machine:ref^ LhatMachine | nil^, c:ref^ Counter, out:ref^ LhatValue -> bool^;
externC^ mock_data:p^ v:LhatValue,
  wrap:externC^ p^ ref^ LhatMachine | nil^, ref^ Counter, ref^ LhatValue -> bool^;;''') + '''exportL^ ref^ def^ Counter {
  self^{ n:i64^ = 0 }
  bump = p^ self^, by:i64^ -> i64^ { self^.n += by return^ self^.n }
  dispose = p^ self^ { print^("dispose", self^.n) }
}
exportL^ let^ same = f^ a:ref^ Counter, b:ref^ Counter | nil^ -> ref^ Counter | nil^ { b }
exportL^ let^ pick = f^ a:ref^ Counter, k:i64^ -> (ref^ Counter | nil^, i64^) {
  if^ k > 0 { return^ a, k }
  return^ nil^, k
}
exportL^ errordef^ Miss { Gone }
exportL^ let^ find = f^ a:ref^ Counter, k:i64^ -> ref^ Counter | nil^ | Miss.Gone {
  if^ k < 0 { return^ Miss.Gone{} }
  if^ k = 0 { return^ nil^ }
  return^ a
}
var^ c = Counter{n = 1}
var^ v = LhatValue{}
print^(rhat_lhat_wrap_Counter(nil^, ref^ c, ref^ v))
mock_data(v, rhat_lhat_wrap_Counter)
print^(c.n)
'''
    positive('ffi_export_lhat_data', data,
             'rhat.Counter data\nrhat.Miss errors Gone\nrhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'rhat.same f^rhat.Counter, rhat.Counter|nil^ -> rhat.Counter|nil^;\n'
             'rhat.pick f^rhat.Counter, number^ -> rhat.Counter|nil^, number^;\n'
             'rhat.find f^rhat.Counter, number^ -> rhat.Counter|rhat.Miss.Gone|nil^;\n'
             'rhat.Counter.bump p^self^, number^ -> number^;\nrhat.Counter.dispose p^self^;\n'
             'true\ntrue\n42 1 1 1\n2 1 3 2 1\n1 1 Gone\ndispose 42\n1\n42\n', extra=lhat_extra)
    errors = 'import^ std.sys\n' + lhat.replace('externC^ mock_run:p^ mode:i32^;', 'externC^ mock_fail:p^;') + '''exportL^ errordef^ Error { Zero, Bad { self^{ code:i32^ = 0 } } }
exportL^ let^ check = f^ k:i64^ -> i64^ | Error {
  if^ k = 0 { let^ e:Error = Error.Zero{}
 return^ e }
  if^ k = 1 { let^ e:Error = Error.Bad{message = "bad one", code = 7}
 return^ e }
  return^ k * 10
}
exportL^ let^ split = f^ k:i64^ -> (i64^, i64^) | Error.Bad {
  if^ k = 1 { return^ Error.Bad{} }
  return^ k, k + 1
}
exportL^ let^ spare = f^ k:i64^ -> i64^ | std.sys.Error {
  if^ k = 2 { return^ std.sys.Error.OutOfMemory{message = "no room"} }
  return^ k
}
mock_fail()
'''
    positive('ffi_export_lhat_error', errors,
             'rhat.Error errors Zero Bad\nrhat.add f^number^, number^ -> number^;\nrhat.half f^number^ -> number^;\n'
             'rhat.greet f^string^ -> string^;\nrhat.flip f^bool^ -> bool^;\nrhat.shout p^string^;\n'
             'rhat.check f^number^ -> number^|rhat.Error;\n'
             'rhat.split f^number^ -> (number^, number^)|rhat.Error.Bad;\n'
             'rhat.spare f^number^ -> number^|std.sys.Error;\n'
             'true\ncheck Zero: Error.Zero\ncheck Bad: bad one\ncheck 1 20\n'
             'split 2 0\nsplit Bad: Error.Bad\nsplit 2 2\n'
             'spare 1 0\nspare 1 1\nspare std.sys.Error.OutOfMemory: no room\n', extra=lhat_extra)
    negative('ffi_export_lhat_error_unexported', header + 'errordef^ E { Bad }\nexportL^ let^ g = f^ -> i32^ | E { E.Bad{} }',
             "errordef^ 'E' needs exportL^ to fail an exportL^ result")
    negative('ffi_export_lhat_error_nil', header + 'exportL^ errordef^ E { Bad }\nexportL^ let^ g = f^ k:i64^ -> i64^ | nil^ | E.Bad { k }',
             'exportL^ result must be')
    negative('ffi_export_lhat_error_ref', header + 'exportL^ ref^ errordef^ E { Bad }', 'or exportL^ to a def^ or errordef^')
    negative('ffi_export_lhat_data_value', header + 'exportL^ ref^ def^ W { self^{ n:i32^ = 0 } }\nexportL^ let^ g = f^ w:W -> i32^ { 0 }',
             "exportL^ parameter 'w' needs")
    negative('ffi_export_lhat_ref_function', header + 'exportL^ ref^ let^ g = f^ x:i32^ -> i32^ { x }', 'or exportL^ to a def^ or errordef^')
    negative('ffi_export_lhat_no_header', 'exportL^ let^ g = f^ x:i32^ -> i32^ { x }', 'exportL^ needs a module with includeC^')
    negative('ffi_export_lhat_param', header + 'externC^ def^ FfiBox;\nexportL^ let^ g = f^ b:ref^ FfiBox -> i32^ { 0 }',
             "exportL^ parameter 'b' needs a number, bool^, string^, exportL^ def^ or LhatValue type annotation")
    negative('ffi_export_lhat_result', header + 'exportL^ let^ g = f^ x:i32^ -> i32^[..] { {} }', 'exportL^ result must be')
    negative('ffi_export_lhat_template', header + 'exportL^ let^ g = template^ f^ x:i32^ -> i32^ { x }', 'exportL^ function cannot be template^')

    # externC^ function types: C function pointers to externC^ and exportC^ functions.
    positive('ffi_function_pointer', '''externC^ qsort:p^ base:i32^[..], n:u64^, size:u64^, compare:externC^ f^ ref^ i32^, ref^ i32^ -> i32^;;
exportC^ let^ descending = f^ a:ref^ i32^, b:ref^ i32^ -> i32^ { *b - *a }
var^ xs:i32^[5] = {3, 1, 4, 1, 5}
qsort(xs[0 .. 5], 5, 4, descending)
print^(xs[0], xs[1], xs[2], xs[3], xs[4])
''', '5 4 3 1 1\n')
    positive('ffi_header_function_pointer', header + '''externC^ ffi_twice:f^ n:i64^ -> i64^;
externC^ ffi_apply:p^ fn:(externC^ f^ i64^ -> i64^;) | nil^, x:i64^ -> i64^;
exportC^ let^ r_negate = f^ n:i64^ -> i64^ { -n }
let^ twice:externC^ f^ i64^ -> i64^; = r_negate
print^(ffi_apply(r_negate, 5), ffi_apply(twice, 6), ffi_apply(nil^, 7))
''', '-5 -6 -1\n', extra=('--runtime', boxes))
    pointer = 'externC^ apply:p^ fn:externC^ f^ i64^ -> i64^;, x:i64^ -> i64^;\n'
    positive('ffi_function_pointer_alias', header + '''
def^Callback = externC^f^i64^ -> i64^;
externC^ffi_apply:p^fn:Callback | nil^, x:i64^ -> i64^;
exportC^let^negate = f^n:i64^ -> i64^ { -n }
let^callback:Callback = negate
print^(ffi_apply(callback, 17), ffi_apply(nil^, 0))
''', '-17 -1\n', extra=('--runtime', boxes))
    positive('callable_signature_alias', '''
def^Function = f^i32^ -> i32^;
let^increment:Function = f^n:i32^ -> i32^ { n + 1 }
print^(increment(5))
''', '6\n')
    negative('ffi_function_pointer_signature', pointer + 'externC^ abs:f^ n:i32^ -> i32^;\nprint^(apply(abs, 1))', "externC^ f^ i64^ -> i64^;")
    negative('ffi_function_pointer_alias_signature', '''
def^Callback = externC^f^i64^ -> i64^;
externC^apply:p^fn:Callback, x:i64^ -> i64^;
externC^abs:f^n:i32^ -> i32^;
print^(apply(abs, 1))
''', 'externC^ f^ i64^ -> i64^;')
    negative('ffi_function_pointer_r_function', pointer + 'let^ g = f^ n:i64^ -> i64^ { n }\nprint^(apply(g, 1))', 'only externC^ and exportC^ functions have a C address')
    negative('ffi_function_pointer_spec', 'externC^ apply:p^ fn:externC^ i64^ -> i64^;', 'externC^ in a type needs a signature')
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
    negative('ffi_conflicting_symbol', 'let^ abs=externC^ abs:f^ n:i32^ ->i32^; & externC^ abs:f^ n:i64^ ->i64^;', 'another signature')
    negative('ffi_alias_conflict', 'let^ a = externC^ abs:f^ n:i32^ ->i32^;\nlet^ b = externC^ abs:f^ n:i64^ ->i64^;', 'another signature')
    negative('ffi_local', 'let^ g=p^ { let^ h = externC^ abs:f^ n:i32^ ->i32^; }', 'only allowed at the top level')
    negative('ffi_effect', 'externC^ puts:p^ s:string^ ->i32^;\nlet^ g=f^ ->i32^ { puts("x") }\nprint^(g())', "f^ cannot call p^ 'puts'")
    # C function pointers held by bindings and fields are called like C functions; an
    # exportC^ function called through one runs as a callback.
    positive('ffi_call_function_pointers', '''
externC^abs:f^x:i32^-> i32^;
externC^strlen:f^s:string^-> u64^;
def^ Ops { self^{ size:externC^f^string^-> u64^;, magnitude:externC^f^i32^-> i32^;, step:externC^p^ref^i64^; } }
exportC^let^bump = p^n:ref^i64^{ *n += 10 }
let^run = p^ ops:ref^Ops {
    var^k:i64^ = 1
    ops.step(ref^ k)
    ops.step(ref^ k)
    print^(ops.size("hello"), ops.magnitude(-7), k)
}
var^ops = Ops{size = strlen, magnitude = abs, step = bump}
run(ref^ ops)
let^g = ops.magnitude
print^(g(-3))
''', '5 7 21\n3\n')
    negative('ffi_pointer_effect', '''
externC^puts:p^s:string^-> i32^;
let^f = f^g:externC^p^string^-> i32^; -> i32^{ g("x") }
print^(f(puts))
''', 'f^ cannot call a p^ C function pointer')
    negative('ffi_pointer_arity', '''
externC^abs:f^x:i32^-> i32^;
let^g = abs
let^h = f^p:externC^f^i32^-> i32^; -> i32^{ p(1, 2) }
print^(h(abs))
''', 'this C function pointer takes 1 arguments, not 2')
    negative('ffi_pointer_struct_value', '''
def^ Point { self^{ x:i32^ = 0 } }
let^h = p^f:externC^p^Point; { f(Point{}) }
''', 'a C function pointer cannot take a def^ by value yet')
    # A thread C starts, not R^, gets data stacks of its own when it calls an exportC^ function.
    foreign = suite.work / 'ffi_foreign.c'
    foreign.write_text(f'''#include "{(suite.repo / 'runtime/rhat_runtime.c').as_posix()}"
typedef struct {{ void (*f)(int64_t); int64_t v; }} Job;
#if defined(_WIN32)
static DWORD WINAPI run(void *data) {{ Job *j = data; j->f(j->v); j->f(j->v + 1); return 0; }}
void ffi_on_thread(void (*f)(int64_t), int64_t v)
{{
    Job j = {{f, v}};
    HANDLE h = CreateThread(NULL, 0, run, &j, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}}
#else
static void *run(void *data) {{ Job *j = data; j->f(j->v); j->f(j->v + 1); return NULL; }}
void ffi_on_thread(void (*f)(int64_t), int64_t v)
{{
    Job j = {{f, v}};
    pthread_t t;
    pthread_create(&t, NULL, run, &j);
    pthread_join(t, NULL);
}}
#endif
''', encoding='utf-8')
    positive('ffi_foreign_thread_callback', '''
externC^ffi_on_thread:p^f:externC^p^i64^;, v:i64^;
exportC^let^work = p^n:i64^{
    var^xs = i64^[grow^]{n repeat^ 3}
    xs.push(n * 10)
    print^("foreign", xs.len, xs[3])
}
var^mine = i64^[grow^]{1, 2}
ffi_on_thread(work, 7)
mine.push(3)
print^("main", mine.len, mine[2])
''', 'foreign 4 70\nforeign 4 80\nmain 3 3\n', extra=('--runtime', foreign))
    # A std.task pool runs jobs of a kind given by C function pointers; the step is an exportC^
    # function, called on the workers.
    counter = suite.work / 'ffi_counter.c'
    counter.write_text(f'''#include "{(suite.repo / 'runtime/rhat_runtime.c').as_posix()}"
typedef struct {{ int64_t left, total; }} Counter;
void *counter_new(int64_t n) {{ Counter *c = calloc(1, sizeof *c); c->left = n; return c; }}
int64_t counter_tick(void *job) {{ Counter *c = job; if (c->left == 0) return -1; c->total += c->left--; return 0; }}
void *counter_finish(void *job) {{ int64_t *r = malloc(sizeof *r); *r = ((Counter *)job)->total; return r; }}
void counter_release(void *job) {{ free(job); }}
void counter_free(void *r) {{ free(r); }}
int64_t counter_value(void *r) {{ int64_t v = *(int64_t *)r; free(r); return v; }}
''', encoding='utf-8')
    positive('ffi_task_foreign_jobs', '''
let^task = import^std.task
externC^counter_new:p^n:i64^-> ref^task.ForeignJob;
externC^counter_tick:p^job:ref^task.ForeignJob -> i64^;
externC^counter_finish:p^job:ref^task.ForeignJob -> ref^task.ForeignResult | nil^;
externC^counter_release:p^job:ref^task.ForeignJob;
externC^counter_free:p^r:ref^task.ForeignResult;
externC^counter_value:p^r:ref^task.ForeignResult -> i64^;
exportC^let^counter_step = p^job:ref^task.ForeignJob -> i64^{ return^counter_tick(job) }
let^run = p^-> nil^| task.Error {
    var^pool = try^task.Pool.new(2)
    let^kind = task.JobKind{step = counter_step, finish = counter_finish, release = counter_release, free_result = counter_free}
    var^a = try^pool.submit(ref^kind, counter_new(100))
    var^b = try^pool.submit(ref^kind, counter_new(10))
    a.wait()
    b.wait()
    let^r = a.take()
    let^s = b.take()
    if^r fits^ref^task.ForeignResult { print^(counter_value(r)) }
    if^s fits^ref^task.ForeignResult { print^(counter_value(s)) }
    print^(a.take() fits^nil^)
    return^nil^
}
let^x = run()
''', '5050\n55\ntrue\n', extra=('--runtime', counter))
    suite.finish()


if __name__ == '__main__':
    main()
