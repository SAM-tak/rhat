"""Measure edit-to-diagnostics and refreshed editor information over LSP stdio."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from lsp import Client, at


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--module-path', action='append', default=[])
    parser.add_argument('--open', action='append', default=[], type=Path)
    parser.add_argument('--samples', type=int, default=10)
    parser.add_argument('--limit', type=float, default=1.0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('--samples must be positive')
    source, server = args.source.resolve(), args.server.resolve()
    original = source.read_bytes().decode('utf-8')
    client = Client(server)
    rows = []

    def send_request(method, params):
        client.serial += 1
        client.send(method, params, client.serial)
        return client.serial

    def response(serial):
        message = client.wait(lambda item: item.get('id') == serial, timeout=120)
        assert 'error' not in message, message
        return message.get('result')

    def diagnostics(path, version):
        return client.wait(lambda item: item.get('method') == 'textDocument/publishDiagnostics'
                           and item['params'].get('version') == version
                           and item['params']['uri'].lower() == path.as_uri().lower(),
                           timeout=120)['params']['diagnostics']

    try:
        client.request('initialize', dict(rootUri=source.parent.as_uri(), capabilities={},
                       initializationOptions=dict(modulePaths=[str(Path(p).resolve())
                                                               for p in args.module_path])))
        client.send('initialized', {})
        started = time.perf_counter()
        client.open(source, original)
        assert diagnostics(source, 1) == []
        rows.append(dict(phase='open', seconds=time.perf_counter() - started))
        for path in args.open:
            path = path.resolve()
            client.open(path, path.read_bytes().decode('utf-8'))
            assert diagnostics(path, 1) == [], path
        td = dict(textDocument=dict(uri=source.as_uri()))
        version = 1

        def corrected(burst=False):
            nonlocal version
            version += 1
            name = f'lsp_latency_revision_{version}'
            content = original + f'\nlet^ {name}=f^ ->i32^ {{42}}\n'
            started = time.perf_counter()
            client.change(source, content, version)
            tokens_id = send_request('textDocument/semanticTokens/full', td)
            assert diagnostics(source, version) == []
            diagnostic_time = time.perf_counter() - started
            tokens = response(tokens_id)
            assert tokens['data'], tokens
            hover = response(send_request('textDocument/hover',
                             dict(**td, position=at(content, name))))
            assert name in hover['contents']['value'], hover
            rows.append(dict(phase='burst_fix' if burst else 'fix', version=version,
                             diagnostics_seconds=diagnostic_time,
                             seconds=time.perf_counter() - started))

        for _ in range(args.samples):
            version += 1
            started = time.perf_counter()
            client.change(source, original + '\nprint^(lsp_latency_missing)\n', version)
            errors = diagnostics(source, version)
            assert errors and 'lsp_latency_missing' in errors[0]['message'], errors
            rows.append(dict(phase='error', version=version,
                             seconds=time.perf_counter() - started))
            corrected()

        # Simulate the editor asking for coloring after every keystroke while
        # obsolete requests and analyses are still queued or running.
        for _ in range(20):
            version += 1
            client.change(source, original + '\nprint^(lsp_latency_missing)\n', version)
            send_request('textDocument/semanticTokens/full', td)
            time.sleep(.005)
        corrected(burst=True)
    finally:
        try:
            client.close()
        finally:
            if client.process.poll() is None:
                client.process.kill()
                client.process.wait()

    maximum = max(row['seconds'] for row in rows)
    report = dict(measured_at=datetime.now(timezone.utc).isoformat(),
                  server=str(server), source=str(source),
                  server_sha256=hashlib.sha256(server.read_bytes()).hexdigest(),
                  source_sha256=hashlib.sha256(original.encode()).hexdigest(),
                  open_documents=[str(source), *[str(p.resolve()) for p in args.open]],
                  limit_seconds=args.limit, max_seconds=maximum, rows=rows)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)
    if maximum > args.limit:
        raise SystemExit(f'Latency {maximum:.3f}s exceeds {args.limit:.3f}s')


if __name__ == '__main__':
    main()
