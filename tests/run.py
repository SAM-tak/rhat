"""End-to-end semantic, provenance, IR, codegen and runtime checks."""
import argparse
import bisect
import math
import os
import pathlib
import re
import subprocess
import struct
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--clang", required=True)
parser.add_argument("--work", required=True)
args = parser.parse_args()
compiler = str(pathlib.Path(args.compiler).resolve())
work = pathlib.Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
repo = pathlib.Path(__file__).resolve().parents[1]
passed = 0


def run(command, expected=0, env=None):
    result = subprocess.run(command, capture_output=True, text=True, timeout=40, env=env)
    if (result.returncode == 0) != (expected == 0):
        raise AssertionError(f"{command}\nexit {result.returncode}\n{result.stdout}{result.stderr}")
    return result


def source(name, text):
    path = work / f"{name}.rh"
    path.write_text(text, encoding="utf-8")
    return path


def positive(name, text, output, inspect=None):
    global passed
    path = source(name, text)
    for opt in ("-O0", "-O2"):
        exe = work / f"{name}_{opt[1:]}.exe"
        ir = work / f"{name}_{opt[1:]}.rir"
        run([compiler, "build", str(path), "-o", str(exe), "--clang", args.clang,
             "--dump-ir", str(ir), opt])
        actual = run([str(exe)]).stdout
        assert actual == output, (name, opt, actual, output)
        if inspect:
            inspect(ir.read_text(), pathlib.Path(str(exe) + ".ll").read_text())
    passed += 1
    print(f"PASS {name}", flush=True)


def negative(name, text, diagnostic):
    global passed
    path = source(name, text)
    result = run([compiler, "check", str(path)], 1)
    assert diagnostic in result.stderr, (name, diagnostic, result.stderr)
    assert re.search(r":\d+:\d+: error:", result.stderr), result.stderr
    passed += 1
    print(f"PASS {name}", flush=True)


def runtime_failure(name, text, diagnostic):
    global passed
    path = source(name, text)
    for opt in ("-O0", "-O2"):
        exe = work / f"{name}_{opt[1:]}.exe"
        run([compiler, "build", str(path), "-o", str(exe), "--clang", args.clang, opt])
        result = run([str(exe)], 1)
        assert diagnostic in result.stderr, (name, result.stderr)
    passed += 1
    print(f"PASS {name}", flush=True)


def layout_ir(ir, ll):
    assert "type C size=12 align=4" in ir
    assert "+0 A.a : i32^" in ir
    assert "+4 B.b : i32^" in ir
    assert "+8 C.c : i32^" in ir
    assert "define void @rh_fn_0" in ll


def region_ir(ir, ll):
    assert "region.construct" in ir and "region.release" in ir
    assert "array.push" in ir and "index.checked" in ir
    assert "destination=%0" in ir
    assert "malloc" not in ll and "@rh_region_init" in ll


def top_cleanup_ir(ir, ll):
    assert "<top-level>" in ir
    assert ir.count("region.release") == 1
    assert ir.index("region.construct") < ir.index("region.release") < ir.index("  return")
    assert "@rh_region_release" in ll


def factorial_ir(ir, ll):
    assert "<anonymous@1:7> -> i64^" in ir
    body = ir.split("function rh_fn_1", 1)[1]
    assert "target=rh_fn_1" in body
    assert "branch.if" in body
    assert "call void @rh_fn_1" in ll


positive("empty_program", "", "")

