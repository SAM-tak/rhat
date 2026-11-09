"""Independent stack shards, bounded typed channels and rejected cross-thread borrows."""
import os
from harness import Suite

HEADER = '''
let^c = import^std.channel
let^t = import^std.thread
'''

BRIDGE = """
import^std.sys
externC^rh_thread_spawn:p^entry:externC^p^ref^std.sys.ThreadArgs;,
    transfer:ref^read^std.sys.ThreadTransfer, args:ref^read^std.sys.ThreadArgs,
    stacks:i64^ -> i64^;
externC^rh_thread_join:p^handle:i64^;
"""


def main():
    suite = Suite('threads')
    positive, negative = suite.positive, suite.negative

    alternate = suite.work / 'alternate_thread.c'
    implementation = (suite.repo / 'runtime/rhat_thread.c').as_posix()
    alternate.write_text(f'''#define rh_thread_spawn original_thread_spawn
#include "{implementation}"
#undef rh_thread_spawn
int64_t rh_thread_spawn(void (*entry)(void *), const RhTransferType *type, void *args, uint64_t stacks)
{{
    puts("alternate runtime");
    return original_thread_spawn(entry, type, args, stacks);
}}
int64_t platform_spawn(void (*entry)(void *), const RhTransferType *type, void *args, uint64_t stacks)
{{
    puts("custom symbol");
    return original_thread_spawn(entry, type, args, stacks);
}}
''', encoding='utf-8')
    launch = '''
let^worker = p^arg:ref^i32^ { print^(*arg) }
var^arg:i32^ = 91
let^handle = rh_thread_spawn(thread_entry^(worker, ref^arg),
    thread_transfer_type^(ref^arg), thread_args^(ref^arg), thread_stack_count^())
assert^(handle != 0)
rh_thread_join(handle)
'''
    positive('alternate_thread_runtime', BRIDGE + launch, 'alternate runtime\n91\n',
             extra=('--thread-runtime', alternate))
    positive('custom_spawn_symbol', (BRIDGE + launch).replace('rh_thread_spawn', 'platform_spawn'),
             'custom symbol\n91\n', extra=('--thread-runtime', alternate))

    positive('public_spawn_without_std_thread', BRIDGE + '''
let^worker = p^args:ref^i32^[grow^] {
    args[0] := 42
    print^(args[0], args.len)
}
var^args = i32^[grow^]{7, 8}
let^handle = rh_thread_spawn(thread_entry^(worker, ref^args),
    thread_transfer_type^(ref^args), thread_args^(ref^args), thread_stack_count^())
assert^(handle != 0)
rh_thread_join(handle)
print^(args[0])
''', '42 2\n7\n')

    custom = suite.work / 'my'
    custom.mkdir(exist_ok=True)
    (custom / 'thread.rh').write_text('module^my.thread\n' + BRIDGE + '''
public^errordef^Error { StartFailed }
public^def^Thread : std.sys.Disposable {
    self^{ handle:i64^ }
    start = template^p^entry, args -> Self^ | Error.StartFailed {
        let^handle = rh_thread_spawn(thread_entry^(entry, args),
            thread_transfer_type^(args), thread_args^(args), thread_stack_count^())
        if^handle = 0 { return^Error.StartFailed{} }
        return^Self^{handle = handle}
    }
    dispose = p^self^ { rh_thread_join(self^.handle) }
}
''', encoding='utf-8')
    positive('custom_thread_library', '''
let^my = import^my.thread
let^worker = p^value:ref^i32^ { print^(*value) }
var^value:i32^ = 73
do^{
    var^thread = try^my.Thread.start(worker, ref^value)
}
print^("joined")
''', '73\njoined\n')
    negative('public_spawn_rejects_views', BRIDGE + '''
let^worker = p^args:ref^string^ {}
var^args = "parent view"
thread_entry^(worker, ref^args)
''', 'thread transfer refuses')
    negative('public_spawn_requires_procedure', BRIDGE + '''
let^worker = p^args:ref^i32^ {}
let^start = f^args:ref^i32^ { thread_entry^(worker, args) }
var^args:i32^ = 0
let^callback = start(ref^args)
''', 'thread operations require p^')
    negative('erased_arguments_cannot_escape', BRIDGE + '''
let^bad = p^ -> ref^read^std.sys.ThreadArgs {
    var^local:i32^ = 1
    return^thread_args^(ref^local)
}
let^escaped = bad()
''', 'escape')
    negative('bridge_arguments_require_reference', BRIDGE + '''
thread_transfer_type^(1)
''', 'must be borrowed with ref^')
    negative('bridge_arity', 'thread_stack_count^(1)\n', 'invalid thread operation arguments')
    positive('enum_thread_arguments', BRIDGE + '''
enum^Mode:i32^ { Ready = 1, Done = 2 }
let^worker = p^arg:ref^Mode { print^(*arg = Mode.Done) }
var^arg = Mode.Done
let^handle = rh_thread_spawn(thread_entry^(worker, ref^arg),
    thread_transfer_type^(ref^arg), thread_args^(ref^arg), thread_stack_count^())
assert^(handle != 0)
rh_thread_join(handle)
''', 'true\n')

    positive('bounded_and_closed', HEADER + '''
var^q = try^c.Channel<i32^>.new(1, 4)
print^(q.try_receive() fits^c.Empty)
var^a:i32^ = 12
var^b:i32^ = 34
try^q.try_send(ref^a)
var^full = q.try_send(ref^b)
if^full fits^c.Error { print^(full fits^c.Error.Full) else^: print^(false^) }
var^r = q.receive()
if^r fits^c.Message<i32^> { print^(r.value) }
try^q.send(ref^b)
q.close()
q.close()
var^closed = q.try_send(ref^a)
if^closed fits^c.Error { print^(closed fits^c.Error.Closed) else^: print^(false^) }
var^s = q.receive()
if^s fits^c.Message<i32^> { print^(s.value) }
print^(q.receive() fits^c.Closed)
var^invalid = c.Channel<i32^>.new(0, 4)
if^invalid fits^c.Error { print^(invalid fits^c.Error.InvalidCapacity) else^: print^(false^) }
var^small = try^c.Channel<i32^>.new(1, 3)
var^oversized = small.try_send(ref^a)
if^oversized fits^c.Error { print^(oversized fits^c.Error.MessageTooLarge) else^: print^(false^) }
''', 'true\ntrue\n12\ntrue\n34\ntrue\ntrue\ntrue\n')

    positive('independent_channels_and_clone', HEADER + '''
var^a = try^c.Channel<i32^>.new(2, 8)
var^b = try^c.Channel<i32^>.new(2, 8)
var^n:i32^ = 7
try^a.send(ref^n)
print^(b.try_receive() fits^c.Empty)
do^{ var^hold = a.clone(); hold.close() }
var^r = a.receive()
if^r fits^c.Message<i32^> { print^(r.value) }
print^(a.receive() fits^c.Closed)
''', 'true\n7\ntrue\n')

    pool = HEADER + '''
def^Args { self^{ jobs:c.Channel<i32^>, results:c.Channel<i32^> } }
let^worker = p^a:ref^Args {
    repeat^{
        var^r = a.jobs.receive()
        if^r fits^c.Closed { break^ }
        if^r fits^c.Message<i32^> {
            var^local = i32^[grow^]{r.value, r.value}
            var^n = local[0] + local[1]
            try^a.results.send(ref^n)
        }
    }
}
var^jobs = try^c.Channel<i32^>.new(64, 4096)
var^results = try^c.Channel<i32^>.new(64, 4096)
var^args = Args{jobs = jobs.clone(), results = results.clone()}
var^one = try^t.spawn(worker, ref^args)
var^two = try^t.spawn(worker, ref^args)
for^i from^1 to^50 { var^n:i32^ = i; try^jobs.send(ref^n) }
jobs.close()
var^total:i32^ = 0
for^i from^1 to^50 {
    var^r = results.receive()
    if^r fits^c.Message<i32^> { total += r.value }
}
one.join()
one.join()
two.join()
print^(total)
'''
    positive('worker_pool', pool, '2550\n')

    positive('owned_result_survives_sender', HEADER + '''
def^Args { self^{ results:c.Channel<u8^[grow^]> } }
let^worker = p^a:ref^Args {
    var^bytes = u8^[grow^]{1, 2, 3}
    try^a.results.send(ref^bytes)
    bytes[0] := 9
}
var^q = try^c.Channel<u8^[grow^]>.new(2, 1024)
var^args = Args{results = q.clone()}
var^thread = try^t.spawn(worker, ref^args)
thread.join()
var^r = q.receive()
if^r fits^c.Message<u8^[grow^]> { print^(r.value.len, r.value[0], r.value[2]) }
''', '3 1 3\n')

    positive('owned_start_arguments', HEADER + '''
def^Args { self^{ out:c.Channel<i32^>, values:i32^[grow^] } }
let^worker = p^a:ref^Args {
    var^sum:i32^ = 0
    for^n in^a.values { sum += n }
    try^a.out.send(ref^sum)
}
var^q = try^c.Channel<i32^>.new(1, 4)
var^args = Args{out = q.clone(), values = {1, 2, 3}}
var^thread = try^t.spawn(worker, ref^args)
args.values[0] := 99
thread.join()
var^r = q.receive()
if^r fits^c.Message<i32^> { print^(r.value) }
''', '6\n')

    positive('nil_and_empty_are_distinct', HEADER + '''
var^q = try^c.Channel<nil^>.new(1, 8)
var^n = nil^
try^q.send(ref^n)
print^(q.receive() fits^c.Message<nil^>)
print^(q.try_receive() fits^c.Empty)
''', 'true\ntrue\n')

    positive('aggregate_union_and_bounded', HEADER + '''
def^Pair { self^{ x:i32^, y:i64^ } }
var^q = try^c.Channel<Pair | nil^>.new(2, 256)
var^value:Pair | nil^ = Pair{x = 3, y = 8}
try^q.send(ref^value)
var^r = q.receive()
if^r fits^c.Message<Pair | nil^> {
    if^r.value fits^Pair { print^(r.value.x, r.value.y) }
}
var^b = try^c.Channel<i32^[max^3]>.new(1, 128)
var^numbers = i32^[max^3]{4, 5}
try^b.send(ref^numbers)
var^s = b.receive()
if^s fits^c.Message<i32^[max^3]> { print^(s.value.len, s.value[1]) }
''', '3 8\n2 5\n')

    positive('automatic_join_and_repeated_shards', HEADER + '''
def^Args { self^{ out:c.Channel<i32^> } }
let^worker = p^a:ref^Args {
    var^values = i32^[grow^]{21, 22}
    var^n = values[0] + values[1]
    try^a.out.send(ref^n)
}
var^q = try^c.Channel<i32^>.new(32, 1024)
for^i from^0 to^19 {
    var^args = Args{out = q.clone()}
    var^thread = try^t.spawn(worker, ref^args)
}
var^sum:i32^ = 0
for^i from^0 to^19 {
    var^r = q.receive()
    if^r fits^c.Message<i32^> { sum += r.value }
}
print^(sum, t.hardware_threads() > 0)
''', '860 true\n')

    positive('close_wakes_waiters', HEADER + '''
def^Args { self^{ input:c.Channel<i32^>, done:c.Channel<i32^>, send:bool^ } }
let^worker = p^a:ref^Args {
    var^n:i32^ = 1
    try^a.done.send(ref^n)
    if^a.send {
        var^status = a.input.send(ref^n)
        if^status fits^c.Error { n := 7 }
    else^:
        var^r = a.input.receive()
        if^r fits^c.Closed { n := 9 }
    }
    try^a.done.send(ref^n)
}
for^mode from^0 to^1 {
    var^input = try^c.Channel<i32^>.new(1, 4)
    var^done = try^c.Channel<i32^>.new(2, 8)
    var^n:i32^ = 0
    if^mode = 1 { try^input.send(ref^n) }
    var^args = Args{input = input.clone(), done = done.clone(), send = mode = 1}
    var^thread = try^t.spawn(worker, ref^args)
    var^ready = done.receive()
    input.close()
    var^r = done.receive()
    if^r fits^c.Message<i32^> { print^(r.value) }
    thread.join()
}
''', '9\n7\n')

    positive('default_channel_is_closed', HEADER + '''
var^q:c.Channel<i32^> = {}
var^n:i32^ = 0
print^(q.receive() fits^c.Closed)
var^hold = q.clone()
hold.close()
var^r = q.send(ref^n)
print^(r fits^c.Error)
''', 'true\ntrue\n')

    positive('large_shards_and_owned_reuse', HEADER + '''
def^Args { self^{ out:c.Channel<i64^> } }
let^worker = p^a:ref^Args {
    for^i from^0 to^2 {
        var^data = i64^[grow^]{i64^(17) repeat^300000}
        var^n = data[299999] + data[0]
        try^a.out.send(ref^n)
    }
}
var^out = try^c.Channel<i64^>.new(8, 64)
var^args = Args{out = out.clone()}
var^a = try^t.spawn(worker, ref^args)
var^b = try^t.spawn(worker, ref^args)
var^main = i64^[grow^]{i64^(99) repeat^300000}
a.join()
b.join()
var^sum:i64^ = 0
for^i from^0 to^5 {
    var^r = out.receive()
    if^r fits^c.Message<i64^> { sum += r.value }
}
print^(sum, main[299999])
''', '204 99\n')

    positive('nested_worker_shards', HEADER + '''
def^Args { self^{ out:c.Channel<i32^> } }
let^leaf = p^a:ref^Args {
    var^values = i32^[grow^]{5, 8}
    var^sum = values[0] + values[1]
    try^a.out.send(ref^sum)
}
let^parent = p^a:ref^Args {
    var^args = Args{out = a.out.clone()}
    var^child = try^t.spawn(leaf, ref^args)
    child.join()
}
var^out = try^c.Channel<i32^>.new(1, 4)
var^args = Args{out = out.clone()}
var^thread = try^t.spawn(parent, ref^args)
thread.join()
var^r = out.receive()
if^r fits^c.Message<i32^> { print^(r.value) }
''', '13\n')

    module = suite.work / 'workers'
    module.mkdir(exist_ok=True)
    (module / 'entry.rh').write_text('''module^workers.entry
let^c = import^std.channel
public^def^Args { self^{ out:c.Channel<i32^> } }
public^let^run = p^a:ref^Args {
    var^value:i32^ = 29
    try^a.out.send(ref^value)
}
''', encoding='utf-8')
    positive('cross_module_worker', HEADER + '''
let^w = import^workers.entry
var^out = try^c.Channel<i32^>.new(1, 4)
var^args = w.Args{out = out.clone()}
var^thread = try^t.spawn(w.run, ref^args)
thread.join()
var^r = out.receive()
if^r fits^c.Message<i32^> { print^(r.value) }
''', '29\n')

    positive('worker_error_is_fatal', HEADER + '''
errordef^E { Bad }
let^worker = p^a:ref^i32^ -> nil^ | E { return^E.Bad{} }
var^n:i32^ = 0
var^thread = try^t.spawn(worker, ref^n)
thread.join()
''', 'worker returned an unhandled error', failure=True)

    for name, ty in [('reference', 'ref^i32^'), ('read_reference', 'ref^read^i32^'),
                     ('slice', 'i32^[..]'), ('string_view', 'string^')]:
        negative('reject_' + name, HEADER + f'var^q = try^c.Channel<{ty}>.new(1, 128)\n',
                 'thread transfer refuses')
    negative('reject_channel_message', HEADER + 'var^q = try^c.Channel<c.Channel<i32^>>.new(1, 128)\n',
             'Channel cannot be sent through a Channel')
    negative('reject_nested_channel_message', HEADER + '''
def^Hidden { self^{ q:c.Channel<i32^> } }
var^q = try^c.Channel<Hidden>.new(1, 128)
''', 'Channel cannot be sent through a Channel')
    negative('reject_disposable', HEADER + '''
import^std.sys
def^Resource : std.sys.Disposable { dispose = p^self^ {} }
var^q = try^c.Channel<Resource>.new(1, 128)
''', 'thread transfer refuses Disposable')
    negative('reject_thread_argument', HEADER + '''
let^worker = p^a:ref^t.Thread {}
var^q:t.Thread = {}
var^thread = try^t.spawn(worker, ref^q)
''', 'thread transfer refuses Disposable')
    negative('reject_argument_view', HEADER + '''
def^Args { self^{ view:string^ } }
let^worker = p^a:ref^Args {}
var^args = Args{view = "hello"}
var^thread = try^t.spawn(worker, ref^args)
''', 'thread transfer refuses')
    negative('reject_parent_binding', HEADER + '''
var^state:i32^ = 0
let^worker = p^a:ref^i32^ { state += 1 }
var^n:i32^ = 0
var^thread = try^t.spawn(worker, ref^n)
''', 'cannot be a value')
    negative('reject_indirect_state', HEADER + '''
def^State {
    var^value:i32^ = 0
    touch = p^ { def^.value += 1 }
}
let^worker = p^a:ref^i32^ { State.touch() }
var^n:i32^ = 0
var^thread = try^t.spawn(worker, ref^n)
''', 'thread entry or exported function cannot access')
    negative('reject_implicit_handle_copy', HEADER + '''
var^q = try^c.Channel<i32^>.new(1, 4)
var^copy = q
''', 'cannot be copied')
    negative('reject_private_intrinsic', '__rh_channel_new(1, 4)\n', 'private thread operation')
    if not suite.args.checks_only:
        def native_lifetime():
            exe = suite.work / 'native-thread-lifetime.exe'
            flags = [] if os.name == 'nt' else ['-pthread', '-lm']
            suite.run([suite.clang, '-std=c11', '-O2', suite.repo / 'tests/thread_runtime.c', *flags, '-o', exe])
            assert suite.run([exe]).stdout == 'native lifetime checks passed\n'
        suite.case('native_lifetime_and_allocation_failure', native_lifetime)
    suite.finish()


if __name__ == '__main__':
    main()
