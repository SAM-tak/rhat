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
parser.add_argument("--filter", default="", help="Run matching named cases (regular expression)")
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
    if args.filter and not re.search(args.filter, name):
        return
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
    if args.filter and not re.search(args.filter, name):
        return
    path = source(name, text)
    result = run([compiler, "check", str(path)], 1)
    assert diagnostic in result.stderr, (name, diagnostic, result.stderr)
    assert re.search(r":\d+:\d+: error:", result.stderr), result.stderr
    passed += 1
    print(f"PASS {name}", flush=True)


def runtime_failure(name, text, diagnostic):
    global passed
    if args.filter and not re.search(args.filter, name):
        return
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
    assert "define internal void @rh_fn_0" in ll


def region_ir(ir, ll):
    assert "region.construct" in ir and "stack.restore" in ir
    assert "array.push" in ir and "index.checked" in ir
    assert "destination=%0" in ir
    assert "malloc" not in ll and "@rh_runtime_init" in ll


def top_cleanup_ir(ir, ll):
    assert "<top-level>" in ir
    assert ir.count("stack.restore") == 1
    assert ir.index("region.construct") < ir.index("stack.restore") < ir.index("  return")
    assert "stack.save" in ir


def factorial_ir(ir, ll):
    assert "<anonymous@1:7> -> i64^" in ir
    body = ir.split("function rh_fn_1", 1)[1]
    assert "target=rh_fn_1" in body
    assert "branch.if" in body
    assert "define internal i64 @rh_fn_1(i64 %sp, i64 %arg0)" in ll
    assert "call i64 @rh_fn_1" in ll


def scalar_check_ir(ir, ll):
    assert "@rh_check_division" not in ll
    assert "@rh_division_fail" in ll
    assert "division_ok_" in ll

positive("division_single_evaluation", '''
let^ denominator=p^ counter:ref^ i32^ -> i32^ { *counter:=*counter+1; return^ 97 }
var^ counter:i32^=0
print(1234 % denominator(ref^ counter),counter)
''', "70 1\n", scalar_check_ir)
for bits in (8,16,32,64,128):
    low=-(1 << (bits-1)); high=(1 << (bits-1))-1
    positive("direct_signed_division_"+str(bits), f'''
let^ divide=f^ a:i{bits}^, b:i{bits}^ -> i{bits}^ {{ return^ a/b }}
let^ remain=f^ a:i{bits}^, b:i{bits}^ -> i{bits}^ {{ return^ a%b }}
print(divide({low},2),remain({low},3),divide({high},-1),remain(-7,3))
''', f"{low//2} {-((-low)%3)} {-high} -1\n", scalar_check_ir)
for op,label in (("/","division"),("%","remainder")):
    runtime_failure("direct_dynamic_"+label+"_overflow", f'''
let^ calculate=f^ a:i128^, b:i128^ -> i128^ {{ return^ a {op} b }}
print(calculate(-170141183460469231731687303715884105728,-1))
''', "invalid integer "+label)
    runtime_failure("direct_dynamic_"+label+"_zero", f'''
let^ calculate=f^ a:u128^, b:u128^ -> u128^ {{ return^ a {op} b }}
print(calculate(340282366920938463463374607431768211455,0))
''', "invalid integer "+label)

def direct_index_ir(ir, ll):
    for helper in ("rh_array_at", "rh_view_at", "rh_bounds", "rh_index_integer"):
        assert "@" + helper not in ll, helper
    assert "icmp ult" in ll and "@rh_index_fail" in ll