positive("push_returns_stable_reference", '''
def^ Item { self^{ key:i32^, val:i32^ } }
var^ items:Item[>..]=[]
var^ marks:(&Item)[>..]=[]
let^ first=items.push(Item{key=7,val=11})
marks.push(first)
var^ i:i32^=0
while^ i < 2000 {
    let^ item=items.push(Item{key=i,val=i+1})
    if^ i % 64 = 0 { marks.push(item) }
    i := i+1
}
first.val := 42
print(items.len,marks.len,marks[0].key,marks[0].val,marks[32].val)
''', "2001 33 7 42 1985\n")
positive("reference_array_literals_and_grouped_types", '''
var^ x:i32^=10
var^ y:i32^=20
var^ a:(&i32^)[>..]=[&x,&y]
let^ b:(&i32^)[2]=[&x,&y]
let^ alias:&(&i32^)[>..]=&a
let^ pointer:&&i32^=a.push(&x);
**pointer := 30
print(*alias[0],*a[1],*b[0],a.len)
''', "30 20 30 3\n")
positive("reference_record_elements", '''
def^ Mark { self^{ ref:&i32^ } }
var^ x:i32^=7
var^ y:i32^=9
var^ a:Mark[>..]=[Mark{ref=&x}]
a.push(Mark{ref=&y});
*a[0].ref := 12
a[1] := Mark{ref=&x}
print(x,*a[1].ref)
''', "12 12\n")
positive("reference_array_owned_return", '''
let^ collect=p^ x:&i32^ -> (&i32^)[>..] {
    var^ refs:(&i32^)[>..]=[]
    refs.push(x)
    return^ refs
}
let^ literal=p^ x:&i32^ -> (&i32^)[>..] { return^ [x] }
let^ append=p^ a:&i32^[>..], x:i32^ { return^ a.push(x) }
var^ x:i32^=42
var^ refs=collect(&x)
var^ other=literal(&x)
var^ data:i32^[>..]=[]
let^ held=append(&data,7)
data.push(8)
print(*refs[0],*other[0],*held)
''', "42 42 7\n")
positive("static_string_array", '''
var^ strings:string^[>..]=["hello"]
let^ held=strings.push("world")
print(strings[0],*held,strings.len)
''', "hello world 2\n")
positive("reference_slot_mutation_preserves_scalars", '''
let^ f=p^ -> i32^ {
    var^ x:i32^=10
    var^ y:i32^=20
    var^ refs:(&i32^)[>..]=[&x]
    let^ slot=refs.push(&x);
    *slot := &y
    print(*refs[0],*refs[1])
    return^ x
}
print(f())
''', "10 20\n10\n")
negative("reference_slot_mutation_owned_escape", '''
let^ bad=p^ -> (&i32^)[>..] {
    var^ x:i32^=10
    var^ y:i32^=20
    var^ refs:(&i32^)[>..]=[&x]
    let^ slot=refs.push(&x);
    *slot := &y
    return^ refs
}
''', "cannot return a reference")
negative("push_inner_owner", '''
var^ marks:(&i32^)[>..]=[]
{ var^ x:i32^=1; marks.push(&x) }
''', "reference would outlive its owner")
negative("push_return_local_element", '''
let^ bad=p^ -> &i32^ { var^ a:i32^[>..]=[]; return^ a.push(1) }
''', "cannot return a reference")
negative("push_return_local_pointer_slot", '''
let^ bad=p^ x:&i32^ -> &&i32^ {
    var^ refs:(&i32^)[>..]=[]
    return^ refs.push(x)
}
''', "cannot return a reference")
negative("push_owned_return_local_references", '''
let^ bad=p^ -> (&i32^)[>..] {
    var^ x:i32^=1
    var^ refs:(&i32^)[>..]=[]
    refs.push(&x)
    return^ refs
}
''', "cannot return a reference")
negative("reference_array_literal_return_local", '''
let^ bad=p^ -> (&i32^)[>..] { var^ x:i32^=1; return^ [&x] }
''', "cannot return a reference")
negative("owned_reference_field_return_local", '''
def^ Box { self^{ refs:(&i32^)[>..] } }
let^ bad=p^ -> Box {
    var^ x:i32^=1
    var^ box=Box{refs=[&x]}
    return^ box
}
''', "cannot return a reference")
for kind, annotation in (("grow", "(&i32^)[>..]"), ("fixed", "(&i32^)[1]")):
    negative("reference_array_loop_alias_"+kind, '''
let^ bad=p^ source:&i32^ -> &i32^ {
    var^ local:i32^=9
    var^ refs:TYPE=[source]
    let^ alias=&refs
    var^ out=source
    var^ n:i32^=0
    while^ n < 2 {
        out := alias[0]
        refs[0] := &local
        n := n+1
    }
    return^ out
}
'''.replace("TYPE",annotation), "cannot return a reference")
negative("push_through_borrowed_reference_array", '''
let^ bad=p^ refs:&(&i32^)[>..], x:&i32^ { refs.push(x) }
''', "cannot store nonstatic references through borrowed storage")
negative("push_immutable", "let^ a:i32^[>..]=[]; a.push(1)", "cannot modify let^")
negative("push_wrong_result", "var^ a:i32^[>..]=[]; let^ x:i32^=a.push(1)", "push returns")
negative("grow_owned_elements", "var^ a:i32^[>..][>..]=[]", "fixed-size")
negative("push_loop_carried_reference_return", '''
let^ bad=p^ source:&i32^ -> (&i32^)[>..] {
    var^ local:i32^=9
    var^ ref=source
    var^ marks:(&i32^)[>..]=[]
    var^ n:i32^=0
    while^ n < 2 {
        marks.push(ref)
        ref := &local
        n := n+1
    }
    return^ marks
}
''', "cannot return a reference")
negative("push_loop_parameter_reference_return", '''
let^ bad=p^ source:&i32^ -> (&i32^)[>..] {
    var^ local:i32^=9
    var^ marks:(&i32^)[>..]=[]
    var^ n:i32^=0
    while^ n < 2 { marks.push(source); source := &local; n := n+1 }
    return^ marks
}
''', "cannot return a reference")
negative("push_loop_reference_chain", '''
let^ bad=p^ source:&i32^ -> (&i32^)[>..] {
    var^ local:i32^=9
    var^ first=source
    var^ second=source
    var^ marks:(&i32^)[>..]=[]
    var^ n:i32^=0
    while^ n < 3 {
        marks.push(first)
        first := second
        second := &local
        n := n+1
    }
    return^ marks
}
''', "cannot return a reference")
chain_declarations = "\n".join(f"var^ r{i}=x" for i in range(9))
chain_assignments = "\n".join(f"r{i} := r{i+1}" for i in range(8))
positive("reference_loop_fixed_point", '''
let^ collect=p^ x:&i32^, y:&i32^ -> (&i32^)[>..] {
    var^ marks:(&i32^)[>..]=[]
    DECLARATIONS
    var^ n:i32^=0
    while^ n < 12 {
        marks.push(r0)
        ASSIGNMENTS
        r8 := y
        n := n+1
    }
    return^ marks
}
var^ x:i32^=10
var^ y:i32^=20
var^ marks=collect(&x,&y)
print(marks.len,*marks[0],*marks[8],*marks[9],*marks[11])
'''.replace("DECLARATIONS",chain_declarations).replace("ASSIGNMENTS",chain_assignments), "12 10 10 20 20\n")

# Exercise all integer widths against Python's unbounded integer oracle.
integer_types = [(f"{sign}{bits}^", bits, sign == "u")
                 for sign in ("i", "u") for bits in (8, 16, 32, 64, 128)]
bit_code, bit_output = [], []
for name, bits, unsigned in integer_types:
    mask = (1 << bits) - 1
    value = mask if unsigned else -1
    bit_code.append(f"{{ let^ x:{name}={value}; print(bitnot^ x, x bitand^ 3, x bitor^ 3, x bitxor^ 3) }}")
    bit_output.append(f"0 3 {value} {mask - 3 if unsigned else -4}")
    for count in (0, 1, -1, bits - 1, 1 - bits, bits, -bits, 2147483647, -2147483648):
        result = 0 if abs(count) >= bits else ((mask << count) & mask if count >= 0 else mask >> -count)
        if not unsigned and result >= 1 << (bits - 1):
            result -= 1 << bits
        bit_code.append(f"{{ let^ x:{name}={value}; print(x bitshift^ {count}) }}")
        bit_output.append(str(result))
    for count in ("u128^(340282366920938463463374607431768211455)",
                  "i128^(-170141183460469231731687303715884105728)"):
        # A typed literal is used below; conversion operands otherwise infer i32.
        literal = count.split("(", 1)[1][:-1]
        typ = count.split("(", 1)[0]
        bit_code.append(f"{{ let^ x:{name}={value}; let^ n:{typ}={literal}; print(x bitshift^ n) }}")
        bit_output.append("0")
positive("integer_bits", "\n".join("(p^ {" + s + "})();" for s in bit_code), "\n".join(bit_output) + "\n")

