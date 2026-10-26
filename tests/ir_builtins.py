"""Exercise emitted operations, optimizer visibility and the runtime boundary."""
import re


def exercise(suite, run, high_water):
    repo, work, compiler, clang = suite.repo, suite.work, suite.compiler, suite.clang
    source, positive, runtime_failure = suite.source, suite.positive, suite.runtime_failure
    positive('inline_builtin_single_evaluation', '''
let^ tick=p^ counter:ref^ i32^, result:i64^ -> i64^ {
    *counter := *counter+1; return^ result
}
var^ counter:i32^=0
var^ a:grow^ i32^[]=[10,20,30,40]
let^ view=a[tick(ref^ counter,1) :: tick(ref^ counter,3)]
a.truncate(tick(ref^ counter,3))
a.append(view)
assert^(tick(ref^ counter,1)=1)
print^(counter,a.len,a[3],a[4])
''', '4 5 20 30\n')
    positive('inline_append_overlap_both_directions', '''
var^ a:grow^ u8^[]=[1,2,3,4,5,6,7,8]
let^ held=a[:: 8]
a.truncate(2)
a.append(held[:: 6])
print^(a.len,a[2],a[7])
a.clear()
a.append(held[2 :: 8])
print^(a.len,a[0],a[2],a[5])
''', '8 1 6\n6 1 3 6\n')
    high_water('view_append_keeps_high_water', '''
var^ a:grow^ i32^[]=[1,2,3,4]
let^ held=a[:: 4]
a.truncate(1)
a.append(held[2 :: 4])
assert^(a.len=3)
a.append(held[:: 0])
assert^(a.len=3)
a.push(9)
assert^(a.len=4)
print^(held[3])
''', [16,16,16], '9\n')
    runtime_failure('slice_maximum_inclusive_end',
                    'let^ a:i32^[1]=[1]; let^ n:i64^=9223372036854775807; let^ v=a[n to^ n]',
                    'line 1: slice range out of bounds')
    runtime_failure('truncate_minimum_length',
                    'var^ a:grow^ i32^[]=[1]; a.truncate(-9223372036854775808)',
                    'line 1: truncate length out of bounds')

    path=source('runtime_boundary', '''
var^ a:grow^ i32^[]=[1,2,3]
let^ v=a[1 :: 3]
a.append(v)
a.truncate(4)
assert^(a.len=4)
print^(a.len,a.pop())
var^ x:f8^=1.5
var^ y:f8^=2.0
print^(x+y,f64^(x),i32^(3.5))
''')
    raw=work/'runtime_boundary.ll'
    run([compiler,'emit',str(path),'-o',str(raw)])
    llvm=raw.read_text()
    runtime=(repo/'runtime/rhat_runtime.c').read_text()
    exported=set(re.findall(r'(?m)^(?:_Noreturn )?void (rh_\w+)\(',runtime))
    declared=set(re.findall(r'(?m)^declare [^\n]*?@(rh_\w+)\(',llvm))
    assert exported==declared,(exported-declared,declared-exported)
    obsolete=('rh_bounds','rh_array_at','rh_array_write','rh_array_append','rh_array_pop',
              'rh_array_clear','rh_array_truncate','rh_make_slice','rh_view_at',
              'rh_union_tag','rh_union_fail','rh_assert','rh_print_i64','rh_check_division',
              'rh_index_integer','rh_check_float_cast','rh_div_i32','rh_div_i64',
              'rh_div_u8','rh_rem_i32','rh_rem_i64','rh_rem_u8')
    for name in obsolete:
        assert not re.search(r'\b'+name+r'\s*\(',runtime),name
        assert '@'+name+'(' not in llvm,name
    assert 'rh_f8_' not in runtime
    for name in declared:
        if name.endswith('_fail') or name=='rh_stack_overflow':
            assert re.search(r'^declare[^\n]*@'+name+r'\([^\n]*\) cold noreturn$',llvm,re.M)
    for opt in suite.opts:
        optimized=work/('runtime_boundary_'+opt[1:]+'.ll')
        run([clang,'-Wno-override-module',opt,'-S','-emit-llvm',str(raw),'-o',str(optimized)])
        text=optimized.read_text()
        definitions=set(re.findall(r'^define[^\n]*?@(rh_\w+)\(',text,re.M))
        external_calls=set(re.findall(r'\bcall\b[^\n]*?@(rh_\w+)\(',text))-definitions
        assert external_calls<=declared,external_calls-declared
        assert not re.search(r'\bcall\b[^\n]*?@rh_(?:f8_|append_)',text)
        for match in re.finditer(r'\bcall\b[^\n]*?@(rh_\w+)\([^\n]*\)[^\n]*\n',text):
            if match.group(1).endswith('_fail') or match.group(1)=='rh_stack_overflow':
                assert text[match.end():].lstrip().startswith('unreachable'),match.group(0)
        exe=work/('runtime_boundary_'+opt[1:]+'.exe')
        run([clang,'-Wno-override-module','-std=c11',opt,str(raw),
             str(repo/'runtime/rhat_runtime.c'),'-o',str(exe)])
        assert run([str(exe)]).stdout=='4 2\n3.5 1.5 3\n'
    suite.mark('minimal_runtime_boundary_and_inlined_helpers')

    # Exercise the actual emitted append helper with lengths that safe R^
    # source cannot fabricate. This verifies the pre-copy overflow checks.
    helpers=re.findall(r'(?ms)^define internal void @rh_append_.*?^}',llvm)
    assert len(helpers)==2
    exported_helpers=work/'append_boundary_helpers.ll'
    metadata='\n'.join(line for line in llvm.splitlines() if re.match(r'^!\d+ = ',line))
    exported_helpers.write_text('%RhStack = type {ptr,ptr,ptr,ptr}\n'
        'declare void @llvm.memcpy.p0.p0.i64(ptr,ptr,i64,i1 immarg)\n'
        'declare void @llvm.memmove.p0.p0.i64(ptr,ptr,i64,i1 immarg)\n'
        'declare void @rh_stack_overflow(i32) cold noreturn\n'+
        '\n'.join(f.replace('define internal ','define ',1) for f in helpers)+'\n'+metadata+'\n')
    c=work/'append_boundary.c'
    c.write_text('''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct {unsigned char *base,*top,*limit,*committed;} Stack;
typedef struct {unsigned char *data;uint64_t len;Stack *stack;} Array;
void rh_append_view(Array *,const void *,uint64_t,uint64_t,int32_t);
void rh_append_literal(Array *,const void *,uint64_t,int32_t);
int main(int argc,char **argv) {
  unsigned char storage[32]={0},seed[8]={1,2,3,4,5,6,7,8};
  Stack s={storage,storage,storage+32,storage+32};Array a={storage,0,&s};
  if(argc>1) {
    if(!strcmp(argv[1],"count")) rh_append_view(&a,seed,UINT64_MAX,1,7);
    if(!strcmp(argv[1],"size")) rh_append_view(&a,seed,1,UINT64_MAX,7);
    if(!strcmp(argv[1],"len")) {a.len=UINT64_MAX;rh_append_view(&a,0,0,1,7);}
    if(!strcmp(argv[1],"partial")) rh_append_view(&a,seed,11,3,7);
    return 2;
  }
  rh_append_view(&a,seed,8,1,7);assert(a.len==8 && s.top==storage+8);
  a.len=2;rh_append_view(&a,storage,6,1,7);
  unsigned char want[8]={1,2,1,2,3,4,5,6};assert(!memcmp(storage,want,8));
  a.len=0;rh_append_view(&a,storage+2,6,1,7);
  assert(a.len==6 && storage[5]==6 && s.top==storage+8);
  rh_append_view(&a,0,0,1,7);assert(a.len==6 && s.top==storage+8);
  rh_append_literal(&a,"abc",3,7);assert(a.len==9 && s.top==storage+9);
  assert(!memcmp(storage+6,"abc",3));return 0;
}
''')
    for opt in suite.opts:
        exe=work/('append_boundary_'+opt[1:]+'.exe')
        run([clang,'-std=c11','-Wno-override-module',opt,str(c),str(exported_helpers),
             str(repo/'runtime/rhat_runtime.c'),'-o',str(exe)])
        run([str(exe)])
        for mode in ('count','size','len','partial'):
            result=run([str(exe),mode],1)
            assert 'at line 7: data stack overflow' in result.stderr,result.stderr
    suite.mark('emitted_append_overflow_overlap_and_empty_source')