for kind in ("i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "i128", "u128"):
    positive("direct_index_" + kind, f'''
var^ fixed:i32^[2]=[10,20]
var^ grow:grow^ i32^[]=[30,40]
let^ view=grow[:: 2]
let^ text="ab"
let^ index:{kind}^=1
fixed[index]:=21
grow[index]:=41
print(fixed[index],grow[index],view[index],text[index])
''', "21 41 41 98\n", direct_index_ir)

for name,setup,access in (
    ("grow", "var^ a:grow^ i32^[]=[1,2]", "a[i]"),
    ("view", "var^ a:grow^ i32^[]=[1,2]; let^ v=a[:: 2]", "v[i]"),
    ("string", 'let^ text="ab"', "text[i]"),
):
    for suffix,kind,value in (("wide", "u128", "18446744073709551617"),
                              ("negative", "i128", "-170141183460469231731687303715884105728")):
        runtime_failure("direct_index_"+name+"_"+suffix,
                        setup+f"; let^ i:{kind}^={value}; print({access})", "out of bounds")


def high_water(name, text, expected, output, append_only=False):
    """Observe actual retained tops in generated code, including loop back-edges.

    R^ references alone cannot reveal a lowered top: pages stay committed.
    The runtime assertions deliberately observe the hidden storage invariant.
    """
    global passed
    if args.filter and not re.search(args.filter, name):
        return
    wrapper = work / (name + "_runtime.c")
    expected_c = ",".join(map(str, expected))
    wrapper.write_text(f'''
#define rh_print_integer rh_base_print_integer
#define rh_print_newline rh_base_print_newline
#define rh_runtime_shutdown rh_base_shutdown
#include "{(repo / 'runtime/rhat_runtime.c').as_posix()}"
#undef rh_print_integer
#undef rh_print_newline
#undef rh_runtime_shutdown
static const uint64_t expected[] = {{{expected_c}}};
static unsigned step;
static int skip_newline;
void rh_print_integer(const void *value, int32_t bits, int32_t uns) {{
  int64_t marker=0;
  if (bits==64) memcpy(&marker,value,8);
  if (bits!=64 || uns || marker!=INT64_C(1234567890123456789)) {{
    rh_base_print_integer(value,bits,uns);
    return;
  }}
  if (step >= sizeof(expected)/sizeof(expected[0]) ||
      (uint64_t)(rh_stacks[0].top-rh_stacks[0].base) != expected[step++])
    rh_panic("generated code lost retained stack top", 0);
  skip_newline=1;
}}
void rh_print_newline(void) {{
  if (skip_newline) skip_newline=0;
  else rh_base_print_newline();
}}
void rh_runtime_shutdown(void) {{
  if (step != sizeof(expected)/sizeof(expected[0])) rh_panic("missing top observation",0);
  rh_base_shutdown();
}}
''', encoding="utf-8")
    # Assertions no longer call the runtime on success. Observe a deliberate
    # print checkpoint instead, while keeping the assertion itself meaningful.
    text = re.sub(r"assert\(([^()]*)\)",
                  r"assert(\1); do^{ let^ top_marker:i64^=1234567890123456789; print(top_marker) }", text)
    path = source(name, text)
    for opt in ("-O0", "-O2"):
        exe = work / (name + opt[1:] + ".exe")
        ir = work / (name + opt[1:] + ".rir")
        run([compiler,"build",str(path),"-o",str(exe),opt,"--clang",args.clang,
             "--runtime",str(wrapper),"--dump-ir",str(ir)])
        assert run([str(exe)]).stdout == output
        typed = ir.read_text()
        assert "top=append" in typed
        if append_only:
            assert "top=retained" not in typed
            llvm = pathlib.Path(str(exe)+".ll").read_text()
            bodies = re.findall(r"(?ms)^define internal [^\n]* @rh_fn_\d+\(.*?^}", llvm)
            assert bodies and all("udiv i64" not in body for body in bodies)
        else:
            assert "top=retained" in typed
    passed += 1
    print("PASS " + name, flush=True)


high_water("push_append_only_loop", '''
let^ next=f^ n:i32^ -> i32^ { return^ n+1 }
var^ a:grow^ i32^[]=[]
var^ i:i32^=0
repeat^ while^ i<30 { a.push(next(i)); i:=i+1 }
assert(a.len=30)
print(a[29])
''', [120], "30\n", append_only=True)
high_water("push_after_append", '''
var^ a:grow^ i32^[]=[1,2]
a.append(a[:: 2])
a.push(5)
assert(a.len=5)
print(a[4])
''', [20], "5\n", append_only=True)
high_water("borrowed_recursive_append_only", '''
let^ write=p^ a:ref^ grow^ i32^[], n:i32^ {
    if^ n > 0 { a.push(n); this^(a,n-1) }
}
var^ a:grow^ i32^[]=[]
write(ref^ a,3)
assert(a.len=3)
print(a[2])
''', [12], "1\n", append_only=True)
high_water("borrowed_append_after_caller_shrink", '''
let^ write=p^ a:ref^ grow^ i32^[] { a.push(9) }
var^ a:grow^ i32^[]=[1,2,3]
let^ view=a[:: 3]
a.clear()
write(ref^ a)
assert(a.len=1)
print(view[2])
''', [12], "3\n")
high_water("literal_append_keeps_high_water", '''
var^ a:grow^ u8^[]=[1,2,3,4,5,6]
a.append("abcd")
let^ held=a[:: 10]
a.clear()
a.append("xy")
assert(a.len=2)
a.push(3)
assert(a.len=3)
print(held[9])
''', [10,10], "100\n")
high_water("push_shrink_loop_backedge", '''
var^ a:grow^ i32^[]=[1,2,3,4]
let^ view=a[:: 4]
var^ i:i32^=0
repeat^ while^ i<3 { a.push(9); assert(true^); a.clear(); i:=i+1 }
print(view[3])
''', [20,20,20], "4\n")
high_water("push_shrink_branch_join", '''
var^ a:grow^ i32^[]=[1,2,3,4]
if^ true^ { a.truncate(1) else^: print(0) }
a.push(9)
assert(a.len=2)
print(a[1])
''', [16], "9\n")
high_water("push_shrink_alias", '''
var^ a:grow^ i32^[]=[1,2,3,4]
let^ ref=ref^ a[3]
var^ alias=ref^ a
alias.clear()
a.push(9)
assert(a.len=1)
print(*ref)
''', [16], "4\n")
high_water("push_after_two_pops", '''
var^ a:grow^ i32^[]=[1,2,3,4]
let^ ref=ref^ a[3]
a.pop()
a.pop()
a.push(9)
assert(a.len=3)
print(*ref)
''', [16], "4\n")
high_water("push_shrink_recursive_effect", '''
let^ cut=p^ a:ref^ grow^ i32^[], n:i32^ {
    if^ n=0 { a.clear() else^: again(a,n-1) }
}
let^ again=p^ a:ref^ grow^ i32^[], n:i32^ { cut(a,n) }
var^ a:grow^ i32^[]=[1,2,3,4]
cut(ref^ a,3)
a.push(9)
assert(a.len=1)
print(a[0])
''', [16], "9\n")
high_water("push_shrunken_owned_result", '''
let^ make=p^ -> grow^ i32^[] {
    var^ result:grow^ i32^[]=[1,2,3,4]
    result.clear()
    return^ result
}
var^ a=make()
a.push(9)
assert(a.len=1)
print(a[0])
''', [16], "9\n")

# A small real reservation exercises the generated byte-capacity guard at
# exact and non-divisible boundaries without allocating a GiB in the tests.
small_runtime = work / "small_array_runtime.c"
small_runtime.write_text('#define RH_STACK_BYTES 65536\n#include "'+
                         (repo/'runtime/rhat_runtime.c').as_posix()+'"\n', encoding="utf-8")
for name,decl,count,element in (
    ("byte", "var^ a:grow^ u8^[]=[]", 65536, "1"),
    ("triple", "def^ Triple { self^{x:u8^=1,y:u8^=2,z:u8^=3} } var^ a:grow^ Triple[]=[]", 21845, "Triple.new()"),
):
    path=source("push_capacity_"+name, decl+f"; var^ i:i32^=0; repeat^ while^ i<{count} {{ a.push({element}); i:=i+1 }} a.push({element})")
    for opt in ("-O0","-O2"):
        exe=work/("push_capacity_"+name+opt[1:]+".exe")
        run([compiler,"build",str(path),"-o",str(exe),opt,"--clang",args.clang,"--runtime",str(small_runtime)])
        assert "data stack overflow" in run([str(exe)],1).stderr
    passed+=1
    print("PASS push_capacity_"+name,flush=True)

positive("empty_program", "", "")

positive("string_tokens_remain_literals", 'print("(",")","[","]","{","}",",",";") let^ f="f^"; let^ p="p^"; let^ t="template^"; print(f,p,t,"true^","def^","+")', "( ) [ ] { } , ;\nf^ p^ template^ true^ def^ +\n")
layout_chain="def^ Large0 { self^{bytes:u8^[1000000]} }\n"+"\n".join(
    f"def^ Large{i} {{ self^{{left:Large{i-1},right:Large{i-1}}} }}" for i in range(1,46))
negative("checked_structure_layout",layout_chain,"layout overflows")

positive("truncate_retains_slots", """
var^ a:grow^ i32^[]=[10,20,30]
let^ held=ref^ a[2]
let^ view=a[:: 3]
a.truncate(1)
print(a.len,*held,view[2])
a.push(99)
a.push(42)
print(*held,view[2])
a.truncate(a.len)
a.truncate(0)
print(a.len,*held)
""", "1 30 30\n42 42\n0 42\n")
runtime_failure("truncate_negative", "var^ a:grow^ i32^[]=[1]; a.truncate(-1)", "truncate length out of bounds")
runtime_failure("truncate_growth", "var^ a:grow^ i32^[]=[1]; a.truncate(2)", "truncate length out of bounds")
negative("truncate_immutable", "let^ a:grow^ i32^[]=[1]; a.truncate(0)", "cannot modify let^")
negative("truncate_pure_borrow", "let^ bad=f^ a:ref^ grow^ i32^[] { a.truncate(0) }", "cannot mutate borrowed storage")
negative("truncate_arity", "var^ a:grow^ i32^[]=[1]; a.truncate()", "expects one new length")
negative("truncate_type", "var^ a:grow^ i32^[]=[1]; let^ n:i32^=0; a.truncate(n)", "must be i64^")
negative("truncate_retains_history", """
let^ bad=p^ -> grow^ (ref^ i32^)[] {
    var^ x:i32^=1
    var^ refs:grow^ (ref^ i32^)[]=[ref^ x]
    refs.truncate(0)
    return^ refs
}
""", "cannot return a reference")

union_types="""
def^ Num { self^{ value:i32^ } }
def^ Pair { self^{ a:u32^, b:u32^ } }
def^ Expr = Num | Pair
"""
positive("nominal_union", union_types+"""
let^ get=f^ value:Expr {
    if^ value fits^ Num { return^ value.value }
    let^ pair=value
    return^ i32^(pair.a+pair.b)
}
let^ choose=f^ n:i32^ -> Expr {
    if^ n > 0 { return^ Num{value=n} }
    return^ Pair{a=3,b=4}
}
let^ branch=f^ yes:bool^ -> Expr { return^ if^ yes: Num{value=9} el^: Pair{a=5,b=6}; }
var^ values:grow^ Expr[]=[Num{value=42}]
values.push(choose(0))
values.push(branch(true^))
print(get(values[0]),get(values[1]),get(values[2]),get(Num{value=5}),get(branch(false^)))
values[0] := Pair{a=7,b=8}
let^ view=values[:: 1]
print(get(view[0]),(values[0] fits^ Num))
""", "42 7 9 5 11\n15 false\n",
         lambda ir,ll: ("type Expr size=12 align=4" in ir and "union.project" in ir
                        and "payload=+4 bytes=8" in ir and "[2 x i32]" in ll)
         or (_ for _ in ()).throw(AssertionError("union IR layout")))
positive("union_bool_byte_payload", """
def^ Bool { self^{ value:bool^ } }
def^ Byte { self^{ value:u8^ } }
def^ Value = Bool | Byte
let^ identity=f^ v:Value { return^ v }
var^ values:grow^ Value[]=[Byte{value=255},Bool{value=true^},Byte{value=128}]
let^ a=identity(values[0])
let^ b=values[1] let^ c=values[2]
assert(a fits^ Byte) assert(b fits^ Bool) assert(c fits^ Byte)
print(a.value,b.value,c.value)
""", "255 true 128\n")
positive("union_high_alignment", """
def^ Small { self^{ value:u8^ } }
def^ Wide { self^{ value:u128^ } }
def^ Value = Small | Wide
def^ Holder { self^{ tag:u8^, value:Value, tail:u8^ } }
let^ identity=f^ v:Value { return^ v }
let^ fixed:Value[2]=[Wide{value=340282366920938463463374607431768211455},Small{value=255}]
var^ a:grow^ Value[]=[]
a.append(fixed[:: 2])
let^ copy=identity(a[0])
var^ h=Holder{tag=7,value=copy,tail=9}
let^ small=a[1]
assert(copy fits^ Wide) assert(small fits^ Small)
print(copy.value,small.value,h.tag,h.tail)
""", "340282366920938463463374607431768211455 255 7 9\n",
         lambda ir,ll: ("type Value size=32 align=16" in ir and "payload=+16 bytes=16" in ir)
         or (_ for _ in ()).throw(AssertionError("wide union layout")))
positive("union_reference_return", """
def^ Ref { self^{ value:ref^ i32^ } }
def^ Empty {}
def^ Value = Ref | Empty
let^ make=f^ x:ref^ i32^ -> Value { return^ Ref{value=x} }
let^ get=f^ value:Value { assert(value fits^ Ref) return^ value.value }
var^ x:i32^=42
let^ value=make(ref^ x)
let^ out=get(value);
*out := 77
print(x)
""", "77\n")
positive("union_nested_and_defaults", """
def^ A { self^{ value:i32^=7 } }
def^ B {}
def^ Inner = A | B
def^ Outer = Inner | B
def^ Box { self^{ value:Inner=A{} } }
let^ inner:Inner=A{}
let^ outer:Outer=inner
let^ box=Box{}
assert(!(outer fits^ B))
let^ nested=outer let^ boxed=box.value
assert(nested fits^ A) assert(boxed fits^ A)
print(nested.value,boxed.value)
""", "7 7\n")
negative("union_unproven_access", union_types+"let^ value:Expr=Num{value=1}; print(value.a)", "field access requires a structure")
negative("union_similar_is_nominal", union_types+"def^ Other { self^{value:i32^} } let^ value:Expr=Other{value=1}", "not an alternative")
negative("union_removed_get", union_types+"let^ value:Expr=Num{value=1}; value.get(Num)", "method call requires a structure")
negative("union_removed_is", union_types+"let^ value:Expr=Num{value=1}; value.is(Num)", "method call requires a structure")
negative("union_duplicate", "def^ A {} def^ Value = A | A", "duplicate union alternative")
negative("union_unknown", "def^ A {} def^ Value = A | Missing", "unknown union alternative")
negative("union_cycle", "def^ A = B def^ B = A", "cyclic")
negative("union_owned", "def^ A { self^{data:grow^ u8^[]} } def^ B {} def^ Value = A | B", "non-owning fixed-size")
negative("union_abstract", "def^ A { self^{abstract^x:i32^} } def^ B {} def^ Value = A | B", "must be concrete")
negative("union_composition", union_types+"def^ Bad : Expr {}", "composition base must be a structure")
negative("union_direct_constructor", union_types+"let^ value=Expr{}", "construct union")
negative("union_missing_initializer", union_types+"def^ Box { self^{value:Expr} } let^ b=Box{}", "needs an initializer")
negative("union_distinct_nominal_sum", union_types+"def^ Other = Num | Pair let^ a:Expr=Num{value=1}; let^ b:Other=a", "not an alternative")
negative("union_private_access", "def^ A { self^{private^x:i32^=7} } def^ B {} def^ Value = A | B let^ v:Value=A{}; assert(v fits^ A); print(v.x)", "private member")
negative("union_local_reference_escape", """
def^ Ref { self^{value:ref^ i32^} } def^ Empty {} def^ Value = Ref | Empty
let^ bad=p^ -> Value { var^ x:i32^=1; return^ Ref{value=ref^ x} }
""", "cannot return a reference")
negative("union_reference_storage_escape", """
def^ Ref { self^{value:ref^ i32^} } def^ Empty {} def^ Value = Ref | Empty
var^ v:Value=Empty{}
do^{ var^ x:i32^=1; v := Ref{value=ref^ x} }
""", "reference would outlive its owner")

positive("slice_fixed_grow_string", """
let^ a:i32^[4]=[10,20,30,40]
let^ middle=a[1 :: 3]
var^ b:grow^ i32^[]=[1]
b.append(middle)
let^ live=b[1 :: 3]
b[1] := 42
b.clear()
b.append(live)
print(middle.len,middle[0],live[0],b[0],b[1])
var^ bytes:grow^ u8^[]=[]
bytes.append("hello")
bytes.append("world"[1 :: 4])
print(bytes.len,bytes[5],"hello"[4],"abc"[3 :: 3].len)
""", "2 20 30 42 30\n8 111 111 0\n")
positive("grow_literal_and_range_slices", """
var^ g=grow^[10,20,30,40]
g.push(50)
let^ tail=g[3 ::]
let^ all=g[::]
let^ closed=g[1 to^ 3]
let^ empty=g[3 to^ 2]
let^ text:string^|nil^="hello"
print(g.len,tail.len,tail[0],all.len,closed.len,closed[2],empty.len,(text?[1 to^ 3])?.len??0)
""", "5 2 40 5 3 40 0 3\n")
negative("grow_unsized_type", "var^ a:i32^[]=[]", "needs grow^")
negative("grow_literal_fixed_target", "var^ a:i32^[2]=grow^[1,2]", "cannot initialize")
negative("grow_literal_empty", "var^ a=grow^[]", "needs a type annotation")
negative("slice_inclusive_open", "var^ a=grow^[1,2]; let^ v=a[1 to^]", "needs both bounds")
positive("slice_nested_return", """
let^ sub=f^ a:i32^[:] {
    let^ local=a[1 :: 3]
    return^ local[1 :: 2]
}
let^ borrowed=f^ a:ref^ grow^ i32^[] { return^ a[::] }
var^ a:grow^ i32^[]=[1,2,3,4]
let^ view=sub(borrowed(ref^ a))
a.push(5)
print(view.len,view[0])
let^ literal=f^ { return^ "hello"[1 :: 4] }
print(literal()[0])
""", "1 3\n101\n")
positive("slice_record_copy", """
def^ Item { self^{ value:i32^ } }
var^ a:grow^ Item[]=[Item{value=7}]
let^ view=a[:: 1]
var^ item=view[0]
item.value := 42
print(a[0].value,item.value)
""", "7 42\n")
positive("slice_reference_and_view_elements", """
var^ x:i32^=7
var^ refs:grow^ (ref^ i32^)[]=[ref^ x]
let^ view=refs[:: 1];
*view[0] := 42
var^ other:grow^ (ref^ i32^)[]=[]
other.append(view)
var^ views:grow^ i32^[:][]=[]
let^ values:i32^[2]=[10,20]
views.push(values[:: 2])
print(x,*other[0],views[0][1])
""", "42 42 20\n")
positive("slice_overlap_append", """
var^ a:grow^ u8^[]=[1,2,3,4]
let^ tail=a[2 :: 4]
a.clear()
a.append(tail)
a.append(a[:: 2])
a.append(a[a.len :: a.len])
print(a.len,a[0],a[1],a[2],a[3])
""", "4 3 4 3 4\n")
for name,text in (
    ("negative_start", "a[-1 :: 0]"),
    ("negative_end", "a[:: -1]"),
    ("past_start", "a[3 ::]"),
    ("past_end", "a[1 :: 3]"),
    ("reversed", "a[2 :: 1]"),
    ("inclusive_past_end", "a[0 to^ 2]"),
    ("overflow_range", "a[1 to^ 9223372036854775807]"),
):
    runtime_failure("slice_"+name, "var^ a:grow^ i32^[]=[1,2]; let^ v="+text, "slice range out of bounds")
runtime_failure("slice_index_bounds", "let^ a:i32^[2]=[1,2]; let^ v=a[:: 1]; print(v[1])", "array index out of bounds")
runtime_failure("string_index_bounds", 'print("abc"[3])', "array index out of bounds")
negative("slice_local_escape", "let^ bad=f^ -> i32^[:] { let^ a:i32^[2]=[1,2]; return^ a[:: 2] }", "cannot return a reference")
negative("slice_outer_escape", "var^ v=\"abc\"[:: 0]; do^{ var^ a:grow^ u8^[]=[1]; v := a[:: 1] }", "reference would outlive its owner")
negative("slice_write", "var^ a:grow^ i32^[]=[1]; var^ v=a[:: 1]; v[0] := 2", "not addressable")
negative("slice_borrow_element", "var^ a:grow^ i32^[]=[1]; let^ v=a[:: 1]; let^ p=ref^ v[0]", "cannot borrow a temporary")
negative("slice_field_write", "def^ Item { self^{x:i32^} } var^ a:grow^ Item[]=[Item{x=1}]; a[:: 1][0].x := 2", "not addressable")
negative("slice_temporary", "let^ v=[1,2][:: 2]", "slice source needs a binding")
negative("slice_index_type", "let^ a:i32^[1]=[1]; let^ n:i32^=0; a[n :: n+1]", "must be i64^")
negative("slice_initializer", "def^ Box { self^{ view:i32^[:] } } let^ b=Box{}", "needs an initializer")
negative("append_mismatch", 'var^ a:grow^ i32^[]=[]; a.append("hello")', "same element type")
negative("append_immutable", 'let^ a:grow^ u8^[]=[]; a.append("hello")', "cannot modify let^")
negative("append_pure_borrow", 'let^ bad=f^ a:ref^ grow^ u8^[] { a.append("hello") }', "cannot mutate borrowed storage")
negative("append_inner_owner", "var^ a:grow^ (ref^ i32^)[]=[]; do^{ var^ x:i32^=1; var^ b:grow^ (ref^ i32^)[]=[ref^ x]; a.append(b[:: 1]) }", "reference would outlive its owner")
negative("append_borrowed_references", "let^ bad=p^ a:ref^ grow^ (ref^ i32^)[], b:(ref^ i32^)[:] { a.append(b) }", "cannot store nonstatic references through borrowed storage")

def direct_pop_ir(ir, ll):
    assert "array.pop" in ir and "pop_ok_" in ll
    assert re.search(r"load i64, ptr %t\d+, !tbaa !5", ll)
    calls = [line for line in ll.splitlines() if "call void @rh_pop_fail(" in line]
    assert calls
    for call in calls:
        assert call + "\n  unreachable\npop_ok_" in ll


positive("pop_clear_slot_reuse", """
def^ Item { self^{ key:i32^, val:i64^ } }
let^ take=p^ a:ref^ grow^ Item[] { return^ a.pop() }
var^ a:grow^ Item[]=[]
a.clear()
let^ held=a.push(Item{key=1,val=42})
a.push(Item{key=2,val=99})
let^ last=take(ref^ a)
print(a.len,last.key,last.val)
a.clear()
print(a.len,held.key,held.val)
a.push(Item{key=3,val=77})
print(held.key,held.val,last.key,last.val)
let^ same=a.pop()
print(a.len,same.key,held.val)
""", "1 2 99\n0 1 42\n3 77 2 99\n0 3 77\n", direct_pop_ir)
positive("pop_copies_before_overlapping_assignment", """
def^ Pair { self^{ x:i64^, y:i64^ } }
var^ a:grow^ Pair[]=[Pair{x=1,y=2},Pair{x=3,y=4}]
let^ held=ref^ a[0]
let^ tail=a[1 :: 2]
a[0]:=a.pop()
print(a.len,held.x,held.y,tail[0].x,tail[0].y)
a.push(Pair{x=5,y=6})
print(tail[0].x,tail[0].y)
""", "1 3 4 3 4\n5 6\n", direct_pop_ir)
positive("typed_storage_keeps_descriptor_aliases", """
def^ Box { self^{ tag:i64^=7, items:grow^ i32^[] } }
let^ mutate=p^ a:ref^ grow^ i32^[], b:ref^ grow^ i32^[] {
    let^ old=a.len
    print(b.pop(),a.len,old)
    a.push(42)
    print(b.len,b[0],b[1])
}
var^ box=Box{items=[10,20]}
let^ held=ref^ box.items[1]
mutate(ref^ box.items,ref^ box.items)
print(box.tag,box.items.len,*held)
""", "20 1 2\n2 10 42\n7 2 42\n", direct_pop_ir)
positive("push_reuses_aliased_slot", """
var^ a:grow^ i32^[]=[42]
let^ held=ref^ a[0]
a.clear()
a.push(*held)
print(*held,a[0])
""", "42 42\n")
positive("pop_reference_elements", """
var^ x:i32^=10
var^ y:i32^=20
var^ refs:grow^ (ref^ i32^)[]=[ref^ x,ref^ y]
let^ last=refs.pop();
*last := 30
refs.clear()
refs.push(ref^ x)
print(y,*refs[0],refs.len)
""", "30 10 1\n")
positive("pure_local_array_pop_clear", """
let^ local=f^ {
    var^ a:grow^ i32^[]=[1,2]
    let^ result=a.pop()
    a.clear()
    return^ result
}
print(local())
""", "2\n")
positive("array_descriptor_field_abi", """
def^ Box { self^{ tag:i64^, items:grow^ i32^[] } }
let^ make=p^ -> Box { var^ b=Box{tag=7,items=[1,2,3]}; return^ b }
var^ box=make()
print(box.tag,box.items.pop(),box.items.len)
box.items.clear()
box.items.push(42)
print(box.items[0])
""", "7 3 2\n42\n", lambda ir,ll: (
    "type Box size=32 align=8" in ir and "+8 Box.items" in ir
    and "{ ptr, i64, ptr }" in ll
) or (_ for _ in ()).throw(AssertionError("array descriptor layout")))
runtime_failure("empty_pop", "var^ a:grow^ i32^[]=[]; a.pop()", "line 1: pop from empty array")
runtime_failure("pop_bounds", "var^ a:grow^ i32^[]=[1]; a.pop(); print(a[0])", "array index out of bounds")
runtime_failure("clear_bounds", "var^ a:grow^ i32^[]=[1]; a.clear(); print(a[0])", "array index out of bounds")
for method in ("pop", "clear"):
    negative(method+"_immutable", f"let^ a:grow^ i32^[]=[1]; a.{method}()", "cannot modify let^")
    negative(method+"_pure_borrow", f"let^ bad=f^ a:ref^ grow^ i32^[] {{ a.{method}() }}", "cannot mutate borrowed storage")
    negative(method+"_arity", f"var^ a:grow^ i32^[]=[1]; a.{method}(1)", "expects no arguments")
    negative(method+"_retains_reference_history", """
let^ bad=p^ -> grow^ (ref^ i32^)[] {
    var^ x:i32^=1
    var^ a:grow^ (ref^ i32^)[]=[ref^ x]
    a.METHOD()
    return^ a
}
""".replace("METHOD", method), "cannot return a reference")
negative("pop_wrong_result", "var^ a:grow^ i32^[]=[1]; let^ x:i64^=a.pop()", "pop returns")
negative("pop_local_reference_escape", """
let^ bad=p^ source:ref^ i32^ -> ref^ i32^ {
    var^ a:grow^ (ref^ i32^)[]=[source]
    return^ a.pop()
}
""", "cannot return a reference")

positive("push_returns_stable_reference", '''
def^ Item { self^{ key:i32^, val:i32^ } }
var^ items:grow^ Item[]=[]
var^ marks:grow^ (ref^ Item)[]=[]
let^ first=items.push(Item{key=7,val=11})
marks.push(first)
var^ i:i32^=0
repeat^ while^ i < 2000 {
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
var^ a:grow^ (ref^ i32^)[]=[ref^ x,ref^ y]
let^ b:(ref^ i32^)[2]=[ref^ x,ref^ y]
let^ alias:ref^ grow^ (ref^ i32^)[]=ref^ a
let^ pointer:ref^ ref^ i32^=a.push(ref^ x);
**pointer := 30
print(*alias[0],*a[1],*b[0],a.len)
''', "30 20 30 3\n")
positive("reference_record_elements", '''
def^ Mark { self^{ ref:ref^ i32^ } }
var^ x:i32^=7
var^ y:i32^=9
var^ a:grow^ Mark[]=[Mark{ref=ref^ x}]
a.push(Mark{ref=ref^ y});
*a[0].ref := 12
a[1] := Mark{ref=ref^ x}
print(x,*a[1].ref)
''', "12 12\n")
positive("reference_array_owned_return", '''
let^ collect=p^ x:ref^ i32^ -> grow^ (ref^ i32^)[] {
    var^ refs:grow^ (ref^ i32^)[]=[]
    refs.push(x)
    return^ refs
}
let^ literal=p^ x:ref^ i32^ -> grow^ (ref^ i32^)[] { return^ [x] }
let^ append=p^ a:ref^ grow^ i32^[], x:i32^ { return^ a.push(x) }
var^ x:i32^=42
var^ refs=collect(ref^ x)
var^ other=literal(ref^ x)
var^ data:grow^ i32^[]=[]
let^ held=append(ref^ data,7)
data.push(8)
print(*refs[0],*other[0],*held)
''', "42 42 7\n")
positive("static_string_array", '''
var^ strings:grow^ string^[]=["hello"]
let^ held=strings.push("world")
print(strings[0],*held,strings.len)
''', "hello world 2\n")
positive("reference_slot_mutation_preserves_scalars", '''
let^ f=p^ -> i32^ {
    var^ x:i32^=10
    var^ y:i32^=20
    var^ refs:grow^ (ref^ i32^)[]=[ref^ x]
    let^ slot=refs.push(ref^ x);
    *slot := ref^ y
    print(*refs[0],*refs[1])
    return^ x
}
print(f())
''', "10 20\n10\n")
negative("reference_slot_mutation_owned_escape", '''
let^ bad=p^ -> grow^ (ref^ i32^)[] {
    var^ x:i32^=10
    var^ y:i32^=20
    var^ refs:grow^ (ref^ i32^)[]=[ref^ x]
    let^ slot=refs.push(ref^ x);
    *slot := ref^ y
    return^ refs
}
''', "cannot return a reference")
negative("push_inner_owner", '''
var^ marks:grow^ (ref^ i32^)[]=[]
do^{ var^ x:i32^=1; marks.push(ref^ x) }
''', "reference would outlive its owner")
negative("push_return_local_element", '''
let^ bad=p^ -> ref^ i32^ { var^ a:grow^ i32^[]=[]; return^ a.push(1) }
''', "cannot return a reference")
negative("push_return_local_pointer_slot", '''
let^ bad=p^ x:ref^ i32^ -> ref^ ref^ i32^ {
    var^ refs:grow^ (ref^ i32^)[]=[]
    return^ refs.push(x)
}
''', "cannot return a reference")
negative("push_owned_return_local_references", '''
let^ bad=p^ -> grow^ (ref^ i32^)[] {
    var^ x:i32^=1
    var^ refs:grow^ (ref^ i32^)[]=[]
    refs.push(ref^ x)
    return^ refs
}
''', "cannot return a reference")
negative("reference_array_literal_return_local", '''
let^ bad=p^ -> grow^ (ref^ i32^)[] { var^ x:i32^=1; return^ [ref^ x] }
''', "cannot return a reference")
negative("owned_reference_field_return_local", '''
def^ Box { self^{ refs:grow^ (ref^ i32^)[] } }
let^ bad=p^ -> Box {
    var^ x:i32^=1
    var^ box=Box{refs=[ref^ x]}
    return^ box
}
''', "cannot return a reference")
for kind, annotation in (("grow", "grow^ (ref^ i32^)[]"), ("fixed", "(ref^ i32^)[1]")):
    negative("reference_array_loop_alias_"+kind, '''
let^ bad=p^ source:ref^ i32^ -> ref^ i32^ {
    var^ local:i32^=9
    var^ refs:TYPE=[source]
    let^ alias=ref^ refs
    var^ out=source
    var^ n:i32^=0
    repeat^ while^ n < 2 {
        out := alias[0]
        refs[0] := ref^ local
        n := n+1
    }
    return^ out
}
'''.replace("TYPE",annotation), "cannot return a reference")
negative("push_through_borrowed_reference_array", '''
let^ bad=p^ refs:ref^ grow^ (ref^ i32^)[], x:ref^ i32^ { refs.push(x) }
''', "cannot store nonstatic references through borrowed storage")
negative("push_immutable", "let^ a:grow^ i32^[]=[]; a.push(1)", "cannot modify let^")
negative("push_wrong_result", "var^ a:grow^ i32^[]=[]; let^ x:i32^=a.push(1)", "push returns")
negative("grow_owned_elements", "var^ a:grow^ (grow^ i32^[])[]=[]", "fixed-size")
negative("push_loop_carried_reference_return", '''
let^ bad=p^ source:ref^ i32^ -> grow^ (ref^ i32^)[] {
    var^ local:i32^=9
    var^ ref=source
    var^ marks:grow^ (ref^ i32^)[]=[]
    var^ n:i32^=0
    repeat^ while^ n < 2 {
        marks.push(ref)
        ref := ref^ local
        n := n+1
    }
    return^ marks
}
''', "cannot return a reference")
negative("push_loop_parameter_reference_return", '''
let^ bad=p^ source:ref^ i32^ -> grow^ (ref^ i32^)[] {
    var^ local:i32^=9
    var^ marks:grow^ (ref^ i32^)[]=[]
    var^ n:i32^=0
    repeat^ while^ n < 2 { marks.push(source); source := ref^ local; n := n+1 }
    return^ marks
}
''', "cannot return a reference")
negative("push_loop_reference_chain", '''
let^ bad=p^ source:ref^ i32^ -> grow^ (ref^ i32^)[] {
    var^ local:i32^=9
    var^ first=source
    var^ second=source
    var^ marks:grow^ (ref^ i32^)[]=[]
    var^ n:i32^=0
    repeat^ while^ n < 3 {
        marks.push(first)
        first := second
        second := ref^ local
        n := n+1
    }
    return^ marks
}
''', "cannot return a reference")
chain_declarations = "\n".join(f"var^ r{i}=x" for i in range(9))
chain_assignments = "\n".join(f"r{i} := r{i+1}" for i in range(8))
positive("reference_loop_fixed_point", '''
let^ collect=p^ x:ref^ i32^, y:ref^ i32^ -> grow^ (ref^ i32^)[] {
    var^ marks:grow^ (ref^ i32^)[]=[]
    DECLARATIONS
    var^ n:i32^=0
    repeat^ while^ n < 12 {
        marks.push(r0)
        ASSIGNMENTS
        r8 := y
        n := n+1
    }
    return^ marks
}
var^ x:i32^=10
var^ y:i32^=20
var^ marks=collect(ref^ x,ref^ y)
print(marks.len,*marks[0],*marks[8],*marks[9],*marks[11])
'''.replace("DECLARATIONS",chain_declarations).replace("ASSIGNMENTS",chain_assignments), "12 10 10 20 20\n")

# Exercise all integer widths against Python's unbounded integer oracle.
integer_types = [(f"{sign}{bits}^", bits, sign == "u")
                 for sign in ("i", "u") for bits in (8, 16, 32, 64, 128)]
bit_code, bit_output = [], []
for name, bits, unsigned in integer_types:
    mask = (1 << bits) - 1
    value = mask if unsigned else -1
    bit_code.append(f"do^{{ let^ x:{name}={value}; print(bitnot^ x, x bitand^ 3, x bitor^ 3, x bitxor^ 3) }}")
    bit_output.append(f"0 3 {value} {mask - 3 if unsigned else -4}")
    for count in (0, 1, -1, bits - 1, 1 - bits, bits, -bits, 2147483647, -2147483648):
        result = 0 if abs(count) >= bits else ((mask << count) & mask if count >= 0 else mask >> -count)
        if not unsigned and result >= 1 << (bits - 1):
            result -= 1 << bits
        bit_code.append(f"do^{{ let^ x:{name}={value}; print(x bitshift^ {count}) }}")
        bit_output.append(str(result))
    for count in ("u128^(340282366920938463463374607431768211455)",
                  "i128^(-170141183460469231731687303715884105728)"):
        # A typed literal is used below; conversion operands otherwise infer i32.
        literal = count.split("(", 1)[1][:-1]
        typ = count.split("(", 1)[0]
        bit_code.append(f"do^{{ let^ x:{name}={value}; let^ n:{typ}={literal}; print(x bitshift^ n) }}")
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
            conversion_code.append(f"do^{{ let^ x:{src}={v}; print({dst}(x)) }}")
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
        float_code.append(f"do^{{ let^ x:{src}=1.5; print({dst}(x)) }}")
    for dst, _, _ in integer_types:
        float_code.append(f"do^{{ let^ x:{src}=12.0; print({dst}(x)) }}")
for src, _, unsigned in integer_types:
    for dst in ("f8^", "f16^", "f32^", "f64^"):
        float_code.append(f"do^{{ let^ x:{src}={12 if unsigned else -12}; print({dst}(x)) }}")
positive("numeric_conversion_matrix", "\n".join("(p^ {" + s + "})();" for s in float_code),
         ("1.5\n" * 4 + "12\n" * 10) * 4 + "-12\n" * 20 + "12\n" * 20)
positive("f8_direct_conversion", '''
print(f8^(1.0625000000000002), f8^(1.0624999999999998))
print(f8^(0.0009765625000000002), f8^(0.0009765624999999998))
''', "1.125 1\n0.001953125 0\n")
positive("bitwise_precedence", "print(2 + 3 bitand^ 1, bitnot^ 0 bitand^ 3, 8 bitshift^ -1 bitxor^ 3)", "3 3 7\n")
positive("numeric_side_effects_and_inference", '''
let^ bump=p^ x:ref^ i32^ -> i32^ { *x := *x+1; return^ *x }
let^ amount=f^ { let^ n:i128^=-1; return^ n }
let^ shift=template^f^ x,n { x bitshift^ n }
var^ x:i32^=0
print(i64^(bump(ref^ x)), x, shift(8,amount()))
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
for bits in (8,16,32,64,128):
    for unsigned in (False,True):
        typ=('u' if unsigned else 'i')+str(bits)+'^'
        upper=math.ldexp(1.0,bits if unsigned else bits-1)
        lower=0.0 if unsigned else -upper
        last=math.nextafter(upper,0.0)
        literal=lambda x:format(x,'.17f')
        positive(f"inline_float_cast_{typ[:-1]}",
                 f'print({typ}({literal(lower)}),{typ}({literal(last)}))',
                 f'{int(lower)} {int(last)}\n',
                 lambda ir,ll: ('fcmp oge double' in ll and 'fcmp olt double' in ll and 'cast_bad_' in ll)
                 or (_ for _ in ()).throw(AssertionError('cast guard missing')))
        runtime_failure(f"inline_float_upper_{typ[:-1]}",f'print({typ}({literal(upper)}))',"floating value out of integer range")
        if not unsigned:
            runtime_failure(f"inline_float_lower_{typ[:-1]}",f'print({typ}({literal(math.nextafter(lower,-math.inf))}))',"floating value out of integer range")
if os.name == "nt":
    for opt in ("-O0", "-O2"):
        exe = work / ("runtime_regions_" + opt[1:] + ".exe")
        run([args.clang, "-std=c11", opt, str(repo / "tests/runtime_regions.c"), "-o", str(exe)])
        assert run([str(exe)]).stdout == "data stacks verified\n"
        for mode, diagnostic in (("commit-failure", "data stack commit failed"),
                                 ("page-failure", "page size query failed"),
                                 ("capacity", "data stack overflow"),
                                 ("append-capacity", "data stack overflow"),
                                 ("element-capacity", "data stack overflow"),
                                 ("truncate-growth", "truncate length out of bounds"),
                                 ("guard", "data stack overflow")):
            assert diagnostic in run([str(exe), mode], 1).stderr
        small = work / ("runtime_small_stack_" + opt[1:] + ".exe")
        run([args.clang, "-std=c11", opt, "-DRH_STACK_BYTES=65536",
             str(repo / "tests/runtime_regions.c"), "-o", str(small)])
        assert run([str(small), "small-limit"]).stdout == "small reservation verified\n"
        assert "data stack overflow" in run([str(small), "guard"], 1).stderr
    passed += 1
    print("PASS windows_page_commit_calls_and_failures", flush=True)

positive("page_crossing_elements", """
def^ Triple { self^{ a:u8^=1,b:u8^=2,c:u8^=3 } }
var^ a:grow^ Triple[]=[]
a.push(Triple.new())
let^ first=ref^ a[0].a
var^ i:i32^=1
repeat^ while^ i<3000 { a.push(Triple.new()); i:=i+1 }
a[0].a:=9
print(*first,a[2999].c,a.len)
def^ Large { self^{ bytes:u8^[10000], marker:i32^=42 } }
var^ b:grow^ Large[]=[]
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
var^ rows:grow^ MixedNumeric[]=[]
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
var^ values:grow^ {kind}[]=[a,b]
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
# emitted IR's exponent/shift/guard-bit algorithm.
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
fp8_source = source("fp8_codec_module", "print(f8^(1.5))")
fp8_module = work / "fp8_codec_module.ll"
run([compiler,"emit",str(fp8_source),"-o",str(fp8_module)])
codec = fp8_module.read_text()
functions = re.findall(r"(?ms)^define internal (?:float|i8) @rh_f8_.*?^}", codec)
assert len(functions)==3
fp8_external = work / "fp8_codec_external.ll"
fp8_external.write_text("\n".join(f.replace("define internal ","define ",1) for f in functions)+"\n")
for opt in ("-O0", "-O2"):
    exe = work / ("fp8_exhaustive_" + opt[1:] + ".exe")
    run([args.clang, "-std=c11", opt, str(c_path), str(fp8_external), "-Wno-override-module", "-o", str(exe)])
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


from artifacts import Executables, overwritten_rhat_rows
registry=Executables()
fixture=work/"binary-identity-fixture.exe"
fixture.write_bytes(b"original Rhat artifact")
registry.add(fixture)
try:
    registry.add(fixture)
except RuntimeError as error:
    assert "duplicate benchmark executable path" in str(error)
else:
    raise AssertionError("duplicate benchmark artifact was accepted")
fixture.write_bytes(b"replacement Rust artifact")
called=[]
try:
    registry.measure(fixture,lambda path: called.append(path))
except RuntimeError as error:
    assert "changed after build" in str(error)
else:
    raise AssertionError("replaced benchmark artifact was executed")
assert not called
fixture.write_bytes(b"original Rhat artifact")
def change_during_run(path):
    path.write_bytes(b"replacement during execution")
    return {"output":"same checksum"}
try:
    registry.measure(fixture,change_during_run)
except RuntimeError as error:
    assert "changed during execution" in str(error)
else:
    raise AssertionError("artifact replacement during a run was accepted")
old_run={"builds":[["rhat.exe","build","push_small.rh","-o","push_small.exe"],
                   ["rustc.exe","push_small.rs","-o","push_small.exe"]],
         "rows":[{"workload":"push","size":"small","language":"rhat"}]}
assert overwritten_rhat_rows(old_run)=={("push","small")}
passed+=1
print("PASS benchmark_executable_identity_and_collision_detection",flush=True)

for workload, spec in WORKLOADS.items():
    text = benchmark_source(workload, spec["smoke"])
    positive("bench_" + workload, text, workload_oracle(workload, spec["smoke"]) + "\n")
from run_bench import oracle as binary_tree_oracle
for name, relative, n, expected in (
        ("graph_linked_retained", "variants/graph_linked.rh", 64, workload_oracle("graph",64)),
        ("bintrees_counted_retained", "variants/bintrees_counted.rh", 6, binary_tree_oracle(6)),
        ("bintrees_length_count", "bintrees.rh", 6, binary_tree_oracle(6))):
    text=(repo/"bench/rhat"/relative).read_text(encoding="utf-8")
    text,hits=re.subn(r"(?m)(=.*?)(\d+)(\s*# BENCH_N)$",lambda m:m[1]+str(n)+m[3],text)
    assert hits==1
    positive("bench_"+name,text,expected+"\n")
for workload, sizes in (("tree", (0, 1)), ("graph", (1, 2, 17)), ("lru", (8, 16, 64, 128)), ("scene", (0, 1, 2, 5)), ("strlist", (0, 1, 17)), ("words", (0, 1, 17)), ("respond", (0, 1, 17)), ("records", (0, 1, 17)), ("interp", (0, 1, 2, 5)), ("sexp", (0, 1, 3)), ("calc", (0, 1, 17))):
    for n in sizes:
        positive(f"bench_{workload}_{n}", benchmark_source(workload, n), workload_oracle(workload, n) + "\n")
calc_helpers=(repo/"bench/rhat/calc.rh").read_text().split("let^ n:i64^",1)[0]
positive("calc_errors_and_precedence",calc_helpers+"""
let^ check=p^ text:string^, pool:ref^ grow^ Expr[] {
    pool.clear()
    let^ result=run(text[::],pool)
    if^ result fits^ Ok { print(result.value) else^: print(0-result.code) }
}
var^ pool:grow^ Expr[]=[]
check("",ref^ pool)
check("(",ref^ pool)
check("(1",ref^ pool)
check("1)",ref^ pool)
check("#1",ref^ pool)
check("1+",ref^ pool)
check("1/(2-2)",ref^ pool)
check("-1",ref^ pool)
check("1+2*3",ref^ pool)
check("(1+2)*3",ref^ pool)
check("8/3",ref^ pool)
check("1-2-3",ref^ pool)
check("18/3/2",ref^ pool)
check("1/0 + (",ref^ pool)
check("1/0 #",ref^ pool)
check("4294967296",ref^ pool)
pool.clear()
let^ failed=run("1+(2*"[:: 5],ref^ pool)
assert(failed fits^ Failure)
print(failed.code,pool.len)
""", "-1\n-1\n-2\n-3\n-3\n-1\n-4\n18446744073709551615\n7\n9\n2\n18446744073709551612\n3\n-1\n-3\n4294967296\n1 2\n")
sexp_helpers=(repo/"bench/rhat/sexp.rh").read_text().split("let^ n:i64^",1)[0]
positive("sexp_copies_symbols_and_errors",sexp_helpers+"""
var^ text:grow^ u8^[]=[]
text.append("(abc 4294967296 ()) ")
var^ pool:grow^ Node[]=[Num{value=0,next=0}]
var^ symbols:grow^ u8^[]=[]
var^ roots:grow^ u32^[]=[]
print(parse_all(text[::],ref^ pool,ref^ symbols,ref^ roots))
text.clear()
text.append("xxxxxxxxxxxxxxxxxxx")
print(pool.len-1,roots.len,walk(ref^ pool,ref^ symbols,roots[0],0))
roots.clear()
print(parse_all("("[:: 1],ref^ pool,ref^ symbols,ref^ roots),roots.len)
print(parse_all(")"[:: 1],ref^ pool,ref^ symbols,ref^ roots),roots.len)
""", "0\n4 1 4294967398\n8 0\n5 0\n")
packed_word_helpers=(repo/"bench/rhat/strlist.rh").read_text().split("let^ n:i64^",1)[0]
positive("packed_words_own_copies", packed_word_helpers+"""
var^ text:grow^ u8^[]=[]
text.append(" a  bc ")
var^ words=split(text[::])
text.clear()
text.append("xxxxxxx")
print(words.count,words.data.len,words.data[0],words.data[1],words.data[2],words.data[3],words.data[4])
""", "2 5 1 97 2 98 99\n")
response_helpers=(repo/"bench/rhat/respond.rh").read_text().split("let^ n:i64^",1)[0]
positive("response_integer_format", response_helpers+"""
var^ out:grow^ u8^[]=[]
emit_int(ref^ out,0)
emit_int(ref^ out,18446744073709551615)
print(out.len,out[0],out[1],out[2],out[20])
""", "21 48 49 56 53\n")
positive("scene_layout", benchmark_source("scene", 0), workload_oracle("scene", 0)+"\n",
         lambda ir,ll: ("type Node size=120 align=4" in ir
                        and "+100 Node.kids : u32^[4]" in ir
                        and "+116 Node.nkids : u8^" in ir)
         or (_ for _ in ()).throw(AssertionError("scene node layout")))
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
var^ scratch:grow^ i32^[]=[7]
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
""", "hat identifiers cannot be declared")
negative("template_requires_function", """
let^ value=template^42
""", "expected 'f^'")
negative("recursive_region_cleanup", """
let^ sum=f^ n:i32^ -> i32^ {
    var^ scratch:grow^ i32^[]=[n]
    if^ n=0 { return^ scratch[0] }
    return^ scratch[0] + this^(n-1)
}
print(sum(10))
""", "holds local owned value")
def stack_ir(ir, ll):
    assert "stack.save" in ir and "stack.restore" in ir
    assert "stack-offset=" in ir and "stack-owner=" in ir
    assert "@rh_array_push" not in ll and "@rh_region_init" not in ll
    assert "{ ptr, i64, ptr }" in ll and "%RhStack" in ll

positive("stack_reuse_and_nested_calls", """
let^ inner=f^ n:i32^ -> i32^ {
    var^ a:grow^ i32^[]=[n]
    return^ a[0]
}
let^ outer=f^ n:i32^ -> i32^ {
    var^ a:grow^ i32^[]=[n+1]
    return^ inner(n)+a[0]
}
var^ long:grow^ i32^[]=[99]
let^ ref=ref^ long[0]
var^ total:i32^=0
var^ i:i32^=0
repeat^ while^ i<100 { total:=total+outer(i); i:=i+1 }
print(total,*ref)
""", "10000 99\n", stack_ir)
positive("recursive_scratch_scope_ends", """
let^ walk=f^ n:i32^ -> i32^ {
    var^ value:i32^=0
    if^ true^ { var^ scratch:grow^ i32^[]=[n]; value:=scratch[0] }
    if^ n=0 { return^ value }
    return^ value+this^(n-1)
}
print(walk(10))
""", "55\n", stack_ir)
positive("recursive_owned_destination_forwarding", """
let^ make=f^ n:i32^ -> grow^ i32^[] {
    if^ n>0 { return^ this^(n-1) }
    return^ [7,8]
}
var^ a=make(10)
print(a.len,a[0],a[1])
""", "2 7 8\n")
positive("owned_result_and_local_scratch", """
def^ Bag { self^{ id:i32^, data:grow^ i32^[] } }
let^ make=f^ n:i32^ -> Bag {
    var^ scratch:grow^ i32^[]=[n+1]
    var^ result:Bag=Bag{id=n,data=[scratch[0]]}
    result.data.push(n+2)
    return^ result
}
var^ a=make(40)
var^ b=make(80)
print(a.id,a.data[0],a.data[1],b.data[0])
""", "40 41 42 81\n")
negative("recursive_owned_initializer", """
let^ make=f^ n:i32^ -> grow^ i32^[] {
    if^ n=0 { return^ [1] }
    var^ a= this^(n-1)
    return^ [a[0]]
}
""", "holds local owned value")
negative("recursive_owned_field_initializer", """
def^ Bag { self^{ id:i32^, data:grow^ i32^[] } }
let^ bad=f^ n:i32^ -> i32^ {
    if^ n=0 { return^ 0 }
    var^ a:Bag=Bag{id=this^(n-1),data=[]}
    return^ a.id
}
""", "holds local owned value")
negative("mutual_recursive_owned_local", """
let^ a=f^ n:i32^ -> i32^ {
    var^ scratch:grow^ i32^[]=[n]
    if^ n=0 { return^ scratch[0] }
    return^ b(n-1)
}
let^ b=f^ n:i32^ -> i32^ { return^ a(n) }
print(a(2))
""", "holds local owned value")
positive("mutual_recursive_borrowed_pool", """
let^ a=p^ pool:ref^ grow^ i32^[], n:i32^ {
    pool.push(n)
    if^ n>0 { b(pool,n-1) }
}
let^ b=p^ pool:ref^ grow^ i32^[], n:i32^ { a(pool,n) }
var^ pool:grow^ i32^[]=[]
a(ref^ pool,5)
print(pool.len,pool[0],pool[5])
""", "6 5 0\n")
positive("template_owned_scope_reuse", """
let^ length=template^f^ item {
    var^ scratch:grow^ i32^[]=[i32^(item)]
    return^ scratch[0]
}
print(length(i64^(42)),length(i32^(9)))
""", "42 9\n")
# Stack reports must include transitive callees and count exclusive branches
# once, while overlapping owners reserve separate indices.
path=source("static_stack_report", """
let^ leaf=f^ -> i32^ { var^ a:grow^ i32^[]=[1]; return^ a[0] }
var^ x:grow^ i32^[]=[2]
if^ true^ { var^ a:grow^ i32^[]=[3]; print(leaf()) el^: var^ b:grow^ i32^[]=[4]; print(leaf()) }
print(x[0])
""")
report=run([compiler,"check",str(path),"--stacks"]).stdout
assert report.startswith("data stacks: 3;"), report
assert "2 own, 3 with callees" in report, report
passed+=1
print("PASS static_stack_report",flush=True)
positive("recursive_borrow", """
let^ retain=f^ p:ref^ i32^, n:i32^ -> ref^ i32^ {
    if^ n=0 { return^ p }
    return^ this^(p,n-1)
}
var^ n:i32^=42
let^ ref=retain(ref^ n,4)
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
let^ make=f^ b:bool^ -> grow^ i32^[] { if^ b: [1] el^: [2,3]; }
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
negative("anonymous_local_borrow", "let^ ref=f^ -> ref^ i32^ { var^ n:i32^=1; return^ ref^ n }()", "cannot return a reference")
negative("recursive_local_borrow", """
let^ bad=f^ p:ref^ i32^, n:i32^ -> ref^ i32^ {
    if^ n=0 { return^ p }
    var^ local:i32^=1
    return^ this^(ref^ local,n-1)
}
""", "cannot return a reference")
negative("anonymous_capture", "var^ outer:i32^=3; print(f^ -> i32^ { outer }())", "unknown value 'outer'")
negative("implicit_void_return", "let^ nothing=f^ {} let^ bad=f^ { nothing() }", "cannot return a value")
negative("no_implicit_last_statement", "let^ bad=f^ -> i32^ { let^ n:i32^=1; n+1 }", "only calls can be expression statements")
negative("no_implicit_procedure_return", "let^ bad=p^ -> i32^ { 1 }", "only calls can be expression statements")
positive("control_arrays", """
let^ choose = f^ n:i32^ -> i32^ {
    if^ n < 0 { return^ -1 else^: return^ n * 2 }
}
var^ xs : i32^[3] = [1, 2, 3]
xs[1] := choose(4)
var^ i:i64^ = 0
repeat^ while^ i < xs.len { print(xs[i]); i := i + 1 }
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
let^ identity = f^ x:ref^ i32^ -> ref^ i32^ { return^ x }
var^ n:i32^ = 9; let^ r=identity(ref^ n); print(*r)
""", "9\n")
positive("record_return", """
def^ Pair { self^{ a:i32^ = 0, b:i32^ = 0 } }
let^ pair = f^ n:i32^ -> Pair { return^ Pair{ a=n, b=n+1 } }
let^ p=pair(20); print(p.a,p.b)
""", "20 21\n")
positive("region_tail", """
def^ Buffer { self^{ tag:i32^ = 7, private^ data:grow^ i32^[] = [1,2] } }
let^ b=Buffer.new(); print(b.tag)
""", "7\n")
positive("early_cleanup", """
let^ inspect = f^ n:i32^ -> i32^ {
    var^ scratch:grow^ i32^[] = [5]
    if^ n = 0 { return^ scratch[0] }
    do^{ var^ inner:grow^ i32^[] = [3]; assert(inner[0] = 3) }
    return^ 9
}
print(inspect(0),inspect(1))
""", "5 9\n")
positive("owned_direct_return", """
let^ make = f^ -> grow^ i32^[] { return^ [3,4] }
var^ a:grow^ i32^[]=make(); print(a[1])
""", "4\n")
positive("evaluation_order", """
let^ change = p^ x:ref^ i32^ -> i32^ { *x := 99; return^ 2 }
let^ combine = p^ a:i32^, b:i32^ { print(a,b) }
var^ n:i32^=1
print(n + change(ref^ n))
n := 1
combine(n,change(ref^ n))
n := 1
print(n,change(ref^ n))
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
let^ set=p^ p:ref^ i32^ { *p := 8 }
var^ n:i32^=1; let^ r=ref^ n; set(r); *r := 9; print(n)
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
negative("local_borrow_return", 'let^ bad=f^ -> ref^ i32^ { var^ x:i32^=1; return^ ref^ x }\n', "cannot return a reference")
negative("inner_borrow_escape", """
var^ outer:i32^=1
var^ ref=ref^ outer
do^{ var^ inner:i32^=2; ref := ref^ inner }
print(*ref)
""", "reference would outlive its owner")
negative("aggregate_borrow_return", """
def^ View { self^{ p:ref^ i32^ } }
let^ bad=f^ -> View { var^ x:i32^=1; return^ View{p=ref^ x} }

""", "cannot return a reference")
negative("argument_borrow_escape", """
let^ id=f^ p:ref^ i32^ -> ref^ i32^ { return^ p }
var^ a:i32^=1; var^ r=ref^ a
do^{ var^ b:i32^=2; r := id(ref^ b) }
""", "reference would outlive its owner")
negative("borrowed_store_escape", """
def^ View { self^{ p:ref^ i32^ } }
let^ bad=p^ v:ref^ View { var^ x:i32^=1; v.p := ref^ x }

""", "reference would outlive its owner")
negative("owned_copy", 'var^ a:grow^ i32^[]=[]; let^ b=a', "owned region values cannot be copied")
negative("owned_reassign", 'var^ a:grow^ i32^[]=[]; a := [1]', "owned values cannot be reassigned")
negative("grow_not_tail", 'def^ A { self^{ a:grow^ i32^[], b:i32^=1 } }\n', "final field")
negative("reference_default_array", 'def^ A { self^{ a:ref^ i32^[2] } }\nlet^ a=A.new()', "needs an initializer")
negative("pure_mutation", 'let^ bad=f^ x:ref^ i32^ { *x := 2 }\n', "cannot mutate borrowed storage")
negative("pure_procedure", 'let^ bad=f^ { print(1) }\n', "print requires p^")
negative("missing_return", 'let^ bad=f^ x:i32^ -> i32^ { if^ x=0 { return^ 1 } }\n', "not all paths return")
negative("immutable", 'let^ x:i32^=1; x := 2', "cannot modify let^")
negative("closure_capture", 'do^{ let^ captured=1 let^ f=f^ ->i32^ { return^ captured } print(f()) }', "unknown value 'captured'")
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
def^ View { self^{ p:ref^ i32^ } }
def^ Context { self^{ view:ref^ View, source:ref^ i32^ } }
let^ bad=p^ c:ref^ Context { c.view.p := c.source }

""", "cannot store nonstatic references through borrowed storage")
negative("pure_mutable_call", """
def^ Counter {
    self^{ n:i32^=0 },
    add=f^mutable^self^ { self^.n := 1 },
}
let^ bad=f^ c:ref^ Counter { c.add() }

""", "cannot mutate borrowed storage")
negative("syntax", "let^ main=p^ {", "unterminated block")
runtime_failure("bounds", 'let^ a=[1,2]; print(a[2])', "out of bounds")
runtime_failure("negative_index", 'let^ a=[1]; print(a[-1])', "out of bounds")
runtime_failure("division", 'print(1 / 0)', "invalid integer division")
runtime_failure("division_overflow", 'let^ low:i32^=-2147483648; print(low / -1)', "invalid integer division")
runtime_failure("assertion", 'assert(false^)', "assertion failed")
runtime_failure("grow_bounds", 'var^ a:grow^ i32^[]=[]; print(a[0])', "out of bounds")
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
def direct_scalar_abi(ir, ll):
    assert "define internal void @rh_fn_0" in ll
    for typ in ("i1", "i8", "i16", "i32", "i64", "i128", "half", "float", "double"):
        assert re.search(r'define internal ' + typ + r' @rh_fn_\d+\(i64 %sp, ' + typ + r' %arg0\)', ll), typ


positive("direct_scalar_abi", '''
let^ id=template^f^ x { return^ x }
''' + '\n'.join(f'let^ v{j}:{typ}^={value}; assert(id(v{j})=v{j})' for j, (typ, value) in enumerate([
    ('bool', 'true^'), ('u8', '255'), ('u16', '65535'), ('u32', '4294967295'),
    ('u64', '18446744073709551615'), ('u128', '340282366920938463463374607431768211455'),
    ('i8', '-128'), ('i16', '-32768'), ('i32', '-2147483648'),
    ('i64', '-9223372036854775808'), ('i128', '-170141183460469231731687303715884105728'),
    ('f8', '1.5'), ('f16', '1.5'), ('f32', '1.5'), ('f64', '1.5')])) + '\nprint(42)', "42\n", direct_scalar_abi)

positive("small_aggregate_value_abi", '''
def^ Pair { self^{ a:i64^, b:i64^ } }
def^ Ref { self^{ p:ref^ i32^ } }
def^ A { self^{ value:i64^ } }
def^ B { self^{ value:i32^ } }
def^ U = A | B
def^ Big { self^{ a:i64^, b:i64^, c:i64^ } }
let^ id=template^f^ x { return^ x }
var^ v:i32^=42
let^ pair=id(Pair{a=12,b=30})
let^ ref=id(Ref{p=ref^ v})
let^ text=id("abc")
let^ view=id(text[1 :: 3])
let^ ary=id([1,2,3,4])
let^ u:U=A{value=42}
let^ got=id(u)
let^ big=id(Big{a=1,b=2,c=3})
assert(got fits^ A)
print(pair.a+pair.b,*ref.p,text,view.len,ary[3],got.value,big.c)
''', '42 42 abc 2 4 42 3\n',
lambda ir,ll: ('define internal { ptr, i64 }' in ll and
              'define internal [4 x i32]' in ll and
              re.search(r'define internal %rh_type_\d+ @rh_fn_\d+\(i64 %sp, %rh_type_\d+ %arg0\)',ll) and
              re.search(r'define internal void @rh_fn_\d+\(i64 %sp, ptr %out, ptr %arg0\)',ll))
              or (_ for _ in ()).throw(AssertionError('small/large value ABI')))
positive("float_cast_single_evaluation", '''
let^ value=p^ n:ref^ i32^ -> f64^ { *n := *n+1; return^ 1.5 }
var^ n:i32^=0
print(i64^(value(ref^ n)),n)
''', '1 1\n')

from ir_builtins import exercise as exercise_builtins
builtin_checks = exercise_builtins(repo, work, compiler, args.clang, source, run,
                                  positive, runtime_failure, high_water)
# The callbacks above increment passed themselves; do not load its old value
# through += before those callbacks have run.
passed += builtin_checks
from ranges import exercise as exercise_ranges
passed += exercise_ranges(run, source, compiler, args.clang, work, repo)
print(f"{passed} integration cases passed (positive/runtime cases at -O0 and -O2)")
