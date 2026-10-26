# Drive the native language server through real framed stdio messages.
import argparse
import json
from pathlib import Path
import queue
import subprocess
import threading
import time

class Client:
    def __init__(self, executable):
        self.process = subprocess.Popen([str(executable), '--stdio'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.messages = queue.Queue()
        self.pending = []
        self.serial = 0
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        self.messages.put(RuntimeError('server closed stdout'))
                        return
                    if line == b'\r\n':
                        break
                    key, value = line.decode().split(':', 1)
                    headers[key.lower()] = value.strip()
                size = int(headers['content-length'])
                self.messages.put(json.loads(self.process.stdout.read(size)))
        except Exception as error:
            self.messages.put(error)

    def send(self, method, params=None, id=None):
        message = dict(jsonrpc='2.0', method=method)
        if params is not None:
            message['params'] = params
        if id is not None:
            message['id'] = id
        data = json.dumps(message, ensure_ascii=False).encode('utf-8')
        self.process.stdin.write(f'Content-Length: {len(data)}\r\n\r\n'.encode()+data)
        self.process.stdin.flush()

    def wait(self, predicate, timeout=30):
        deadline = time.monotonic()+timeout
        for n, item in enumerate(self.pending):
            if predicate(item):
                return self.pending.pop(n)
        while time.monotonic() < deadline:
            item = self.messages.get(timeout=max(.01, deadline-time.monotonic()))
            if isinstance(item, Exception):
                self.process.poll()
                raise AssertionError((item, self.process.returncode))
            if predicate(item):
                return item
            self.pending.append(item)
        raise AssertionError(('timed out', self.pending))

    def request(self, method, params=None):
        self.serial += 1
        id = self.serial
        self.send(method, params, id)
        response = self.wait(lambda x: x.get('id') == id)
        assert 'error' not in response, response
        return response.get('result')

    def open(self, path, content, version=1):
        self.send('textDocument/didOpen', dict(textDocument=dict(uri=path.as_uri(), languageId='rhat', version=version, text=content)))

    def change(self, path, content, version):
        self.send('textDocument/didChange', dict(textDocument=dict(uri=path.as_uri(), version=version), contentChanges=[dict(text=content)]))

    def diagnostics(self, path, version):
        return self.wait(lambda x: x.get('method') == 'textDocument/publishDiagnostics' and x['params'].get('version') == version and x['params']['uri'].lower() == path.as_uri().lower())['params']['diagnostics']

    def close(self):
        self.request('shutdown')
        self.send('exit')
        self.process.stdin.close()
        assert self.process.wait(timeout=10) == 0
        assert self.process.stderr.read() == b''


def at(content, needle, occurrence=0, shift=0):
    start = -1
    for _ in range(occurrence+1):
        start = content.index(needle, start+1)
    start += shift
    prefix = content[:start]
    return dict(line=prefix.count('\n'), character=len(prefix.rsplit('\n',1)[-1].encode('utf-16-le'))//2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', required=True)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    server = Path(args.server).resolve()
    client = Client(server)
    client.send('textDocument/hover', {}, -1)
    assert client.wait(lambda x: x.get('id') == -1)['error']['code'] == -32002
    capabilities = client.request('initialize', dict(rootUri=work.as_uri(), capabilities={}))
    assert capabilities['serverInfo']['name'] == 'rhatls'
    assert capabilities['capabilities']['positionEncoding'] == 'utf-16'
    assert capabilities['capabilities']['textDocumentSync']['change'] == 2
    client.send('initialized', {})
    path = work/'unsaved 日本語.rh'
    content = '''def^ Point {self^{x:i64^=42, private^ hidden:i32^=0}}
let^ twice=f^ n {let^ value:i64^=n return^ value*2}
var^ point=Point{}
print(twice(point.x))
'''
    client.open(path, content)
    assert client.diagnostics(path, 1) == []
    assert not path.exists(), 'editor buffers must not be written to disk'
    def request(method, needle, occurrence=0, shift=0, text=content):
        return client.request('textDocument/'+method, dict(textDocument=dict(uri=path.as_uri()), position=at(text, needle, occurrence, shift)))
    hover = request('hover', 'point.x', shift=6)
    assert 'i64^' in hover['contents']['value'], hover
    definition = request('definition', 'point.x', shift=6)
    assert definition['range']['start'] == at(content, 'x:i64^'), definition
    hover=request('hover', 'twice(point')
    assert hover['contents']['value'].startswith('f^ twice(n:i64^)'), hover
    assert request('definition', 'twice(point')['range']['start'] == at(content, 'twice=f^')
    refs = client.request('textDocument/references', dict(textDocument=dict(uri=path.as_uri()), position=at(content,'point.x'), context=dict(includeDeclaration=True)))
    assert len(refs) == 2, refs
    outline = client.request('textDocument/documentSymbol', dict(textDocument=dict(uri=path.as_uri())))
    assert {'Point','x','twice','point'} <= {x['name'] for x in outline}, outline
    tokens = client.request('textDocument/semanticTokens/full', dict(textDocument=dict(uri=path.as_uri())))['data']
    assert len(tokens) % 5 == 0 and len(tokens) > 40
    line = column = 0
    semantic_names = []
    for offset in range(0, len(tokens), 5):
        delta_line, delta_column, length, kind, modifiers = tokens[offset:offset+5]
        line += delta_line
        column = delta_column if delta_line else column + delta_column
        token = content.splitlines()[line][column:column+length]
        assert kind in {1, 2, 3, 4, 5, 6}, (token, kind)
        assert '^' not in token, token
        semantic_names.append(token)
    assert {'Point', 'twice', 'point', 'x'} <= set(semantic_names), semantic_names
    signature = request('signatureHelp','twice(point',shift=6)
    assert signature['signatures'][0]['parameters'][0]['label'] == 'n', signature
    completions = request('completion','point.x',shift=6)
    assert 'x' in {x['label'] for x in completions}, completions
    assert 'hidden' not in {x['label'] for x in completions}, completions
    print('PASS semantic hover, definitions, references, outline, signatures, completion, tokens', flush=True)

    broken = content.replace('point.x','point.')
    client.change(path,broken,2)
    assert client.diagnostics(path,2)
    completions = request('completion','point.',shift=6,text=broken)
    assert 'x' in {x['label'] for x in completions}, completions
    client.change(path,content,3)
    assert client.diagnostics(path,3) == []
    print('PASS invalid edit recovery and incomplete member completion',flush=True)

    # Astral Unicode before the use exercises UTF-16 columns and incremental edits.
    unicode = 'let^ 日本語:i64^=41\nprint("😀",日本語)\n'
    client.change(path,unicode,4)
    assert client.diagnostics(path,4) == []
    result=request('definition','日本語)',text=unicode)
    assert result['range']['start'] == at(unicode,'日本語:i64^'), result
    pos=at(unicode,'41')
    client.send('textDocument/didChange',dict(textDocument=dict(uri=path.as_uri(),version=5),contentChanges=[dict(range=dict(start=pos,end=dict(line=pos['line'],character=pos['character']+2)),text='42')]))
    assert client.diagnostics(path,5) == []
    print('PASS Unicode paths, identifiers, UTF-16 and incremental synchronization',flush=True)

    module = work/'lib'/'math.rh'
    module.parent.mkdir(exist_ok=True)
    module.write_text('module^ lib.math\npublic^ let^ twice=f^ x:i64^ {x*2}\n',encoding='utf-8')
    main = 'let^ math=import^ lib.math\nprint(math.twice(21))\n'
    client.change(path,main,6)
    assert client.diagnostics(path,6) == []
    result=request('definition','twice(21)',text=main)
    assert result['uri'].lower() == module.as_uri().lower(), result
    client.open(module,'module^ lib.math\npublic^ let^ twice=f^ x:bool^ {if^ x: 1 el^: 0;}\n')
    assert client.diagnostics(path,6), 'unsaved dependency must change entry diagnostics'
    client.change(module,'module^ lib.math\npublic^ let^ twice=f^ x:i64^ {x*2}\n',2)
    assert client.diagnostics(path,6) == []
    assert 'twice' in {x['name'] for x in client.request('workspace/symbol', dict(query='twic'))}
    print('PASS imports, cross-module navigation and unsaved dependency invalidation',flush=True)

    # References from the defining module also include open importing roots.
    imported_refs = client.request('textDocument/references',dict(textDocument=dict(uri=module.as_uri()),position=dict(line=1,character=13),context=dict(includeDeclaration=True)))
    assert len(imported_refs) == 2, imported_refs

    scoped = """def^ Base {self^{x:i64^=42, private^ secret:i32^=0}}
def^ Derived:Base {}
let^ twice=f^ n:i64^ {if^ n<1: 0 el^: this^(n-1);}
var^ derived=Derived{}
let^ x:i32^=1
do^{let^ x:i64^=2 print(x)}
print(x,derived.x,twice(2))
"""
    client.change(path,scoped,7)
    assert client.diagnostics(path,7) == []
    assert request('definition','print(x)',shift=6,text=scoped)['range']['start'] == at(scoped,'x:i64^=2')
    assert request('definition','print(x,',shift=6,text=scoped)['range']['start'] == at(scoped,'x:i32^=1')
    assert request('definition','this^',text=scoped)['range']['start'] == at(scoped,'twice=f^')
    completions=request('completion','derived.x',shift=8,text=scoped)
    assert 'x' in {x['label'] for x in completions}, completions
    assert 'secret' not in {x['label'] for x in completions}, completions
    own="""def^ Hidden {self^{private^ secret:i64^=42} read=f^ self^ {self^.secret}}
let^ box=Hidden{}
print(box.read())
"""
    client.change(path,own,8)
    assert client.diagnostics(path,8) == []
    completions=request('completion','self^.secret',shift=6,text=own)
    assert 'secret' in {x['label'] for x in completions}, completions
    print('PASS lexical shadowing, recursive navigation, inheritance and private visibility',flush=True)

    for version,invalid in [(9,'let^ x:UnknownType=1'),(10,'let^ x:f128^=1.0'),(11,'let^ bad=f^ {var^ x=1 return^ &x} let^ r=bad()')]:
        client.change(path,invalid,version)
        assert client.diagnostics(path,version)
    floating='let^ x:f16^=1.25 let^ y:f32^=1.5 print(x,y)\n'
    client.change(path,floating,12)
    assert client.diagnostics(path,12) == []
    # Stale document versions must never roll back a newer editor buffer.
    client.change(path,'bad!',11)
    assert client.diagnostics(path,12) == []
    for version in range(13,63):
        client.change(path,'let^ number:i64^='+str(version)+'\nprint(number)\n',version)
    assert client.diagnostics(path,62) == []
    print('PASS semantic error recovery, numeric allocation cleanup and coalesced edits',flush=True)

    entry_module='module^ app\nlet^ value:i64^=42\nprint(value)\n'
    client.change(path,entry_module,63)
    assert client.diagnostics(path,63) == []
    library='module^ lib.constants\nlet^ hidden:i64^=42\npublic^ let^ answer:i64^=42\npublic^ let^ read=f^ {hidden}\n'
    client.change(path,library,64)
    assert client.diagnostics(path,64) == []
    outline=client.request('textDocument/documentSymbol',dict(textDocument=dict(uri=path.as_uri())))
    assert {'hidden','answer','read'} <= {x['name'] for x in outline}, outline
    print('PASS module entry statements and standalone library constants',flush=True)
    loop='for^ index from^ 1 to^ 3 {print(index)}\n'
    client.change(path,loop,65)
    assert client.diagnostics(path,65) == []
    assert request('definition','index)',text=loop)['range']['start'] == at(loop,'index from^')
    standard='import^ std.random\nvar^ rng=std.random.new(42)\nprint(rng.next())\n'
    client.change(path,standard,66)
    assert client.diagnostics(path,66) == []
    print('PASS counted-loop binding navigation and standard library resolution',flush=True)
    narrowed="""def^ Box {self^{value:i64^|nil^=42}}
let^ box=Box{}
if^ box.value fits^ i64^ {print(box.value)}
let^ local:i64^|nil^=42
if^ local? {print(local)}
"""
    client.change(path,narrowed,67)
    assert client.diagnostics(path,67) == []
    assert request('hover','print(box.value)',shift=10,text=narrowed)['contents']['value'] == 'value: i64^'
    assert request('hover','print(local)',shift=6,text=narrowed)['contents']['value'] == 'local: i64^'
    print('PASS narrowed local and member hover types',flush=True)
    client.send('unsupported/request',{},9999)
    assert client.wait(lambda x: x.get('id') == 9999)['error']['code'] == -32601

    client.send('textDocument/didClose',dict(textDocument=dict(uri=path.as_uri())))
    client.send('textDocument/didClose',dict(textDocument=dict(uri=module.as_uri())))
    client.close()
    assert subprocess.run([str(server),'--version'],capture_output=True,text=True).stdout.strip() == 'rhatls 0.1.0'
    exit_only=subprocess.run([str(server)],input=b'Content-Length: 33\r\n\r\n{"jsonrpc":"2.0","method":"exit"}',capture_output=True,timeout=10)
    assert exit_only.returncode == 1
    print('PASS lifecycle, unsupported requests, shutdown and exit',flush=True)

if __name__ == '__main__':
    main()
