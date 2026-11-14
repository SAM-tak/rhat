"""lib/lhat on a host that links a real L^: L^ scripts run with lhat.task and lhat.async
registered as L^'s std.task and std.async."""
from pathlib import Path
import sys

from harness import Suite

HOST = r'''// Checks, compiles and runs an L^ script on a program whose std.task and std.async are R^'s.
#include <stdio.h>
#include <stdlib.h>
#include "lhat.h"
#include "stdlib/channel.h"
#include "stdlib/io.h"
bool rhat_lhat_register_lhat_task(LhatProgram *program, const char *module);
bool rhat_lhat_register_lhat_async(LhatProgram *program, const char *module);
static char *host_load(void *context, const char *path, size_t *length)
{
    (void)context;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)n + 1);
    *length = fread(text, 1, (size_t)n, f);
    text[*length] = 0;
    fclose(f);
    return text;
}
// The debugger pause points host functions offered on the main machine: what a DAP pause stops at.
static int64_t host_pauses;
static inline int64_t host_paused(void)
{
    return host_pauses;
}
static void host_hook(LhatMachine *machine, void *context, LhatDebugEvent event,
                      const LhatFrameInfo *where)
{
    (void)machine;
    (void)context;
    (void)where;
    host_pauses += event == LHAT_DEBUG_HOST_PAUSE_POINT;
}
// A host's own roots, under integer keys of its own making.
static inline void host_clobber(LhatMachine *machine)
{
    bool refused = false;
    for (LhatInteger k = -2; k <= 2; k++) {
        lhat_machine_table_set(machine, lhat_machine_host_root(machine), lhat_integer(k),
                               lhat_integer(k * 100), &refused);
    }
}
static inline int32_t host_run(const char *path, bool (*more)(LhatProgram *, const char *))
{
    LhatProgram *program = lhat_program_new(true, host_load, NULL);
    if ((more && !more(program, "rhat")) || !lhatstdlib_io_register(program) ||
        !lhatstdlib_channel_register(program) ||
        !rhat_lhat_register_lhat_task(program, "std.task") ||
        !rhat_lhat_register_lhat_async(program, "std.async")) {
        return -1;
    }
    const LhatUnit *root = lhat_program_check(program, path);
    for (const LhatUnit *u = lhat_program_units(program); u; u = lhat_unit_next(u)) {
        for (size_t k = 0; k < lhat_unit_diagnostic_count(u); k++) {
            char text[1024];
            lhat_unit_diagnostic_write(u, k, false, text, sizeof text);
            fprintf(stderr, "%s\n", text);
        }
    }
    if (!root || lhat_program_has_errors(program) || !lhat_unit_ok(root) ||
        !lhat_program_compile(program)) {
        return -2;
    }
    LhatMachine *machine = lhat_machine_new();
    lhat_program_install(program, machine);
    lhat_machine_set_debug_hook(machine, host_hook, NULL);
    LhatRunResult ran = lhat_run(machine, lhat_unit_proto(root));
    if (ran.status != LHAT_RUN_OK) {
        char text[1024] = "";
        lhat_value_write(ran.value, text, sizeof text);
        fprintf(stderr, "%s %s (line %u)\n", lhat_run_status_message(ran.status), text,
                (unsigned)ran.line);
    }
    fflush(stdout);
    lhat_machine_dispose(machine);
    lhat_program_free(program);
    return (int32_t)ran.status;
}
'''

MAIN = '''includeC^"host.h"
import^lhat.task
import^lhat.async
externC^def^LhatProgram;
externC^host_run:p^path:string^, more:externC^p^ref^LhatProgram, string^-> bool^; | nil^-> i32^;
{extra}print^(host_run("{script}", {more}))
'''


