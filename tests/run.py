"""End-to-end semantic, provenance, IR, codegen and runtime checks."""
import argparse
import os
import pathlib
import re
import subprocess
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
    assert "+0 A.a : i32" in ir
    assert "+4 B.b : i32" in ir
    assert "+8 C.c : i32" in ir
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


positive("empty_program", "", "")
positive("declarations_only", "let^ unused=p^ { print(99) } def^ Empty {}", "")
positive("ordinary_main", "let^ main=p^ { print(99) } print(1)", "1\n")
positive("explicit_main", "let^ main=f^ n:i32 -> i32 { return^ n+1 } print(main(41))", "42\n")
positive("interleaved_declarations", """
print(1)
later()
def^ Value { self^{ n:i32=3 } }
let^ value=Value.new()
let^ later=p^ { print(2) }
print(value.n)
""", "1\n2\n3\n")
positive("top_return", """
var^ scratch:i32[>..]=[7]
print(scratch[0])
return^
let^ unused=p^ { print(99) }
""", "7\n", top_cleanup_ir)
negative("top_return_value", "return^ 1", "cannot return a value")
positive("layout", (repo / "examples/layout.rh").read_text(), "0 42 12\n", layout_ir)
positive("nominal_template", (repo / "examples/nominal_template.rh").read_text(),
         "R^ Person\nR^ Person\nfile.rh\n")