conversion_code, conversion_output = [], []
for src, src_bits, src_unsigned in integer_types:
    values = (0, (1 << src_bits) - 1) if src_unsigned else (-1, -(1 << (src_bits - 1)), (1 << (src_bits - 1)) - 1)
    for v in values:
        for dst, dst_bits, dst_unsigned in integer_types:
            result = v & ((1 << dst_bits) - 1)
            if not dst_unsigned and result >= 1 << (dst_bits - 1):
                result -= 1 << dst_bits
            conversion_code.append(f"{{ let^ x:{src}={v}; print({dst}(x)) }}")
            conversion_output.append(str(result))
positive("integer_conversions", "\n".join("(p^ {" + s + "})();" for s in conversion_code), "\n".join(conversion_output) + "\n")

positive("float_conversions", '''
let^ inferred = f^ x:f64^ { i32^(x) }
print(inferred(-12.75), u8^(255.75), i8^(-128.0))
let^ a:i128^=-170141183460469231731687303715884105728
let^ b:u128^=340282366920938463463374607431768211455
print(f64^(a), f64^(b))
''', "-12 255 -128\n-1.7014118346046923e+38 3.4028236692093846e+38\n")
float_code = []
for src in ("f8^", "f16^", "f32^", "f64^"):
    for dst in ("f8^", "f16^", "f32^", "f64^"):
        float_code.append(f"{{ let^ x:{src}=1.5; print({dst}(x)) }}")
    for dst, _, _ in integer_types:
        float_code.append(f"{{ let^ x:{src}=12.0; print({dst}(x)) }}")
for src, _, unsigned in integer_types:
    for dst in ("f8^", "f16^", "f32^", "f64^"):
        float_code.append(f"{{ let^ x:{src}={12 if unsigned else -12}; print({dst}(x)) }}")
positive("numeric_conversion_matrix", "\n".join("(p^ {" + s + "})();" for s in float_code),
         ("1.5\n" * 4 + "12\n" * 10) * 4 + "-12\n" * 20 + "12\n" * 20)
positive("f8_direct_conversion", '''
print(f8^(1.0625000000000002), f8^(1.0624999999999998))
print(f8^(0.0009765625000000002), f8^(0.0009765624999999998))
''', "1.125 1\n0.001953125 0\n")
positive("bitwise_precedence", "print(2 + 3 bitand^ 1, bitnot^ 0 bitand^ 3, 8 bitshift^ -1 bitxor^ 3)", "3 3 7\n")
positive("numeric_side_effects_and_inference", '''
let^ bump=p^ x:&i32^ -> i32^ { *x := *x+1; return^ *x }
let^ amount=f^ { let^ n:i128^=-1; return^ n }
let^ shift=template^f^ x,n { x bitshift^ n }
var^ x:i32^=0
print(i64^(bump(&x)), x, shift(8,amount()))
print(8 bitshift^ amount())
''', "1 1 4\n4\n")
negative("bits_on_float", "print(1.0 bitxor^ 2.0)", "bitwise operands must be integers")
negative("bitnot_on_bool", "print(bitnot^ true^)", "invalid unary operand")
negative("conversion_arity", "print(i32^())", "numeric conversion expects one argument")
negative("conversion_string", 'print(i32^("12"))', "numeric conversion requires a number")
negative("conversion_result_mismatch", "let^ x:i64^=i32^(1)", "conversion result")
for name, value in (("negative_unsigned", "u8^(-0.5)"), ("too_large", "i8^(128.0)"),
                    ("too_negative", "i8^(-129.0)"), ("nan", "i32^(f8^(1000.0))"),
                    ("infinity", "i64^(1.0 / 0.0)"), ("wide_upper", "u128^(340282366920938463463374607431768211456.0)")):
    runtime_failure("float_cast_" + name, "print(" + value + ")", "floating value out of integer range")
if os.name == "nt":
    for opt in ("-O0", "-O2"):
        exe = work / ("runtime_regions_" + opt[1:] + ".exe")
        run([args.clang, "-std=c11", opt, str(repo / "tests/runtime_regions.c"), "-o", str(exe)])
        assert run([str(exe)]).stdout == "page commits verified\n"
        for mode, diagnostic in (("commit-failure", "region commit failed"),
                                 ("page-failure", "page size query failed"),
                                 ("capacity", "region capacity exceeded")):
            assert diagnostic in run([str(exe), mode], 1).stderr
    passed += 1
    print("PASS windows_page_commit_calls_and_failures", flush=True)

positive("page_crossing_elements", """
def^ Triple { self^{ a:u8^=1,b:u8^=2,c:u8^=3 } }
var^ a:Triple[>..]=[]
a.push(Triple.new())
let^ first=&a[0].a
var^ i:i32^=1
while^ i<3000 { a.push(Triple.new()); i:=i+1 }
a[0].a:=9
print(*first,a[2999].c,a.len)
def^ Large { self^{ bytes:u8^[10000], marker:i32^=42 } }
var^ b:Large[>..]=[]
b.push(Large.new())
b.push(Large{marker=99})
b.push(Large.new())
print(b[0].marker,b[1].marker,b[2].bytes[9999],b.len)
""", "9 3 3000\n42 99 0 3\n")
positive("numeric_example", (repo / "examples/numeric.rh").read_text(),
         "true 42\n-128 340282366920938463463374607431768211455 "
         "-170141183460469231731687303715884105728\n"
         "1.125 1.0009765625 1.0000001192092896\n")

# Exercise the generated arithmetic, full-width formatting and compiler-rt's
# i128 division ABI, including values that cannot travel through i64/double.
for signed in (False, True):
    for bits in (8, 16, 32, 64, 128):
        name = ("i" if signed else "u") + str(bits)
        kind = name + "^"
        maximum = 2 ** (bits - int(signed)) - 1
        minimum = -(2 ** (bits - 1)) if signed else 0
        wrap = minimum if signed else 0
        positive("integer_" + name, f"""
let^ high:{kind}={maximum}
let^ low:{kind}={minimum}
let^ one:{kind}=1
let^ seven:{kind}=7
let^ two:{kind}=2
let^ wrapped=high+one
assert(wrapped={wrap})
let^ product=high*two
assert(product={-2 if signed else maximum-1})
print(low,high,high/seven,high%seven)
""", f"{minimum} {maximum} {maximum//7} {maximum%7}\n")
        negative("integer_overflow_" + name, f"let^ n:{kind}={maximum+1}", "does not fit")
        negative("integer_underflow_" + name, f"let^ n:{kind}={minimum-1}", "does not fit")
        runtime_failure("division_zero_" + name,
                        f"let^ n:{kind}=7; let^ z:{kind}=0; print(n/z)", "invalid integer division")
        if signed:
            runtime_failure("division_overflow_" + name,
                            f"let^ n:{kind}={minimum}; let^ d:{kind}=-1; print(n/d)",
                            "invalid integer division")
            runtime_failure("remainder_overflow_" + name,
                            f"let^ n:{kind}={minimum}; let^ d:{kind}=-1; print(n%d)",
                            "invalid integer remainder")

