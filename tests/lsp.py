# Drive the native language server through real framed stdio messages.
import argparse
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time

class Client:
    def __init__(self, executable):
        self.process = subprocess.Popen([str(executable), 'lsp', '--stdio'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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


def flatten_outline(items):
    return [symbol for item in items for symbol in [item, *flatten_outline(item.get('children', []))]]


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
    assert capabilities['serverInfo']['name'] == 'rhat'
    assert capabilities['capabilities']['positionEncoding'] == 'utf-16'
    assert capabilities['capabilities']['textDocumentSync']['change'] == 2
    client.send('initialized', {})
    path = work/'unsaved 日本語.rh'
    content = '''def^Point {self^{x:i64^=42, private^hidden:i32^=0}}
let^twice=f^n {let^value:i64^=n return^value*2}
var^point=Point{}
print^(twice(point.x))
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
    assert hover['contents']['value'].startswith('twice : f^n:i64^'), hover
    assert request('definition', 'twice(point')['range']['start'] == at(content, 'twice=f^')
    refs = client.request('textDocument/references', dict(textDocument=dict(uri=path.as_uri()), position=at(content,'point.x'), context=dict(includeDeclaration=True)))
    assert len(refs) == 2, refs
    outline = client.request('textDocument/documentSymbol', dict(textDocument=dict(uri=path.as_uri())))
    assert {'Point','twice','point'} <= {x['name'] for x in outline}, outline
    point_symbol = next(x for x in outline if x['name'] == 'Point')
    assert {'x','hidden'} == {x['name'] for x in point_symbol['children']}, outline
    twice_symbol = next(x for x in outline if x['name'] == 'twice')
    assert 'value' in {x['name'] for x in twice_symbol['children']}, outline
    assert twice_symbol['selectionRange']['start'] == at(content,'twice=f^'), twice_symbol
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

    interpolated_path = work/'interpolated.rh'
    interpolated = '''var^inner=1
var^outer=2
var^after=3
print^($"inner {inner} {{inner}} {outer:04} {(outer)} {$"nested {inner} text"} text")
print^(after)
'''
    client.open(interpolated_path, interpolated)
    client.diagnostics(interpolated_path, 1)
    data = client.request('textDocument/semanticTokens/full', dict(textDocument=dict(uri=interpolated_path.as_uri())))['data']
    line = column = 0
    found = []
    for offset in range(0, len(data), 5):
        delta_line, delta_column, length = data[offset:offset+3]
        line += delta_line
        column = delta_column if delta_line else column + delta_column
        found.append((line, column, length))
    lines = interpolated.splitlines()
    assert [lines[l][c:c+n] for l, c, n in found if l == 3] == ['inner', 'outer', 'outer', 'inner'], found
    assert [lines[l][c:c+n] for l, c, n in found if l == 4] == ['after'], found
    assert lines[3][found[[l for l, _, _ in found].index(3)][1]-1] == '{', found
    print('PASS semantic tokens in interpolation holes', flush=True)

    # A string in a hole, and a brace in that string, must not move the scope of what is around them.
    scope_path = work/'interpolated_scope.rh'
    scoped_text = '''let^run=p^ {
    let^shown=1
    print^($"{"{"} {shown}")
    print^(shown)
}
print^(1)
'''
    client.open(scope_path, scoped_text)
    client.diagnostics(scope_path, 1)
    def scope_labels(needle, shift=0):
        items = client.request('textDocument/completion', dict(textDocument=dict(uri=scope_path.as_uri()), position=at(scoped_text, needle, 0, shift)))
        return {x['label'] for x in items}
    assert 'shown' in scope_labels('print^(shown)', 7)
    assert 'shown' not in scope_labels('print^(1)')
    print('PASS scopes around interpolated strings', flush=True)

    broken = content.replace('point.x','point.')
    client.change(path,broken,2)
    assert client.diagnostics(path,2)
    completions = request('completion','point.',shift=6,text=broken)
    assert 'x' in {x['label'] for x in completions}, completions
    client.change(path,content,3)
    assert client.diagnostics(path,3) == []
    print('PASS invalid edit recovery and incomplete member completion',flush=True)

    # Astral Unicode before the use exercises UTF-16 columns and incremental edits.
    unicode = 'let^ 日本語:i64^=41\nprint^("😀",日本語)\n'
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
    main = 'let^ math=import^ lib.math\nprint^(math.twice(21))\n'
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

    # A dot-containing search path still resolves to the canonical unsaved dependency.
    alias_root = work/'path aliases'
    (alias_root/'nested').mkdir(parents=True,exist_ok=True)
    alias_module = alias_root/'dep.rh'
    alias_source = 'module^dep\npublic^let^value=f^n:i32^ ->i32^ {n}\n'
    alias_module.write_text(alias_source,encoding='utf-8')
    alias_main = alias_root/'main.rh'
    alias_client = Client(server)
    alias_client.request('initialize',dict(rootUri=alias_root.as_uri(),capabilities={},
                         initializationOptions=dict(modulePaths=[str(alias_root/'nested'/'..')])))
    alias_client.send('initialized',{})
    alias_client.open(alias_main,'import^dep\nprint^(dep.value(1))\n')
    assert alias_client.diagnostics(alias_main,1) == []
    alias_client.open(alias_module,'module^dep\npublic^let^value=f^n:bool^ ->i32^ {if^n:1 el^:0;}\n')
    assert alias_client.diagnostics(alias_main,1), 'path aliases must share unsaved dependency contents'
    alias_client.change(alias_module,alias_source,2)
    assert alias_client.diagnostics(alias_main,1) == []
    alias_client.close()
    print('PASS normalized and dot-containing dependency paths share unsaved overlays',flush=True)

    # References from the defining module also include open importing roots.
    imported_refs = client.request('textDocument/references',dict(textDocument=dict(uri=module.as_uri()),position=dict(line=1,character=13),context=dict(includeDeclaration=True)))
    assert len(imported_refs) == 2, imported_refs

    namespace_path = work/'namespaces.rh'
    namespace_source = '''import^lib.math
let^math=import^lib.math
print^(lib . math . twice(1),math.twice(2))
let^shadow=p^math:i64^ {print^(math)}
shadow(3)
print^("lib.math") # lib.math
'''
    client.open(namespace_path,namespace_source)
    assert client.diagnostics(namespace_path,1) == []
    data = client.request('textDocument/semanticTokens/full', dict(textDocument=dict(uri=namespace_path.as_uri())))['data']
    line = column = 0
    names = []
    for offset in range(0,len(data),5):
        delta_line, delta_column, length, kind, modifiers = data[offset:offset+5]
        line += delta_line
        column = delta_column if delta_line else column+delta_column
        names.append((line,namespace_source.splitlines()[line][column:column+length],kind))
    assert [(line,name) for line,name,kind in names if kind == 11] == [
        (0,'lib'),(0,'math'),(1,'math'),(1,'lib'),(1,'math'),
        (2,'lib'),(2,'math'),(2,'math'),
    ], names
    assert (2,'twice',2) in names, names
    assert (3,'math',4) in names, names
    client.send('textDocument/didClose',dict(textDocument=dict(uri=namespace_path.as_uri())))
    print('PASS import namespace paths, aliases and lexical shadowing colors',flush=True)

    scoped = """def^ Base {self^{x:i64^=42, private^ secret:i32^=0}}
def^ Derived:Base {}
let^ twice=f^ n:i64^ {if^ n<1: 0 el^: this^(n-1);}
var^ derived=Derived{}
let^ x:i32^=1
do^{let^ x:i64^=2 print^(x)}
print^(x,derived.x,twice(2))
"""
    client.change(path,scoped,7)
    assert client.diagnostics(path,7) == []
    assert request('definition','print^(x)',shift=7,text=scoped)['range']['start'] == at(scoped,'x:i64^=2')
    assert request('definition','print^(x,',shift=7,text=scoped)['range']['start'] == at(scoped,'x:i32^=1')
    assert request('definition','this^',text=scoped)['range']['start'] == at(scoped,'twice=f^')
    completions=request('completion','derived.x',shift=8,text=scoped)
    assert 'x' in {x['label'] for x in completions}, completions
    assert 'secret' not in {x['label'] for x in completions}, completions
    own="""def^ Hidden {self^{private^ secret:i64^=42} read=f^ self^ {self^.secret}}
let^ box=Hidden{}
print^(box.read())
"""
    client.change(path,own,8)
    assert client.diagnostics(path,8) == []
    completions=request('completion','self^.secret',shift=6,text=own)
    assert 'secret' in {x['label'] for x in completions}, completions
    print('PASS lexical shadowing, recursive navigation, inheritance and private visibility',flush=True)

    for version,invalid in [(9,'let^ x:UnknownType=1'),(10,'let^ x:f128^=1.0'),(11,'let^ bad=f^ {var^ x=1 return^ ref^ x} let^ r=bad()')]:
        client.change(path,invalid,version)
        assert client.diagnostics(path,version)
    floating='let^ x:f16^=1.25 let^ y:f32^=1.5 print^(x,y)\n'
    client.change(path,floating,12)
    assert client.diagnostics(path,12) == []
    # Stale document versions must never roll back a newer editor buffer.
    client.change(path,'bad!',11)
    assert client.diagnostics(path,12) == []
    for version in range(13,63):
        client.change(path,'let^ number:i64^='+str(version)+'\nprint^(number)\n',version)
    assert client.diagnostics(path,62) == []
    print('PASS semantic error recovery, numeric allocation cleanup and coalesced edits',flush=True)

    entry_module='module^ app\nlet^ value:i64^=42\nprint^(value)\n'
    client.change(path,entry_module,63)
    assert client.diagnostics(path,63) == []
    library='module^ lib.constants\nlet^ hidden:i64^=42\npublic^ let^ answer:i64^=42\npublic^ let^ read=f^ {hidden}\n'
    client.change(path,library,64)
    assert client.diagnostics(path,64) == []
    outline=client.request('textDocument/documentSymbol',dict(textDocument=dict(uri=path.as_uri())))
    assert {'hidden','answer','read'} <= {x['name'] for x in outline}, outline
    print('PASS module entry statements and standalone library constants',flush=True)
    loop='for^ index from^ 1 to^ 3 {print^(index)}\n'
    client.change(path,loop,65)
    assert client.diagnostics(path,65) == []
    assert request('definition','index)',text=loop)['range']['start'] == at(loop,'index from^')
    standard='import^ std.random\nvar^ rng=std.random.new(42)\nprint^(rng.next())\n'
    client.change(path,standard,66)
    assert client.diagnostics(path,66) == []
    print('PASS counted-loop binding navigation and standard library resolution',flush=True)
    narrowed="""def^ Box {self^{value:i64^|nil^=42}}
let^ box=Box{}
if^ box.value fits^ i64^ {print^(box.value)}
let^ local:i64^|nil^=42
if^ local? {print^(local)}
"""
    client.change(path,narrowed,67)
    assert client.diagnostics(path,67) == []
    assert request('hover','print^(box.value)',shift=11,text=narrowed)['contents']['value'] == 'value: i64^'
    assert request('hover','print^(local)',shift=7,text=narrowed)['contents']['value'] == 'local: i64^'
    print('PASS narrowed local and member hover types',flush=True)
    reference='''var^ value:i32^=7
let^ view:ref^ i32^=ref^ value
let^ read=f^ item:ref^ i32^ ->i32^ {*item}
print^(read(view))
'''
    client.change(path,reference,68)
    assert client.diagnostics(path,68) == []
    assert request('hover','view))',text=reference)['contents']['value'] == 'view: ref^ i32^'
    signature=request('signatureHelp','read(view)',shift=5,text=reference)
    assert 'item:ref^ i32^' in signature['signatures'][0]['label'], signature
    completions=request('completion','view))',text=reference)
    assert 'ref^' in {item['label'] for item in completions}, completions
    print('PASS reference keyword hover, signature and completion',flush=True)
    overloads='''let^ choose=f^ n:i32^ ->i32^ {n+1}
& f^ n:string^ ->i32^ {42}
let^ dispatch=choose
print^(dispatch(1),choose("s"))
def^ V {self^{n:i32^=1} op^+ =f^ self^,rhs:i32^ ->i32^ {self^.n+rhs}}
var^ v=V{}
print^(v+1)
'''
    client.change(path,overloads,69)
    assert client.diagnostics(path,69) == []
    hover=request('hover','dispatch(1)',text=overloads)['contents']['value']
    assert ' & ' in hover and 'f^ i32^ -> i32^;' in hover and 'f^ string^ -> i32^;' in hover, hover
    signatures=request('signatureHelp','dispatch(1)',shift=9,text=overloads)
    assert len(signatures['signatures']) == 2, signatures
    definition=request('definition','choose("s")',text=overloads)
    assert definition['range']['start'] == at(overloads,'& f^',shift=2), definition
    assert definition['range']['end'] == at(overloads,'& f^',shift=4), definition
    definition=request('definition','v+1',shift=1,text=overloads)
    assert definition['range']['start'] == at(overloads,'op^+'), definition
    print('PASS callable intersections, overload navigation and operator navigation',flush=True)
    self_dot = '''let^ math=import^std.math
def^ F2 {
 self^{x:f32^=3.0,y:f32^=4.0}
 op^dot^=f^ self^,rhs:F2 ->f32^ {self^.x*rhs.x+self^.y*rhs.y}
 length=f^ self^ ->f32^ {math.sqrt(self^ dot^ self^)}
}
var^ v=F2{}
print^(v.length())
'''
    client.change(path,self_dot,70)
    diagnostics=client.diagnostics(path,70)
    assert len(diagnostics) == 1, diagnostics
    diagnostic=diagnostics[0]
    assert "operator 'dot^'" in diagnostic['message'], diagnostic
    assert "'ref^ F2' and 'ref^ F2'" in diagnostic['message'], diagnostic
    assert diagnostic['range']['start'] == at(self_dot,'dot^ self^'), diagnostic
    assert diagnostic['range']['end'] == at(self_dot,'dot^ self^',shift=4), diagnostic
    client.change(path,self_dot.replace('self^ dot^ self^','self^ dot^ *self^'),71)
    assert client.diagnostics(path,71) == []
    print('PASS nested operator diagnostic and explicit reference operand',flush=True)
    unicode_source = ('# 日本語🙂\r\n'
                      'def^ Box {self^{値:i32^=42} read=f^ self^ ->i32^ {self^.値}}\r\n'
                      'var^ 箱=Box{}\r\n'
                      'print^("🙂",箱.値,箱.read())\r\n')
    client.change(path,unicode_source,72)
    assert client.diagnostics(path,72) == []
    data=client.request('textDocument/semanticTokens/full',dict(textDocument=dict(uri=path.as_uri())))['data']
    row=column=0
    resolved=[]
    for n in range(0,len(data),5):
        delta,offset,length,kind,modifiers=data[n:n+5]
        row+=delta
        column=offset if delta else column+offset
        encoded=unicode_source.splitlines()[row].encode('utf-16-le')
        resolved.append(encoded[column*2:(column+length)*2].decode('utf-16-le'))
    assert {'Box','箱','値','read'} <= set(resolved), resolved
    invalid=unicode_source.replace('箱.read()',']')
    client.change(path,invalid,73)
    diagnostics=client.diagnostics(path,73)
    assert diagnostics[0]['range']['start'] == at(invalid,']'), diagnostics
    client.change(path,unicode_source,74)
    assert client.diagnostics(path,74) == []
    print('PASS indexed source positions and streaming semantic tokens with UTF-16 and CRLF',flush=True)

    # Force reference-set growth and many semantic lookups, then enqueue stale
    # coloring requests between edits as the editor does during fast typing.
    large='let^ anchor:i32^=42\n'
    large+='\n'.join(f'let^ value_{n}=anchor print^(value_{n})' for n in range(400))
    client.change(path,large,75)
    assert client.diagnostics(path,75) == []
    data=client.request('textDocument/semanticTokens/full',dict(textDocument=dict(uri=path.as_uri())))['data']
    assert len(data) >= 400*3*5, len(data)
    for version in range(76,96):
        client.change(path,large+'\nprint^(missing)\n',version)
        client.send('textDocument/semanticTokens/full',dict(textDocument=dict(uri=path.as_uri())),10000+version)
    client.change(path,large,96)
    assert client.diagnostics(path,96) == []
    for version in range(76,96):
        response=client.wait(lambda item: item.get('id') == 10000+version)
        assert 'result' in response or response['error']['code'] == -32801, response
    data=client.request('textDocument/semanticTokens/full',dict(textDocument=dict(uri=path.as_uri())))['data']
    assert len(data) >= 400*3*5, len(data)
    print('PASS large reference index and stale semantic requests during rapid edits',flush=True)
    # Saves and changes in other tabs invalidate the global analysis revision,
    # but VS Code does not retry documentSymbol on ContentModified responses.
    changing_tab = work/'outline_invalidation.rh'
    client.open(changing_tab, 'let^ other=0\n')
    for version in range(97, 107):
        client.change(changing_tab, f'let^ other={version}\n', version)
        client.send('textDocument/documentSymbol', dict(textDocument=dict(uri=path.as_uri())), 11000+version)
        client.send('textDocument/didSave', dict(textDocument=dict(uri=path.as_uri())))
        client.send('workspace/didChangeWatchedFiles', dict(changes=[dict(uri=path.as_uri(), type=2)]))
    for version in range(97, 107):
        response = client.wait(lambda item: item.get('id') == 11000+version)
        assert 'error' not in response, response
        assert 'anchor' in {item['name'] for item in response['result']}, response
    client.send('textDocument/didClose', dict(textDocument=dict(uri=changing_tab.as_uri())))
    print('PASS outline requests survive saves and workspace invalidation', flush=True)
    # Independent diagnostics must survive the first error, while uses of a
    # failed binding must not manufacture more errors. Unused signatures are
    # resolved even when a method has an unavailable parameter type.
    recovery = """def^ F2 {
 self^{x:f32^=1.0,y:f32^=2.0}
 broken=f^self^,other:Missing ->f32^ {0.0}
}
let^ cross=f^a:F2,b:F2 ->f32^ {a.x*b.y-a.y*b.x}
let^ first=missing_one
let^ dependent=first+1
let^ second=missing_two
let^ read=f^a:F2 ->f32^ {a.x}
var^ value=F2{}
print^(read(value))
"""
    client.change(path,recovery,97)
    diagnostics=client.diagnostics(path,97)
    assert len(diagnostics) == 3, diagnostics
    for name in ('Missing','missing_one','missing_two'):
        assert sum(name in d['message'] for d in diagnostics) == 1, diagnostics
    assert request('definition','read(value)',text=recovery)['range']['start'] == at(recovery,'read=f^')
    assert request('hover','a.x}',shift=2,text=recovery)['contents']['value'] == 'x: f32^'

    def tokens(text, document=path, with_modifiers=False):
        data=client.request('textDocument/semanticTokens/full',dict(textDocument=dict(uri=document.as_uri())))['data']
        row=column=0
        result={}
        for n in range(0,len(data),5):
            delta,offset,length,kind,modifiers=data[n:n+5]
            row+=delta
            column=offset if delta else column+offset
            result[(row,column)]=(length,kind,modifiers) if with_modifiers else (length,kind)
        return result

    colored=tokens(recovery)
    for spelling in ('F2,b:F2','F2 ->f32^'):
        pos=at(recovery,spelling)
        assert colored[(pos['line'],pos['character'])] == (2,1), colored
    print('PASS multiple diagnostics, cascade suppression and unused signature coloring',flush=True)

    adjacent_path=work/'adjacent-namespace.rh'
    adjacent_source='''import^std.fs
let^fs=import^std.fs
do^{
    let^first=try^fs.read("example.csv")
    let^second=try^ fs.read("example.csv")
    let^third=try^std.fs.read("example.csv")
    let^fourth=try^ std.fs.read("example.csv")
catch^:
    print^("failed")
}
'''
    client.open(adjacent_path,adjacent_source)
    diagnostics=client.diagnostics(adjacent_path,1)
    assert diagnostics == [], diagnostics
    colored=tokens(adjacent_source,adjacent_path)
    for spelling in ('try^fs.read','try^ fs.read','try^std.fs.read','try^ std.fs.read'):
        start=spelling.index('fs') if 'std.' not in spelling else spelling.index('std')
        for match in re.finditer(r'\w+',spelling[start:]):
            name=match.group()
            pos=at(adjacent_source,spelling,shift=start+match.start())
            assert colored[(pos['line'],pos['character'])] == (len(name),2 if name == 'read' else 11), (spelling,colored)
    client.send('textDocument/didClose',dict(textDocument=dict(uri=adjacent_path.as_uri())))
    print('PASS import namespace colors immediately after try hats',flush=True)

    composition_path=work/'composition.rh'
    composition='''def^Reader {}
def^Writer {}
def^Stream : Reader, Writer {}
def^MemoryReader : Reader {}
'''
    client.open(composition_path,composition)
    assert client.diagnostics(composition_path,1) == []
    colored=tokens(composition,composition_path)
    for spelling,name,shift in [('Stream : Reader','Reader',9),
                                ('Reader, Writer','Writer',8),
                                ('MemoryReader : Reader','Reader',15)]:
        pos=at(composition,spelling,shift=shift)
        params=dict(textDocument=dict(uri=composition_path.as_uri()),position=pos)
        hover=client.request('textDocument/hover',params)
        assert name in hover['contents']['value'], hover
        definition=client.request('textDocument/definition',params)
        assert definition['uri'].lower() == composition_path.as_uri().lower(), definition
        assert definition['range']['start'] == at(composition,name), definition
        assert colored[(pos['line'],pos['character'])] == (len(name),1), colored
    client.send('textDocument/didClose',dict(textDocument=dict(uri=composition_path.as_uri())))
    io_path=Path(__file__).resolve().parents[1]/'stdlib'/'std'/'io.rh'
    io_source=io_path.read_text(encoding='utf-8')
    client.open(io_path,io_source)
    assert client.diagnostics(io_path,1) == []
    colored=tokens(io_source,io_path)
    for spelling,name,shift in [('Stream : Reader','Reader',9),
                                ('Reader, Writer','Writer',8),
                                ('MemoryReader : Reader','Reader',15),
                                ('MemoryWriter : Writer','Writer',15)]:
        pos=at(io_source,spelling,shift=shift)
        params=dict(textDocument=dict(uri=io_path.as_uri()),position=pos)
        hover=client.request('textDocument/hover',params)
        assert name in hover['contents']['value'], hover
        definition=client.request('textDocument/definition',params)
        assert definition['uri'].lower() == io_path.as_uri().lower(), definition
        assert definition['range']['start'] == at(io_source,'def^'+name,shift=4), definition
        assert colored[(pos['line'],pos['character'])] == (len(name),1), colored
    client.send('textDocument/didClose',dict(textDocument=dict(uri=io_path.as_uri())))
    print('PASS composition base hover, definition and semantic colors',flush=True)

    dynamic_path=work/'dynamic-syntax.rh'
    dynamic_source='''def^Iface {abstract^get:f^self^ ->i32^;}
def^Value:Iface {get=f^self^ ->i32^ {7}}
var^v=Value{}
var^one:dynamic^Iface=ref^v
var^two:dynamic^^Iface=ref^one
let^many=Iface[dynamic^2]{v,ref^v}
let^old=Iface[ref^dyn^2]{v,ref^v}
var^direct:dynamic^Iface[2]={ref^v,ref^v}
let^take=p^values:ref^dynamic^Iface[2] {print^(values[0].get())}
take(ref^direct)
let^probe=p^x:dynamic^Iface {if^x fits^dynamic^Iface {print^(x.get())}}
probe(one)
print^((*two).get(),many[0].get(),old[1].get())
'''
    client.open(dynamic_path,dynamic_source)
    diagnostics=client.diagnostics(dynamic_path,1)
    assert diagnostics == [], diagnostics
    colored=tokens(dynamic_source,dynamic_path)
    for match in re.finditer(r'(?:dynamic|ref)\^+',dynamic_source):
        prefix=dynamic_source[:match.start()]
        pos=dict(line=prefix.count('\n'),character=len(prefix.rsplit('\n',1)[-1]))
        # Like ref^, dynamic^ uses TextMate's keyword.other category. Semantic
        # keyword tokens would override it with the control-flow color.
        assert (pos['line'],pos['character']) not in colored, colored
    for spelling,shift in [('dynamic^Iface',8),('dynamic^^Iface',9),
                            ('dynamic^Iface[2]',8),('ref^dynamic^Iface[2]',12),
                            ('Iface[dynamic^2]',0),('Iface[ref^dyn^2]',0)]:
        pos=at(dynamic_source,spelling,shift=shift)
        assert colored[(pos['line'],pos['character'])] == (5,1), (spelling,colored)
        definition=client.request('textDocument/definition',dict(textDocument=dict(uri=dynamic_path.as_uri()),position=pos))
        assert definition['range']['start'] == at(dynamic_source,'Iface'), definition
    pos=at(dynamic_source,'one:dynamic',shift=0)
    hover=client.request('textDocument/hover',dict(textDocument=dict(uri=dynamic_path.as_uri()),position=pos))
    assert 'ref^ dyn^ Iface' in hover['contents']['value'], hover
    completion=client.request('textDocument/completion',dict(textDocument=dict(uri=dynamic_path.as_uri()),position=at(dynamic_source,'dynamic^Iface',shift=3)))
    assert 'dynamic^' in {item['label'] for item in completion}, completion
    client.send('textDocument/didClose',dict(textDocument=dict(uri=dynamic_path.as_uri())))
    print('PASS dynamic shorthand, array attributes, keyword colors and type navigation',flush=True)

    errors_path=work/'error-colors.rh'
    errors_source='''import^std.io
let^io=import^std.io
errordef^Error { OpenFailed }
let^local=p^ ->i32^|Error.OpenFailed {return^Error.OpenFailed{}}
let^foreign=p^ ->i32^|io.Error.Closed {return^io.Error.Closed{}}
let^qualified=p^ ->nil^|std.io.Error.Closed {return^std.io.Error.Closed{}}
let^spaced=p^ ->i32^|Error . OpenFailed|io . Error . Closed {return^io . Error . Closed{}}
let^bytes=u8^[]{}
let^value:i32^=1
def^Primitives {self^{b:bool^, c:char^, s:string^, n:u64^, f:f32^}}
print^("u8^ Error.OpenFailed") # i32^ Error.OpenFailed
'''
    client.open(errors_path,errors_source)
    diagnostics=client.diagnostics(errors_path,1)
    assert diagnostics == [], diagnostics
    colored=tokens(errors_source,errors_path)
    for spelling,target_path,target_source,leaf in [
            ('Error.OpenFailed',errors_path,errors_source,'OpenFailed'),
            ('Error . OpenFailed',errors_path,errors_source,'OpenFailed'),
            ('io.Error.Closed',io_path,io_source,'Closed'),
            ('io . Error . Closed',io_path,io_source,'Closed'),
            ('std.io.Error.Closed',io_path,io_source,'Closed')]:
        # Check annotations and constructions, skipping strings and comments.
        for match in re.finditer(re.escape(spelling),errors_source):
            if errors_source[:match.start()].count('\n') == at(errors_source,'print^')['line']:
                continue
            for name,offset,target in [('Error',spelling.index('Error'),'Error'),
                                       (leaf,spelling.index(leaf),leaf)]:
                prefix=errors_source[:match.start()+offset]
                pos=dict(line=prefix.count('\n'),character=len(prefix.rsplit('\n',1)[-1]))
                assert colored[(pos['line'],pos['character'])] == (len(name),1), (spelling,colored)
                params=dict(textDocument=dict(uri=errors_path.as_uri()),position=pos)
                definition=client.request('textDocument/definition',params)
                assert definition['uri'].lower() == target_path.as_uri().lower(), definition
                expected=at(target_source,'errordef^Error',shift=9) if target == 'Error' else at(target_source,target)
                assert definition['range']['start'] == expected, (name,definition)
                hover=client.request('textDocument/hover',params)
                assert name in hover['contents']['value'], hover
    for name in ('u8^','i32^','nil^','bool^','char^','string^','u64^','f32^'):
        pos=at(errors_source,name)
        assert colored[(pos['line'],pos['character'])] == (len(name)-1,1), (name,colored)
    assert not any(row == at(errors_source,'print^')['line'] for row,column in colored), colored
    for spelling in ('std.io.Error.Closed','io.Error.Closed'):
        pos=at(errors_source,spelling)
        assert colored[(pos['line'],pos['character'])][1] == 11, colored
    client.send('textDocument/didClose',dict(textDocument=dict(uri=errors_path.as_uri())))
    fs_path=io_path.with_name('fs.rh')
    fs_source=fs_path.read_text(encoding='utf-8')
    client.open(fs_path,fs_source)
    diagnostics=client.diagnostics(fs_path,1)
    assert diagnostics == [], diagnostics
    colored=tokens(fs_source,fs_path)
    for spelling in ('Error.OpenFailed','io.Error.Closed','io.Error.ReadFailed','io.Error.WriteFailed'):
        for match in re.finditer(re.escape(spelling),fs_source):
            prefix=fs_source[:match.start()]
            if prefix.rsplit('\n',1)[-1].lstrip().startswith('#'):
                continue
            for name in ('Error',spelling.rsplit('.',1)[-1]):
                row=prefix.count('\n')
                column=len(prefix.rsplit('\n',1)[-1])+spelling.index(name)
                assert colored[(row,column)] == (len(name),1), (spelling,row,colored)
    client.send('textDocument/didClose',dict(textDocument=dict(uri=fs_path.as_uri())))
    print('PASS local and imported error group/leaf navigation and built-in type colors',flush=True)

    constants_module=work/'lib'/'constants.rh'
    constants_module.write_text('''module^lib.constants
public^enum^State:u8^ {Cold,Hot}
public^let^LIMIT:i32^=7
''',encoding='utf-8')
    constants_path=work/'constant-colors.rh'
    constants_source='''let^constants=import^lib.constants
enum^Mode:u8^ {Off,On}
let^fixed=1
var^changing=2
let^run=p^arg:i32^ {
    let^local=arg+fixed
    var^mutable=local
    let^left,right=local,mutable
    let^pair=f^ ->(i32^,i32^) {1,2}
    let^a,b=pair()
    let^alias=pair
    let^c,d=alias()
    print^(fixed,changing,local,mutable,left,right,a,b,c,d,Mode.On,constants.State.Hot,constants.LIMIT)
    do^{var^local=3 print^(local)}
    print^(local)
}
run(0)
'''
    client.open(constants_path,constants_source)
    diagnostics=client.diagnostics(constants_path,1)
    assert diagnostics == [], diagnostics
    colored=tokens(constants_source,constants_path,with_modifiers=True)
    for match in re.finditer(r'\b(?:fixed|local|left|right|a|b|c|d|alias|Off|On|Hot|LIMIT|changing|mutable|arg)\b',constants_source):
        prefix=constants_source[:match.start()]
        row=prefix.count('\n')
        column=len(prefix.rsplit('\n',1)[-1])
        name=match.group()
        length,kind,modifiers=colored[(row,column)]
        mutable=name in ('changing','mutable') or (name == 'local' and 'var^local' in constants_source.splitlines()[row])
        assert length == len(name), (name,colored)
        assert kind == (4 if name == 'arg' else 5), (name,kind)
        assert bool(modifiers & 2) == (name != 'arg' and not mutable), (name,row,modifiers)
    params=dict(textDocument=dict(uri=constants_path.as_uri()),position=at(constants_source,'Mode.On',shift=5))
    definition=client.request('textDocument/definition',params)
    assert definition['range']['start'] == at(constants_source,'On}'), definition
    params['position']=at(constants_source,'constants.State.Hot',shift=16)
    definition=client.request('textDocument/definition',params)
    assert definition['uri'].lower() == constants_module.as_uri().lower(), definition
    client.send('textDocument/didClose',dict(textDocument=dict(uri=constants_path.as_uri())))
    print('PASS let values, enum leaves, imported constants, shadowing and callable alias colors',flush=True)

    # Recovery stays inside the failed statement/member, and resets its scope.
    damaged="""let^ scan=p^n:i32^ {
 let^ bad=]
 let^ bad2=missing_inside
 let^ good=n
 print^(good)
}
scan(1)
def^ Box {self^{x:i32^=42}
 bad=f^n: ->i32^ {0}
 read=f^self^ ->i32^ {self^.x}
}
var^ box=Box{}
print^(box.read())
let^ tail:i32^=42
print^(tail)
"""
    client.change(path,damaged,98)
    diagnostics=client.diagnostics(path,98)
    assert len(diagnostics) == 3, diagnostics
    assert request('definition','print^(good)',shift=7,text=damaged)['range']['start'] == at(damaged,'good=n')
    assert request('definition','box.read()',shift=4,text=damaged)['range']['start'] == at(damaged,'read=f^')
    assert request('hover','print^(tail)',shift=7,text=damaged)['contents']['value'] == 'tail: i32^'
    print('PASS statement and member syntax recovery with later navigation',flush=True)

    lexical='let^ broken="\\z"\nlet^ number=0xQQ\nlet^ tail:i32^=42\nprint^(tail)\n'
    client.change(path,lexical,99)
    diagnostics=client.diagnostics(path,99)
    assert len(diagnostics) == 2, diagnostics
    assert request('hover','print^(tail)',shift=7,text=lexical)['contents']['value'] == 'tail: i32^'
    unfinished='let^ broken="open\nlet^ tail:i32^=42\nprint^(tail)\n'
    client.change(path,unfinished,100)
    assert len(client.diagnostics(path,100)) == 1
    pos=at(unfinished,'tail:i32^')
    assert tokens(unfinished)[(pos['line'],pos['character'])] == (4,5)
    print('PASS lexical recovery and coloring after an unfinished quote',flush=True)

    imports='import^ absent_module\nlet^ bad=absent_module.read()\nlet^ other=another_missing\nlet^ tail:i32^=42\nprint^(tail)\n'
    client.change(path,imports,101)
    diagnostics=client.diagnostics(path,101)
    assert len(diagnostics) == 2, diagnostics
    assert request('hover','print^(tail)',shift=7,text=imports)['contents']['value'] == 'tail: i32^'
    partial='def^ Box {self^{value:i32^=42} read=f^self{ ->i32^ {self^.value}}\nlet^ tail:i32^=42\nprint^(tail)\n'
    client.change(path,partial,102)
    assert client.diagnostics(path,102)
    assert request('hover','print^(tail)',shift=7,text=partial)['contents']['value'] == 'tail: i32^'
    client.change(path,'let^ tail:i32^=42\nprint^(tail)\n',103)
    assert client.diagnostics(path,103) == []
    print('PASS unavailable imports, malformed inference bodies and fixing all errors',flush=True)

    structural="""def^ Broken {self^{x:MissingField=1}}
def^ Recursive {self^{x:Recursive}}
def^ Duplicate {}
def^ Duplicate {}
enum^ Small:u8^ {A=256}
let^ tail:i32^=42
print^(tail)
"""
    client.change(path,structural,104)
    diagnostics=client.diagnostics(path,104)
    assert len(diagnostics) == 4, diagnostics
    assert request('hover','print^(tail)',shift=7,text=structural)['contents']['value'] == 'tail: i32^'
    many='\n'.join(f'let^ broken_{n}=missing_{n}' for n in range(125))
    many+='\nlet^ tail:i32^=42\nprint^(tail)\n'
    client.change(path,many,105)
    assert len(client.diagnostics(path,105)) == 100
    assert request('hover','print^(tail)',shift=7,text=many)['contents']['value'] == 'tail: i32^'
    client.change(path,'let^ tail:i32^=42\nprint^(tail)\n',106)
    assert client.diagnostics(path,106) == []
    print('PASS damaged type definitions and bounded diagnostics with later editor information',flush=True)

    broken_library="""module^ lib.partial
public^ let^ broken=]
public^ let^ answer:i32^=42
public^ let^ read=f^n:Unknown ->i32^ {0}
"""
    client.change(path,broken_library,107)
    diagnostics=client.diagnostics(path,107)
    assert len(diagnostics) == 2, diagnostics
    assert not any('executable bindings' in d['message'] for d in diagnostics), diagnostics
    assert request('hover','answer:i32^',text=broken_library)['contents']['value'] == 'answer: i32^'
    client.change(path,'let^ tail:i32^=42\nprint^(tail)\n',108)
    assert client.diagnostics(path,108) == []
    print('PASS standalone library detection after multiple diagnostics',flush=True)

    coloring="""let^ isfinite=f^x:f64^ ->bool^ {x-x=0.0}
 & f^x:f32^ ->bool^ {x-x=0.0}
let^ report=p^x:f64^ {print^(x)}
 & p^x:f32^ {print^(x)}
"""
    client.change(path,coloring,109)
    assert client.diagnostics(path,109) == []
    colored=tokens(coloring)
    for line_no,line in enumerate(coloring.splitlines()):
        column=line.index('x:')
        assert colored[(line_no,column)] == (1,4), (line,colored)
    definition=request('definition','isfinite=f^',text=coloring)
    assert definition['range']['start'] == at(coloring,'isfinite=f^'), definition
    math_path=Path(__file__).resolve().parents[1]/'stdlib/std/math.rh'
    math_source=math_path.read_bytes().decode('utf-8')
    client.open(math_path,math_source)
    assert client.diagnostics(math_path,1) == []
    colored=tokens(math_source,math_path)
    # Include all intrinsic arms: multi-argument declarations, compact f^x,
    # bodyful overloads and both the first and subsequent arms.
    for line_no,line in enumerate(math_source.splitlines()):
        for match in re.finditer(r'(?<![\w])([xyz]|sign):f(?:32|64)\^',line):
            spelling=match.group(1)
            assert colored[(line_no,match.start())] == (len(spelling),4), (line,colored)
    client.send('textDocument/didClose',dict(textDocument=dict(uri=math_path.as_uri())))
    print('PASS overload keyword spans and consistent foreign parameter coloring',flush=True)

    result_signatures="""def^ Random {
 self^{state:i64^=0}
 reseed=p^self^,seed:i64^ {self^.state:=seed}
}
let^ consume=p^n:i64^ {print^(n)}
let^ empty=f^ {}
let^ nil_result=p^ {return^ nil^}
let^ number_result=p^n:i64^ {return^ n}
externC^srand:p^seed:u32^;
var^ rng=Random{}
rng.reseed(1)
consume(1)
empty()
nil_result()
number_result(1)
"""
    client.change(path,result_signatures,110)
    assert client.diagnostics(path,110) == []
    for needle,shift,expected in [
        ('rng.reseed(1)',4,'reseed : p^self^:Random, seed:i64^'),
        ('consume(1)',0,'consume : p^n:i64^'),
        ('empty()',0,'empty : f^'),
        ('nil_result()',0,'nil_result : p^ -> nil^'),
        ('number_result(1)',0,'number_result : p^n:i64^ -> i64^'),
        ('srand:p^',0,'srand : p^seed:u32^'),
    ]:
        hover=request('hover',needle,shift=shift,text=result_signatures)
        assert hover['contents']['value'] == expected, hover
    signature=request('signatureHelp','rng.reseed(1)',shift=11,text=result_signatures)
    assert signature['signatures'][0]['label'] == 'reseed : p^self^:Random, seed:i64^', signature
    print('PASS no-result signatures omit the arrow and remain distinct from nil results',flush=True)

    self_type="""def^ F2 {
 self^{x:i32^=4}
 add=f^self^,other:Self^ ->Self^ {F2{x=self^.x+other.x}}
 copy=f^self^ ->Self^ {*self^}
}
var^ value=F2{}
print^(value.add(value.copy()).x)
"""
    client.change(path,self_type,111)
    assert client.diagnostics(path,111) == []
    assert request('hover','Self^',text=self_type)['contents']['value'] == 'Self^: F2'
    definition=request('definition','Self^',text=self_type)
    assert definition['range']['start'] == at(self_type,'F2 {'), definition
    assert request('hover','copy=f^',text=self_type)['contents']['value'] == 'copy : f^self^:F2 -> Self^'
    colored=tokens(self_type)
    for occurrence,_ in enumerate(re.finditer(r'Self\^',self_type)):
        position=at(self_type,'Self^',occurrence)
        assert colored[(position['line'],position['character'])] == (4,1), colored
    broken_self='def^ Broken {self^{bad:Self^^}}\nlet^ outside:Self^=1\nlet^ good:i32^=42\nprint^(good)\n'
    client.change(path,broken_self,112)
    diagnostics=client.diagnostics(path,112)
    assert len(diagnostics) == 2, diagnostics
    assert all('outside the enclosing type definition' in d['message'] for d in diagnostics), diagnostics
    assert request('hover','print^(good)',shift=7,text=broken_self)['contents']['value'] == 'good: i32^'
    client.change(path,self_type,113)
    assert client.diagnostics(path,113) == []
    print('PASS lexical Self types, navigation, coloring and scope recovery',flush=True)
    # Diagnostics must not use symbol-name searching or zero-width ranges.
    # Repeat the same spelling and place non-BMP text before it to exercise
    # source token widths independently of UTF-16 editor coordinates.
    position_cases = [
        ('def^ F2 {self^{x:f32^=0.0} '
         'op^+ = f^self^, o:ref^Self^ -> Self^ '
         '{F2{x=self^.x + b.x}}}\n', 'b.x', 'b', 'qualified name'),
        ('def^ Box {self^{値:i32^=42}}\n'
         'let^ read=f^箱:Box ->i32^ {let^ label="🙂" let^ 値=箱.値 return^箱.missing}\n',
         'missing', 'missing', 'field'),
        ('def^ Box {self^{値:i32^=42}}\n'
         'let^ read=f^箱:Box ->i32^ {let^ label="🙂" let^ 値=箱.値 return^箱.missing()}\n',
         'missing', 'missing', 'method'),
        ('let^ read=f^ ->i32^ {let^ label="🙂" return^`missing name`}\n',
         '`missing name`', '`missing name`', 'unknown'),
        ('print^("🙂",])\n', ']', ']', 'expression'),
        ('print^("🙂",1 == 2)\n', '==', '==', 'C-style'),
    ]
    for version, (source, needle, spelling, message) in enumerate(
            [(source.replace('\n', newline), needle, spelling, message)
             for newline in ('\n', '\r\n')
             for source, needle, spelling, message in position_cases], 114):
        client.change(path,source,version)
        diagnostics=client.diagnostics(path,version)
        assert len(diagnostics) == 1, diagnostics
        diagnostic=diagnostics[0]
        assert message in diagnostic['message'], diagnostic
        assert diagnostic['range'] == dict(start=at(source,needle),
                                           end=at(source,needle,shift=len(spelling))), diagnostic
    print('PASS exact diagnostic token ranges with repeated names, UTF-16 and CRLF',flush=True)

    borrowed = ('def^ V {self^{n:i32^=0} '
                'op^+ =f^self^,other:ref^Self^ {V{n=self^.n+other.n}}}\n'
                'let^a=V{n=3}\nlet^b=V{n=4}\nlet^c=a+b\nprint^(c.n)\n')
    client.change(path,borrowed,126)
    assert client.diagnostics(path,126) == []
    assert request('hover','c=a+b',text=borrowed)['contents']['value'] == 'c: V'
    definition=request('definition','a+b',shift=1,text=borrowed)
    assert definition['range']['start'] == at(borrowed,'op^+'), definition
    print('PASS automatically borrowed operator operands, result hover and navigation',flush=True)

    numeric_result = ('let^random_int=p^limit:i64^ {return^limit-1}\n'
                      'let^run=p^weapon:i64^ {\n'
                      'let^damage:i64^=if^weapon=1:12+random_int(12) el^:5*(1+random_int(3));\n'
                      'print^(damage)\n}\nrun(1) run(2)\n')
    for version,source in [(127,numeric_result),
                           (128,numeric_result.replace('p^limit:i64^ {','p^limit:i64^ ->i64^ {'))]:
        client.change(path,source,version)
        diagnostics=client.diagnostics(path,version)
        assert diagnostics == [], diagnostics
        hover=request('hover','random_int=p^',text=source)['contents']['value']
        assert hover.endswith('-> i64^'), hover
        assert request('hover','damage:i64^',text=source)['contents']['value'] == 'damage: i64^'
    print('PASS pending numeric call results keep contextual literal widths',flush=True)

    conditional_result = ('let^front_sector=f^x:i64^ {x}\n'
                          'let^back_sector=f^x:i64^ {if^x<0:-1 el^:x;}\n'
                          'let^neighbor=f^sector:i64^ {\n'
                          'let^a=front_sector(1) let^b=back_sector(2)\n'
                          'let^value=if^a=sector:b el^b=sector:a el^:-1;\n'
                          'return^value\n}\n'
                          'print^(back_sector(0),neighbor(1))\n')
    for version,source in [(129,conditional_result),
                           (130,conditional_result.replace('f^x:i64^ {','f^x:i64^ ->i64^ {'))]:
        client.change(path,source,version)
        assert client.diagnostics(path,version) == []
        for name in ('front_sector=f^', 'back_sector=f^'):
            hover=request('hover',name,text=source)['contents']['value']
            assert hover.endswith('-> i64^'), hover
        assert request('hover','value=if^',text=source)['contents']['value'] == 'value: i64^'
    print('PASS pending conditional branches keep inferred numeric result widths',flush=True)

    literal_overloads = ('let^min=f^a:f32^,b:f32^ {if^a<b:a el^:b;}\n'
                         '& f^a:i64^,b:i64^ {if^a<b:a el^:b;}\n'
                         'let^text="hello"\n'
                         'let^limit=min(text.bytes.len,160)\n'
                         'let^reverse=min(160,text.bytes.len)\n'
                         'print^(limit,reverse)\n')
    client.change(path,literal_overloads,131)
    assert client.diagnostics(path,131) == []
    for name in ('limit=min', 'reverse=min'):
        hover=request('hover',name,text=literal_overloads)['contents']['value']
        assert hover.endswith(': i64^'), hover
    client.change(path,literal_overloads+'print^(min(1,2))\n',132)
    diagnostics=client.diagnostics(path,132)
    assert len(diagnostics) == 1 and 'ambiguous overload' in diagnostics[0]['message'], diagnostics
    client.change(path,literal_overloads,133)
    assert client.diagnostics(path,133) == []
    print('PASS candidate-specific numeric literals, result hover and ambiguity recovery',flush=True)

    unicode_operators = ('def^V {self^{n:i32^=2}\n'
                         'op^× =f^self^,other:ref^Self^ {self^.n-other.n}\n'
                         'op^⋅ =f^self^,other:ref^Self^ {self^.n*other.n}}\n'
                         'def^Holder {self^{delta:V=V{}}}\n'
                         'let^左=V{} let^右=V{n=3} let^box=Holder{}\n'
                         'print^("🙂",左×右,左⋅右,左 × 右,左 ⋅ 右,左.n≦右.n,box.delta×(左))\n')
    client.change(path,unicode_operators,134)
    assert client.diagnostics(path,134) == []
    for symbol in ('×', '⋅'):
        definition=request('definition','左'+symbol+'右',shift=1,text=unicode_operators)
        assert definition['range']['start'] == at(unicode_operators,'op^'+symbol), definition
    data=client.request('textDocument/semanticTokens/full',dict(textDocument=dict(uri=path.as_uri())))['data']
    row=column=0
    colored={}
    for n in range(0,len(data),5):
        delta,offset,length,kind,modifiers=data[n:n+5]
        row+=delta
        column=offset if delta else column+offset
        line=unicode_operators.splitlines()[row].encode('utf-16-le')
        spelling=line[column*2:(column+length)*2].decode('utf-16-le')
        assert not any(symbol in spelling for symbol in '×⋅≦≤≧≥≠'), spelling
        colored[row,column]=spelling
    for needle,shift,name in [('左×右',0,'左'),('左×右',2,'右'),
                              ('左⋅右',0,'左'),('左⋅右',2,'右'),
                              ('box.delta×',4,'delta')]:
        pos=at(unicode_operators,needle,shift=shift)
        assert colored.get((pos['line'],pos['character'])) == name, (name,colored)
    bad_operator=unicode_operators+'let^bad=左×1\n'
    client.change(path,bad_operator,135)
    diagnostics=client.diagnostics(path,135)
    assert len(diagnostics) == 1 and 'cross^' in diagnostics[0]['message'], diagnostics
    assert diagnostics[0]['range'] == dict(start=at(bad_operator,'×1'),
                                          end=at(bad_operator,'×1',shift=1)), diagnostics
    client.change(path,unicode_operators,136)
    assert client.diagnostics(path,136) == []
    print('PASS Unicode operator aliases, navigation and exact diagnostic ranges',flush=True)

    captures = ('var^pictures=i32^[grow^]{}\n'
                'let^new_picture=p^ {pictures.push(1)}\n'
                'let^bad=missing_capture_initializer\n'
                'new_picture()\n')
    for version,source in [(137,captures),
                           (138,captures.replace('i32^[grow^]{}','missing_capture_value')),
                           (139,captures+'let^unused=p^x {print^(pictures.len,x)}\n')]:
        client.change(path,source,version)
        diagnostics=client.diagnostics(path,version)
        expected=2 if version in (138,139) else 1
        assert len(diagnostics) == expected, diagnostics
        assert all('\x01' not in d['message'] and 'top_pictures' not in d['message'] for d in diagnostics), diagnostics
        if version == 139:
            assert any("cannot infer parameter 'x' of 'unused'" in d['message'] for d in diagnostics), diagnostics
    clean_captures=captures.replace('let^bad=missing_capture_initializer\n','')
    client.change(path,clean_captures,140)
    assert client.diagnostics(path,140) == []
    unused_capture=clean_captures.replace('new_picture()\n','')
    client.change(path,unused_capture,141)
    diagnostics=client.diagnostics(path,141)
    assert len(diagnostics) == 1 and "cannot infer captured binding 'pictures' of 'new_picture'" in diagnostics[0]['message'], diagnostics
    assert diagnostics[0]['range']['start'] == at(unused_capture,'new_picture=p^'), diagnostics
    client.change(path,clean_captures,142)
    assert client.diagnostics(path,142) == []
    print('PASS hidden capture inference cascade suppression and independent parameter diagnostics',flush=True)

    annotated_binding = ('let^bad=missing_before_message\n'
                         'var^message:u8^[max^160]=missing_message_initializer\n'
                         'let^clear_message=p^ {message.clear()}\n'
                         'clear_message()\n')
    client.change(path,annotated_binding,143)
    diagnostics=client.diagnostics(path,143)
    assert len(diagnostics) == 2, diagnostics
    hover=request('hover','message:u8^',text=annotated_binding)['contents']['value']
    assert hover == 'message: u8^[max^160]', hover
    for version,source,expected in [
            (144,annotated_binding.replace('missing_message_initializer','{}'),1),
            (145,annotated_binding.replace('u8^[max^160]','i64^'),3),
            (146,annotated_binding.replace('u8^[max^160]','MissingMessageType'),2)]:
        client.change(path,source,version)
        diagnostics=client.diagnostics(path,version)
        assert len(diagnostics) == expected, diagnostics
        hover=request('hover','message:',text=source)['contents']['value']
        assert hover == ('message: i64^' if version == 145 else
                         'message: ?' if version == 146 else 'message: u8^[max^160]'), hover
    repaired_binding=annotated_binding.replace('let^bad=missing_before_message\n','').replace('missing_message_initializer','{}')
    client.change(path,repaired_binding,147)
    assert client.diagnostics(path,147) == []
    assert request('hover','message:u8^',text=repaired_binding)['contents']['value'] == 'message: u8^[max^160]'
    print('PASS resolved variable annotations survive initializer errors and recover after edits',flush=True)

    suffix_path = work/'numeric_literal_suffixes.rh'
    suffix_text = ('let^hex=0x88884444u64\nlet^small=-128i8\n'
                   'let^floating=1.25f32\nlet^make=f^ {42u64}\nlet^result=make()\n')
    client.open(suffix_path,suffix_text)
    assert client.diagnostics(suffix_path,1) == []
    for needle,detail in [('hex=', 'hex: u64^'),('small=', 'small: i8^'),
                          ('floating=', 'floating: f32^'),('result=', 'result: u64^')]:
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=suffix_path.as_uri()),
            position=at(suffix_text,needle)))
        assert hover['contents']['value'] == detail, hover
    client.change(suffix_path,suffix_text.replace('0x88884444u64','0x88884444u'),2)
    assert any('invalid numeric literal suffix' in d['message'] for d in client.diagnostics(suffix_path,2))
    client.change(suffix_path,suffix_text,3)
    assert client.diagnostics(suffix_path,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=suffix_path.as_uri())))
    print('PASS numeric literal suffix types, hover and edit recovery',flush=True)

    attributes=('def^Node {self^{n:i32^=7}}\nvar^n=Node{}\n'
                'var^refs:Node[ref^max^10]={ref^n}\n'
                'let^view:Node[ref^..]=refs[..]\n'
                'let^borrow:ref^Node[ref^max^10]=ref^refs\n'
                'print^(view[0].n,borrow.len)\n')
    client.change(path,attributes,148)
    assert client.diagnostics(path,148) == []
    for needle,detail in [('refs:Node','refs: Node[ref^ max^10]'),
                          ('view:Node','view: Node[ref^ ..]'),
                          ('borrow:ref^','borrow: ref^ Node[ref^ max^10]')]:
        assert request('hover',needle,text=attributes)['contents']['value'] == detail
    print('PASS array attributes and element references in source and hover',flush=True)

    sized=('def^Node {self^{n:i32^=7}}\nvar^node=Node{}\n'
           'var^refs=Node[ref^]{ref^node repeat^ 2}\n'
           'var^values=i32^[]{7 repeat^ 3}\n'
           'let^copy=i32^[]{values[..]...}\n'
           'let^read=f^xs:ref^i32^[] {xs.len}\n'
           'print^(read(ref^values),refs[0].n,copy[0])\n')
    client.change(path,sized,149)
    assert client.diagnostics(path,149) == []
    for needle,detail in [('refs=Node','refs: Node[ref^]'),('values=i32^','values: i32^[]'),
                          ('copy=i32^','copy: i32^[]')]:
        assert request('hover',needle,text=sized)['contents']['value'] == detail
    broken_sized=sized+'values.push(1)\n'
    client.change(path,broken_sized,150)
    diagnostics=client.diagnostics(path,150)
    assert len(diagnostics) == 1 and 'fixed length after construction' in diagnostics[0]['message'], diagnostics
    assert request('hover','values=i32^',text=broken_sized)['contents']['value'] == 'values: i32^[]'
    client.change(path,sized,151)
    assert client.diagnostics(path,151) == []
    print('PASS runtime-sized owning array construction, signatures and diagnostic recovery',flush=True)

    literals=('def^B {self^{n:i32^=7}}\nvar^a:B[2]={B{},B{}}\n'
              'let^refs=B[ref^ 2]{a[0],ref^a[1]}\n'
              'let^typed=B[ref^ 2]{a[0],a[1]}\n'
              'var^bounded=B[ref^ max^10]{a[0]}\n'
              'var^numbers=u32^[max^10]{0,1,2,3}\n'
              'print^(refs[0].n,typed[1].n,bounded.len,numbers[3])\n')
    client.change(path,literals,152)
    assert client.diagnostics(path,152) == []
    for needle,detail in [('refs=B','refs: B[ref^ 2]'),('typed=B','typed: B[ref^ 2]'),
                          ('bounded=B','bounded: B[ref^ max^10]'),('numbers=u32^','numbers: u32^[max^10]')]:
        assert request('hover',needle,text=literals)['contents']['value'] == detail
    assert request('definition','B[ref^ 2]{a[0],a[1]}',text=literals)['range']['start'] == at(literals,'B {')
    broken_literal=literals+'let^bad=i32^[ref^ 1]{42}\n'
    client.change(path,broken_literal,153)
    diagnostics=client.diagnostics(path,153)
    assert len(diagnostics) == 1 and 'cannot borrow a temporary' in diagnostics[0]['message'], diagnostics
    assert request('hover','refs=B',text=broken_literal)['contents']['value'] == 'refs: B[ref^ 2]'
    client.change(path,literals,154)
    assert client.diagnostics(path,154) == []
    print('PASS array literal attributes, automatic element borrowing and recovery',flush=True)

    generic = '''def^Vec2<T> {
 self^{x:T,y:T}
 ZERO=Self^{x=T.ZERO,y=T.ZERO}
 ONE=Self^{x=T.ONE,y=T.ONE}
}
let^Float2=Vec2<f32^>
let^a:Float2=Float2.ONE
print^(a.x)
'''
    client.change(path,generic,155)
    assert client.diagnostics(path,155) == []
    assert request('hover','a.x',shift=2,text=generic)['contents']['value'] == 'x: f32^'
    alias_definition=request('definition','a:Float2',shift=2,text=generic)
    assert alias_definition['range']['start'] == at(generic,'Float2=Vec2'), alias_definition
    constant_definition=request('definition','Float2.ONE',shift=7,text=generic)
    assert constant_definition['range']['start'] == at(generic,'ONE=Self^'), constant_definition
    outline=client.request('textDocument/documentSymbol',dict(textDocument=dict(uri=path.as_uri())))
    assert {'Vec2','Float2','ZERO','ONE'} <= {x['name'] for x in flatten_outline(outline)}, outline
    assert not any(x['name'].endswith('>') for x in flatten_outline(outline)), outline
    broken_generic=generic.replace('Vec2<f32^>','Vec2<string^>')
    client.change(path,broken_generic,156)
    assert any('has no constant' in d['message'] for d in client.diagnostics(path,156))
    client.change(path,generic,157)
    assert client.diagnostics(path,157) == []
    print('PASS generic definitions, aliases, type constants, navigation and edit recovery',flush=True)

    nested_path = work/'nested_array_types.rh'
    nested_text = '''def^Cell { self^{n:i32^=0} }
var^grid:Cell[2 [3]] = {{Cell{},Cell{},Cell{}},{Cell{},Cell{},Cell{}}}
let^identity = template^f^x:ref^Cell[2 [3]] -> ref^(Cell[3])[2] { x }
let^result = identity(ref^grid)
let^deferred = template^p^x:(Cell[3])[2] {}
print^(result[1][2].n)
'''
    client.open(nested_path,nested_text)
    assert client.diagnostics(nested_path,1) == []
    for needle in ('grid:','grid)','result ='):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=nested_path.as_uri()),
            position=at(nested_text,needle)))
        assert 'Cell[2, 3]' in hover['contents']['value'], hover
        assert 'Cell[3][2]' not in hover['contents']['value'], hover
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=nested_path.as_uri()),
        position=at(nested_text,'deferred =')))
    assert 'x:Cell[2, 3]' in hover['contents']['value'], hover
    classified = tokens(nested_text,nested_path)
    for position in (at(nested_text,'Cell[2 [3]]'),at(nested_text,'Cell[3]')):
        assert classified[(position['line'],position['character'])] == (4,1), classified
        definition = client.request('textDocument/definition',dict(textDocument=dict(uri=nested_path.as_uri()),
            position=position))
        assert definition['range']['start'] == at(nested_text,'Cell {'), definition
    broken = nested_text.replace('Cell[2 [3]]', 'Cell[3][2]',1)
    client.change(nested_path,broken,2)
    assert any('consecutive array type brackets' in d['message'] for d in client.diagnostics(nested_path,2))
    client.change(nested_path,nested_text,3)
    assert client.diagnostics(nested_path,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=nested_path.as_uri())))
    print('PASS nested array type hover, navigation and bracket-chain recovery',flush=True)

    rectangular_path = work/'rectangular_array_types.rh'
    rectangular_text = '''def^Cell { self^{n:i32^=0} }
var^grid:Cell[2,3] = {{Cell{},Cell{},Cell{}},{Cell{},Cell{},Cell{}}}
let^deferred = template^p^x:Cell[2,3] {}
let^cell = grid[1,2]
print^(cell.n)
'''
    client.open(rectangular_path,rectangular_text)
    assert client.diagnostics(rectangular_path,1) == []
    for needle in ('grid:','deferred ='):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=rectangular_path.as_uri()),
            position=at(rectangular_text,needle)))
        assert 'Cell[2, 3]' in hover['contents']['value'], hover
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=rectangular_path.as_uri()),
        position=at(rectangular_text,'cell =')))
    assert 'cell: Cell' in hover['contents']['value'], hover
    classified = tokens(rectangular_text,rectangular_path)
    position = at(rectangular_text,'Cell[2,3]')
    assert classified[(position['line'],position['character'])] == (4,1), classified
    definition = client.request('textDocument/definition',dict(textDocument=dict(uri=rectangular_path.as_uri()),
        position=position))
    assert definition['range']['start'] == at(rectangular_text,'Cell {'), definition
    broken = rectangular_text.replace('grid[1,2]', 'grid[1,2,0]')
    client.change(rectangular_path,broken,2)
    assert any('indexing requires an array' in d['message'] for d in client.diagnostics(rectangular_path,2))
    client.change(rectangular_path,rectangular_text,3)
    assert client.diagnostics(rectangular_path,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=rectangular_path.as_uri())))
    print('PASS rectangular array type hover, navigation and index arity recovery',flush=True)

    grow_grid_path = work/'rectangular_grow_types.rh'
    grow_grid_text = '''var^grid=i32^[grow^,3]{{1,2,3}}
let^deferred=template^p^x:i32^[grow^,3] {}
grid.push({4,5,6})
print^(grid[1,2])
'''
    client.open(grow_grid_path,grow_grid_text)
    assert client.diagnostics(grow_grid_path,1) == []
    for needle in ('grid=','deferred='):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=grow_grid_path.as_uri()),
            position=at(grow_grid_text,needle)))
        assert 'i32^[grow^, 3]' in hover['contents']['value'], hover
    client.send('textDocument/didClose',dict(textDocument=dict(uri=grow_grid_path.as_uri())))
    print('PASS growable rectangular array hover and template signatures',flush=True)

    array_names_path = work/'array_canonical_names.rh'
    array_names_text = '''var^inferred={{0 repeat^3} repeat^3}
var^rows=(i32^[grow^])[max^8]{}
let^deferred=template^p^a:(i32^[3])[2], b:(i32^[grow^])[max^8], c:(read^i32^[..])[2] {}
print^(inferred[1,1])
'''
    client.open(array_names_path,array_names_text)
    assert client.diagnostics(array_names_path,1) == []
    for needle,signature in (('inferred=','i32^[3, 3]'),('rows=','i32^[max^8 [grow^]]'),
                             ('deferred=','i32^[2, 3]'),('deferred=','i32^[max^8 [grow^]]'),
                             ('deferred=','i32^[2 read^[..]]')):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=array_names_path.as_uri()),
            position=at(array_names_text,needle)))
        assert signature in hover['contents']['value'], hover
    client.send('textDocument/didClose',dict(textDocument=dict(uri=array_names_path.as_uri())))
    print('PASS unified array dimensions and canonical outer-first type names',flush=True)

    spread_path = work/'spread_array_types.rh'
    spread_text = '''var^src=i32^[2,3]{}
var^inferred={src...}
var^shape=i32^[_^,_^]{src...}
var^rows=u8^[2 []]{{1},{2,3}}
var^deep={rows...}
'''
    client.open(spread_path,spread_text)
    assert client.diagnostics(spread_path,1) == []
    for needle,signature in (('inferred=','i32^[2, 3]'),('shape=','i32^[2, 3]'),('deep=','u8^[2 []]')):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=spread_path.as_uri()),
            position=at(spread_text,needle)))
        assert signature in hover['contents']['value'], hover
    client.send('textDocument/didClose',dict(textDocument=dict(uri=spread_path.as_uri())))
    print('PASS spread array source types, inferred shapes and deep-copy hover',flush=True)

    jagged_path = work/'jagged_arrays.rh'
    jagged_text = '''var^rows=i32^[max^8[grow^]]{{1},{2,3}}
let^row=rows[0]
row.push(4)
print^(row.len,rows[1][0])
'''
    client.open(jagged_path,jagged_text)
    assert client.diagnostics(jagged_path,1) == []
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=jagged_path.as_uri()),
        position=at(jagged_text,'row=')))
    assert 'ref^' in hover['contents']['value'] and 'i32^[grow^]' in hover['contents']['value'], hover
    broken = jagged_text.replace('row.push(4)', 'rows.clear()')
    client.change(jagged_path,broken,2)
    assert any('cannot shrink' in d['message'] for d in client.diagnostics(jagged_path,2))
    client.change(jagged_path,jagged_text,3)
    assert client.diagnostics(jagged_path,3) == []
    unsupported = jagged_text.replace('max^8','grow^')
    client.change(jagged_path,unsupported,4)
    diagnostics = client.diagnostics(jagged_path,4)
    assert len(diagnostics) == 1, diagnostics
    assert diagnostics[0]['range']['start']['line'] == 0, diagnostics
    assert 'nonempty fixed-size' in diagnostics[0]['message'], diagnostics
    client.change(jagged_path,jagged_text,5)
    assert client.diagnostics(jagged_path,5) == []
    unsupported_constructor = jagged_text.replace('i32^[max^8[grow^]]{{1},{2,3}}','i32^[[grow^]](8)')
    client.change(jagged_path,unsupported_constructor,6)
    diagnostics = client.diagnostics(jagged_path,6)
    assert len(diagnostics) == 1 and diagnostics[0]['range']['start']['line'] == 0, diagnostics
    assert 'parentheses are reserved for size/capacity' in diagnostics[0]['message'], diagnostics
    client.change(jagged_path,jagged_text,7)
    assert client.diagnostics(jagged_path,7) == []
    fixed_jagged_text = jagged_text.replace('max^8','2')
    client.change(jagged_path,fixed_jagged_text,8)
    assert client.diagnostics(jagged_path,8) == []
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=jagged_path.as_uri()),
        position=at(fixed_jagged_text,'row=')))
    assert 'ref^' in hover['contents']['value'] and 'i32^[grow^]' in hover['contents']['value'], hover
    broken = fixed_jagged_text.replace('{{1},{2,3}}','{{1}}')
    client.change(jagged_path,broken,9)
    assert any('exactly 2 elements' in d['message'] for d in client.diagnostics(jagged_path,9))
    client.change(jagged_path,fixed_jagged_text,10)
    assert client.diagnostics(jagged_path,10) == []
    repeat_max_text = 'var^grid=u8^[2[3]]{{0 repeat^max^} repeat^max^}\nprint^(grid[1][2])\n'
    client.change(jagged_path,repeat_max_text,11)
    assert client.diagnostics(jagged_path,11) == []
    bad_repeat_max = repeat_max_text.replace('u8^[2[3]]{{0 repeat^max^} repeat^max^}',
                                            'u8^[grow^]{0 repeat^max^}')
    client.change(jagged_path,bad_repeat_max,12)
    assert any('requires a fixed-length array type' in d['message']
               for d in client.diagnostics(jagged_path,12))
    client.change(jagged_path,repeat_max_text,13)
    assert client.diagnostics(jagged_path,13) == []
    inferred_text = ('var^a=i32^[_^]{0 repeat^5}\n'
                     'var^b=i32^[_^,_^]{{0 repeat^3} repeat^3}\n'
                     'var^c:i32^[2,_^]={{1,2,3},{4,5,6}}\n')
    client.change(jagged_path,inferred_text,14)
    assert client.diagnostics(jagged_path,14) == []
    for needle,detail in [('a=', 'i32^[5]'),('b=', 'i32^[3, 3]'),('c:', 'i32^[2, 3]')]:
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=jagged_path.as_uri()),
            position=at(inferred_text,needle)))
        assert detail in hover['contents']['value'], hover
    broken = inferred_text.replace('0 repeat^5','0 repeat^max^')
    client.change(jagged_path,broken,15)
    diagnostics = client.diagnostics(jagged_path,15)
    assert len(diagnostics) == 1 and 'cannot infer array length _^' in diagnostics[0]['message'], diagnostics
    client.change(jagged_path,inferred_text,16)
    assert client.diagnostics(jagged_path,16) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=jagged_path.as_uri())))
    print('PASS owned jagged row hover, borrow diagnostics and edit recovery',flush=True)

    threads_path = work/'implicit_thread_calls.rh'
    threads_text = '''let^c = import^std.channel
let^t = import^std.thread
let^worker = p^out:ref^c.Channel<i32^> {
    var^value:i32^ = 42
    try^out.send(ref^value)
}
var^q = try^c.Channel<i32^>.new(1, 4)
var^thread = try^t.spawn(worker, ref^q)
var^result = q.receive()
if^result fits^c.Message<i32^> { print^(result.value) }
thread.join()
'''
    client.open(threads_path,threads_text)
    assert client.diagnostics(threads_path,1) == []
    classified = tokens(threads_text,threads_path)
    for match in re.finditer(r'\b[ct](?=\s*=\s*import\^|\.)',threads_text):
        position = at(threads_text,threads_text[match.start():],shift=0)
        assert classified[(position['line'],position['character'])] == (1,11), (position,classified)
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=threads_path.as_uri()),
            position=position))
        assert hover is None, hover
        assert client.request('textDocument/definition',dict(textDocument=dict(uri=threads_path.as_uri()),
            position=position)) is None
    client.send('textDocument/didClose',dict(textDocument=dict(uri=threads_path.as_uri())))
    print('PASS implicit disposal and error reporting do not replace import aliases',flush=True)

    channel_path = Path(__file__).resolve().parents[1]/'stdlib/std/channel.rh'
    channel_text = channel_path.read_text(encoding='utf-8')
    client.open(channel_path,channel_text)
    assert client.diagnostics(channel_path,1) == []
    colored = tokens(channel_text,channel_path)
    sys_path = channel_path.with_name('sys.rh')
    sys_text = sys_path.read_text(encoding='utf-8')
    for document in (channel_path, channel_path.with_name('thread.rh')):
        text = document.read_text(encoding='utf-8')
        if document != channel_path:
            client.open(document,text)
            assert client.diagnostics(document,1) == []
        if 'Disposable' not in text:
            continue
        position = at(text,'Disposable')
        classified = tokens(text,document)
        assert classified[(position['line'],position['character'])] == (10,1), classified
        definition = client.request('textDocument/definition',dict(textDocument=dict(uri=document.as_uri()),
            position=position))
        if isinstance(definition,list):
            definition = definition[0]
        assert definition['uri'] == sys_path.as_uri(), definition
        assert definition['range']['start'] == at(sys_text,'Disposable'), definition
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=document.as_uri()),position=position))
        assert 'std.sys.Disposable' in hover['contents']['value'], hover
        if document.name == 'thread.rh':
            position = at(text,'ThreadEntry')
            definition = client.request('textDocument/definition',dict(
                textDocument=dict(uri=document.as_uri()),position=position))
            if isinstance(definition,list):
                definition = definition[0]
            assert definition['uri'].lower() == sys_path.as_uri().lower(), definition
            assert definition['range']['start'] == at(sys_text,'ThreadEntry'), definition
            assert classified[(position['line'],position['character'])] == (11,1), classified
            hover = client.request('textDocument/hover',dict(
                textDocument=dict(uri=document.as_uri()),position=position))
            assert 'externC^ p^' in hover['contents']['value'], hover
            definition = client.request('textDocument/definition',dict(
                textDocument=dict(uri=document.as_uri()), position=at(text,'rh_thread_spawn(worker')))
            if isinstance(definition,list):
                definition = definition[0]
            assert definition['uri'].lower() == document.as_uri().lower(), definition
            assert definition['range']['start'] == at(text,'rh_thread_spawn:p^'), definition
            position = at(text,'thread_entry^')
            position['character'] += len('thread_')
            completions = client.request('textDocument/completion',dict(
                textDocument=dict(uri=document.as_uri()),position=position))
            labels = {item['label'] for item in completions}
            assert {'thread_entry^','thread_transfer_type^','thread_args^','thread_stack_count^'} <= labels, labels
            assert 'thread_spawn^' not in labels, labels
            hover = client.request('textDocument/hover',dict(
                textDocument=dict(uri=document.as_uri()),position=at(text,'spawn = template^p^',1)))
            assert 'template^p^' in hover['contents']['value'] and 'Thread' in hover['contents']['value'], hover
            for needle, expected in (
                    ('worker =', 'worker: std.sys.ThreadEntry'),
                    ('worker, transfer', 'worker: std.sys.ThreadEntry'),
                    ('transfer =', 'transfer: ref^read^ std.sys.ThreadTransfer'),
                    ('transfer, thread_args^', 'transfer: ref^read^ std.sys.ThreadTransfer'),
                    ('h = rh_thread_spawn', 'h: i64^')):
                hover = client.request('textDocument/hover',dict(
                    textDocument=dict(uri=document.as_uri()),position=at(text,needle)))
                assert hover['contents']['value'] == expected, (needle,hover)
            for name, result in (
                    ('thread_entry^', 'std.sys.ThreadEntry'),
                    ('thread_transfer_type^', 'std.sys.ThreadTransfer'),
                    ('thread_args^', 'std.sys.ThreadArgs'),
                    ('thread_stack_count^', 'i64^')):
                position = at(text,name)
                hover = client.request('textDocument/hover',dict(
                    textDocument=dict(uri=document.as_uri()),position=position))
                assert name in hover['contents']['value'] and result in hover['contents']['value'], hover
                assert client.request('textDocument/definition',dict(
                    textDocument=dict(uri=document.as_uri()),position=position)) is None
            help = client.request('textDocument/signatureHelp',dict(
                textDocument=dict(uri=document.as_uri()),position=at(text,'entry, args)',shift=len('entry, '))))
            assert help['activeParameter'] == 1, help
            assert [p['label'] for p in help['signatures'][0]['parameters']] == ['entry','args'], help
            for needle in ('Thread | Error.StartFailed', 'Thread.spawn(entry'):
                position = at(text,needle)
                assert classified[(position['line'],position['character'])] == (6,1), classified
                definition = client.request('textDocument/definition',dict(
                    textDocument=dict(uri=document.as_uri()),position=position))
                assert definition['range']['start'] == at(text,'Thread :'), definition
            position = at(text,'spawn(entry, args)')
            hover = client.request('textDocument/hover',dict(
                textDocument=dict(uri=document.as_uri()),position=position))
            assert 'template^p^' in hover['contents']['value'], hover
            definition = client.request('textDocument/definition',dict(
                textDocument=dict(uri=document.as_uri()),position=position))
            assert definition['range']['start'] == at(text,'spawn = template^p^'), definition
            refs = client.request('textDocument/references',dict(
                textDocument=dict(uri=document.as_uri()),position=at(text,'worker ='),
                context=dict(includeDeclaration=True)))
            assert len(refs) == 2, refs
        if document != channel_path:
            client.send('textDocument/didClose',dict(textDocument=dict(uri=document.as_uri())))
    for name in ('InvalidCapacity','OutOfMemory','Closed','Full','MessageTooLarge'):
        position = at(channel_text,name)
        assert colored[(position['line'],position['character'])] == (len(name),1), (name,colored)
    for name in ('channel_new^','channel_send^','channel_receive^','channel_clone^','channel_close^','channel_valid^'):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=channel_path.as_uri()),
            position=at(channel_text,name)))
        assert name in hover['contents']['value'] and 'channel_handle^<T>' in hover['contents']['value'], hover
    refs = client.request('textDocument/references',dict(textDocument=dict(uri=channel_path.as_uri()),
        position=at(channel_text,'sent ='),context=dict(includeDeclaration=True)))
    expected = [at(channel_text,'sent ='),at(channel_text,'sent(channel_send^',0),at(channel_text,'sent(channel_send^',1)]
    assert len(refs) == 3 and {tuple(r['range']['start'].values()) for r in refs} == {
        tuple(p.values()) for p in expected}, refs
    definition = client.request('textDocument/definition',dict(textDocument=dict(uri=channel_path.as_uri()),
        position=at(channel_text,'sent(channel_send^')))
    if isinstance(definition,list):
        definition = definition[0]
    assert definition['range']['start'] == at(channel_text,'sent ='), definition
    client.send('textDocument/didClose',dict(textDocument=dict(uri=channel_path.as_uri())))

    handle_path = work/'typed_channel.rh'
    handle_text = '''import^std.sys
def^Resource<T> : std.sys.Disposable { dispose = p^self^ {} }
var^h:channel_handle^<i32^> = channel_new^(i32^, 1, 4)
channel_close^(ref^h)
'''
    client.open(handle_path,handle_text)
    assert client.diagnostics(handle_path,1) == []
    position = at(handle_text,'channel_handle^')
    assert tokens(handle_text,handle_path)[(position['line'],position['character'])] == (14,1)
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=handle_path.as_uri()),
        position=at(handle_text,'channel_new^')))
    assert 'channel_new^: p^' in hover['contents']['value'], hover
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=handle_path.as_uri()),
        position=at(handle_text,'h:')))
    assert 'channel_handle^<i32^>' in hover['contents']['value'], hover
    position = at(handle_text,'Disposable')
    assert tokens(handle_text,handle_path)[(position['line'],position['character'])] == (10,1)
    definition = client.request('textDocument/definition',dict(textDocument=dict(uri=handle_path.as_uri()),
        position=position))
    if isinstance(definition,list):
        definition = definition[0]
    assert definition['uri'].lower() == sys_path.as_uri().lower(), definition
    assert definition['range']['start'] == at(sys_text,'Disposable'), definition
    client.send('textDocument/didClose',dict(textDocument=dict(uri=handle_path.as_uri())))

    short_errors_path = work/'short_errors.rh'
    short_errors_text = '''errordef^E {A,B,C,D {message=f^self^ ->string^ {"explicit"}}}
var^a=E.A{}
print^(a.message())
'''
    client.open(short_errors_path,short_errors_text)
    assert client.diagnostics(short_errors_path,1) == []
    colored = tokens(short_errors_text,short_errors_path)
    for name in ('A','B','C','D'):
        position = at(short_errors_text,name)
        assert colored[(position['line'],position['character'])] == (1,1), (name,colored)
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=short_errors_path.as_uri()),
            position=position))
        assert hover['contents']['value'] == f'{name}: E.{name}', hover
    outline = client.request('textDocument/documentSymbol',dict(textDocument=dict(uri=short_errors_path.as_uri())))
    methods = [item for item in flatten_outline(outline) if item['name'] == 'message']
    assert len(methods) == 1, methods
    assert methods[0]['selectionRange']['start'] == at(short_errors_text,'message=f^'), methods
    completion = client.request('textDocument/completion',dict(textDocument=dict(uri=short_errors_path.as_uri()),
        position=at(short_errors_text,'a.message()',shift=2)))
    items = completion['items'] if isinstance(completion,dict) else completion
    assert 'message' in {item['label'] for item in items}, items
    client.send('textDocument/didClose',dict(textDocument=dict(uri=short_errors_path.as_uri())))

    generic_refs_path = work/'generic_refs.rh'
    generic_refs_text = '''module^generic_refs
let^sent=p^status:i32^ {}
public^def^Box<T> {
    self^{value:T}
    send=p^self^ {sent(0)}
    shadow=p^self^,sent:p^i32^; {sent(0)}
    local=p^self^ {let^sent=p^n:i32^ {} sent(0)}
    block=p^self^ {
        do^{let^sent=p^n:i32^ {} sent(0)}
        sent(1)
        let^cb=sent
        cb(2)
    }
}
'''
    client.open(generic_refs_path,generic_refs_text)
    assert client.diagnostics(generic_refs_path,1) == []
    refs = client.request('textDocument/references',dict(textDocument=dict(uri=generic_refs_path.as_uri()),
        position=at(generic_refs_text,'sent=p^'),context=dict(includeDeclaration=False)))
    expected = [at(generic_refs_text,'sent(0)'),at(generic_refs_text,'sent(1)'),
                at(generic_refs_text,'cb=sent',shift=3)]
    assert len(refs) == 3 and {tuple(r['range']['start'].values()) for r in refs} == {
        tuple(p.values()) for p in expected}, refs
    for occurrence, declaration in ((1,'sent:p^i32^;'),(2,'sent=p^n:i32^')):
        definition = client.request('textDocument/definition',dict(
            textDocument=dict(uri=generic_refs_path.as_uri()),position=at(generic_refs_text,'sent(0)',occurrence)))
        assert definition['range']['start'] == at(generic_refs_text,declaration), definition
    client.send('textDocument/didClose',dict(textDocument=dict(uri=generic_refs_path.as_uri())))
    print('PASS uninstantiated generic references, channel helper navigation and lexical shadowing',flush=True)

    statics="""def^ V {self^{x:i32^} ZERO=Self^{x=0} make=f^ x:i32^ ->Self^ {Self^{x=x}} get=f^ self^ ->i32^ {self^.x+def^.make(1).x}}
let^ W=V
let^ a=V.make(1)
print^(a.get(),W.make(2).x,V.ZERO.x)
"""
    client.change(path,statics,158)
    assert client.diagnostics(path,158) == []
    for needle,shift,want in [('a.get',2,{'get','x'}),('V.make',2,{'ZERO','make'}),('W.make',2,{'ZERO','make'}),
                              ('def^.make',5,{'ZERO','make'}),('self^.x',6,{'get','x'})]:
        completions=request('completion',needle,shift=shift,text=statics)
        assert want == {x['label'] for x in completions} & {'ZERO','make','get','x'}, (needle,completions)
    print('PASS static members complete on types and def^, methods on values',flush=True)

    qualified="""import^std.math.vector2
let^F2 = std.math.vector2.Vector2
def^F2 { self^{ x:f32^ } }
"""
    client.change(path,qualified,159)
    duplicate=client.diagnostics(path,159)
    assert [d['range']['start'] for d in duplicate if 'duplicate declaration' in d['message']] == [at(qualified,'F2 {')], duplicate
    token_data=client.request('textDocument/semanticTokens/full',dict(textDocument=dict(uri=path.as_uri())))['data']
    line=column=0
    spelled={}
    for offset in range(0,len(token_data),5):
        delta_line,delta_column,length,kind,_=token_data[offset:offset+5]
        line+=delta_line
        column=delta_column if delta_line else column+delta_column
        if line == 1:
            spelled[qualified.splitlines()[1][column:column+length]]=kind
    assert spelled == {'F2':1,'std':11,'math':11,'vector2':11,'Vector2':1}, spelled
    print('PASS duplicate declarations and qualified type paths are located',flush=True)

    # A syntax error inside the only caller's body hides the calls, not the captures' types.
    broken_caller=('var^pictures=i32^[grow^]{}\n'
                   'let^hud_point=f^x:i64^ {pictures[x]}\n'
                   'let^hud_quad=p^x:i64^ {print^(hud_point(x))}\n'
                   'let^upload=p^ {\n    var^hud=i32^[grow^]\n    hud_quad(0)\n}\n'
                   'upload()\n')
    client.change(path,broken_caller,160)
    diagnostics=client.diagnostics(path,160)
    assert len(diagnostics) == 1 and 'needs {elements}' in diagnostics[0]['message'], diagnostics
    print('PASS a broken function body does not cascade into capture inference',flush=True)
    specialized='''let^write=p^x {print^("v",x)}
let^pair=f^a,b {a}
write(1) write("s") write(true^) write(2)
print^(pair(1,"s"),pair("a",2))
'''
    client.change(path,specialized,161)
    assert client.diagnostics(path,161) == []
    for needle in ('write=p^','write("s")'):
        hover=request('hover',needle,text=specialized)['contents']['value']
        assert hover == 'write : p^x:i32^; & p^x:string^; & p^x:bool^;', hover
    hover=request('hover','pair=f^',text=specialized)['contents']['value']
    assert hover == 'pair : f^a:i32^, b:string^ -> i32^; & f^a:string^, b:i32^ -> string^;', hover
    print('PASS call-site specializations listed as hover intersections',flush=True)
    typed='''def^ Box {self^{n:i32^=0}}
let^x=1
print^(typeof^(x).signature^,i32^.signature^,Box.signature^)
'''
    client.change(path,typed,162)
    assert client.diagnostics(path,162) == []
    for needle in ('(x).sig','i32^.sig','Box.sig'):
        labels=[x['label'] for x in request('completion',needle,shift=len(needle)-3,text=typed)]
        assert labels == ['signature^'], (needle,labels)
    assert request('hover','(x).',shift=1,text=typed)['contents']['value'] == 'x: i32^'
    print('PASS signature^ completion after type expressions and typeof^ operand hover',flush=True)

    readonly_path = work/'readonly.rh'
    readonly_text = ('def^Cell{self^{x:i32^=1}}\n'
                     'let^view=f^x:ref^Cell ->ref^read^Cell {x}\n'
                     'var^a=Cell{}\n'
                     'let^r=view(ref^a)\n'
                     'print^(r.x)\n')
    client.open(readonly_path,readonly_text)
    assert client.diagnostics(readonly_path,1) == []
    hover = client.request('textDocument/hover',dict(
        textDocument=dict(uri=readonly_path.as_uri()),position=at(readonly_text,'r=view')))
    assert 'ref^read^' in hover['contents']['value'], hover
    client.change(readonly_path,readonly_text+'a.x:=2\n',2)
    diagnostics = client.diagnostics(readonly_path,2)
    assert diagnostics == [], diagnostics
    client.change(readonly_path,readonly_text+'r.x:=2\n',3)
    diagnostics = client.diagnostics(readonly_path,3)
    assert len(diagnostics) == 1 and 'read^' in diagnostics[0]['message'], diagnostics
    client.change(readonly_path,readonly_text,4)
    assert client.diagnostics(readonly_path,4) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=readonly_path.as_uri())))
    print('PASS read-reference hover, alias mutation, capability diagnostics and recovery',flush=True)

    try_path = work/'try_destinations.rh'
    try_text = '''errordef^E { Bad, Other }
errordef^G { Open }
let^work=p^ ->nil^|E {return^E.Bad{}}
let^open=p^ ->nil^|G {return^G.Open{}}
let^passes=p^ ->nil^|G {
    try^open()
    return^nil^
}
do^{
    do^{
        try^work()
    catch^E.Bad:
        print^(1)
    }
catch^:
    print^(2)
}
do^{
    try^open()
catch^:
    print^(3)
}
try^open()
'''
    client.open(try_path,try_text)
    assert client.diagnostics(try_path,1) == []
    def on_try(method,needle,occurrence=0):
        return client.request('textDocument/'+method,dict(textDocument=dict(uri=try_path.as_uri()),
                              position=at(try_text,needle,occurrence)))
    def said(needle,occurrence,expected):
        value=on_try('hover',needle,occurrence)['contents']['value']
        assert value == expected, repr(value)
    said('try^open()',0,"try^: G.Open\nG.Open -> result of 'passes' at 5:12")
    assert on_try('definition','try^open()')['range']['start'] == at(try_text,'p^ ->nil^|G {\n    try^')
    said('try^work()',0,'try^: E.Bad | E.Other\nE.Bad -> catch^ at 12:5\nE.Other -> catch^ at 15:1')
    assert on_try('definition','try^work()')['range']['start'] == at(try_text,'catch^E.Bad')
    said('try^open()',1,'try^: G.Open\nG.Open -> catch^ at 20:1')
    said('try^open()',2,'try^: G.Open\nG.Open -> out of the unit: the runtime reports it and exits')
    labels=[x['label'] for x in client.request('textDocument/completion',dict(
        textDocument=dict(uri=try_path.as_uri()),position=at(try_text,'try^open()',2)))]
    assert labels.count('try^') <= 1, labels
    client.send('textDocument/didClose',dict(textDocument=dict(uri=try_path.as_uri())))
    print('PASS try^ hover and definition name where each error goes',flush=True)

    alias_path = work/'local_type.rh'
    alias_text = '''let^g=template^p^x {
    let^kind=typeof^x
    var^y:kind=x
    if^kind=i32^ {print^(y)}
}
g(1)
'''
    client.open(alias_path,alias_text)
    assert client.diagnostics(alias_path,1) == []
    def alias_definition(needle):
        return client.request('textDocument/definition',dict(textDocument=dict(uri=alias_path.as_uri()),
                              position=at(alias_text,needle)))['range']['start']
    assert alias_definition('kind=x') == at(alias_text,'kind=typeof^x')
    assert alias_definition('kind=i32^') == at(alias_text,'kind=typeof^x')
    client.send('textDocument/didClose',dict(textDocument=dict(uri=alias_path.as_uri())))
    print('PASS a local type goes to its binding',flush=True)

    short_alias_path = work/'short_type_alias.rh'
    short_alias_text = '''import^std.math.vector2
let^F2=std.math.vector2.Vector2
let^LongVectorAlias=std.math.vector2.Vector2
var^vertices=F2[grow^]{}
let^read=f^p:F2 {
 var^child=1
 return^p.x+f32^(child)
}
var^point=LongVectorAlias{}
print^(read(point))
'''
    client.open(short_alias_path,short_alias_text)
    assert client.diagnostics(short_alias_path,1) == []
    colored=tokens(short_alias_text,short_alias_path)
    for needle in ('grow^','var^child'):
        pos=at(short_alias_text,needle)
        assert (pos['line'],pos['character']) not in colored, (needle,colored)
    for needle, length in (('F2[grow^]',2),('F2 {',2),('LongVectorAlias{}',15)):
        pos=at(short_alias_text,needle)
        assert colored[(pos['line'],pos['character'])] == (length,1), (needle,colored)
        hover=client.request('textDocument/hover',dict(textDocument=dict(uri=short_alias_path.as_uri()),position=pos))
        assert hover['range']['start'] == pos, hover
        assert hover['range']['end'] == dict(line=pos['line'],character=pos['character']+length), hover
    client.send('textDocument/didClose',dict(textDocument=dict(uri=short_alias_path.as_uri())))
    print('PASS type aliases color only their written tokens and keep exact hover ranges',flush=True)

    inline_path = work/'inline.rh'
    inline_text = ('let^show=template^p^... {\n'
                   ' inline^for^x in^... {print^(x)}\n'
                   '}\nshow(1,"two",true^)\n')
    client.open(inline_path,inline_text)
    assert client.diagnostics(inline_path,1) == []
    invalid_inline = inline_text.replace('print^(x)', 'x.missing()')
    client.change(inline_path,invalid_inline,2)
    diagnostics = client.diagnostics(inline_path,2)
    assert diagnostics and any('method call requires a structure' in d['message']
                               for d in diagnostics), diagnostics
    client.change(inline_path,inline_text,3)
    assert client.diagnostics(inline_path,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=inline_path.as_uri())))
    print('PASS inline expansion diagnostics and recovery',flush=True)

    template_path = work/'template_diagnostics.rh'
    template_def = ('let^max=template^p^... {\n'
                    ' var^best=...[0]\n'
                    ' inline^for^x in^... {if^x>best {best:=x}}\n'
                    ' return^best\n}\n')
    template_calls = ('print^(max(2,3,5))\n'
                      'print^(max(2,""))\n'
                      'print^(max(2,""))\n'
                      'print^(max(2,true^))\n')
    template_text = template_def+template_calls
    client.open(template_path,template_text)
    diagnostics = client.diagnostics(template_path,1)
    calls = [d for d in diagnostics if 'in instantiation' in d['message']]
    assert {d['range']['start']['line'] for d in calls} == {6,7,8}, diagnostics
    assert all(d['range']['start']['character'] == 7 for d in calls), calls
    primary = [d for d in diagnostics if 'in instantiation' not in d['message']]
    assert len(primary) == 1 and primary[0]['range']['start']['line'] == 2, diagnostics
    for d in calls:
        related = d['relatedInformation'][0]
        assert related['location']['uri'].lower() == template_path.as_uri().lower(), related
        assert related['location']['range'] == primary[0]['range'], related
    assert len(primary[0]['relatedInformation']) == 3, primary

    nested = template_def+('let^outer=template^p^... {return^max(...)}\n'
                          'print^(outer(2,""))\n')
    client.change(template_path,nested,2)
    diagnostics = client.diagnostics(template_path,2)
    calls = [d for d in diagnostics if 'in instantiation' in d['message']]
    assert {d['range']['start']['line'] for d in calls} == {5,6}, diagnostics

    inference = 'let^bad=template^f^x {x+1}\nprint^(bad("oops"))\n'
    client.change(template_path,inference,3)
    diagnostics = client.diagnostics(template_path,3)
    assert any('in instantiation' in d['message'] and d['range']['start']['line'] == 1
               for d in diagnostics), diagnostics
    client.change(template_path,template_def+'print^(max(2,3,5))\n',4)
    assert client.diagnostics(template_path,4) == []

    recursive = template_def+('let^outer=template^p^n:i32^,... ->i32^ {\n'
                             ' if^n>0 {return^this^(n-1,...)}\n'
                             ' return^max(...)\n}\n'
                             'print^(outer(1,2,""))\n')
    client.change(template_path,recursive,5)
    diagnostics = client.diagnostics(template_path,5)
    calls = [d for d in diagnostics if 'in instantiation' in d['message']]
    assert {d['range']['start']['line'] for d in calls} == {6,7,9}, diagnostics

    template_module = work/'lib'/'template_diagnostics.rh'
    template_module.write_text('module^lib.template_diagnostics\npublic^'+template_def,encoding='utf-8')
    imported = ('let^lib=import^lib.template_diagnostics\n'
                'print^(lib.max(2,""))\n')
    client.change(template_path,imported,6)
    diagnostics = client.diagnostics(template_path,6)
    assert len(diagnostics) == 1 and 'in instantiation' in diagnostics[0]['message'], diagnostics
    related = diagnostics[0]['relatedInformation'][0]['location']
    assert related['uri'].lower() == template_module.as_uri().lower(), related
    assert related['range']['start']['line'] == 3, related
    client.change(template_path,imported.replace('""','3'),7)
    assert client.diagnostics(template_path,7) == []
    normal = template_def+('let^outer=p^n:i32^ {return^max(n,"")}\n'
                          'print^(outer(2))\n')
    client.change(template_path,normal,8)
    diagnostics = client.diagnostics(template_path,8)
    calls = [d for d in diagnostics if 'in instantiation' in d['message']]
    assert len(calls) == 2, diagnostics
    assert {tuple(d['range']['start'].values()) for d in calls} == {
        tuple(at(normal,'max(n').values()), tuple(at(normal,'outer(2)').values())}, diagnostics
    client.send('textDocument/didClose',dict(textDocument=dict(uri=template_path.as_uri())))
    print('PASS template call-site diagnostics, cached instances, nesting and recovery',flush=True)

    inferred_fields = work/'inferred_fields.rh'
    field_text = 'def^ A {self^{count=0,label=""}}\nlet^a=A{}\nprint^(a.count)\n'
    client.open(inferred_fields,field_text)
    assert client.diagnostics(inferred_fields,1) == []
    hover = client.request('textDocument/hover',dict(
        textDocument=dict(uri=inferred_fields.as_uri()),position=at(field_text,'count=0')))
    assert 'i32^' in hover['contents']['value'], hover
    client.change(inferred_fields,field_text.replace('count=0','count'),2)
    diagnostics = client.diagnostics(inferred_fields,2)
    assert any('requires a type annotation or an initializer' in d['message'] for d in diagnostics), diagnostics
    client.change(inferred_fields,field_text,3)
    assert client.diagnostics(inferred_fields,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=inferred_fields.as_uri())))
    print('PASS inferred field defaults, hover and diagnostic recovery',flush=True)

    static_path = work/'static_members.rh'
    static_text = ('def^ Counter {var^count=0 increment=p^ {def^.count+=1}}\n'
                   'Counter.increment()\nprint^(Counter.count)\n')
    client.open(static_path,static_text)
    static_diagnostics = client.diagnostics(static_path,1)
    assert static_diagnostics == [], static_diagnostics
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=static_path.as_uri()),
        position=at(static_text,'count=0')))
    assert 'i32^' in hover['contents']['value'] and '\x01' not in hover['contents']['value'], hover
    definition = client.request('textDocument/definition',dict(textDocument=dict(uri=static_path.as_uri()),
        position=at(static_text,'Counter.count',shift=8)))
    if isinstance(definition,list):
        definition = definition[0]
    assert definition['range']['start'] == at(static_text,'count=0'), definition
    completion = client.request('textDocument/completion',dict(textDocument=dict(uri=static_path.as_uri()),
        position=at(static_text,'Counter.count',shift=8)))
    items = completion['items'] if isinstance(completion,dict) else completion
    assert 'count' in {item['label'] for item in items}, items
    client.change(static_path,static_text.replace('count=0','count=""'),2)
    assert client.diagnostics(static_path,2)
    client.change(static_path,static_text,3)
    assert client.diagnostics(static_path,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=static_path.as_uri())))
    print('PASS mutable static members, hover, navigation and recovery',flush=True)

    typed_static_path = work/'typed_static_members.rh'
    typed_static_text = ('def^ A {let^step:f32^=0.2 let^RADIUS=f32^(16.0) var^count:i64^=0}\n'
                         'let^value=A.step\nlet^radius=A.RADIUS\nA.count+=1\nprint^(value,A.count,radius)\n')
    client.open(typed_static_path,typed_static_text)
    diagnostics = client.diagnostics(typed_static_path,1)
    assert diagnostics == [], diagnostics
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=typed_static_path.as_uri()),
        position=at(typed_static_text,'value=A')))
    assert 'f32^' in hover['contents']['value'], hover
    for needle,shift in [('step:f32^',0),('A.step',2),('RADIUS=f32^',0),('radius=A',0),('A.RADIUS',2)]:
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=typed_static_path.as_uri()),
            position=at(typed_static_text,needle,shift=shift)))
        assert hover and 'f32^' in hover['contents']['value'], hover
    definition = client.request('textDocument/definition',dict(
        textDocument=dict(uri=typed_static_path.as_uri()),position=at(typed_static_text,'A.step',shift=2)))
    if isinstance(definition,list):
        definition = definition[0]
    assert definition['range']['start'] == at(typed_static_text,'step:f32^'), definition
    client.send('textDocument/didClose',dict(textDocument=dict(uri=typed_static_path.as_uri())))
    print('PASS explicit typed static declarations, inferred hover and navigation',flush=True)

    entry_path = work/'entry_methods.rh'
    entry_text = ('var^count=0\nlet^fixed=7\n'
                  'def^ Counter {self^{} increment=p^ {count+=1}\n'
                  'step=p^self^ {count+=1} read=f^self^ {count+fixed}}\n'
                  'var^c=Counter{}\nCounter.increment() c.step() print^(c.read())\n')
    client.open(entry_path,entry_text)
    assert client.diagnostics(entry_path,1) == []
    for needle in ('count+=1','count+fixed','fixed}}'):
        position = at(entry_text,needle)
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=entry_path.as_uri()),
            position=position))
        assert 'i32^' in hover['contents']['value'], hover
        definition = client.request('textDocument/definition',dict(textDocument=dict(uri=entry_path.as_uri()),
            position=position))
        if isinstance(definition,list):
            definition = definition[0]
        name = 'fixed' if needle.startswith('fixed') else 'count'
        assert definition['range']['start'] == at(entry_text,name+'='), definition
    client.change(entry_path,entry_text.replace('count=0','count=""'),2)
    assert client.diagnostics(entry_path,2)
    client.change(entry_path,entry_text,3)
    assert client.diagnostics(entry_path,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=entry_path.as_uri())))
    print('PASS entry bindings in static and instance methods, navigation and recovery',flush=True)

    capture_path = work/'local_captures.rh'
    capture_text = ('let^apply=p^cb:p^i32^; {cb(1)}\nlet^run=p^ {\n'
                    ' var^count=0\n let^step=p^delta {count+=delta}\n'
                    ' apply(step) print^(count)\n}\nrun()\n')
    client.open(capture_path,capture_text)
    assert client.diagnostics(capture_path,1) == []
    position = at(capture_text,'count+=delta')
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=capture_path.as_uri()),
        position=position))
    assert 'i32^' in hover['contents']['value'], hover
    definition = client.request('textDocument/definition',dict(textDocument=dict(uri=capture_path.as_uri()),
        position=position))
    if isinstance(definition,list):
        definition = definition[0]
    assert definition['range']['start'] == at(capture_text,'count=0'), definition
    hover = client.request('textDocument/hover',dict(textDocument=dict(uri=capture_path.as_uri()),
        position=at(capture_text,'step=p^')))
    assert '\x01' not in hover['contents']['value'] and 'capture_' not in hover['contents']['value'], hover
    assert 'delta:i32^' in hover['contents']['value'], hover
    client.change(capture_path,capture_text.replace('var^count','let^count'),2)
    diagnostics = client.diagnostics(capture_path,2)
    assert any('let^' in d['message'] for d in diagnostics), diagnostics
    client.change(capture_path,capture_text,3)
    assert client.diagnostics(capture_path,3) == []
    client.send('textDocument/didClose',dict(textDocument=dict(uri=capture_path.as_uri())))
    print('PASS local captures, callback arguments, navigation and recovery',flush=True)

    top_capture_path = work/'top_capture_signatures.rh'
    top_capture_text = '''var^data=i32^[grow^]{1}
let^count:i64^=7
let^patch=p^name:read^u8^[..] -> i64^ {data.push(2) return^count+name.len}
let^zero=p^ -> i64^ {return^data.len}
print^(patch("ok".bytes),zero())
'''
    client.open(top_capture_path,top_capture_text)
    assert client.diagnostics(top_capture_path,1) == []
    for needle in ('patch=p^','patch("ok"','zero=p^'):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=top_capture_path.as_uri()),
            position=at(top_capture_text,needle)))
        detail = hover['contents']['value']
        assert '\x01' not in detail and 'top_' not in detail, hover
        assert ('p^name:read^u8^[..]' if needle.startswith('patch') else 'p^ -> i64^') in detail, hover
    help = client.request('textDocument/signatureHelp',dict(textDocument=dict(uri=top_capture_path.as_uri()),
        position=at(top_capture_text,'patch("ok"',shift=6)))
    assert help and help['signatures'][0]['parameters'] == [{'label':'name'}], help
    assert '\x01' not in json.dumps(help) and '\\u0001' not in json.dumps(help), help
    client.send('textDocument/didClose',dict(textDocument=dict(uri=top_capture_path.as_uri())))
    print('PASS top-level hidden parameters stay out of hover and signature help',flush=True)

    borrow_path = work/'borrow_iteration.rh'
    borrow_text = 'var^a={1,2}\nfor^ref^item in^a {*item+=1}\nfor^read^value in^a {print^(*value)}\n'
    client.open(borrow_path,borrow_text)
    assert client.diagnostics(borrow_path,1) == []
    for needle, expected in (('item in^','ref^ i32^'), ('value in^','ref^read^ i32^')):
        hover = client.request('textDocument/hover',dict(textDocument=dict(uri=borrow_path.as_uri()),
            position=at(borrow_text,needle)))
        assert expected.replace(' ','') in hover['contents']['value'].replace(' ',''), hover
    client.send('textDocument/didClose',dict(textDocument=dict(uri=borrow_path.as_uri())))
    print('PASS reference iteration hover and diagnostics',flush=True)

    outline_path = work/'outline 日本語.rh'
    outline_text = '''def^Left {
 self^{x:i32^=0}
 let^LIMIT=3
 update=p^self^ {
  let^text="fake { braces }"
  #[ { nested comment } ]#
  let^inner=f^n:i32^ {let^step=1 return^n+step}
  self^.x+=inner(1)
 }
}
def^Right {self^{x:i32^=0} update=p^self^ {self^.x+=1}}
def^Alias=Left
var^left=Left{}
var^right=Right{}
left.update() right.update()
let^日本語=1
'''
    client.open(outline_path,outline_text)
    assert client.diagnostics(outline_path,1) == []
    tree = client.request('textDocument/documentSymbol',dict(textDocument=dict(uri=outline_path.as_uri())))
    roots = {item['name']:item for item in tree}
    assert set(roots) == {'Left','Right','Alias','left','right','日本語'}, tree
    left_members = {item['name']:item for item in roots['Left']['children']}
    assert set(left_members) == {'x','LIMIT','update'}, tree
    right_members = {item['name']:item for item in roots['Right']['children']}
    assert set(right_members) == {'x','update'}, tree
    locals = {item['name']:item for item in left_members['update']['children']}
    assert set(locals) == {'text','inner'}, tree
    assert [item['name'] for item in locals['inner']['children']] == ['step'], tree
    assert roots['日本語']['selectionRange']['start'] == at(outline_text,'日本語=1'), tree
    def position_key(position):
        return position['line'], position['character']
    def check_outline_ranges(items, parent=None):
        for item in items:
            start, end = map(position_key,(item['range']['start'],item['range']['end']))
            selection = item['selectionRange']
            assert start <= position_key(selection['start']) <= position_key(selection['end']) <= end, item
            if parent:
                assert position_key(parent['range']['start']) <= start <= end <= position_key(parent['range']['end']), item
            check_outline_ranges(item['children'],item)
    check_outline_ranges(tree)
    workspace = client.request('workspace/symbol',dict(query='update'))
    assert any(item['name']=='update' and 'location' in item for item in workspace), workspace
    client.send('textDocument/didClose',dict(textDocument=dict(uri=outline_path.as_uri())))
    print('PASS hierarchical outlines, declaration ranges, duplicate member names and workspace symbols',flush=True)

    client.send('unsupported/request',{},9999)
    assert client.wait(lambda x: x.get('id') == 9999)['error']['code'] == -32601

    # Pure calls retain the capability of the actual source, and display explicit origins.
    pure_reads = work/'pure_reads.rh'
    pure_text = ('def^Cell {self^{n:i32^=1}}\n'
                 'let^identity=f^x:ref^Cell ->ref^Cell {x}\n'
                 'let^explicit=f^x:ref^Cell ->ref^read^(x)Cell {x}\n'
                 'let^tail=f^x:i32^[..] ->read^(x)i32^[..] {x}\n'
                 'var^a=Cell{}\n'
                 'let^r:ref^read^Cell=ref^a\n'
                 'let^readonly_result=identity(r)\n'
                 'let^writable_result=identity(ref^a)\n')
    client.open(pure_reads, pure_text)
    assert client.diagnostics(pure_reads, 1) == []
    for needle, expected in (('readonly_result=', 'ref^read^ Cell'),
                             ('writable_result=', 'ref^ Cell'),
                             ('explicit=f^', 'read^(x)'), ('tail=f^', 'read^(x)')):
        hover = client.request('textDocument/hover', dict(textDocument=dict(uri=pure_reads.as_uri()),
                                                        position=at(pure_text, needle)))
        assert hover and expected in hover['contents']['value'], (needle, hover)
    client.send('textDocument/didClose',dict(textDocument=dict(uri=pure_reads.as_uri())))
    print('PASS pure reference capability inference and explicit origin hovers',flush=True)

    # A name right after a hat is the declaration even if it recurs later.
    hats = work/'hats.rh'
    hat_text = 'let^count = f^count:i64^ -> i64^ { count }\nlet^zero = f^ -> i64^ { 0 }\n'
    client.open(hats, hat_text)
    assert client.diagnostics(hats, 1) == []
    hover = client.request('textDocument/hover', dict(textDocument=dict(uri=hats.as_uri()), position=at(hat_text, 'count = f^count')))
    assert hover['contents']['value'] == 'count : f^count:i64^ -> i64^', hover
    hover = client.request('textDocument/hover', dict(textDocument=dict(uri=hats.as_uri()), position=at(hat_text, 'zero = f^ ->')))
    assert hover['contents']['value'] == 'zero : f^ -> i64^', hover
    client.send('textDocument/didClose',dict(textDocument=dict(uri=hats.as_uri())))
    client.send('textDocument/didClose',dict(textDocument=dict(uri=path.as_uri())))
    client.send('textDocument/didClose',dict(textDocument=dict(uri=module.as_uri())))
    client.close()
    exit_only=subprocess.run([str(server),'lsp'],input=b'Content-Length: 33\r\n\r\n{"jsonrpc":"2.0","method":"exit"}',capture_output=True,timeout=10)
    assert exit_only.returncode == 1
    print('PASS lifecycle, unsupported requests, shutdown and exit',flush=True)

if __name__ == '__main__':
    main()