def main():
    suite = Suite('lhat host', options=('--lhat',))
    lhat = Path(suite.args.lhat).resolve()
    release = lhat / 'build' / 'release'
    names = ('lhatstdlib', 'lhat', 'lhatport', 'lhatthread')
    libs = [release / (f'{n}.lib' if sys.platform == 'win32' else f'lib{n}.a') for n in names]
    extra = ['--module-path', suite.repo / 'lib', '--c-include', lhat / 'include',
             '--c-include', release / 'include', '--c-include', lhat]
    for lib in libs:
        extra += ['--link-lib', lib]
    if sys.platform == 'win32':
        extra.append('--crt-dll')  # L^ builds with the DLL CRT.
    (suite.work / 'host.h').write_text(HOST, encoding='utf-8')

    # rhat is R^ for the main module; its exportL^ declarations register as L^'s rhat module.
    def case(name, script, expected, rhat='', status='0'):
        path = suite.work / f'{name}.lh'
        path.write_text(script, encoding='utf-8')
        more = 'rhat_lhat_register_main' if rhat else 'nil^'
        text = MAIN.replace('{script}', path.as_posix()).replace('{extra}', rhat)
        suite.positive(name, text.replace('{more}', more), expected + status + '\n', extra=extra)

    # Jobs run on R^ pool workers; one waits on a timer there while the other goes on.
    case('task_pool', '''import^std.io
import^std.task
import^std.async
try^std.task.start(2)
let^gen1 = p^{ yield^0 return^40 }
let^gen2 = p^{ yield^std.async.timer(0.05) return^2 }
let^t1 = try^std.task.async(gen1())
let^t2 = try^std.task.async(gen2())
var^n = 0
n += try^std.task.await(t1)
n += try^std.task.await(t2)
std.io.print(n, std.task.workers())
std.task.stop()
std.io.print(std.task.workers())
''', '42 2\n0\n')

    # A closure job; a result taken twice; a failing job, whose Failed says what failed() says; a
    # task waited on through std.async.
    case('task_outcomes', '''import^std.io
import^std.task
import^std.async
try^std.task.start(2)
let^t = try^std.task.async(f^{ 20 })
std.io.print(try^std.task.await(t), std.task.await(t) catch^ -1)
let^bad = try^std.task.async(p^{ panic^"boom" })
std.io.print(std.task.await(bad) catch^ -2, bad.done(), bad.failed() != nil^)
let^worse = try^std.task.async(p^{ panic^"bang" })
std.io.print((std.task.await(worse) catch^ it^.message) = worse.failed())
let^gen = p^{ yield^std.async.timer(0.05) return^7 }
let^slow = try^std.task.async(gen())
let^id = slow.awaitable()
std.io.print(std.async.wait(5) = id, std.async.pending(), try^std.task.await(slow))
std.task.stop()
''', '20 -1\n-2 true true\ntrue\ntrue 0 7\n')

    # A task carried to another job's machine through a channel: shared, and awaited there.
    case('task_shared', '''import^std.io
import^std.task
import^std.channel
try^std.task.start(2)
let^c = try^std.channel.new(std.task.Task<number^>)
let^first = try^std.task.async(f^{ 20 })
try^c.push(first)
let^second = try^std.task.async(p^{
    let^handed = c.demand(2)
    return^ if^handed?: std.task.await(handed) catch^ 0 + 1 el^: 0;
})
std.io.print(try^std.task.await(second))
std.task.stop()
''', '21\n')

    # join waits inside a job without holding its worker: with one worker, the job it waits for
    # runs meanwhile; a failure ends the join with the TaskError await would answer.
    case('task_join', '''import^std.io
import^std.task
import^std.async
try^std.task.start(1)
let^slow = p^{ yield^std.async.timer(0.05) return^20 }
let^inner = try^std.task.async(slow())
let^add = p^{
    let^v = try^ await^ inner.join()
    return^v + 1
}
let^outer = try^std.task.async(add())
std.io.print(try^std.task.await(outer))
let^bad = try^std.task.async(p^{ panic^"boom" })
let^probe = p^{
    let^v = await^ bad.join() catch^ -1
    return^v
}
let^second = try^std.task.async(probe())
std.io.print(try^std.task.await(second))
std.task.stop()
''', '21\n-1\n')

    # async's check, registered after lhat.task's functions: a job answering two values does not
    # fit the one slot a task carries, so the script does not compile.
    case('task_slot', '''import^std.task
let^job = p^{ yield^0 return^(1, 2) }
let^t = std.task.async(job()) catch^ panic^ it^
''', '', status='-2')

    # await offers a debugger pause point while it waits, so a DAP pause stops it there.
    case('task_pause', '''import^std.io
import^std.task
import^std.async
import^rhat
try^std.task.start(1)
let^slow = p^{ yield^std.async.timer(0.1) return^5 }
let^t = try^std.task.async(slow())
let^before = rhat.paused()
std.io.print(try^std.task.await(t), rhat.paused() > before)
std.task.stop()
''', '5 true\n', rhat='''externC^rhat_lhat_register_main:p^program:ref^LhatProgram, module:string^-> bool^;
externC^host_paused:p^-> i64^;
exportL^let^paused = p^-> i64^{ return^host_paused() }
''')

    # std.async on the host's own thread.
    case('async_waits', '''import^std.io
import^std.async
let^a = std.async.timer(0)
let^b = std.async.timer(60)
std.io.print(std.async.pending(), (std.async.next() ?? 99) < 1)
std.async.drop(b)
std.io.print(std.async.wait(1) = a, std.async.pending(), std.async.next() = nil^)
''', '2 true\ntrue 0 true\n')
    # A value the host roots, and carries off its machine and back; the host's own roots under
    # integer keys leave it be.
    case('value_wraps', '''import^std.io
import^rhat
std.io.print(rhat.roundtrip("hello"), rhat.roundtrip(42))
''', 'hello 42\n', rhat='''let^value = import^lhat.value
let^l = import^lhat.l
externC^rhat_lhat_register_main:p^program:ref^LhatProgram, module:string^-> bool^;
externC^host_clobber:p^machine:ref^l.LhatMachine;
exportL^let^roundtrip = p^machine:ref^l.LhatMachine, v:l.LhatValue -> l.LhatValue {
    let^held = value.Held.new(machine, v)
    host_clobber(machine)
    if^held fits^value.Held {
        let^carried = value.Carried.new(held.get())
        if^carried fits^value.Carried {
            let^again = carried.into(machine)
            if^again fits^l.LhatValue { return^again }
        }
    }
    return^l.lhat_nil()
}
''')
    suite.finish()


if __name__ == '__main__':
    main()