for name in ("void", "bool", "u8", "u16", "u32", "u64", "u128", "i8", "i16", "i32", "i64", "i128", "f8", "f16", "f32", "f64", "string"):
    negative("no_bare_type_" + name, f"let^ x:{name}=0", "unknown type")
    negative("reserved_type_" + name, f"def^ {name}^ {{}}", "cannot be redeclared")

positive("plain_type_names_are_user_symbols", """
def^ bool { self^{ n:i8^=7 } }
def^ u8 { self^{ n:u16^=8 } }
def^ string { self^{ text:string^="user" } }
let^ i32=f^ f64:i32^ { f64+1 }
let^ f16=41
print(bool.new().n,u8.new().n,string.new().text,i32(f16))
""", "7 8 user 42\n")
positive("mixed_numeric_layout", """
def^ MixedNumeric {
    self^{ first:u8^=1, wide:i128^=170141183460469231731687303715884105727,
           half:f16^=1.5, single:f32^=2.25, counter:u64^=18446744073709551615 }
}
var^ rows:MixedNumeric[>..]=[]
rows.push(MixedNumeric.new())
rows.push(MixedNumeric{first=2,wide=-170141183460469231731687303715884105728})
print(rows[0].first,rows[0].wide,rows[0].half,rows[0].single,rows[0].counter)
print(rows[1].first,rows[1].wide)
""", "1 170141183460469231731687303715884105727 1.5 2.25 18446744073709551615\n"
         "2 -170141183460469231731687303715884105728\n",
         lambda ir, ll: ("type MixedNumeric size=48 align=16" in ir) or
         (_ for _ in ()).throw(AssertionError(ir)))

