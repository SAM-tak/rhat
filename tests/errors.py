"""Error values, explicit propagation, lazy replacement, handlers, and cleanup."""
from pathlib import Path

from harness import Suite


def main():
    suite = Suite('error')
    run, positive, negative = suite.run, suite.positive, suite.negative
    work = suite.work

    prefix = '''errordef^ E {Bad,Missing}
let^ read=f^ n:i32^ ->i32^|E.Bad|E.Missing {
 if^ n=0 {return^ E.Bad{}}
 if^ n<0 {return^ E.Missing{}}
 return^ n
}
'''
    positive('error_plain_construction', '\n'.join([
        'errordef^ E {Bad,Missing}',
        'let^ a=E.Bad{}',
        'let^ b:E.Bad={}',
        'let^ c=E.Bad.new()',
        'let^ accept=f^ value:E.Bad {value fits^ E.Bad}',
        'print^(a fits^ E.Bad,b fits^ E.Bad,c fits^ E.Bad,accept({}))',
        'let^ read=f^ ->i32^|E.Bad {return^ E.Bad{}}',
        'print^(read() catch^ 42)',
    ]), 'true true true true\n42\n')

    positive('error_replacement', prefix+'''
print^(read(2) catch^ 9,read(0) catch^ 9)
print^(10+read(0) catch^ 2,read(0) catch^ 2+10)
print^((read(0) catch^ nil^)??7)
print^(read(0) catch^ if^ it^ fits^ E.Bad: 11 el^: 12;)
''', '2 9\n12 12\n7\n11\n')

    positive('error_lazy', prefix+'''
let^ tick=p^ count:ref^ i32^ ->i32^ {*count+=1 return^ 11}
var^ count=0
print^(read(2) catch^ tick(ref^ count),count)
print^(read(0) catch^ tick(ref^ count),count)
print^(try^ 42)
''', '2 0\n11 1\n42\n')

    positive('error_propagation', prefix+'''
let^ wrap=f^ n:i32^ ->i32^|E.Bad|E.Missing {return^ try^ read(n)+1}
let^ inferred=f^ n:i32^ {return^ try^ read(n)+2}
print^((wrap(2) catch^ 0),(wrap(0) fits^ E.Bad),(wrap(-1) fits^ E.Missing))
print^((inferred(2) catch^ 0),(inferred(0) fits^ E.Bad))
''', '3 true true\n4 true\n')

    positive('error_rethrow', prefix+'''
let^ wrap=f^ n:i32^ {return^ read(n) catch^ try^ it^}
let^ selective=f^ n:i32^ {
 return^ read(n) catch^ for^ it^: when^ fits^ E.Bad: 12 other^: try^ it^;
}
print^((wrap(2) catch^ 0),(wrap(0) fits^ E.Bad))
print^((selective(2) catch^ 0),(selective(0) catch^ 0),(selective(-1) fits^ E.Missing))
''', '2 true\n2 12 true\n')

    positive('error_bottom', prefix+'''
let^ failed=f^ ->i32^|E.Bad {return^ (try^ E.Bad{})+1}
let^ id=f^ n:i32^ {n}
let^ argument=f^ ->i32^|E.Bad {return^ id(try^ E.Bad{})}
let^ folded=f^ {try^ E.Bad{} catch^: 12}
print^((failed() fits^ E.Bad),(argument() fits^ E.Bad),folded())
let^ choose=f^ flag:bool^ ->i32^|E.Bad {return^ if^ flag: 7 el^: try^ E.Bad{};}
print^((choose(true^) catch^ 0),(choose(false^) fits^ E.Bad))
''', 'true true 12\n7 true\n')

    positive('error_default_clause', prefix+'''
let^ handled=f^ n:i32^ ->i32^ {
 return^ try^ read(n)
catch^ E.Bad:
 return^ 11
catch^:
 return^ 12
}
print^(handled(2),handled(0),handled(-1))
do^{let^ n=try^ read(0) print^(n) catch^: print^(it^ fits^ E.Bad)}
''', '2 11 12\ntrue\n')

    positive('error_nested_clauses', prefix+'''
do^{
 do^{
  print^(try^ read(-1))
 catch^ E.Bad:
  print^(10)
 }
 print^(99)
catch^ E.Missing:
 print^(20)
}
let^ partially=f^ n:i32^ {
 return^ try^ read(n)
catch^ E.Bad:
 return^ 11
}
print^((partially(2) catch^ 0),(partially(0) catch^ 0),(partially(-1) fits^ E.Missing))
''', '20\n2 11 true\n')

    positive('error_if_clause', prefix+'''
let^ choose=p^ flag:bool^ {
 if^ flag {
  print^(try^ read(0))
 else^:
  print^(try^ read(-1))
 catch^ E.Bad:
  print^(10)
 catch^:
  print^(20)
 }
}
choose(true^) choose(false^)
''', '10\n20\n')

    positive('error_loop_clause', prefix+'''
var^ count=0
for^ i from^ -1 to^ 1 {
 count+=1
 print^(try^ read(i))
catch^ E.Bad:
 print^(10,i)
catch^:
 print^(20,i)
}
print^(count)
''', '20 -1\n10 0\n1\n3\n')

    positive('error_loop_controls', prefix+'''
var^ count=0
for^ i from^ -1 to^ 2 {
 count+=1
 print^(try^ read(i))
catch^ E.Missing:
 next^
catch^:
 break^
last^:
 print^(30)
epilog^:
 print^(40)
}
print^(count)
let^ early=f^ {
 repeat^ 1 {let^ n=try^ read(0) return^ n catch^: return^ 77}
 return^ 0
}
print^(early())
''', '30\n40\n2\n77\n')

    positive('error_header_bypasses_inner_clause', prefix+'''
do^{
 if^ (try^ read(0))>0 {
  print^(try^ read(-1))
 catch^:
  print^(99)
 }
catch^:
 print^(11)
}
''', '11\n')

    positive('error_pre_bypasses_main_clause', prefix+'''
do^{
 repeat^ 1 {
 pre^: let^ n=try^ read(0)
 main^: print^(try^ read(-1))
 catch^: print^(99)
 }
catch^: print^(12)
}
''', '12\n')

    positive('error_focus_shadow', prefix+'''
for^ 3 to^ 3 {
 let^ x=E.Bad{} catch^ (E.Missing{} catch^ if^ it^ fits^ E.Missing: it^^ fits^ E.Bad el^: false^;)
 print^(x)
}
''', 'true\n')

    positive('error_unit_success', '''
errordef^ E {Bad}
let^ save=f^ n:i32^ ->nil^|E.Bad {if^ n>0: nil^ el^: E.Bad{};}
let^ wrap=p^ n:i32^ {try^ save(n)}
print^(wrap(1) fits^ nil^,wrap(0) fits^ E.Bad)
let^ explicit=p^ n:i32^ ->nil^|E.Bad {try^ save(n) return^}
print^(explicit(1) fits^ nil^,explicit(0) fits^ E.Bad)
''', 'true true\ntrue true\n')

    positive('error_no_implicit_throw', prefix+'''
do^{
 let^ raw=read(0)
 print^(raw fits^ E.Bad)
 print^(try^ read(1))
catch^:
 print^(99)
}
''', 'true\n1\n')

    positive('error_owned_cleanup', prefix+'''
def^ Bag {self^{items:grow^ i32^[]}}
repeat^ 3 {
 do^{
  var^ before:grow^ i32^[]=[1,2]
  var^ pending=Bag{items=[3,try^ read(0)]}
  print^(pending.items.len,before.len)
 catch^:
  var^ after:grow^ i32^[]=[7]
  print^(after[0])
 }
}
''', '7\n7\n7\n')

    positive('error_owned_result_retry', prefix+'''
def^ Bag {self^{tag:i32^,items:grow^ i32^[]}}
let^ build=f^ ->Bag {
 return^ Bag{tag=1,items=[1,try^ read(0)]}
catch^:
 return^ Bag{tag=9,items=[7,8]}
}
repeat^ 3 {var^ bag=build() print^(bag.tag,bag.items.len,bag.items[0],bag.items[1])}
''', '9 2 7 8\n9 2 7 8\n9 2 7 8\n')

    positive('error_group', '''
errordef^ E {Bad,Missing}
def^ Result = i32^ | E
let^ read=f^ n:i32^ ->Result {
 if^ n>0 {return^ n}
 let^ error:E=E.Bad{}
 return^ error
}
let^ wrap=f^ n:i32^ ->Result {return^ try^ read(n)+1}
let^ grouped=wrap(0)
assert^(grouped fits^ E)
let^ error=grouped
print^((wrap(2) catch^ 0),error fits^ E.Bad)
do^{print^(try^ read(0)) catch^ E.Bad: print^(11) catch^: print^(12)}
''', '3 true\n11\n')

    positive('error_random', '''
import^ std.random
var^ rng=std.random.new(12345)
print^(rng.rangeint(5,2) catch^ i64^(99))
do^{try^ rng.load(0) catch^ std.random.Error.InvalidState: print^(12)}
''', '99\n12\n')

    positive('error_bottom_sections', prefix+'''
let^ pre=p^ ->i32^|E.Bad {
 repeat^ 2 {pre^: try^ E.Bad{} main^: print^(99)}
}
let^ condition=p^ ->i32^|E.Bad {
 repeat^ while^ try^ (E.Bad{}) {first^: let^ n=1 main^: print^(n)}
}
let^ first=p^ ->i32^|E.Bad {
 repeat^ 2 {first^: let^ n:i32^=try^ E.Bad{} main^: print^(n)}
 return^ 1
}
print^((pre() fits^ E.Bad),(condition() fits^ E.Bad),(first() fits^ E.Bad))
''', 'true true true\n')

    positive('error_default_initializer', prefix+'''
def^ Item {self^{n:i32^=try^ read(0)}}
do^{let^ item=Item{} print^(item.n) catch^: print^(12)}
''', '12\n')

    positive('error_nullable_shortcircuit', prefix+'''
let^ value=f^ n:i32^|nil^ ->i32^|E.Bad {return^ n ?? try^ E.Bad{}}
let^ flag=f^ b:bool^ ->bool^|E.Bad {return^ b or^ try^ E.Bad{}}
let^ number=value(42)
assert^(number fits^ i32^)
print^(number,value(nil^) fits^ E.Bad)
print^((flag(true^) catch^ false^),(flag(false^) fits^ E.Bad))
''', '42 true\ntrue true\n')

    positive('error_assignment_abort', prefix+'''
var^ a=1 var^ b=2
do^{a,b:=3,try^ read(0) catch^: print^(a,b)}
print^(a,b)
''', '1 2\n1 2\n')

    runtime = work / 'error_cleanup_runtime.c'
    repo = Path(__file__).resolve().parents[1]
    runtime.write_text('''
#define RH_STACK_BYTES 65536
#define rh_runtime_shutdown rh_base_shutdown
#define rh_print_integer rh_base_print_integer
#define rh_print_newline rh_base_print_newline
#include "'''+(repo/'runtime/rhat_runtime.c').as_posix()+'''"
#undef rh_runtime_shutdown
#undef rh_print_integer
#undef rh_print_newline
static int skip_newline;
static uint64_t observations;
void rh_print_integer(const void *value, int32_t bits, int32_t uns) {
 int64_t marker=0;
 if (bits==64) memcpy(&marker,value,8);
 if (bits!=64 || uns || marker!=INT64_C(1234567890123456789)) {
  rh_base_print_integer(value,bits,uns);
  return;
 }
 uint64_t bytes=0;
 for (uint64_t j=0;j<rh_stack_count;j++) bytes+=(uint64_t)(rh_stacks[j].top-rh_stacks[j].base);
 if (bytes!=8) rh_panic("partial result was not rewound before retry",0);
 observations++;
 skip_newline=1;
}
void rh_print_newline(void) {
 if (skip_newline) skip_newline=0;
 else rh_base_print_newline();
}
void rh_runtime_shutdown(void) {
 if (observations!=10000) rh_panic("missing error cleanup observation",0);
 for (uint64_t j=0;j<rh_stack_count;j++) {
  if (rh_stacks[j].top!=rh_stacks[j].base) rh_panic("error cleanup leaked storage",0);
 }
 rh_base_shutdown();
}
''', encoding='utf-8')

    def checkpoint_ir(ir, ll):
        assert 'result.checkpoint' in ir and 'result.rewind' in ir
        assert 'stack.restore' in ir
        assert 'invoke ' not in ll and 'landingpad ' not in ll

    positive('error_repeated_cleanup', prefix+'''
def^ Bag {self^{items:grow^ i32^[]}}
let^ build=p^ ->Bag {
 return^ Bag{items=[1,2,try^ read(0)]}
catch^:
 return^ Bag{items=[7,8]}
}
repeat^ 10000 {
 do^{
  var^ a:grow^ i32^[]=[1,2]
  var^ b:grow^ i32^[]=[3,try^ read(0)]
 catch^:
  var^ c=build()
  assert^(c.items[0]=7)
  let^ marker:i64^=1234567890123456789
  print^(marker)
 }
}
print^(42)
''', '42\n', inspect=checkpoint_ir, extra=['--runtime', runtime])

    positive('error_early_unit_return', prefix+'''
let^ save=p^ flag:bool^ {
 if^ flag {return^}
 try^ E.Bad{}
}
print^(save(true^) fits^ nil^,save(false^) fits^ E.Bad)
let^ handled=p^ {
 try^ read(0)
 return^ catch^: print^(12)
}
handled()
''', 'true true\n12\n')

    positive('error_optional_argument', '''
errordef^ E {Bad}
def^ Item {run=f^ self^,n:i32^ ->i32^ {n}}
let^ test=f^ item:ref^ Item|nil^ {
 let^ result=item?.run(try^ E.Bad{})
 return^ result
}
let^ index=f^ items:ref^ i32^[2]|nil^ {
 let^ result=items?[try^ E.Bad{}]
 return^ result
}
var^ item=Item{}
var^ items:i32^[2]=[1,2]
print^(test(nil^) fits^ nil^,test(ref^ item) fits^ E.Bad)
print^(index(nil^) fits^ nil^,index(ref^ items) fits^ E.Bad)
''', 'true true\ntrue true\n')

    positive('error_optional_assignment', prefix+'''
var^ a:i32^|nil^=nil^
var^ b:i32^|nil^=1
do^{a?:=try^ E.Bad{} print^(10) catch^: print^(99)}
do^{b?:=try^ E.Bad{} print^(99) catch^: print^(20)}
do^{b?+=try^ E.Bad{} print^(99) catch^: print^(30)}
assert^(b fits^ i32^)
print^(a fits^ nil^,b)
''', '10\n20\n30\ntrue 1\n')

    positive('error_destination_abort', prefix+'''
var^ items:i32^[2]=[1,2]
do^{items[try^ E.Bad{}]:=42 catch^: print^(10)}
do^{let^ a,b=1,try^ E.Bad{} catch^: print^(20)}
do^{let^ a,b,c=1,try^ E.Bad{},3 catch^: print^(30)}
let^ update=p^ items:ref^ i32^[2]|nil^ {
 items?[try^ E.Bad{}]?:=42
}
let^ unguarded=p^ items:ref^ i32^[2] {items?[try^ E.Bad{}]?:=42}
print^(update(nil^) fits^ nil^,update(ref^ items) fits^ E.Bad,items[0])
print^(unguarded(ref^ items) fits^ E.Bad)
''', '10\n20\n30\ntrue true 1\ntrue\n')

    negatives = [
        ('error_implicit_union_constructor','let^ bad:i32^|E.Bad={}','contextual construction requires'),
        ('error_implicit_union_argument','let^ accept=p^ value:i32^|E.Bad {} accept({})','contextual construction requires'),
        ('error_legacy_prefix','let^ bad=error^E.Bad{}','error^ construction has been removed'),
        ('error_nested_mixed','def^ Inner=i32^|E.Bad def^ Outer=Inner|nil^ let^ inner:Inner=1 let^ v:Outer=inner print^(try^ v)','nested mixed nominal unions'),
        ('error_catch_infallible','print^(42 catch^ 0)','requires an error alternative'),
        ('error_catch_after_try','print^(try^ read(0) catch^ 0)','requires an error alternative'),
        ('error_signature','let^ f=f^ ->i32^ {return^ try^ read(0)} print^(f())','not included in the function result'),
        ('error_partial_signature','let^ f=f^ ->i32^|E.Bad {return^ try^ read(0)} print^(f())','not included in the function result'),
        ('error_clause_without_try','do^{let^ n=read(0) catch^: print^(1)}','error-producing try^'),
        ('error_clause_only_identity','do^{try^ 42 catch^: print^(1)}','error-producing try^'),
        ('error_nonerror_filter','do^{try^ read(0) catch^ i32^: print^(1)}','only declared errors'),
        ('error_default_order','do^{try^ read(0) catch^: print^(1) catch^ E.Bad: print^(2)}','final catch arm'),
        ('error_hidden_body_local','do^{let^ secret=42 try^ read(0) catch^: print^(secret)}','unknown value'),
        ('error_hidden_loop_local','repeat^ 1 {first^: let^ secret=42 main^: try^ read(0) catch^: print^(secret)}','unknown value'),
        ('error_return_rhs','let^ n=read(0) catch^ return^ 0','expected'),
        ('error_focus_escape','let^ bad=f^ ->ref^ E.Bad {return^ (E.Bad{} catch^ ref^ it^)} let^ ref=bad()',"borrowing storage requires var^"),
    ]
    positive('error_top_level_success', prefix+'let^ n=try^ read(5) print^(n)', '5\n')
    suite.runtime_failure('error_top_level_first', prefix+'print^(1) let^ n=try^ read(0) print^(n)',
                          'line 7: unhandled error E.Bad')
    suite.runtime_failure('error_top_level_last', prefix+'let^ n=try^ read(-1) print^(n)', 'unhandled error E.Missing')
    suite.runtime_failure('error_top_level_variant', prefix+'do^{try^ E.Bad{}}', 'unhandled error E.Bad')
    suite.runtime_failure('panic_message', prefix+'print^(1)\npanic^ "not here"\nprint^(2)', 'line 8: panic: not here')
    suite.runtime_failure('panic_catch_rhs', prefix+'let^ n=read(-1) catch^ panic^ it^\nprint^(n)', 'panic: E.Missing')
    positive('panic_catch_rhs_success', prefix+'let^ n=read(3) catch^ panic^ it^\nprint^(n)', '3\n')
    suite.runtime_failure('panic_catch_arm', prefix+'do^{\n let^ n=try^ read(0)\n print^(n)\ncatch^:\n panic^ it^\n}',
                          'line 11: panic: E.Bad')
    positive('panic_function_path', prefix+'let^ g=f^ n:i32^ ->i32^ { if^ n<0 { panic^ "negative" } return^ n }\nprint^(g(2))', '2\n')
    suite.runtime_failure('panic_function', prefix+'let^ g=f^ n:i32^ ->i32^ { if^ n<0 { panic^ "negative" } return^ n }\nprint^(g(-1))',
                          'panic: negative')
    negatives.append(('panic_value_type', 'panic^ 3', 'panic^ takes a string^ or an error value'))
    for name, text, diagnostic in negatives:
        negative(name,prefix+text,diagnostic)
    suite.finish()


if __name__ == '__main__':
    main()
