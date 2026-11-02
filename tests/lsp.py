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

    def tokens(text, document=path):
        data=client.request('textDocument/semanticTokens/full',dict(textDocument=dict(uri=document.as_uri())))['data']
        row=column=0
        result={}
        for n in range(0,len(data),5):
            delta,offset,length,kind,modifiers=data[n:n+5]
            row+=delta
            column=offset if delta else column+offset
            result[(row,column)]=(length,kind)
        return result

    colored=tokens(recovery)
    for spelling in ('F2,b:F2','F2 ->f32^'):
        pos=at(recovery,spelling)
        assert colored[(pos['line'],pos['character'])] == (2,1), colored
    print('PASS multiple diagnostics, cascade suppression and unused signature coloring',flush=True)

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

    client.send('unsupported/request',{},9999)
    assert client.wait(lambda x: x.get('id') == 9999)['error']['code'] == -32601

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
