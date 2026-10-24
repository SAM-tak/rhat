"""Measure newly ported workloads, keeping historical bintrees data intact."""
import argparse
from datetime import datetime, timezone
import json
import os
import pathlib
import platform
import shutil

from artifacts import Executables, overwritten_rhat_rows
from run_bench import HERE, ROOT, UPSTREAM, command, measure, sha, sized

WORKLOADS = {
    "sexp": dict(sizes=(20_000,100_000,300_000), smoke=8,
                 cpp=("owning-pointer / owned symbols", "index arena / borrowed symbols"),
                 rust=("sexp_box","sexp_arena"),
                 rhat_label="R^ nominal union / copied-symbol arenas (LLVM/Clang)",
                 representation="R^ generates the original text, parses it into 24-byte nominal union nodes "
                 "linked by u32 first-child/previous-sibling indices, and copies symbol bytes into a separate "
                 "owned stable region. Symbols retain u32 offsets/lengths, not borrowed input views. Index "
                 "zero is a sentinel excluded from the count. EOF/unclosed-list errors explicitly propagate "
                 "through a nominal ParseResult union; no nonlocal return or variable-size ADT is claimed. "
                 "Original sizes, depth four, RNG, parse, reverse sibling order, four pass-dependent walks "
                 "and all five checksum lines are preserved."),
    "calc": dict(sizes=(100_000,400_000,1_600_000), smoke=128,
                 cpp=("owning AST / exceptions", "index arena / error codes"), rust=("calc_box","calc_arena"),
                 rhat_label="R^ nominal AST / explicit result unions (LLVM/Clang)",
                 representation="R^ generates and damages the original arithmetic inputs, parses every input "
                 "with the original precedence into 24-byte nominal AST nodes and evaluates wrapping u64. "
                 "ParseResult/EvalResult unions explicitly propagate EOF, missing-parenthesis, token and "
                 "division-by-zero failures. All partially built nodes count toward the total. Text and node "
                 "regions clear/truncate and reuse storage like C++/Rust arenas; no unbounded append or "
                 "per-node heap allocation is used. Links are u32 indices; Goose's u16 relative links, "
                 "variable-size nodes and nonlocal return are not claimed. Original sizes, depth choices, "
                 "RNG, corruption, parse/evaluation order and all eight checksum lines are preserved."),
    "records": dict(sizes=(500_000, 4_000_000, 16_000_000), smoke=256,
                    cpp=("virtual objects", "variant / owned string", "variant / inline text"), rust=("records",),
                    rhat_label="R^ native nominal union / inline text (LLVM/Clang)",
                    representation="R^ ports records_fixed.goose with a native nominal Event union, 24-byte "
                    "records and a 15-byte inline text capacity. Tag tests and checked payload extraction are "
                    "generated from types; no manually synchronized kind/payload struct is used. Original "
                    "sizes, RNG consumption, all four alternatives, actual per-Say word generation and four "
                    "scans are preserved. This is the original fixed-mode variant; variable-size ADTs and "
                    "separately owned string payloads are not claimed."),
    "interp": dict(sizes=(16,20,24), smoke=6, parameter="depth",
                   cpp=("virtual owning tree", "variant arena", "switch arena"), rust=("interp_box","interp_arena"),
                   rhat_label="R^ native nominal union / u32 arena (LLVM/Clang)",
                   representation="R^ uses a native nominal Expr union with 12-byte fixed nodes and u32 "
                   "child indices, matching the C++ switch / Rust arena representation. The generated tag "
                   "tests and checked payload extraction dispatch Num/Add/Mul/Neg. Original tree shape, "
                   "postorder construction, seeds, depths and eight pass-dependent evaluations modulo "
                   "1000003 are preserved. Goose's variable-size nodes and relative references are not claimed."),
    "respond": dict(sizes=(100_000, 400_000, 1_600_000), smoke=64,
                    cpp=("response DTO", "streaming JSON"), rust=("respond_dto", "respond_stream"),
                    rhat_label="R^ streaming JSON / reused byte region (LLVM/Clang)",
                    representation="R^ ports the original respond_stream.goose variant, generating the same "
                    "requests and JSON bytes, including real quote/backslash escaping. The output byte region "
                    "is cleared and reused per request, as in the C++/Rust streaming controls. Decimal digits "
                    "use a 24-byte inline scratch array. FNV-1a, original request counts, RNG consumption, "
                    "item counts, quantities, prices and all three checksums are preserved. The DTO-building "
                    "Goose variant and general nested owned strings/arrays are not implemented by this port."),
    "words": dict(sizes=(500_000, 4_000_000, 16_000_000), smoke=1024,
                  cpp=("owned-key unordered_map", "borrowed-key unordered_map", "open table"),
                  rust=("words_hashmap", "words_open"),
                  rhat_label="R^ packed words / borrowed-key open table (LLVM/Clang)",
                  representation="R^ stores vocabulary entries as 21-byte fixed-capacity words and copies "
                  "split words into one owned packed byte region (one-byte varint lengths). The open-addressed "
                  "table holds borrowed u8 slices into that stable packed region and i32 counts (24-byte slots). "
                  "Keys are never copied into the table. FNV-1a, linear probing, original vocabulary sizes, "
                  "skewed word selection, RNG sequence, text construction, split and all three checksums "
                  "are preserved. Packed strings are a library representation; general variable-size array "
                  "elements and a built-in dictionary are not claimed."),
    "strlist": dict(sizes=(200_000, 2_000_000, 8_000_000), smoke=128,
                    cpp=("owned strings", "borrowed string views", "flat offsets"),
                    rust=("strlist_owned", "strlist_borrowed"),
                    rhat_label="R^ owned packed word list (LLVM/Clang)",
                    representation="R^ copies each split word into a single owned byte region, prefixed by "
                    "its one-byte varint length (original generated lengths are 3..20). The original text "
                    "and copied word list coexist; scans decode the packed lengths four times. A WordList "
                    "library struct holds the count and owned data region. This does not claim general "
                    "variable-size array elements or nested owned strings in the language. No word is "
                    "replaced by a borrowed view or offset pair. All original sizes, seed sequence, text "
                    "splitting, copies, passes and two checksum lines are preserved."),
    "scene": dict(sizes=(12, 14, 16), smoke=4, parameter="depth",
                  cpp=("owning-pointer tree", "index arena"), rust=("scene_box", "scene_arena"),
                  rhat_label="R^ inline u32-child arena (LLVM/Clang)",
                  representation="R^ uses 120-byte nodes with inline local/world transforms, four u32 child "
                  "indices and a u8 child count, matching the C++ expert / Rust arena layout. Preorder node "
                  "creation, xorshift64 seeds, all original depths, 16 animation/update/gather frames and "
                  "f32 matrix/translation arithmetic are preserved. Composition writes through references "
                  "to disjoint fields in the node. R^ retains bounds checks and stable OS-backed array "
                  "storage; Goose's 117-byte limited-array/relative-reference layout is not claimed."),
    "lru": dict(sizes=(2_000_000, 8_000_000, 32_000_000), smoke=2048,
                cpp=("list / unordered_map", "index arena / open table"),
                rust=("lru_hashmap", "lru_open"),
                rhat_label="R^ reusable index arena / open table (LLVM/Clang)",
                representation="R^ uses 16-byte nodes, 8-byte hash slots, two sentinel nodes and a u32 free list. "
                "Pop reuses freed node indices; backward-shift deletion preserves open-addressing probes. "
                "The pool never exceeds capacity + 2 nodes. This matches the original C++ expert / Rust open-table "
                "representation, including hash, eviction, explicit removal and recency walk. Original sizes, "
                "seed and all six checksum lines are preserved. Indices are ordinary integers, without "
                "Goose's pool-bound relative-reference guarantees or generation checks. Arrays use statically assigned stable "
                "1 GiB data stacks and retain committed pages when their logical length decreases."),
    "push": dict(sizes=(1_000_000, 16_000_000, 64_000_000), smoke=1000,
                 cpp=("vector indices", "reserved vector indices", "deque pointers"), rust=("push",),
                 rhat_label="R^ stable region / real pointer marks (LLVM/Clang)",
                 representation="R^ stores 8-byte Items and retains real Item references returned by push, "
                 "marking every 64th record across all subsequent growth. Each array reserves 1 GiB of virtual "
                 "address space and commits only used pages; elements never move. The original sizes, RNG, "
                 "two final scans and checksums are preserved. C++ deque also retains pointers; C++ vector "
                 "and Rust Vec controls retain indices. No mark is replaced by an index in the R^ port."),
    "sum": dict(sizes=(1_000_000, 16_000_000, 128_000_000), smoke=1000,
                cpp=("vector", "reserved vector"), rust=("sum",)),
    "blur": dict(sizes=(1024, 2048, 8192), smoke=16,
                 cpp=("flat", "row pointers"), rust=("blur_index", "blur_windows")),
    "particles": dict(sizes=(100_000, 1_000_000, 4_000_000), smoke=32,
                      cpp=("scalar AoS", "SoA", "vector AoS"),
                      rust=("particles_scalar", "particles")),
    "tree": dict(sizes=(16, 20, 24), smoke=6, parameter="depth",
                 cpp=("unique_ptr", "new/delete", "index arena"), rust=("tree_box", "tree_arena"),
                 rhat_label="R^ u32-index node pool (LLVM/Clang)",
                 representation="R^ uses 12-byte nodes (i32 value, two u32 child indices), with index zero as null. "
                 "It reserves a stable OS region and grows it by push; C++/Rust index arenas reserve exact capacity. "
                 "Relative references and per-node allocation are not used. Eight full traversals and the original seeds are preserved."),
    "graph": dict(sizes=(100_000, 500_000, 2_000_000), smoke=64, parameter="vertices",
                  cpp=("nested vectors", "CSR", "linked index arena"),
                  rust=("graph_nested", "graph_csr", "graph_arena"),
                  rhat_label="R^ CSR / checked i32 arrays (LLVM/Clang)",
                  representation="R^ uses the original two-pass CSR count/prefix/fill representation, matching C++ VARIANT 1 and Rust CSR. "
                  "Source and destination arrays, start offsets, fill cursors and contiguous edges retain all construction cost. "
                  "The previous linked index implementation is preserved in rhat/variants/graph_linked.rh. "
                  "Its queue overwrites an initialized prefix and pushes only when its high-water length grows; "
                  "a separate tail resets between the four BFS sources. There is no unbounded append across sources. "
                  "This adds an enqueue branch compared with Vec::clear. Edges, seed sequence, distances and checksums are unchanged."),
}


