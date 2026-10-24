"""Bind each benchmark label to its own build artifact, rejecting replacement."""
import hashlib
import os
import pathlib


class Executables:
    def __init__(self):
        self.hashes = {}
        self._paths = set()

    @staticmethod
    def key(path):
        return str(pathlib.Path(path).resolve())

    @staticmethod
    def digest(path):
        return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

    def add(self, path):
        key = self.key(path)
        normalized = os.path.normcase(key)
        if normalized in self._paths:
            raise RuntimeError(f"duplicate benchmark executable path: {key}")
        self._paths.add(normalized)
        self.hashes[key] = self.digest(key)

    def measure(self, path, runner):
        key = self.key(path)
        if key not in self.hashes:
            raise RuntimeError(f"unregistered benchmark executable: {key}")
        expected = self.hashes[key]
        if self.digest(key) != expected:
            raise RuntimeError(f"benchmark executable changed after build: {key}")
        result = runner(path)
        if self.digest(key) != expected:
            raise RuntimeError(f"benchmark executable changed during execution: {key}")
        return dict(result, executable_sha256=expected)

def overwritten_rhat_rows(data):
    """Identify old R^ labels whose executable was subsequently built by Rust."""
    final_producer, rhat_outputs = {}, {}
    for argv in data.get("builds", []):
        if "-o" not in argv:
            continue
        output = str(argv[argv.index("-o")+1]).replace('\\', '/').casefold()
        inputs = [str(arg).replace('\\', '/') for arg in argv]
        source = next((arg for arg in inputs if arg.endswith('.rh')), None)
        if source and len(argv) > 1 and argv[1] == 'build':
            producer = 'rhat'
            rhat_outputs[pathlib.PurePosixPath(source).stem] = output
        elif any(arg.endswith('.rs') for arg in inputs):
            producer = 'rust'
        else:
            producer = 'other'
        final_producer[output] = producer
    invalid = set()
    for row in data.get('rows', []):
        if row.get('language') != 'rhat':
            continue
        workload, size = row.get('workload', 'bintrees'), row['size']
        output = rhat_outputs.get(f'{workload}_{size}')
        if output and final_producer.get(output) != 'rhat':
            invalid.add((workload,size))
    return invalid