positive("regions", (repo / "examples/regions.rh").read_text(), "10 2003\n200\n", region_ir)
positive("control_arrays", """
let^ choose = f^ n:i32 -> i32 {
    if^ n < 0 { return^ -1 } else^ { return^ n * 2 }
}
var^ xs : i32[3] = [1, 2, 3]
xs[1] := choose(4)
var^ i:i64 = 0
while^ i < xs.len { print(xs[i]); i := i + 1 }
assert(choose(-2) = -1)
assert((false^ and^ (1 / 0 = 0)) = false^)
assert(true^ or^ (1 / 0 = 0))
""", "1\n8\n3\n")
positive("private_inheritance", """
def^ A {
    self^{ private^ secret:i32 = 7 },
    read = f^self^ -> i32 { return^ self^.secret },
}
def^ B : A { self^{ other:i32 = 9 } }
let^ b = B.new(); print(b.read())
""", "7\n")
positive("return_borrow", """
let^ identity = f^ x:&i32 -> &i32 { return^ x }
var^ n:i32 = 9; let^ r=identity(&n); print(*r)
""", "9\n")
positive("record_return", """
def^ Pair { self^{ a:i32 = 0, b:i32 = 0 } }
let^ pair = f^ n:i32 -> Pair { return^ Pair{ a=n, b=n+1 } }
let^ p=pair(20); print(p.a,p.b)
""", "20 21\n")
positive("region_tail", """
def^ Buffer { self^{ tag:i32 = 7, private^ data:i32[>..] = [1,2] } }
let^ b=Buffer.new(); print(b.tag)
""", "7\n")
positive("early_cleanup", """
let^ inspect = f^ n:i32 -> i32 {
    var^ scratch:i32[>..] = [5]
    if^ n = 0 { return^ scratch[0] }
    { var^ inner:i32[>..] = [3]; assert(inner[0] = 3) }
    return^ 9
}
print(inspect(0),inspect(1))
""", "5 9\n")
positive("owned_direct_return", """
let^ make = f^ -> i32[>..] { return^ [3,4] }
var^ a:i32[>..]=make(); print(a[1])
""", "4\n")
positive("evaluation_order", """
let^ change = p^ x:&i32 -> i32 { *x := 99; return^ 2 }
let^ combine = p^ a:i32, b:i32 { print(a,b) }
var^ n:i32=1
print(n + change(&n))
n := 1
combine(n,change(&n))
n := 1
print(n,change(&n))
""", "3\n1 2\n1 2\n")
positive("numeric", """
let^ big:i64=9223372036854775807
let^ byte:u8=255
print(big,byte,1.5 * 2.0,true^)
let^ low:i32=-2147483648
assert(low - 1 = 2147483647)
print(7 / 2,7 % 2)
""", "9223372036854775807 255 3 true\n3 1\n")
positive("paths with spaces", 'print(42)', "42\n")
positive("reference_mutation", """
let^ set=p^ p:&i32 { *p := 8 }
var^ n:i32=1; let^ r=&n; set(r); *r := 9; print(n)
""", "9\n")
positive("mutable_receiver", """
def^ Counter {
    self^{ n:i32=0 },
    add=f^mutable^self^ -> i32 { self^.n := self^.n + 1; return^ self^.n },
}
var^ c=Counter.new(); print(c.add(),c.add())
""", "1 2\n")
positive("diamond", """
def^ A { self^{ n:i32=5 } }
def^ B:A {}
def^ C:A {}
def^ D:B,C {}
let^ d=D.new(); print(d.n)
""", "5\n")
positive("explicit_method_conflict", """
def^ A { n=f^self^ -> i32 { return^ 1 } }
def^ B { n=f^self^ -> i32 { return^ 2 } }
def^ C:A,B { override^ n=f^self^ -> i32 { return^ 3 } }
let^ c=C.new(); print(c.n())
""", "3\n")
positive("trait_implementation", """
def^ Contract {
    self^{ abstract^ n:i32 },
    abstract^ read:f^self^ -> i32;
}
def^ Implementation {
    self^{ n:i32=42 },
    read=f^self^ -> i32 { return^ self^.n },
}
def^ Combined:Contract,Implementation {}
def^ Reversed:Implementation,Contract {}
let^ use=p^ v:Contract { print(v.read()) }
use(Combined.new()); use(Reversed.new())
""", "42\n42\n")
positive("mixed_layout", """
def^ Mixed { self^{ a:u8=3, b:i64=100, c:i32[2]=[7,8] } }
let^ m=Mixed.new(); print(m.a,m.b,m.c[1])
""", "3 100 8\n", lambda ir, ll: (
    "type Mixed size=24 align=8" in ir and "+8 Mixed.b : i64" in ir
) or (_ for _ in ()).throw(AssertionError(ir)))
negative("nominal_mismatch", """
def^ Named { abstract^ name:f^self^ -> string^; }
def^ File { name=f^self^ -> string^ { return^ "file" } }
let^ use=p^ x:Named { print(x.name()) }
use(File.new())
""", "requires nominal 'Named'")
negative("template_missing_member", """
def^ Empty {}
template^ let^ use=p^ x { print(x.name()) }
use(Empty.new())
""", "has no method 'name'")
negative("template_keeps_nominal", """
def^ A {}
def^ B {}
template^ let^ use=p^ x:A {}
use(B.new())
""", "requires nominal 'A'")
negative("private", 'def^ A { self^{ private^ x:i32=1 } }\nprint(A.new().x)', "private member")
negative("template_private", """
def^ A { self^{ private^ x:i32=1 } }
template^ let^ use=p^ a { print(a.x) }
use(A.new())
""", "private member")
negative("derived_private", """
def^ A { self^{ private^ x:i32=1 } }
def^ B:A { read=f^self^ -> i32 { return^ self^.x } }

""", "private member")
negative("abstract_instance", 'def^ A { self^{ abstract^ x:i32 } }\nlet^ a=A.new()', "cannot construct abstract")
negative("wrong_abstract_type", """
def^ A { self^{ abstract^ x:i32 } }
def^ B:A { self^{ x:i64=0 } }

""", "cannot replace its inherited slot")
negative("wrong_override", """
def^ A { abstract^ foo:f^self^ -> i32; }
def^ B:A { foo=f^self^ -> i64 { return^ 1 } }

""", "changes its contract")
negative("composition_cycle", 'def^ A:B {} def^ B:A {}\n', "cyclic def^ composition")
negative("layout_cycle", 'def^ A { self^{ a:A } }\n', "recursive by-value layout")
negative("duplicate_field", 'def^ A { self^{ a:i32=1,a:i32=2 } }\n', "duplicate field")
negative("local_borrow_return", 'let^ bad=f^ -> &i32 { var^ x:i32=1; return^ &x }\n', "cannot return a reference")
negative("inner_borrow_escape", """
var^ outer:i32=1
var^ ref=&outer
{ var^ inner:i32=2; ref := &inner }
print(*ref)
""", "reference would outlive its owner")
negative("aggregate_borrow_return", """
def^ View { self^{ p:&i32 } }
let^ bad=f^ -> View { var^ x:i32=1; return^ View{p=&x} }

""", "cannot return a reference")
negative("argument_borrow_escape", """
let^ id=f^ p:&i32 -> &i32 { return^ p }
var^ a:i32=1; var^ r=&a
{ var^ b:i32=2; r := id(&b) }
""", "reference would outlive its owner")
negative("borrowed_store_escape", """
def^ View { self^{ p:&i32 } }
let^ bad=p^ v:&View { var^ x:i32=1; v.p := &x }

""", "reference would outlive its owner")
negative("owned_copy", 'var^ a:i32[>..]=[]; let^ b=a', "owned region values cannot be copied")
negative("owned_reassign", 'var^ a:i32[>..]=[]; a := [1]', "owned values cannot be reassigned")
negative("grow_not_tail", 'def^ A { self^{ a:i32[>..], b:i32=1 } }\n', "final field")
negative("reference_default_array", 'def^ A { self^{ a:&i32[2] } }\nlet^ a=A.new()', "needs an initializer")
negative("pure_mutation", 'let^ bad=f^ x:&i32 { *x := 2 }\n', "cannot mutate borrowed storage")
negative("pure_procedure", 'let^ bad=f^ { print(1) }\n', "print requires p^")
negative("missing_return", 'let^ bad=f^ x:i32 -> i32 { if^ x=0 { return^ 1 } }\n', "not all paths return")
negative("recursive", 'let^ bad=f^ x:i32 -> i32 { return^ bad(x) }\n', "recursive calls are not supported")
negative("immutable", 'let^ x:i32=1; x := 2', "cannot modify let^")
negative("closure", '{ let^ f=f^ { return^ 1 } }', "function values")
negative("literal_overflow", 'let^ x:i32=2147483648', "does not fit")
negative("unknown_type", 'let^ x:Missing=1', "unknown type")
negative("recursive_default", 'def^ A { self^{ n:i32=A.new().n } }\nlet^ a=A.new()', "recursive default construction")
negative("default_capture", 'def^ A { self^{ n:i32=local } }\nlet^ local:i32=1; let^ a=A.new()', "cannot capture caller locals")
negative("void_parameter", 'let^ bad=f^ x:void {}\n', "parameter cannot have void")
negative("false_mutable_self", 'let^ bad=f^mutable^ x:i32 {}\n', "requires an f^ method receiver")
negative("method_conflict", 'def^ A { n=f^self^ -> i32 { return^ 1 } } def^ B { n=f^self^ -> i32 { return^ 2 } } def^ C:A,B {}\n', "is ambiguous")
negative("inherited_field_method", 'def^ A { self^{ n:i32=0 } } def^ B { n=f^self^ -> i32 { return^ 2 } } def^ C:A,B {}\n', "field/method name collision")
negative("private_abstract_field", 'def^ A { self^{ private^ abstract^ n:i32 } }\n', "private abstract fields")
negative("indirect_borrow_store", """
def^ View { self^{ p:&i32 } }
def^ Context { self^{ view:&View, source:&i32 } }
let^ bad=p^ c:&Context { c.view.p := c.source }

""", "cannot store nonstatic references through borrowed storage")
negative("pure_mutable_call", """
def^ Counter {
    self^{ n:i32=0 },
    add=f^mutable^self^ { self^.n := 1 },
}
let^ bad=f^ c:&Counter { c.add() }

""", "cannot mutate borrowed storage")
negative("syntax", "let^ main=p^ {", "unterminated block")
runtime_failure("bounds", 'let^ a=[1,2]; print(a[2])', "out of bounds")
runtime_failure("negative_index", 'let^ a=[1]; print(a[-1])', "out of bounds")
runtime_failure("division", 'print(1 / 0)', "invalid integer division")
runtime_failure("division_overflow", 'let^ low:i32=-2147483648; print(low / -1)', "invalid integer division")
runtime_failure("assertion", 'assert(false^)', "assertion failed")
runtime_failure("grow_bounds", 'var^ a:i32[>..]=[]; print(a[0])', "out of bounds")
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