for name in ("i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "i128", "u128"):
    positive("index_" + name, f"let^ a=[10,20]; let^ i:{name}^=1; print(a[i])", "20\n")
runtime_failure("wide_unsigned_index", "let^ a=[1]; let^ i:u128^=18446744073709551616; print(a[i])", "out of bounds")
runtime_failure("high_u64_index", "let^ a=[1]; let^ i:u64^=18446744073709551615; print(a[i])", "out of bounds")
runtime_failure("negative_i128_index", "let^ a=[1]; let^ i:i128^=-1; print(a[i])", "out of bounds")

for kind in ("f8^", "f16^", "f32^", "f64^"):
    positive("float_ops_" + kind[:-1], f"""
let^ add=template^f^ a,b {{ a+b }}
let^ negative=f^ x:{kind} {{ -x }}
let^ a:{kind}=1.5
let^ b:{kind}=0.5
var^ values:{kind}[>..]=[a,b]
values.push(add(a,b))
print(a+b,a-b,a*b,a/b,negative(b),values[2])
assert(a>b and^ b<a and^ a>=a and^ b<=b and^ a!=b)
let^ zero:{kind}=0.0
let^ nan=zero/zero
assert(nan!=nan)
assert(!(nan=nan))
""", "2 1 0.75 3 -0.5 2\n")

positive("f8_rounding", """
let^ even:f8^=1.0625
let^ above:f8^=1.0625000000000000000000000000000000001
let^ odd:f8^=1.1875
let^ tiny:f8^=0.0009765625
let^ tiny_above:f8^=0.000976562500000000000000000000001
let^ maximum:f8^=448
let^ doubled=maximum+maximum
assert(doubled!=doubled)
print(even,above,odd,tiny,tiny_above)
""", "1 1.125 1.25 0 0.001953125\n")
positive("f16_rounding", """
let^ even:f16^=1.00048828125
let^ above:f16^=1.000488281250000000000000000000000001
let^ odd:f16^=1.00146484375
let^ tiny:f16^=0.0000000298023223876953125
let^ tiny_above:f16^=0.0000000298023223876953125000000000001
print(even,above,odd,tiny,tiny_above)
""", "1 1.0009765625 1.001953125 0 5.9604644775390625e-08\n")
positive("f32_rounding", """
let^ n:f32^=16777217
let^ x:f32^=1.000000059604644775390625
let^ y:f32^=1.000000059604644775390625000000001
print(n,x,y)
""", "16777216 1 1.0000001192092896\n")
positive("float_literal_conditional", """
let^ choose=f^ x:f16^ { if^ x>0: 0.5 el^: x; }
let^ n:f16^=-2.0
print(choose(n))
""", "-2\n")
negative("f8_literal_overflow", "let^ n:f8^=465.0", "does not fit 'f8^'")
negative("f16_literal_overflow", "let^ n:f16^=65520.0", "does not fit 'f16^'")
negative("f32_literal_overflow", "let^ n:f32^=4e38", "does not fit 'f32^'")
negative("f64_literal_overflow", "let^ n:f64^=1e309", "does not fit 'f64^'")
negative("f128_pending", "let^ n:f128^=1.0", "f128^ is pending")

# Independent nearest-neighbour FP8 oracle. Compare the software codec and
# all four operations over every pair of the 256 encodings, including zeros,
# subnormals and NaNs. The oracle enumerates values instead of using the
# runtime's exponent/shift/guard-bit algorithm.
def fp8_value(code):
    exponent, fraction = (code >> 3) & 15, code & 7
    value = (fraction / 512 if exponent == 0 else
             math.ldexp(1 + fraction / 8, exponent - 7))
    if exponent == 15 and fraction == 7:
        value = math.nan
    return -value if code & 128 else value


fp8_values = [fp8_value(n) for n in range(256)]
fp8_positive = fp8_values[:127]


def fp8_nearest(value):
    if not math.isfinite(value) or abs(value) > 464:
        return 127
    sign = 128 if math.copysign(1, value) < 0 else 0
    value = abs(value)
    hi = bisect.bisect_left(fp8_positive, value)
    if hi == 0:
        code = 0
    elif hi == 127:
        code = 126
    else:
        lo = hi - 1
        dl, dh = value - fp8_positive[lo], fp8_positive[hi] - value
        code = lo if dl < dh or (dl == dh and lo % 2 == 0) else hi
    return sign | code


midpoints = [(a+b)/2 for a, b in zip(fp8_positive, fp8_positive[1:])] + [464.0]
cast_values = [0.0, -0.0, math.inf, -math.inf, math.nan]
for middle in midpoints:
    for x in (math.nextafter(middle, -math.inf), middle, math.nextafter(middle, math.inf)):
        cast_values.extend((x, -x))
cast_bits = [struct.unpack("<Q", struct.pack("<d", x))[0] for x in cast_values]
cast_expected = [fp8_nearest(x) for x in cast_values]

expected = []
for operation in range(4):
    for a in fp8_values:
        for b in fp8_values:
            value = (a + b if operation == 0 else a - b if operation == 1 else
                     a * b if operation == 2 else a / b if b != 0 else math.nan)
            expected.append(fp8_nearest(value))
decode_bits = [struct.unpack("<I", struct.pack("<f", value))[0] for value in fp8_values]
c_path = work / "fp8_exhaustive.c"
c_path.write_text("""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
float rh_f8_decode(uint8_t);
uint8_t rh_f8_encode(float);
uint8_t rh_f8_encode_f64(double);
static const uint32_t decoded[256]={""" + ",".join(map(str, decode_bits)) + "};\n"
    "static const unsigned char expected[]={" + ",".join(map(str, expected)) + "};\n"
    "static const uint64_t cast_bits[]={" + ",".join(f"UINT64_C({x})" for x in cast_bits) + "};\n"
    "static const unsigned char cast_expected[]={" + ",".join(map(str, cast_expected)) + "};\n" + """
int main(void) {
    for (unsigned j=0;j<sizeof(cast_expected);j++) {
        double value;
        memcpy(&value,&cast_bits[j],8);
        unsigned actual=rh_f8_encode_f64(value),want=cast_expected[j];
        if ((want&127)==127 ? (actual&127)!=127 : actual!=want) {
            fprintf(stderr,"FP8 cast mismatch j=%u actual=%u expected=%u",j,actual,want);
            return 1;
        }
    }
    for (unsigned a=0;a<256;a++) {
        float value=rh_f8_decode((uint8_t)a);
        uint32_t bits;
        memcpy(&bits,&value,4);
        if (bits!=decoded[a]) return 1;
    }
    for (unsigned op=0;op<4;op++) for (unsigned a=0;a<256;a++) for (unsigned b=0;b<256;b++) {
        volatile float x=rh_f8_decode((uint8_t)a),y=rh_f8_decode((uint8_t)b);
        float result=op==0?x+y:op==1?x-y:op==2?x*y:x/y;
        unsigned actual=rh_f8_encode(result), want=expected[op*65536+a*256+b];
        if ((want&127)==127 ? (actual&127)!=127 : actual!=want) {
            fprintf(stderr,"FP8 mismatch op=%u a=%u b=%u actual=%u expected=%u",op,a,b,actual,want);
            return 1;
        }
    }
    return 0;
}
""", encoding="utf-8")
for opt in ("-O0", "-O2"):
    exe = work / ("fp8_exhaustive_" + opt[1:] + ".exe")
    run([args.clang, "-std=c11", opt, str(c_path), str(repo / "runtime/rhat_runtime.c"), "-o", str(exe)])
    run([str(exe)])
passed += 1
print("PASS fp8_exhaustive_262144_operations", flush=True)

# Keep the new ports executable at both optimization levels in the main suite.
sys.path.insert(0, str(repo / "bench"))
from run_workloads import WORKLOADS, oracle as workload_oracle
def benchmark_source(workload, n):
    text = (repo / f"bench/rhat/{workload}.rh").read_text(encoding="utf-8")
    text, hits = re.subn(r"(?m)(=.*?)(\d+)(\s*# BENCH_N)$",
                        lambda m: m[1] + str(n) + m[3], text)
    assert hits == 1
    return text


for workload, spec in WORKLOADS.items():
    text = benchmark_source(workload, spec["smoke"])
    positive("bench_" + workload, text, workload_oracle(workload, spec["smoke"]) + "\n")
for workload, sizes in (("tree", (0, 1)), ("graph", (1, 2, 17))):
    for n in sizes:
        positive(f"bench_{workload}_{n}", benchmark_source(workload, n), workload_oracle(workload, n) + "\n")
positive("declarations_only", "let^ unused=p^ { print(99) } def^ Empty {}", "")
positive("ordinary_main", "let^ main=p^ { print(99) } print(1)", "1\n")
positive("explicit_main", "let^ main=f^ n:i32^ -> i32^ { return^ n+1 } print(main(41))", "42\n")
positive("interleaved_declarations", """
print(1)
later()
def^ Value { self^{ n:i32^=3 } }
let^ value=Value.new()
let^ later=p^ { print(2) }
print(value.n)
""", "1\n2\n3\n")
positive("top_return", """
var^ scratch:i32^[>..]=[7]
print(scratch[0])
return^
let^ unused=p^ { print(99) }
""", "7\n", top_cleanup_ir)
negative("top_return_value", "return^ 1", "cannot return a value")
positive("layout", (repo / "examples/layout.rh").read_text(), "0 42 12\n", layout_ir)
positive("nominal_template", (repo / "examples/nominal_template.rh").read_text(),
         "R^ Person\nR^ Person\nfile.rh\n")
positive("regions", (repo / "examples/regions.rh").read_text(), "10 2003\n200\n", region_ir)
positive("factorial", (repo / "examples/factorial.rh").read_text(), "3628800\n", factorial_ir)
positive("factorial_boundaries", """
let^ factorial = f^ n:i64^ { if^ n < 2: 1 el^: n * this^(n - 1); }
print(factorial(0), factorial(1), factorial(10), factorial(20))
""", "1 1 3628800 2432902008176640000\n")
positive("inferred_return", """
let^ twice=f^ n:i64^ { n * 2 }
let^ wrap=f^ n:i64^ { twice(n) }
let^ explicit=f^ n:i64^ { return^ n + 1 }
let^ result=wrap(21)
print(result,explicit(result))
""", "42 43\n")
positive("anonymous_value_binding", """
let^ a=f^ n:i64^ { n*2 }(21)
let^ b=(f^ n:i64^ { n+1 })(a)
print(a,b)
""", "42 43\n")
positive("nested_this", """
print(f^ n:i64^ {
    if^ n < 2: 1
    el^: (f^ m:i64^ { if^ m = 0: 0 el^: 1 + this^(m - 1); }(2)) * this^(n - 1);
}(4))
""", "8\n")
positive("mutual_return_inference", """
let^ even=f^ n:i32^ { if^ n=0: true^ el^: odd(n-1); }
let^ odd=f^ n:i32^ { if^ n=0: false^ el^: even(n-1); }
print(even(10),odd(10),odd(11))
""", "true false true\n")
positive("conditional_chain", """
let^ sign=f^ n:i64^ {
    if^ n < 0: -1 el^ n = 0: 0 el^: 1;
}
print(sign(-5),sign(0),sign(7))
print(if^ true^: if^ false^: 1 el^: 2; el^: 3;)
""", "-1 0 1\n2\n")
positive("conditional_lazy", """
let^ choose=p^ n:i32^ -> i32^ { print(n); return^ n }
let^ a=if^ true^: choose(1) el^: choose(2);
let^ b=if^ false^: choose(3) el^: choose(4);
print(a,b)
""", "1\n4\n1 4\n")
positive("implicit_record_return", """
def^ Pair { self^{ a:i32^=0,b:i32^=0 } }
let^ make=f^ n:i32^ { Pair{a=n,b=n+1} }
let^ p=make(8)
print(p.a,p.b)
""", "8 9\n")
positive("inferred_trait_implementation", """
def^ Reader { abstract^ read:f^self^ -> i64^; }
def^ Value:Reader { read=f^self^ { 42 } }
let^ v=Value.new()
print(v.read())
""", "42\n")
positive("template_inferred_return", """
let^ sum=template^f^ n { if^ n=0: 0 el^: n + this^(n-1); }
let^ large:i64^=20
print(sum(10),sum(large))
""", "55 210\n")
positive("anonymous_templates", """
def^ Named { name=f^self^ { "anonymous" } }
template^p^ item { print(item.name()) }(Named.new())
print(template^f^ n { if^ n=0: 1 el^: n*this^(n-1); }(5))
let^ value=template^f^ n { n+1 }(41)
print(value)
let^ run=p^ { template^p^ n { print(n) }(7) }
run()
""", "anonymous\n120\n42\n7\n")
positive("template_methods", """
def^ Printer {
    show=template^p^ self^,item { print(item.name()) }
    twice=template^f^ self^,n { n+n }
}
def^ Item { name=f^self^ { "method" } }
var^ printer=Printer.new()
printer.show(Item.new())
let^ large:i64^=21
print(printer.twice(large))
""", "method\n42\n")
negative("template_old_declaration", """
template^ let^ use=p^ x {}
""", "expected 'f^'")
negative("template_old_method", """
def^ A { template^ use=p^ self^,x {} }
""", "expected '='")
negative("template_requires_function", """
let^ value=template^42
""", "expected 'f^'")
positive("recursive_region_cleanup", """
let^ sum=f^ n:i32^ -> i32^ {
    var^ scratch:i32^[>..]=[n]
    if^ n=0 { return^ scratch[0] }
    return^ scratch[0] + this^(n-1)
}
print(sum(10))
""", "55\n")
positive("recursive_borrow", """
let^ retain=f^ p:&i32^, n:i32^ -> &i32^ {
    if^ n=0 { return^ p }
    return^ this^(p,n-1)
}
var^ n:i32^=42
let^ ref=retain(&n,4)
print(*ref)
""", "42\n")
positive("method_this_receiver", """
def^ Counter {
    abstract^ unit:f^self^ -> i64^;
    total=f^self^, n:i64^ { if^ n=0: 0 el^: self^.unit() + this^(n-1); },
}
def^ Double:Counter { unit=f^self^ { 2 } }
let^ counter=Double.new()
print(counter.total(3))
""", "6\n")
positive("conditional_owned_destination", """
let^ make=f^ b:bool^ -> i32^[>..] { if^ b: [1] el^: [2,3]; }
var^ a=make(false^)
var^ b=make(true^)
print(a.len,a[0],a[1],b.len,b[0])
""", "2 2 3 1 1\n")
negative("recursive_return_unknown", "let^ bad=f^ n:i32^ { return^ this^(n) }", "cannot infer return type")
negative("conditional_missing_else", "print(if^ true^: 1;)", "requires a final el^:")
negative("conditional_missing_end", "print(if^ true^: 1 el^: 2)", "expected ';'")
negative("conditional_type_mismatch", 'print(if^ true^: 1 el^: "text";)', "inferred types disagree")
negative("conditional_nonbool", "print(if^ 1: 2 el^: 3;)", "condition requires bool")
negative("this_at_top", "this^()", "requires an enclosing function")
negative("this_arity", "print(f^ n:i32^ -> i32^ { this^() }(1))", "expects 1 arguments")
negative("this_argument_type", 'print(f^ n:i32^ -> i32^ { this^("bad") }(1))', "requires nominal 'i32^'")
negative("anonymous_local_borrow", "let^ ref=f^ -> &i32^ { var^ n:i32^=1; return^ &n }()", "cannot return a reference")
negative("recursive_local_borrow", """
let^ bad=f^ p:&i32^, n:i32^ -> &i32^ {
    if^ n=0 { return^ p }
    var^ local:i32^=1
    return^ this^(&local,n-1)
}
""", "cannot return a reference")
negative("anonymous_capture", "var^ outer:i32^=3; print(f^ -> i32^ { outer }())", "unknown value 'outer'")
negative("implicit_void_return", "let^ nothing=f^ {} let^ bad=f^ { nothing() }", "cannot return a value")
negative("no_implicit_last_statement", "let^ bad=f^ -> i32^ { let^ n:i32^=1; n+1 }", "only calls can be expression statements")
negative("no_implicit_procedure_return", "let^ bad=p^ -> i32^ { 1 }", "only calls can be expression statements")
positive("control_arrays", """
let^ choose = f^ n:i32^ -> i32^ {
    if^ n < 0 { return^ -1 } else^ { return^ n * 2 }
}
var^ xs : i32^[3] = [1, 2, 3]
xs[1] := choose(4)
var^ i:i64^ = 0
while^ i < xs.len { print(xs[i]); i := i + 1 }
assert(choose(-2) = -1)
assert((false^ and^ (1 / 0 = 0)) = false^)
assert(true^ or^ (1 / 0 = 0))
""", "1\n8\n3\n")
positive("private_inheritance", """
def^ A {
    self^{ private^ secret:i32^ = 7 },
    read = f^self^ -> i32^ { return^ self^.secret },
}
def^ B : A { self^{ other:i32^ = 9 } }
let^ b = B.new(); print(b.read())
""", "7\n")
positive("return_borrow", """
let^ identity = f^ x:&i32^ -> &i32^ { return^ x }
var^ n:i32^ = 9; let^ r=identity(&n); print(*r)
""", "9\n")
positive("record_return", """
def^ Pair { self^{ a:i32^ = 0, b:i32^ = 0 } }
let^ pair = f^ n:i32^ -> Pair { return^ Pair{ a=n, b=n+1 } }
let^ p=pair(20); print(p.a,p.b)
""", "20 21\n")
positive("region_tail", """
def^ Buffer { self^{ tag:i32^ = 7, private^ data:i32^[>..] = [1,2] } }
let^ b=Buffer.new(); print(b.tag)
""", "7\n")
positive("early_cleanup", """
let^ inspect = f^ n:i32^ -> i32^ {
    var^ scratch:i32^[>..] = [5]
    if^ n = 0 { return^ scratch[0] }
    { var^ inner:i32^[>..] = [3]; assert(inner[0] = 3) }
    return^ 9
}
print(inspect(0),inspect(1))
""", "5 9\n")
positive("owned_direct_return", """
let^ make = f^ -> i32^[>..] { return^ [3,4] }
var^ a:i32^[>..]=make(); print(a[1])
""", "4\n")
positive("evaluation_order", """
let^ change = p^ x:&i32^ -> i32^ { *x := 99; return^ 2 }
let^ combine = p^ a:i32^, b:i32^ { print(a,b) }
var^ n:i32^=1
print(n + change(&n))
n := 1
combine(n,change(&n))
n := 1
print(n,change(&n))
""", "3\n1 2\n1 2\n")
positive("numeric", """
let^ big:i64^=9223372036854775807
let^ byte:u8^=255
print(big,byte,1.5 * 2.0,true^)
let^ low:i32^=-2147483648
assert(low - 1 = 2147483647)
print(7 / 2,7 % 2)
""", "9223372036854775807 255 3 true\n3 1\n")
positive("paths with spaces", 'print(42)', "42\n")
positive("reference_mutation", """
let^ set=p^ p:&i32^ { *p := 8 }
var^ n:i32^=1; let^ r=&n; set(r); *r := 9; print(n)
""", "9\n")
positive("mutable_receiver", """
def^ Counter {
    self^{ n:i32^=0 },
    add=f^mutable^self^ -> i32^ { self^.n := self^.n + 1; return^ self^.n },
}
var^ c=Counter.new(); print(c.add(),c.add())
""", "1 2\n")
positive("diamond", """
def^ A { self^{ n:i32^=5 } }
def^ B:A {}
def^ C:A {}
def^ D:B,C {}
let^ d=D.new(); print(d.n)
""", "5\n")
positive("explicit_method_conflict", """
def^ A { n=f^self^ -> i32^ { return^ 1 } }
def^ B { n=f^self^ -> i32^ { return^ 2 } }
def^ C:A,B { override^ n=f^self^ -> i32^ { return^ 3 } }
let^ c=C.new(); print(c.n())
""", "3\n")
positive("trait_implementation", """
def^ Contract {
    self^{ abstract^ n:i32^ },
    abstract^ read:f^self^ -> i32^;
}
def^ Implementation {
    self^{ n:i32^=42 },
    read=f^self^ -> i32^ { return^ self^.n },
}
def^ Combined:Contract,Implementation {}
def^ Reversed:Implementation,Contract {}
let^ use=p^ v:Contract { print(v.read()) }
use(Combined.new()); use(Reversed.new())
""", "42\n42\n")
positive("mixed_layout", """
def^ Mixed { self^{ a:u8^=3, b:i64^=100, c:i32^[2]=[7,8] } }
let^ m=Mixed.new(); print(m.a,m.b,m.c[1])
""", "3 100 8\n", lambda ir, ll: (
    "type Mixed size=24 align=8" in ir and "+8 Mixed.b : i64^" in ir
) or (_ for _ in ()).throw(AssertionError(ir)))
negative("nominal_mismatch", """
def^ Named { abstract^ name:f^self^ -> string^; }
def^ File { name=f^self^ -> string^ { return^ "file" } }
let^ use=p^ x:Named { print(x.name()) }
use(File.new())
""", "requires nominal 'Named'")
negative("template_missing_member", """
def^ Empty {}
let^ use=template^p^ x { print(x.name()) }
use(Empty.new())
""", "has no method 'name'")
negative("template_keeps_nominal", """
def^ A {}
def^ B {}
let^ use=template^p^ x:A {}
use(B.new())
""", "requires nominal 'A'")
negative("private", 'def^ A { self^{ private^ x:i32^=1 } }\nprint(A.new().x)', "private member")
negative("template_private", """
def^ A { self^{ private^ x:i32^=1 } }
let^ use=template^p^ a { print(a.x) }
use(A.new())
""", "private member")
negative("derived_private", """
def^ A { self^{ private^ x:i32^=1 } }
def^ B:A { read=f^self^ -> i32^ { return^ self^.x } }

""", "private member")
negative("abstract_instance", 'def^ A { self^{ abstract^ x:i32^ } }\nlet^ a=A.new()', "cannot construct abstract")
negative("wrong_abstract_type", """
def^ A { self^{ abstract^ x:i32^ } }
def^ B:A { self^{ x:i64^=0 } }

""", "cannot replace its inherited slot")
negative("wrong_override", """
def^ A { abstract^ foo:f^self^ -> i32^; }
def^ B:A { foo=f^self^ -> i64^ { return^ 1 } }

""", "changes its contract")
negative("composition_cycle", 'def^ A:B {} def^ B:A {}\n', "cyclic def^ composition")
negative("layout_cycle", 'def^ A { self^{ a:A } }\n', "recursive by-value layout")
negative("duplicate_field", 'def^ A { self^{ a:i32^=1,a:i32^=2 } }\n', "duplicate field")
negative("local_borrow_return", 'let^ bad=f^ -> &i32^ { var^ x:i32^=1; return^ &x }\n', "cannot return a reference")
negative("inner_borrow_escape", """
var^ outer:i32^=1
var^ ref=&outer
{ var^ inner:i32^=2; ref := &inner }
print(*ref)
""", "reference would outlive its owner")
negative("aggregate_borrow_return", """
def^ View { self^{ p:&i32^ } }
let^ bad=f^ -> View { var^ x:i32^=1; return^ View{p=&x} }

""", "cannot return a reference")
negative("argument_borrow_escape", """
let^ id=f^ p:&i32^ -> &i32^ { return^ p }
var^ a:i32^=1; var^ r=&a
{ var^ b:i32^=2; r := id(&b) }
""", "reference would outlive its owner")
negative("borrowed_store_escape", """
def^ View { self^{ p:&i32^ } }
let^ bad=p^ v:&View { var^ x:i32^=1; v.p := &x }

""", "reference would outlive its owner")
negative("owned_copy", 'var^ a:i32^[>..]=[]; let^ b=a', "owned region values cannot be copied")
negative("owned_reassign", 'var^ a:i32^[>..]=[]; a := [1]', "owned values cannot be reassigned")
negative("grow_not_tail", 'def^ A { self^{ a:i32^[>..], b:i32^=1 } }\n', "final field")
negative("reference_default_array", 'def^ A { self^{ a:&i32^[2] } }\nlet^ a=A.new()', "needs an initializer")
negative("pure_mutation", 'let^ bad=f^ x:&i32^ { *x := 2 }\n', "cannot mutate borrowed storage")
negative("pure_procedure", 'let^ bad=f^ { print(1) }\n', "print requires p^")
negative("missing_return", 'let^ bad=f^ x:i32^ -> i32^ { if^ x=0 { return^ 1 } }\n', "not all paths return")
negative("immutable", 'let^ x:i32^=1; x := 2', "cannot modify let^")
negative("closure", '{ let^ f=f^ { return^ 1 } }', "function values")
negative("literal_overflow", 'let^ x:i32^=2147483648', "does not fit")
negative("unknown_type", 'let^ x:Missing=1', "unknown type")
negative("recursive_default", 'def^ A { self^{ n:i32^=A.new().n } }\nlet^ a=A.new()', "recursive default construction")
negative("default_capture", 'def^ A { self^{ n:i32^=local } }\nlet^ local:i32^=1; let^ a=A.new()', "cannot capture caller locals")
negative("void_parameter", 'let^ bad=f^ x:void^ {}\n', "parameter cannot have void")
negative("false_mutable_self", 'let^ bad=f^mutable^ x:i32^ {}\n', "requires an f^ method receiver")
negative("method_conflict", 'def^ A { n=f^self^ -> i32^ { return^ 1 } } def^ B { n=f^self^ -> i32^ { return^ 2 } } def^ C:A,B {}\n', "is ambiguous")
negative("inherited_field_method", 'def^ A { self^{ n:i32^=0 } } def^ B { n=f^self^ -> i32^ { return^ 2 } } def^ C:A,B {}\n', "field/method name collision")
negative("private_abstract_field", 'def^ A { self^{ private^ abstract^ n:i32^ } }\n', "private abstract fields")
negative("indirect_borrow_store", """
def^ View { self^{ p:&i32^ } }
def^ Context { self^{ view:&View, source:&i32^ } }
let^ bad=p^ c:&Context { c.view.p := c.source }

""", "cannot store nonstatic references through borrowed storage")
negative("pure_mutable_call", """
def^ Counter {
    self^{ n:i32^=0 },
    add=f^mutable^self^ { self^.n := 1 },
}
let^ bad=f^ c:&Counter { c.add() }

""", "cannot mutate borrowed storage")
negative("syntax", "let^ main=p^ {", "unterminated block")
runtime_failure("bounds", 'let^ a=[1,2]; print(a[2])', "out of bounds")
runtime_failure("negative_index", 'let^ a=[1]; print(a[-1])', "out of bounds")
runtime_failure("division", 'print(1 / 0)', "invalid integer division")
runtime_failure("division_overflow", 'let^ low:i32^=-2147483648; print(low / -1)', "invalid integer division")
runtime_failure("assertion", 'assert(false^)', "assertion failed")
runtime_failure("grow_bounds", 'var^ a:i32^[>..]=[]; print(a[0])', "out of bounds")
path = source("preserve_input", '')
before = path.read_bytes()
run([compiler, "emit", str(path), "-o", str(path)], 1)
assert path.read_bytes() == before
passed += 1
print("PASS preserve_input", flush=True)

# Reproduce a new terminal: no RHAT_CLANG and no Clang directory on PATH.
clean_env = dict(os.environ)
clean_env.pop("RHAT_CLANG", None)
clang_dir = pathlib.Path(args.clang).resolve().parent
clean_env["PATH"] = os.pathsep.join(
    part for part in clean_env.get("PATH", "").split(os.pathsep)
    if part and pathlib.Path(part).resolve() != clang_dir
)
path = source("driver_default", "print(42)")
exe = work / "driver_default.exe"
for _ in range(2):
    run([compiler, "build", str(path), "-o", str(exe), "-O2"], env=clean_env)
    assert run([str(exe)]).stdout == "42\n"
passed += 1
print("PASS default_clang_and_rebuild", flush=True)

bad_env = dict(clean_env)
missing_clang = str(work / "does-not-exist-clang.exe")
bad_env["RHAT_CLANG"] = missing_clang
result = run([compiler, "build", str(path), "-o", str(exe)], 1, env=bad_env)
assert "does-not-exist-clang.exe" in result.stderr, result.stderr
assert "LLVM IR retained at" in result.stderr, result.stderr
assert pathlib.Path(str(exe) + ".ll").exists()
run([compiler, "build", str(path), "-o", str(exe), "--clang", args.clang], env=bad_env)
assert run([str(exe)]).stdout == "42\n"
passed += 1
print("PASS clang_override_precedence_and_diagnostic", flush=True)
print(f"{passed} integration cases passed (positive/runtime cases at -O0 and -O2)")
