"""Shared test harness: arguments, case helpers and a parallel case pool.

Cases are queued and run on worker threads; finish() waits for them, reports
every failure and exits nonzero if any case failed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
import traceback


class Suite:
    def __init__(self, label):
        parser = argparse.ArgumentParser()
        parser.add_argument('--compiler', required=True)
        parser.add_argument('--clang', required=True)
        parser.add_argument('--work', required=True)
        parser.add_argument('--filter', default='', help='Run matching named cases (regular expression)')
        parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
        parser.add_argument('--checks-only', action='store_true', help='Skip native builds')
        parser.add_argument('--all-opts', action='store_true', help='Also build cases at -O0')
        parser.add_argument('--timings', action='store_true', help='Report cumulative subprocess times')
        self.args = parser.parse_args()
        self.opts = ('-O0', '-O2') if self.args.all_opts else ('-O2',)
        self.label = label
        self.compiler = str(Path(self.args.compiler).resolve())
        self.clang = str(Path(self.args.clang).resolve())
        self.work = Path(self.args.work).resolve()
        self.work.mkdir(parents=True, exist_ok=True)
        self.repo = Path(__file__).resolve().parents[1]
        self.passed = 0
        self.failures = []
        self.lock = threading.Lock()
        self.pool = ThreadPoolExecutor(max_workers=max(1, self.args.jobs))
        self.pending = []
        self.started = time.perf_counter()
        self.timings = {}

    def run(self, command, success=True, env=None, timeout=90):
        command = list(map(str, command))
        started = time.perf_counter()
        try:
            p = subprocess.run(command, capture_output=True, text=True, timeout=timeout, env=env)
        finally:
            if self.args.timings:
                stage = ('rhat ' + command[1] if command[0] == self.compiler else
                         'clang' if command[0] == self.clang else 'executable')
                with self.lock:
                    count, elapsed = self.timings.get(stage, (0, 0))
                    self.timings[stage] = count + 1, elapsed + time.perf_counter() - started
        assert (p.returncode == 0) == success, (command, p.returncode, p.stdout, p.stderr)
        return p

    def source(self, name, text):
        path = self.work / (name + '.rh')
        path.write_text(text, encoding='utf-8')
        return path

    def selected(self, name):
        return not self.args.filter or re.search(self.args.filter, name)

    def case(self, name, body):
        """Queue body() as one named case; return its future, or None if filtered."""
        if not self.selected(name):
            return None
        def task():
            try:
                body()
            except BaseException:
                with self.lock:
                    self.failures.append((name, traceback.format_exc()))
                    print('FAIL', name, flush=True)
                return
            with self.lock:
                self.passed += 1
                print('PASS', name, flush=True)
        future = self.pool.submit(task)
        self.pending.append(future)
        return future

    def mark(self, name):
        """Record a case that ran inline on the calling thread."""
        with self.lock:
            self.passed += 1
            print('PASS', name, flush=True)

    def build(self, name, opt, extra=(), inspect=False):
        """Build one case at one optimization level; return (exe, rir)."""
        path = self.work / (name + '.rh')
        exe = self.work / (name + opt + '.exe')
        rir = exe.with_suffix('.rir')
        dumps = ['--dump-ir', rir, '--emit-llvm', str(exe) + '.ll'] if inspect else []
        self.run([self.compiler, 'build', path, '-o', exe, '--clang', self.clang, opt,
                  *dumps, *extra])
        return exe, rir

    def positive(self, name, text, expected, inspect=None, failure=False, extra=()):
        """Build at each level in opts, run, and compare stdout (or stderr on failure)."""
        if self.args.checks_only:
            self.case(name, lambda: self.run([self.compiler, 'check', self.source(name, text)]))
            return
        def body():
            self.source(name, text)
            for opt in self.opts:
                exe, rir = self.build(name, opt, extra, inspect=inspect is not None)
                if inspect:
                    inspect(rir.read_text(), Path(str(exe) + '.ll').read_text())
                p = self.run([exe], not failure)
                if failure:
                    assert expected in p.stderr, (name, opt, p.stderr)
                else:
                    assert p.stdout == expected, (name, opt, p.stdout, expected)
        self.case(name, body)

    def runtime_failure(self, name, text, diagnostic, extra=()):
        self.positive(name, text, diagnostic, failure=True, extra=extra)

    def negative(self, name, text, diagnostic):
        """Check that the compiler rejects text with a located diagnostic."""
        def body():
            p = self.run([self.compiler, 'check', self.source(name, text)], False)
            assert diagnostic in p.stderr, (name, diagnostic, p.stderr)
            assert re.search(r':\d+:\d+: error:', p.stderr), p.stderr
        self.case(name, body)

    def finish(self):
        for future in self.pending:
            future.result()
        self.pool.shutdown()
        for name, trace in self.failures:
            print(f'--- {name}\n{trace}', file=sys.stderr, flush=True)
        coverage = ('native builds skipped' if self.args.checks_only else
                    f'executable cases checked at {" and ".join(self.opts)}')
        print(f'{self.passed} {self.label} cases passed, {len(self.failures)} failed; {coverage}; '
              f'{time.perf_counter() - self.started:.2f}s elapsed.', flush=True)
        if self.args.timings:
            for stage, (count, elapsed) in sorted(self.timings.items()):
                print(f'  {stage}: {count} commands, {elapsed:.2f}s cumulative', flush=True)
        sys.exit(1 if self.failures else 0)
