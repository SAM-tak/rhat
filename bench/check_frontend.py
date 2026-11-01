"""Time complete, successful frontend checks, including process startup."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--module-path', action='append', default=[])
    parser.add_argument('--samples', type=int, default=20)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('--samples must be positive')

    compilers = {'current': args.compiler.resolve()}
    if args.baseline:
        compilers['baseline'] = args.baseline.resolve()
    source = args.source.resolve()
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    stdlib = Path(__file__).resolve().parents[1] / 'stdlib'
    roots = [Path(path).resolve() for path in args.module_path] + [source.parent]
    pending, inputs = [source, *stdlib.rglob('*.rh')], {}
    while pending:
        path = pending.pop()
        if path in inputs:
            continue
        inputs[path] = sha(path)
        for module in re.findall(r'import\^\s*([\w.]+)', path.read_text(encoding='utf-8')):
            relative = Path(*module.split('.')).with_suffix('.rh')
            for root in [stdlib] if module.startswith('std.') else roots:
                imported = (root / relative).resolve()
                if imported.is_file():
                    pending.append(imported)
                    break
    compiler_hashes = {name: sha(exe) for name, exe in compilers.items()}
    commands = {name: [str(exe), 'check', str(source), '--no-cache']
                for name, exe in compilers.items()}
    for command in commands.values():
        for path in args.module_path:
            command.extend(['--module-path', str(Path(path).resolve())])

    def run(command):
        started = time.perf_counter()
        result = subprocess.run(command, capture_output=True, timeout=60)
        elapsed = time.perf_counter() - started
        if result.returncode:
            raise RuntimeError(result.stderr.decode('utf-8', errors='replace'))
        return elapsed

    for command in commands.values():
        run(command)
        run(command)
    samples = {name: [] for name in commands}
    for index in range(args.samples):
        order = list(commands) if index % 2 == 0 else list(reversed(commands))
        for name in order:
            samples[name].append(run(commands[name]))

    if any(sha(path) != digest for path, digest in inputs.items()):
        raise RuntimeError('Source files changed during measurement')
    if any(sha(compilers[name]) != digest for name, digest in compiler_hashes.items()):
        raise RuntimeError('Compiler changed during measurement')
    report = {
        'measured_at': datetime.now(timezone.utc).isoformat(),
        'source': str(source), 'source_sha256': sha(source),
        'scope': 'fresh process; parse, types, lifetimes and stacks; no cache',
        'input_sha256': {str(path): digest for path, digest in sorted(inputs.items())},
        'results': {},
    }
    for name, values in samples.items():
        row = dict(command=commands[name], compiler_sha256=sha(compilers[name]),
                   median_seconds=statistics.median(values),
                   min_seconds=min(values), max_seconds=max(values), samples_seconds=values)
        report['results'][name] = row
        print(f'{name}: median {row["median_seconds"]:.6f}s '
              f'(min {min(values):.6f}s, max {max(values):.6f}s)')
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
