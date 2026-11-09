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

    positive('current_thread_id_without_spawn', HEADER + '''
let^id = t.current_id()
print^(id > 0, id = t.current_id())
''', 'true true\n')
    positive('thread_ids_match_worker_and_survive_join', HEADER + '''
struct^Args { out:c.Channel<i64^> }
let^worker = p^arg:ref^Args {
    var^id = t.current_id()
    assert^(id = t.current_id())
    try^arg.out.send(ref^id)
}
let^read_id = f^value:ref^read^t.Thread -> i64^ { value.id() }
let^main_id = t.current_id()
var^out = try^c.Channel<i64^>.new(2, 16)
var^arg = Args{out = out.clone()}
var^one = try^t.spawn(worker, ref^arg)
var^two = try^t.spawn(worker, ref^arg)
let^one_id = read_id(ref^one)
let^two_id = two.id()
assert^(one_id > 0 and^ two_id > 0 and^ one_id != two_id)
assert^(one_id != main_id and^ two_id != main_id)
one.join()
two.join()
assert^(one.id() = one_id and^ two.id() = two_id)
var^last_id = i64^(0)
for^i from^1 to^2 {
    var^r = out.receive()
    if^r fits^c.Message<i64^> {
        assert^(r.value = one_id or^ r.value = two_id)
        assert^(r.value != last_id)
        last_id := r.value
    else^: assert^(false^)
    }
}
var^previous = two_id
for^i from^1 to^10 {
    var^thread = try^t.spawn(worker, ref^arg)
    assert^(thread.id() > previous)
    previous := thread.id()
    thread.join()
    var^r = out.receive()
    if^r fits^c.Message<i64^> { assert^(r.value = previous) }
}
var^empty:t.Thread = {}
assert^(empty.id() = 0)
assert^(t.current_id() = main_id)
print^("thread IDs match")
''', 'thread IDs match\n')
    negative('current_thread_id_requires_procedure', HEADER + '''
let^bad = f^ { t.current_id() }
print^(bad())
''', 'f^ cannot call p^')

    positive('sleep_seconds_and_worker', HEADER + '''
t.sleep(0)
t.sleep(-1)
t.sleep(0.002)
let^worker = p^arg:ref^i32^ {
    t.sleep(0.002)
    print^(*arg)
}
var^arg:i32^ = 42
var^thread = try^t.spawn(worker, ref^arg)
thread.join()
print^("awake")
''', '42\nawake\n')
    negative('sleep_requires_procedure', HEADER + '''
let^bad = f^ { t.sleep(0.001) }
bad()
''', 'f^ cannot call p^')
    for name, value in (('nan','m.sqrt(-1.0)'),('infinity','m.exp(1000.0)')):
        suite.runtime_failure('sleep_rejects_' + name, HEADER +
            'let^m = import^std.math\nt.sleep(' + value + ')\n',
            'sleep requires finite seconds')

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
int64_t test_channel_holds(const void *args)
{{
    int64_t handle;
    memcpy(&handle, args, sizeof(handle));
    return atomic_load(&channel_of(handle)->holds);
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
''', "contains noncopyable 'channel_handle^<i32^>' at 'handle' and cannot be copied")
    positive('public_typed_handles', '''
let^c = import^std.channel
let^receive = p^h:ref^channel_handle^<i32^> -> c.Message<i32^> | c.Closed | c.Empty {
    return^channel_receive^(h, i64^(0))
}
var^h = channel_new^(i32^, i64^(2), i64^(8))
print^(channel_valid^(ref^h))
h := channel_clone^(ref^h)
do^{
    var^copy = channel_clone^(ref^h)
    var^value:i32^ = 42
    print^(channel_send^(ref^copy, ref^value, i64^(0)))
}
var^result = receive(ref^h)
if^result fits^c.Message<i32^> { print^(result.value) }
channel_close^(ref^h)
print^(receive(ref^h) fits^c.Closed)
var^failed = channel_new^(i32^, i64^(0), i64^(8))
print^(channel_valid^(ref^failed))
print^(receive(ref^failed) fits^c.Closed)
''', 'true\n0\n42\ntrue\nfalse\ntrue\n')
    positive('default_typed_handle_cleanup', '''
def^Holder { self^{ h:channel_handle^<i32^> } }
var^q:Holder = {}
print^("empty")
''', 'empty\n')
    positive('typed_handle_array_cleanup', '''
var^h = channel_new^(i32^, i64^(1), i64^(8))
do^{
    var^holds:channel_handle^<i32^>[grow^] = { channel_clone^(ref^h) }
    print^(channel_valid^(ref^holds[0]))
}
print^(channel_valid^(ref^h))
channel_close^(ref^h)
''', 'true\ntrue\n')
    positive('typed_handle_refcounts', '''
import^std.sys
externC^test_channel_holds:f^args:ref^read^std.sys.ThreadArgs -> i64^;
var^h = channel_new^(i32^, i64^(1), i64^(8))
print^(test_channel_holds(thread_args^(ref^h)))
do^{
    var^copy = channel_clone^(ref^h)
    var^holds:channel_handle^<i32^>[grow^] = { channel_clone^(ref^h) }
    print^(test_channel_holds(thread_args^(ref^h)))
}
print^(test_channel_holds(thread_args^(ref^h)))
h := channel_clone^(ref^h)
print^(test_channel_holds(thread_args^(ref^h)))
''', '1\n3\n1\n1\n', extra=('--thread-runtime', alternate))
    (custom / 'channel.rh').write_text('''module^my.channel
let^c = import^std.channel
public^def^Queue<T> {
    self^{ private^h:channel_handle^<T> }
    new = p^ -> Self^ { return^Self^{h = channel_new^(T, i64^(2), i64^(128))} }
    clone = p^self^ -> Self^ { return^Self^{h = channel_clone^(ref^self^.h)} }
    send = p^self^, value:ref^read^T -> i64^ {
        return^channel_send^(ref^self^.h, value, i64^(1))
    }
    receive = p^self^ -> c.Message<T> | c.Closed | c.Empty {
        return^channel_receive^(ref^self^.h, i64^(1))
    }
}
''', encoding='utf-8')
    positive('custom_channel_across_thread', HEADER + '''
let^my = import^my.channel
def^Args { self^{ out:my.Queue<i32^> } }
let^worker = p^args:ref^Args {
    var^n:i32^ = 67
    assert^(args.out.send(ref^n) = 0)
}
var^q = my.Queue<i32^>.new()
var^args = Args{out = q.clone()}
var^thread = try^t.spawn(worker, ref^args)
thread.join()
var^r = q.receive()
if^r fits^c.Message<i32^> { print^(r.value) }
''', '67\n')
    negative('typed_handle_send_mismatch', '''
var^h = channel_new^(i32^, i64^(1), i64^(8))
var^wrong:i64^ = 1
channel_send^(ref^h, ref^wrong, i64^(0))
''', "handle's element type")
    negative('typed_handle_receive_mismatch', '''
let^c = import^std.channel
let^bad = p^h:ref^channel_handle^<i32^> -> c.Message<i64^> | c.Closed | c.Empty {
    return^channel_receive^(h, i64^(0))
}
var^h = channel_new^(i32^, i64^(1), i64^(8))
var^r = bad(ref^h)
''', 'channel_receive^ result must be')
    negative('typed_handle_cannot_be_forged',
             'var^h:channel_handle^<i32^> = 123\n', 'channel_handle^')
    negative('typed_handle_cannot_be_copied', '''
var^h = channel_new^(i32^, i64^(1), i64^(8))
var^copy = h
''', "is a noncopyable 'channel_handle^<i32^>' and cannot be copied")
    handle_members = '''
def^Inner { self^{ handle:channel_handle^<i32^> } }
def^Outer { self^{ inner:Inner } }
'''
    negative('user_handle_member_copy_reason', handle_members + '''
var^value:Outer = {}
var^copy = value
''', "contains noncopyable 'channel_handle^<i32^>' at 'inner.handle' and cannot be copied")
    negative('user_handle_member_argument_reason', handle_members + '''
let^take = p^value:Outer {}
var^value:Outer = {}
take(value)
''', "contains noncopyable 'channel_handle^<i32^>' at 'inner.handle' and cannot be passed by value")
    negative('array_handle_member_copy_reason', handle_members + '''
def^Many { self^{ items:Inner[2] } }
var^value:Many = {}
var^copy = value
''', "contains noncopyable 'channel_handle^<i32^>' at 'items[].handle'")
    negative('union_handle_member_copy_reason', handle_members + '''
let^make = p^ -> Outer | nil^ { return^Outer{} }
var^value = make()
var^copy = value
''', "contains noncopyable 'channel_handle^<i32^>' at 'inner.handle'")
    negative('typed_handle_clone_requires_argument', 'channel_clone^()\n', 'invalid thread operation arguments')
    negative('typed_handle_temporary_needs_owner',
             'channel_new^(i32^, i64^(1), i64^(8))\n', 'temporary has nothing to dispose it')
    negative('typed_handle_rejects_integer',
             'channel_receive^(i64^(123), i64^(0))\n', 'requires a reference to channel_handle^')
    negative('typed_handle_cannot_be_cast_to_integer', '''
var^h = channel_new^(i32^, i64^(1), i64^(8))
print^(i64^(h))
''', 'numeric conversion requires a number')
    negative('typed_handle_rejects_message_handles', HEADER + '''
var^q = try^c.Channel<channel_handle^<i32^>>.new(1, 128)
''', 'Channel cannot be sent through a Channel')
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
