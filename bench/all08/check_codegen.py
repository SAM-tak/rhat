"""Check the effective storage alias proof after LLVM optimization."""
from pathlib import Path
import re
import subprocess


def verify(data, root):
    row = next(r for r in data['rows'] if r['workload']=='lru' and r['size']=='small' and r['language']=='rhat')
    source = Path(row['executable']+'.ll')
    raw = source.read_text()
    symbol = re.search(r'; remove\ndefine internal void @(\w+)\(', raw).group(1)
    output = root/'build/all08-effective-codegen.ll'
    # Use the same Clang recorded by the benchmark harness.
    clang = data['environment']['tools']['clang'] if 'tools' in data['environment'] else None
    if clang is None:
        # The build command records the resolved --clang argument.
        build = next(c for c in data['builds'] if str(source)[:-3] in c)
        clang = build[build.index('--clang')+1]
    subprocess.run([clang,'-S','-emit-llvm','-O2','-Wno-override-module',str(source),'-o',str(output)],check=True)
    optimized = output.read_text()
    body = re.search(r'^define[^\n]*@'+re.escape(symbol)+r'\([^\n]*\)[^\n]*\{\n(.*?)^\}',optimized,re.M|re.S).group(1)
    # map_remove writes entries in its probe loop. The descriptor is unchanged:
    # length and data must each be loaded just once, before the loop.
    assert len(re.findall(r'\bload i64\b',body))==1,body
    assert len(re.findall(r'\bload ptr\b',body))==1,body
    first_store=body.index('store i32')
    assert body.index('load i64')<first_store and body.index('load ptr')<first_store
    print('CODEGEN PASS: lru remove reads descriptor length/data once; no loop reloads.')
    return str(output.relative_to(root))