def next_random(r):
    mask = (1 << 64) - 1
    r ^= (r << 13) & mask
    r ^= r >> 7
    return (r ^ (r << 17)) & mask


def oracle(name, n):
    """Independent, untimed smoke oracle. Larger runs cross-check C++ and Rust."""
    r = 12345
    if name == "sexp":
        # Generate an independent semantic tree, then render it. Expected walks
        # use this tree directly, independently checking the R^ parser/pools.
        def make(depth, seed):
            seed = next_random(seed)
            if depth == 0 or seed % 4 == 0:
                if (seed >> 8) % 2 == 0:
                    sr, word = seed, []
                    for _ in range(3+seed % 12):
                        sr = next_random(sr)
                        word.append(97+sr % 26)
                    return ('sym', bytes(word)), seed
                return ('num', (seed >> 16) % 100000), seed
            children = []
            for _ in range(2+(seed >> 24) % 4):
                child,seed = make(depth-1,seed)
                children.append(child)
            return ('list',children), seed
        def render(node):
            if node[0] == 'sym': return node[1]+b' '
            if node[0] == 'num': return str(node[1]).encode()+b' '
            return b'('+b''.join(render(child) for child in node[1])+b') '
        def count(node):
            return 1+(sum(count(child) for child in node[1]) if node[0] == 'list' else 0)
        def walk(node,p):
            if node[0] == 'sym': return len(node[1])+node[1][0]+p
            if node[0] == 'num': return node[1] ^ p
            return 1+sum(walk(child,p) for child in reversed(node[1]))
        roots = []
        for _ in range(n):
            root,r = make(4,r)
            roots.append(root)
        length = sum(len(render(root)) for root in roots)
        nodes = sum(count(root) for root in roots)
        total = sum(walk(root,p) for p in range(4) for root in roots)
        return f"{length}\n{nodes}\n{n}\n0\n{total}"
    elif name == "calc":
        # Immutable string generation + an independent Pratt parser, rather
        # than the benchmark's expr/term/factor recursive-descent functions.
        mask = (1 << 64)-1
        class ParseFailure(Exception):
            def __init__(self,code): self.code=code
        def generate(depth,seed):
            seed=next_random(seed)
            kind=seed % 8
            if depth == 0 or kind == 0: return str(1+(seed >> 8) % 99),seed
            if kind == 1:
                child,seed=generate(depth-1,seed)
                return '-'+child,seed
            if kind == 2:
                child,seed=generate(depth-1,seed)
                return '('+child+')',seed
            left,seed=generate(depth-1,seed)
            op='+-*/'[(seed >> 16) % 4]
            right,seed=generate(depth-1,next_random(seed))
            return left+' '+op+' '+right,seed
        class Pratt:
            def __init__(self,text): self.text,self.pos,self.nodes=text,0,0
            def peek(self):
                while self.pos < len(self.text) and self.text[self.pos] == ' ': self.pos+=1
                return self.text[self.pos] if self.pos < len(self.text) else ''
            def node(self,kind,*args):
                self.nodes+=1
                return (kind,*args)
            def primary(self):
                ch=self.peek()
                if not ch: raise ParseFailure(1)
                if ch.isdigit():
                    start=self.pos
                    while self.pos < len(self.text) and self.text[self.pos].isdigit(): self.pos+=1
                    return self.node('num',int(self.text[start:self.pos]))
                self.pos+=1
                if ch == '-': return self.node('neg',self.primary())
                if ch != '(': raise ParseFailure(3)
                result=self.expr(0)
                if self.peek() != ')': raise ParseFailure(2)
                self.pos+=1
                return self.node('paren',result)
            def expr(self,minimum):
                left=self.primary()
                while True:
                    op=self.peek()
                    precedence={'+':1,'-':1,'*':2,'/':2}.get(op,-1)
                    if precedence < minimum: return left
                    self.pos+=1
                    right=self.expr(precedence+1)
                    left=self.node(op,left,right)
        def evaluate(node):
            kind=node[0]
            if kind == 'num': return node[1] & mask
            a=evaluate(node[1])
            if kind == 'neg': return -a & mask
            if kind == 'paren': return a
            b=evaluate(node[2])
            if kind == '+': return (a+b) & mask
            if kind == '-': return (a-b) & mask
            if kind == '*': return (a*b) & mask
            if b == 0: raise ParseFailure(4)
            return a//b
        chars=nodes=ok=checksum=0
        errors=[0]*5
        for _ in range(n):
            r=next_random(r)
            text,final=generate(3+r % 4,r >> 8)
            kind=final % 16
            if kind == 0:
                index=text.rfind(')')
                index=0 if index < 0 else index
                text=text[:index]+(' ' if text[index] == ')' else '#')+text[index+1:]
            elif kind == 1:
                index=(final >> 8) % len(text)
                text=text[:index]+'#'+text[index+1:]
            elif kind == 2: text=text[:len(text)//2]
            elif kind == 3: text+=' / 0'
            chars+=len(text)
            parser=Pratt(text)
            try:
                tree=parser.expr(0)
                if parser.peek(): raise ParseFailure(3)
                value=evaluate(tree)
            except ParseFailure as failure:
                errors[failure.code]+=1
            else:
                ok+=1
                checksum=(checksum*31+value) & mask
            nodes+=parser.nodes
        return '\n'.join(map(str,(chars,nodes,ok,*errors[1:],checksum)))
    elif name == "records":
        scores = []
        for _ in range(n):
            r = next_random(r)
            kind, ident = r % 8, (r >> 3) % 100_000
            if kind < 4:
                score = ident+(r >> 20) % 201-100+(r >> 40) % 201-100
            elif kind < 6:
                word_rng = next_random(r)
                length = 4+word_rng % 12
                word = []
                for _ in range(length):
                    word_rng = next_random(word_rng)
                    word.append(97+word_rng % 26)
                score = ident+len(word)+word[0]
            elif kind < 7:
                score = ident+(r >> 20) % 256
            else:
                score = 1
            scores.append(score)
        return f"{n}\n{4*sum(scores)}"
    elif name == "interp":
        count = 0
        def make(depth, seed):
            nonlocal count
            count += 1
            seed = next_random(seed)
            if depth == 0:
                return ('num', seed % 1000)
            kind = seed % 8
            if kind < 1:
                return ('neg', make(depth-1, seed))
            return ('add' if kind < 5 else 'mul', make(depth-1, seed), make(depth-1, next_random(seed)))
        root = make(n, r)
        def evaluate(node, p):
            if node[0] == 'num': return (node[1]+p) % 1_000_003
            if node[0] == 'neg': return (1_000_003-evaluate(node[1],p)) % 1_000_003
            a,b = evaluate(node[1],p),evaluate(node[2],p)
            return (a+b if node[0] == 'add' else a*b) % 1_000_003
        return f"{count}\n{sum(evaluate(root,p) for p in range(8)) % 1_000_003}"
    elif name == "respond":
        mask = (1 << 64)-1
        combined = total_bytes = 0
        seed = r
        for index in range(n):
            seed = next_random(seed)
            r0 = next_random(seed)
            nr = next_random(r0 >> 8)
            length, odd = 3+nr % 8, (nr >> 8) % 8 == 0
            name = []
            for i in range(length):
                nr = next_random(nr)
                name.append(('"' if nr % 2 == 0 else chr(92)) if odd and i == 1 else chr(97+nr % 26))
            items, ir = [], r0 >> 16
            for _ in range(1+r0 % 8):
                ir = next_random(ir)
                sr = next_random(ir)
                sku = chr(65+sr % 26)+chr(65+(sr >> 8) % 26)
                for _ in range(4):
                    sr = next_random(sr)
                    sku += chr(48+sr % 10)
                items.append(dict(sku=sku, qty=1+(ir >> 8) % 5, price=100+(ir >> 16) % 99900))
            response = dict(id=(r0 >> 4) % 1_000_000, user="".join(name), items=items,
                            total=sum(item['qty']*item['price'] for item in items))
            encoded = json.dumps(response, separators=(',', ':')).encode('ascii')
            total_bytes += len(encoded)
            h = 14695981039346656037
            for byte in encoded:
                h = ((h ^ byte)*1099511628211) & mask
            combined ^= (h*31+index) & mask
        return f"{n}\n{total_bytes}\n{combined}"
    elif name == "words":
        from collections import Counter
        vocab = []
        for _ in range(n//10+16):
            r = next_random(r)
            length = 3+r % 12
            word = []
            for _ in range(length):
                r = next_random(r)
                word.append(97+r % 26)
            vocab.append(bytes(word))
        counts = Counter()
        for _ in range(n):
            r = next_random(r)
            a = r % len(vocab)
            counts[vocab[a*a//len(vocab)]] += 1
        return f"{n}\n{len(counts)}\n{sum(count*(len(w)+w[0]) for w,count in counts.items())}"
    elif name == "strlist":
        words = []
        for _ in range(n):
            r = next_random(r)
            length = 3+r % 18
            word = []
            for _ in range(length):
                r = next_random(r)
                word.append(97+r % 26)
            words.append(bytes(word))
        text = b" ".join(words)+b" "
        parts = [part for part in text.split(b" ") if part]
        return f"{len(parts)}\n{4*sum(len(w)+w[0]+w[-1] for w in parts)}"
    elif name == "scene":
        # Independent integer affine algebra: no packed nodes, child indices,
        # f32 arithmetic or recursive update of a mutable pool.
        nodes = []
        def build(depth, seed, parent):
            seed = next_random(seed)
            axis, quarter = seed % 3, (seed >> 8) % 4
            c, sn = (1, 0, -1, 0)[quarter], (0, 1, 0, -1)[quarter]
            if axis == 0:
                matrix = ((1, 0, 0), (0, c, -sn), (0, sn, c))
            elif axis == 1:
                matrix = ((c, 0, sn), (0, 1, 0), (-sn, 0, c))
            else:
                matrix = ((c, -sn, 0), (sn, c, 0), (0, 0, 1))
            me = len(nodes)
            nodes.append((parent, matrix, [(seed >> k) % 9 for k in (16, 24, 32)]))
            if depth:
                for i in range(1 + ((seed >> 40) % 4)):
                    build(depth-1, next_random((seed+i) & ((1 << 64)-1)), me)
        build(n, r, -1)
        ident = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
        total = 0
        for frame in range(16):
            worlds = []
            for i, (parent, local_m, local_t) in enumerate(nodes):
                if i % 7 == frame % 7:
                    local_t[0], local_t[2] = frame % 5, 8-frame % 5
                pm, pt = (ident, (0, 0, 0)) if parent < 0 else worlds[parent]
                wm = tuple(tuple(sum(a*b for a,b in zip(row,col))
                                 for col in zip(*local_m)) for row in pm)
                wt = tuple(sum(a*b for a,b in zip(row,local_t))+t for row,t in zip(pm,pt))
                worlds.append((wm, wt))
                total += sum(wt)
        return f"{len(nodes)}\n{total}"
    elif name == "lru":
        from collections import OrderedDict
        cache = OrderedDict()
        cap = n//8
        keys = cap*2
        hits = misses = removes = 0
        for _ in range(n):
            r = next_random(r)
            a = r % keys
            key = a*a//keys
            if (r >> 40) % 16 == 0:
                if key in cache:
                    del cache[key]
                    removes += 1
            elif key in cache:
                hits += 1
                cache[key] += 1
                cache.move_to_end(key, last=False)
            else:
                misses += 1
                if len(cache) == cap:
                    cache.popitem()
                cache[key] = key % 1000
                cache.move_to_end(key, last=False)
        walk = 0
        for key in cache:
            walk = (walk*31+key) & ((1 << 64)-1)
        return f"{hits}\n{misses}\n{removes}\n{len(cache)}\n{walk}\n{sum(cache.values())}"
    elif name == "push":
        total = 0
        for _ in range(n):
            r = next_random(r)
            total += r % 1_000_000
        marks = (n+63)//64
        return f"{n}\n{marks}\n{total}\n{sum(i % 97 for i in range(0,n,64))}"
    elif name == "sum":
        total = 0
        for _ in range(n):
            r = next_random(r)
            total += sum((r % 1000) ^ p for p in range(8))
    elif name == "blur":
        a, b = [], [0] * (n*n)
        for _ in range(n*n):
            r = next_random(r)
            a.append(r % 256)
        for _ in range(16):
            for y in range(1, n-1):
                for x in range(1, n-1):
                    i = y*n+x
                    b[i] = (a[i-n-1] + 2*a[i-n] + a[i-n+1]
                            + 2*a[i-1] + 4*a[i] + 2*a[i+1]
                            + a[i+n-1] + 2*a[i+n] + a[i+n+1]) >> 4
            a, b = b, a
        total = sum(a)
    elif name == "particles":
        total = 0
        for _ in range(n):
            r = next_random(r)
            x, y, z = (float((r >> shift) % 1024) for shift in (0, 12, 24))
            vx, vy, vz = (((r >> shift) % 256 - 128) / 16.0 for shift in (36, 44, 52))
            # All these dyadic values are exactly representable in binary32.
            for _ in range(200):
                vy -= 0.0625
                x, y, z = x+vx, y+vy, z+vz
                if y < 0:
                    y, vy = -y, -(vy*0.5)
            total += int(x*16) + int(y*16) + int(z*16)
    elif name == "tree":
        def visit(depth, seed):
            own = sum((seed % 1000) ^ p for p in range(8))
            if depth == 0:
                return own
            return own + visit(depth-1, next_random(seed)) + visit(depth-1, next_random((seed+1) & ((1 << 64)-1)))
        return f"{(1 << (n+1))-1}\n{visit(n, r)}"
    elif name == "graph":
        # Lists in generation order and Python integers, independent of the
        # R^ linked pool and its reusable queue representation.
        adj = [[] for _ in range(n)]
        for _ in range(n*8):
            r = next_random(r)
            adj[r % n].append((r >> 24) % n)
        total, seed = 0, 999
        for _ in range(4):
            seed = next_random(seed)
            src = seed % n
            distance = [-1] * n
            distance[src] = 0
            queue = [src]
            for u in queue:
                total += distance[u]
                for v in adj[u]:
                    if distance[v] == -1:
                        distance[v] = distance[u]+1
                        queue.append(v)
        return f"{n*9}\n{total}"
    else:
        raise ValueError(f"no independent oracle for {name}")
    return f"{n}\n{total}"


def report(data, out):
    out.mkdir(parents=True, exist_ok=True)
    (out / "workloads_results.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    lines = ["# Additional R^ workloads", "", f"Measured: {data['started_utc']} (UTC).", "",
             "R^ replaces Goose. All comparison implementations were built and measured in this session.", "",
             f"Whole-process wall time, two warm-ups, best of {data['reps']} timed samples; peak resident working set.",
             "R^ retains bounds/range checks and OS-backed, stable-address arrays. No affinity is set.", "",
             "Comparison / R^ time: above 1 means R^ is faster. No suite-wide average is claimed.", "",
             "Smoke sizes are correctness checks and are not performance evidence.", "",
             *(["Storage: statically assigned data stacks, scope watermark rewinds, and Windows fault-driven commits (up to 1 MiB).",
                "The LLVM push fast path makes no runtime call; array descriptors are 24 bytes.", ""]
               if data.get("memory_model") == "static_data_stacks_fault_commit" else []),
             *( ["Executable paths are unique per language; every run verifies its build-time SHA-256 before and after execution.", ""] if data.get("executable_hashes") else []),
             *(["Index bounds and address calculations are emitted inline, including full-width 128-bit checks.",
                "Push checks remaining bytes without division; proven append-only paths update the top directly.", ""]
               if data.get("codegen_model") in ("inline_array_and_arithmetic_checks", "direct_scalar_checked_range_loops", "small_values_inline_guards_literals") else []),
             *(["Internal functions pass scalars/references directly; canonical i64 loops use checked range guards and a checked fallback.", ""]
               if data.get("codegen_model") in ("direct_scalar_checked_range_loops", "small_values_inline_guards_literals") else []),
             *(["Non-owning values up to 16 bytes pass directly; union/float guards and checked literal appends are inline.", ""]
               if data.get("codegen_model") == "small_values_inline_guards_literals" else []),
             "## Environment", "", "| Item | Value |", "|---|---|"]
    for key, value in data["environment"].items():
        text = str(value).replace("|", "/").replace("\n", "; ")
        lines.append(f"| {key} | {text} |")
    invalid_rows = overwritten_rhat_rows(data)
    if invalid_rows:
        affected = ', '.join(sorted({name for name,_ in invalid_rows}))
        lines += ["", "## Invalid historical R^ rows", "",
                  f"R^ rows for **{affected}** are INVALID: Rust overwrote their executable before execution.",
                  "Their recorded timings measure Rust and cannot be used as R^ performance evidence.",
                  "Affected R^ timings, memory and ratios are suppressed below. Raw data is retained for audit.",
                  "Corrected measurements use language-specific artifact names and per-run executable SHA-256 verification."]
    if data.get("metadata_corrections"):
        lines += ["", *data["metadata_corrections"]]
    for name in data["workloads"]:
        lines += ["", "## " + name, ""]
        if data.get("representations", {}).get(name):
            lines += [data["representations"][name], ""]
        parameter = data.get("parameters", {}).get(name, "N")
        lines += [f"| Size / {parameter} | Implementation | Best ms | Peak MiB | Comparison / R^ time |",
                  "|---|---|---:|---:|---:|"]
        for row in data["rows"]:
            if row["workload"] != name:
                continue
            rhat = next(r for r in data["rows"] if r["workload"] == name
                        and r["size"] == row["size"] and r["language"] == "rhat")
            invalid = (name,row['size']) in invalid_rows
            if invalid and row['language'] == 'rhat':
                lines.append(f"| {row['size']} / {row['n']} | {row['label']} **INVALID: overwritten by Rust** | — | — | — |")
            else:
                ratio = "—" if invalid else f"{row['best_ms']/rhat['best_ms']:.3f}x"
                lines.append(f"| {row['size']} / {row['n']} | {row['label']} | {row['best_ms']:.2f} | "
                             f"{row['peak_bytes']/1048576:.2f} | {ratio} |")
    lines += ["", "Every warm-up/sample checks its checksum. Each size first cross-checks independently built",
              "C++ and Rust controls; smoke sizes additionally use a Python oracle. The oracle is outside timing.",
              "Raw samples, checksums, build commands, source/LLVM/compiler/runtime SHA-256 and tool versions",
              "are saved in workloads_results.json. Historical bintrees measurements are in "
              f"[results.md]({pathlib.Path(os.path.relpath(HERE / 'results.md', out)).as_posix()}).", ""]
    (out / "workloads_results.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workloads", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    parser.add_argument("--sizes", nargs="+", choices=("smoke", "small", "medium", "large"), default=["small", "medium", "large"])
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--compiler", type=pathlib.Path, default=ROOT / "build/Release/rhat.exe")
    parser.add_argument("--work", type=pathlib.Path, default=ROOT / "build/bench-workloads")
    parser.add_argument("--out", type=pathlib.Path, default=HERE)
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    if args.report_only:
        report(json.loads((args.out / "workloads_results.json").read_text(encoding="utf-8")), args.out)
        return
    if os.name != "nt" or args.reps < 1 or args.timeout < 1:
        parser.error("Windows and positive --reps/--timeout required")
    tools = {name: shutil.which(exe) for name, exe in
             (("clang", "clang"), ("msvc", "cl"), ("clangcl", "clang-cl"), ("rustc", "rustc"))}
    if not all(tools.values()):
        parser.error("load scripts/devshell.ps1; Clang, MSVC, Rust required")
    compiler, work = args.compiler.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
        cpu = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    env = dict(cpu=cpu, os=platform.platform(), commit=command(["git", "rev-parse", "HEAD"], cwd=ROOT),
               dirty=command(["git", "status", "--porcelain"], cwd=ROOT),
               compiler_sha256=sha(compiler), runtime_sha256=sha(ROOT / "runtime/rhat_runtime.c"))
    for key, exe in tools.items():
        banner = command([exe, "/Bv", "/?"] if key == "msvc" else [exe, "--version"])
        if key == "msvc":
            env[key] = next((line.strip() for line in banner.splitlines()
                             if "Compiler Version" in line or "コンパイラ バージョン" in line), None)
            if not env[key]:
                raise RuntimeError("could not identify MSVC version")
        else:
            env[key] = banner.splitlines()[0]
    binaries = Executables()
    data = dict(codegen_model="small_values_inline_guards_literals", memory_model="static_data_stacks_fault_commit", stack_reports={}, executable_hashes=binaries.hashes, started_utc=datetime.now(timezone.utc).isoformat(), upstream_commit=UPSTREAM,
                environment=env, workloads=args.workloads, warmups=2, reps=args.reps,
                builds=[], rows=[], controls=[], source_hashes={},
                representations={name: WORKLOADS[name].get("representation", "") for name in args.workloads},
                parameters={name: WORKLOADS[name].get("parameter", "N") for name in args.workloads},
                harness_source=pathlib.Path(__file__).read_bytes().decode("utf-8"))
    for directory in (ROOT / "src", ROOT / "runtime", HERE):
        for path in directory.rglob("*"):
            if path.is_file() and path.suffix in (".c", ".h", ".rh", ".rs", ".cpp", ".py"):
                data["source_hashes"][str(path.relative_to(ROOT))] = sha(path)

    def add_implementation(implementations, label, language, exe):
        binaries.add(exe)
        implementations.append((label, language, exe))

    def measured(exe):
        return binaries.measure(exe, lambda path: measure(path, args.timeout))

    def build(argv):
        data["builds"].append(list(map(str, argv)))
        command(argv, cwd=work)

    shutil.copyfile(HERE / "upstream/rust/bench.rs", work / "bench.rs")
    for name in args.workloads:
        spec = WORKLOADS[name]
        for size in args.sizes:
            n = spec["smoke"] if size == "smoke" else spec["sizes"][("small", "medium", "large").index(size)]
            implementations = []
            src = work / f"{name}_{size}.rh"
            sized(HERE / f"rhat/{name}.rh", src, n)
            exe = work / f"rhat_{name}_{size}.exe"
            build([compiler, "build", src, "-o", exe, "-O2", "--clang", tools["clang"],
                   "--dump-ir", src.with_suffix(".rir")])
            data["stack_reports"][f"{name}/{size}"] = command([compiler, "check", src, "--stacks"], cwd=work)
            data["source_hashes"][str(src)] = sha(src)
            data["source_hashes"][str(exe) + ".ll"] = sha(pathlib.Path(str(exe) + ".ll"))
            add_implementation(implementations, spec.get("rhat_label", "R^ LLVM/Clang"), "rhat", exe)
            for tc in ("msvc", "clangcl"):
                for variant, label in enumerate(spec["cpp"]):
                    exe = work / f"cpp_{name}_{size}_{tc}_{variant}.exe"
                    build([tools[tc], "/nologo", "/O2", "/EHsc", "/std:c++20", f"/DBENCH_N={n}",
                           f"/DVARIANT={variant}", HERE / f"upstream/cpp/{name}.cpp",
                           "/Fe" + str(exe), "/Fo" + str(exe.with_suffix(".obj"))])
                    add_implementation(implementations, f"C++ {label} ({tc})", "cpp", exe)
            for rust in spec["rust"]:
                src = work / f"rust_{rust}_{size}.rs"
                sized(HERE / f"upstream/rust/{rust}.rs", src, n)
                exe = src.with_suffix(".exe")
                build([tools["rustc"], "-O", "-C", "codegen-units=1", src, "-o", exe])
                add_implementation(implementations, f"Rust {rust}", "rust", exe)
            # Establish the result from two independent controls before timing.
            cpp = next(exe for _, lang, exe in implementations if lang == "cpp")
            rust = next(exe for _, lang, exe in implementations if lang == "rust")
            cpp_control, rust_control = measured(cpp), measured(rust)
            expected = cpp_control["output"]
            if rust_control["output"] != expected:
                raise RuntimeError(f"{name}/{size}: C++/Rust control mismatch")
            if size == "smoke" and expected != oracle(name, n):
                raise RuntimeError(f"{name}: Python oracle mismatch")
            data["controls"].append(dict(workload=name, size=size, n=n, output=expected,
                                         cpp=str(cpp), rust=str(rust), cpp_control=cpp_control,
                                         rust_control=rust_control, python_oracle=size == "smoke"))
            samples = {label: [] for label, _, _ in implementations}
            warmups = {label: [] for label, _, _ in implementations}
            for iteration in range(2 + args.reps):
                ordered = implementations if iteration % 2 == 0 else list(reversed(implementations))
                for label, _, exe in ordered:
                    result = measured(exe)
                    if result["output"] != expected:
                        raise RuntimeError(f"{name}/{size} {label}: checksum {result['output']} != {expected}")
                    (warmups if iteration < 2 else samples)[label].append(result)
                    print(f"{name}/{size} {label} round {iteration+1}: {result['ms']:.2f} ms verified", flush=True)
            for label, language, exe in implementations:
                data["rows"].append(dict(executable=str(exe), executable_sha256=binaries.hashes[binaries.key(exe)], workload=name, size=size, n=n, label=label, language=language,
                                         best_ms=min(s["ms"] for s in samples[label]),
                                         peak_bytes=max(s["peak_bytes"] for s in samples[label]),
                                         samples=samples[label], warmups=warmups[label]))
            report(data, args.out)


if __name__ == "__main__":
    main()
