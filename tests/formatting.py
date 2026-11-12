"""Formatting preserves tokens and syntax, and agrees between CLI and LSP."""
from pathlib import Path
from harness import Suite
from lsp import Client


def main():
    suite = Suite('formatting')

    def fixture(name, settings=''):
        root = suite.work / name
        root.mkdir(parents=True, exist_ok=True)
        (root / '.editorconfig').write_text('root=true\n[*.rh]\n' + settings, encoding='utf-8')
        return root / 'input.rh'

    def expect(name, source, expected, settings=''):
        def body():
            path = fixture(name, settings)
            path.write_bytes(source.encode('utf-8'))
            check = suite.run([suite.compiler, 'format-check', path], source == expected)
            if source != expected:
                assert 'formatting differs' in check.stderr
                assert path.read_bytes() == source.encode('utf-8')
            suite.run([suite.compiler, 'format', path])
            actual = path.read_bytes().decode('utf-8')
            assert actual == expected, (name, actual, expected)
            suite.run([suite.compiler, 'format-check', path])
            suite.run([suite.compiler, 'format', path])
            assert path.read_bytes() == expected.encode('utf-8'), name
        suite.case(name, body)

    expect('borrow_iteration',
           'var^a={1,2}\nfor^ ref^ v in^ a{*v+=1}\nfor^ read^ v in^ a{print^(*v)}\n',
           'var^a = {1, 2}\nfor^ref^v in^a{*v += 1}\nfor^read^v in^a{print^(*v)}\n')
    expect('explicit_static_declarations',
           'def^A {\nlet^step:f32^=0.2\nvar^count:i64^=0\n}\n',
           'def^A {\n    let^step:f32^= 0.2\n    var^count:i64^= 0\n}\n')
    expect('numeric_type_suffixes',
           'let^hex=0x8888_4444_u64\nlet^f=1.25f32\nlet^n=-128i8\n',
           'let^hex = 0x8888_4444_u64\nlet^f = 1.25f32\nlet^n = -128i8\n')
    expect('hat_spacing', 'let^ x = self^ + 1\n', 'let^x = self^+ 1\n')
    expect('nested_array_types',
           'var^a:i32^[ 2 [ 3 ] ]={{1,2,3},{4,5,6}}\nvar^b:(i32^[ 3 ])[2]=a\n',
           'var^a:i32^[2 [3]] = {{1, 2, 3}, {4, 5, 6}}\nvar^b: (i32^[3])[2] = a\n')
    expect('nested_array_kinds',
           'let^f=p^a:T[ max^64[grow^] ],b:T[4 [3 [2]]],c:T[grow^[3]] {}\n',
           'let^f = p^a:T[max^64 [grow^]], b:T[4 [3 [2]]], c:T[grow^ [3]] {}\n')
    expect('inferred_array_lengths',
           'var^a=i32^[_^]{0 repeat^5}\nvar^b=i32^[_^ ,_^]{{0 repeat^3} repeat^3}\n',
           'var^a = i32^[_^]{0 repeat^5}\nvar^b = i32^[_^, _^]{{0 repeat^3} repeat^3}\n')
    expect('rectangular_arrays',
           'var^a:i32^[2 ,3]={{1,2,3},{4,5,6}}\na[ 1 ,2 ]+=7\n',
           'var^a:i32^[2, 3] = {{1, 2, 3}, {4, 5, 6}}\na[1, 2] += 7\n')
    expect('nested_read_slice_types',
           'let^f=p^a:read^u8^[..],b:u8^[2 read^[..]]{}\n',
           'let^f = p^a:read^u8^[..], b:u8^[2 read^[..]]{}\n')
    expect('rectangular_grow_arrays',
           'var^a=i32^[grow^ ,3]{}\na.push({1,2,3})\na[0, 2]+=1\n',
           'var^a = i32^[grow^, 3]{}\na.push({1, 2, 3})\na[0, 2] += 1\n')
    expect('dynamic_reference_types',
           'let^get=p^x:dynamic^ Shape,ys:Shape[ dynamic^ .. ],zs:Shape[ ref^ dyn^ 2 ] {}\n',
           'let^get = p^x:dynamic^Shape, ys:Shape[dynamic^..], zs:Shape[ref^dyn^2] {}\n')
    expect('dynamic_array_precedence',
           'let^get=p^x:ref^ dynamic^ Shape [ 2 ],ys:dynamic^ Shape [ .. ] {}\n',
           'let^get = p^x:ref^dynamic^Shape[2], ys:dynamic^Shape[..] {}\n')
    expect('space_after_hat', 'let^x=self^+1\n', 'let^ x = self^ + 1\n', 'rhat_space_after_hat=true\n')
    expect('calls_and_operators', 'let^x  =  foo ( 1 ,2 )+bar[ 3 ]\n',
           'let^x = foo(1, 2) + bar[3]\n')
    expect('variadic_index', 'print^(...[0])\n', 'print^(...[0])\n')
    expect('variadic_index_removes_space', 'print^(... [ 0 ])\n', 'print^(...[0])\n')
    expect('variadic_slice_and_member', 'print^(... [0 .. 2],... .len)\n',
           'print^(...[0 .. 2],....len)\n')
    expect('variadic_parameter_and_forwarding', 'let^show=p^...:i32^{print^(... [0])}\nshow(1,2)\n',
           'let^show = p^...:i32^{print^(...[0])}\nshow(1, 2)\n')
    expect('unary_minus', 'let^x = - 1 * ( -y )\n', 'let^x = -1 * (-y)\n')
    expect('dereferences', 'print^( * r, ** rr, 2*3 )\n', 'print^(*r, **rr, 2 * 3)\n')
    expect('comparison_spacing', 'if^n<0 { print^(n>1,n<=2,n>=3,0<n<5) }\n',
           'if^n < 0 { print^(n > 1, n <= 2, n >= 3, 0 < n < 5) }\n')
    expect('comparison_and_generic_arguments',
           'def^Vec2<T>{self^{x:T}}\nlet^F=Vec2<f32^>\nprint^(a<b,c>d)\n',
           'def^Vec2<T>{self^{x:T}}\nlet^F = Vec2<f32^>\nprint^(a < b, c > d)\n')
    expect('dereference_after_block', 'if^true^ {}\n* tail:=* tail+1\n',
           'if^true^{}\n*tail := *tail + 1\n')
    expect('dereference_after_hat', 'let^f=f^r {return^* r}\n',
           'let^f = f^r {return^*r}\n')
    expect('yield_like_return', 'let^g=p^n:i32^ {\n    yield^ n\n    yield^\n}\n',
           'let^g = p^n:i32^{\n    yield^n\n    yield^\n}\n')
    expect('dereference_parentheses_and_power', 'print^(* (r),** (rr),2**3,2*3)\n',
           'print^(*(r), **(rr), 2 ** 3, 2 * 3)\n')
    expect('inline_records', 'let^a={x=1,y=2}\nlet^b={ x=1 }\nlet^c={ }\n',
           'let^a = {x = 1, y = 2}\nlet^b = { x = 1 }\nlet^c = {}\n')
    expect('indent_and_braces', 'let^f=p^x\n{\nif^x>1\n{\nprint^(1)\nel^:\nprint^(2)\n}\n}\n',
           'let^f = p^x {\n    if^x > 1 {\n        print^(1)\n    el^:\n        print^(2)\n    }\n}\n')
    expect('next_line_braces', 'let^f=p^x {\nif^x {\nprint^(1)\n}\n}\n',
           'let^f = p^x\n{\n    if^x\n    {\n        print^(1)\n    }\n}\n', 'rhat_brace_style=next_line\n')
    expect('wrapped_next_line_block', 'let^f=p^{print^(alpha,beta,gamma,delta)}\n',
           'let^f = p^\n{\n    print^(\n        alpha,\n        beta,\n        gamma,\n        delta\n    )\n}\n',
           'rhat_brace_style=next_line\nmax_line_length=20\n')
    expect('multiline_function_head', 'let^g:f^i32^\n    ->i32^; = f^x {\nx\n}\n',
           'let^g:f^i32^\n    -> i32^; = f^x {\n    x\n}\n')
    expect('blank_lines', '\n\nlet^a=1   \n\n\n\nlet^b=2\n\n', 'let^a = 1\n\nlet^b = 2\n')
    expect('comments', 'let^a=1  # one\n#[ block\n   #[ nested ]# comment ]#\nlet^b=2\n',
           'let^a = 1  # one\n#[ block\n   #[ nested ]# comment ]#\nlet^b = 2\n')
    expect('comment_only_crlf', '# comment\r\n', '# comment\r\n')
    expect('comment_eol', '# comment\r\nlet^x=1 # tail\r\n',
           '# comment\nlet^x = 1 # tail\n', 'end_of_line=lf\n')
    expect('literal_bytes', 'print^( $"a {  x  } b", 0x1_F, `a b`, \'it\'\'s\', "a\\n" )\n',
           'print^($"a {  x  } b", 0x1_F, `a b`, \'it\'\'s\', "a\\n")\n')
    expect('layout_operators', 'let^n=sizeof^i32^\nlet^a=alignof^(i32^[3])\nlet^b=bytesof^x\n',
           'let^n = sizeof^i32^\nlet^a = alignof^(i32^[3])\nlet^b = bytesof^x\n')
    expect('interpolation_with_nested_function', 'print^(\n$"a {(f^{1})()} b"\n)\n',
           'print^(\n    $"a {(f^{1})()} b"\n)\n')
    expect('unicode_and_crlf_literals', 'let^f=p^{\r\nprint^("🙂日本語\r\ntext")\r\n}\r\n',
           'let^f = p^{\r\n    print^("🙂日本語\r\ntext")\r\n}\r\n')
    expect('line_string', 'let^s= """ words # untouched   \nprint^(s)\n',
           'let^s = """ words # untouched   \nprint^(s)\n')
    expect('tabs', 'let^f=p^{\r\nprint^(1)\r\n}\r\n',
           'let^f = p^{\r\n\tprint^(1)\r\n}\r\n', 'indent_style=tab\n')
    expect('final_newline_off', 'let^x=1\n', 'let^x = 1', 'insert_final_newline=false\n')
    expect('end_of_line', 'let^x=1\nlet^y=2\n', 'let^x = 1\r\nlet^y = 2\r\n', 'end_of_line=crlf\n')
    expect('long_call', 'print^(alpha,beta(gamma,delta),epsilon)\n',
           'print^(\n    alpha,\n    beta(gamma, delta),\n    epsilon\n)\n', 'max_line_length=30\n')
    expect('wrap_parameter_commas_before_array_types',
           'let^parse = p^text:u8^[..], cursor:ref^Cursor, pool:ref^Node[grow^] {print^(1)}\n',
           'let^parse = p^text:u8^[..], cursor:ref^Cursor,\n    pool:ref^Node[grow^] {print^(1)}\n',
           'max_line_length=60\n')
    expect('wrap_parameters_repeatedly',
           'let^f = p^aaaaaaaaaa:i32^, bbbbbbbbbb:i32^, cccccccccc:i32^, dddddddddd:i32^, eeeeeeeeee:i32^{1}\n',
           'let^f = p^aaaaaaaaaa:i32^, bbbbbbbbbb:i32^,\n'
           '    cccccccccc:i32^, dddddddddd:i32^,\n    eeeeeeeeee:i32^{1}\n',
           'max_line_length=50\n')
    parameter_prefix = 'let^f = p^a:i32^, b:i32^,'
    expect('wrap_parameters_exact_width', parameter_prefix + ' c:i32^{1}\n',
           parameter_prefix + '\n    c:i32^{1}\n',
           f'max_line_length={len(parameter_prefix)}\n')
    expect('short_parameters_stay_on_one_line',
           'let^f = p^a:i32^, b:i32^, c:i32^{1}\n',
           'let^f = p^a:i32^, b:i32^, c:i32^{1}\n', 'max_line_length=60\n')
    expect('wrap_call_with_typed_array',
           'print^(first_argument, u8^[grow^]{}, last_argument)\n',
           'print^(\n    first_argument,\n    u8^[grow^]{},\n    last_argument\n)\n',
           'max_line_length=40\n')
    expect('generics_and_refs', 'def^V<T>{\nself^{x:T,y:ref^T}\nZERO=Self^{x=T.ZERO}\n}\nlet^F=V<f32^>\n',
           'def^V<T>{\n    self^{x:T, y:ref^T}\n    ZERO = Self^{x = T.ZERO}\n}\nlet^F = V<f32^>\n')
    expect('nominal_method_bodies', 'def^V<T>{\nself^{x:T}\n'
           'op^+ =f^self^,b:Self^ ->Self^{\nSelf^{x=self^.x+b.x}\n}\n'
           'get=f^self^ ->T {\nself^.x\n}\n}\nprint^(1)\n',
           'def^V<T>{\n    self^{x:T}\n'
           '    op^+ = f^self^, b:Self^-> Self^{\n        Self^{x = self^.x + b.x}\n    }\n'
           '    get = f^self^-> T {\n        self^.x\n    }\n}\nprint^(1)\n')
    expect('nominal_method_multiline_head', 'def^V{\nself^{x:i32^}\n'
           'copy=f^self^\n->Self^{\nSelf^{x=self^.x}\n}\n}\n',
           'def^V{\n    self^{x:i32^}\n'
           '    copy = f^self^\n        -> Self^{\n        Self^{x = self^.x}\n    }\n}\n')
    expect('nominal_multiline_fields_and_constants', 'def^V{\nself^{\nx:i32^\n}\n'
           'ZERO=Self^{\nx=0\n}\nget=f^self^ {\nself^.x\n}\n}\n',
           'def^V{\n    self^{\n        x:i32^\n    }\n'
           '    ZERO = Self^{\n        x = 0\n    }\n    get = f^self^{\n        self^.x\n    }\n}\n')
    expect('inferred_field_defaults', 'def^A{self^{i=0,s=""}}\n',
           'def^A{self^{i = 0, s = ""}}\n')
    expect('mutable_static_member', 'def^A{\nvar^n=0\ninc=p^{def^.n+=1}\n}\n',
           'def^A{\n    var^n = 0\n    inc = p^{def^.n += 1}\n}\n')
    expect('nested_named_initializers',
           'def^Node{}\ndef^Xf{}\ndef^F3{}\npool.push(Node{\nid=1,\nlocal=Xf{\nm=rotation(r),\nt=F3{\nx=1,\ny=2\n}\n},\nworld=identity()\n})\n',
           'def^Node{}\ndef^Xf{}\ndef^F3{}\npool.push(Node{\n    id = 1,\n    local = Xf{\n        m = rotation(r),\n'
           '        t = F3{\n            x = 1,\n            y = 2\n        }\n    },\n    world = identity()\n})\n')
    expect('nested_anonymous_record_initializers',
           'let^v={\na={\nb={\nc=1\n}\n},\nd=2\n}\n',
           'let^v = {\n    a = {\n        b = {\n            c = 1\n        }\n    },\n    d = 2\n}\n')
    expect('match_expression', 'let^x=for^mode:\nwhen^1: "one"\nother^: "other"\n;\n',
           'let^x = for^mode:\nwhen^1: "one"\nother^: "other"\n;\n')
    expect('match_expression_in_function',
           'let^sent=f^status {\nfor^status:\nwhen^1: 10\nwhen^2: 20\nother^: 0;\n}\n',
           'let^sent = f^status {\n    for^status:\n    when^1: 10\n'
           '    when^2: 20\n    other^: 0;\n}\n')
    expect('bound_match_expression',
           'let^f=f^status {\nlet^x=for^status:\nwhen^1:\n10\nother^:\n0;\nx\n}\n',
           'let^f = f^status {\n    let^x = for^status:\n    when^1:\n        10\n'
           '    other^:\n        0;\n    x\n}\n')
    expect('match_statement_in_function',
           'let^sent=p^status {\nfor^status {\nwhen^1:\nprint^(10)\nother^:\nprint^(0)\n}\n}\n',
           'let^sent = p^status {\n    for^status {\n    when^1:\n        print^(10)\n'
           '    other^:\n        print^(0)\n    }\n}\n')
    expect('nested_match_expressions',
           'let^f=f^a,b {\nfor^a:\nwhen^1:\nfor^b:\nwhen^2:\n20\nother^:\n0;\nother^:\n1;\n}\n',
           'let^f = f^a, b {\n    for^a:\n    when^1:\n        for^b:\n'
           '        when^2:\n            20\n        other^:\n            0;\n'
           '    other^:\n        1;\n}\n')
    expect('repeat_max_nested_array', 'var^c=u8^[2[3]]{{0 repeat^max^} repeat^max^}\n',
           'var^c = u8^[2 [3]]{{0 repeat^max^} repeat^max^}\n')
    expect('loop_sections', 'repeat^while^true^{\nmain^:\nprint^(1)\nlast^:\nprint^(2)\n}\nprint^(3)\n',
           'repeat^while^true^{\nmain^:\n    print^(1)\nlast^:\n    print^(2)\n}\nprint^(3)\n')
    expect('catch_expression_before_do', 'print^(read(-1) catch^42)\ndo^{\nprint^(1)\n}\nprint^(2)\n',
           'print^(read(-1) catch^42)\ndo^{\n    print^(1)\n}\nprint^(2)\n')
    expect('catch_expression_named_fallback', 'let^v=read() catch^fallback()\nprint^(v)\n',
           'let^v = read() catch^fallback()\nprint^(v)\n')
    expect('catch_clause_with_expression', 'do^{\nprint^try^read()\ncatch^Error.Invalid:\n'
           'print^(read() catch^42)\nprint^(1)\n}\nprint^(2)\n',
           'do^{\n    print^try^read()\ncatch^Error.Invalid:\n'
           '    print^(read() catch^42)\n    print^(1)\n}\nprint^(2)\n')
    expect('nested_do_after_catch_expression', 'let^f=p^{\nprint^(read() catch^fallback())\n'
           'do^{\nprint^try^read()\ncatch^Error.Invalid:\nprint^(0)\n}\nprint^(2)\n}\nprint^(3)\n',
           'let^f = p^{\n    print^(read() catch^fallback())\n'
           '    do^{\n        print^try^read()\n    catch^Error.Invalid:\n        print^(0)\n'
           '    }\n    print^(2)\n}\nprint^(3)\n')
    expect('unresolved_import', 'import^not.installed\nlet^x=unknown(1)\n',
           'import^not.installed\nlet^x = unknown(1)\n')
    expect('call_newline', 'let^f=f^x {x}\nlet^v=f\n(1)()\n',
           'let^f = f^x {x}\nlet^v = f\n(1)()\n')
    expect('empty', '', '')

    def invalid():
        path = fixture('invalid')
        for source in ('let^ = (', 'print^("unterminated)', '#[ missing', 'def^V<T>{self^{x:T}'):
            path.write_text(source, encoding='utf-8')
            for command in ('format', 'format-check'):
                result = suite.run([suite.compiler, command, path], False)
                assert 'cannot format invalid syntax' in result.stderr
                assert path.read_text(encoding='utf-8') == source
    suite.case('invalid_source_is_preserved', invalid)

    def config():
        path = fixture('hierarchy', 'indent_size=2\nrhat_space_after_hat=true\n')
        sub = path.parent / 'sub'
        sub.mkdir(exist_ok=True)
        (sub / '.editorconfig').write_text('[file{1..3}.rh]\nindent_size=3\ninsert_final_newline=false\n'
                                          '[file[4-5].rh]\nindent_size=unset\n', encoding='utf-8')
        source = 'let^f=p^{\nprint^(1)\n}\n'
        for name, indent, final in [('file2.rh', 3, ''), ('file4.rh', 4, '\n'), ('other.rh', 2, '\n')]:
            file = sub / name
            file.write_text(source, encoding='utf-8')
            suite.run([suite.compiler, 'format', file])
            assert file.read_text() == 'let^ f = p^ {\n' + ' '*indent + 'print^(1)\n}' + final
    suite.case('editorconfig_hierarchy_globs_and_unset', config)

    def errors_example():
        path = fixture('errors_example')
        path.write_bytes((suite.repo / 'examples/errors.rh').read_bytes())
        suite.run([suite.compiler, 'format', path])
        lines = path.read_text(encoding='utf-8').splitlines()
        assert any('if^n < 0 {' in line for line in lines), lines
        assert 'do^{' in lines, lines
        assert 'catch^Error.Invalid:' in lines, lines
        assert lines[-1].startswith('print^'), lines
        suite.run([suite.compiler, 'format-check', path])
        if not suite.args.checks_only:
            result = suite.run([suite.compiler, 'run', path, '--clang', suite.clang], False)
            assert result.stdout == '42\ninvalid input\n', result.stdout
            assert 'panic: Error.Invalid' in result.stderr, result.stderr
    suite.case('errors_example_keeps_top_level_indent', errors_example)

    def graph_example():
        path = fixture('graph_example')
        path.write_bytes((suite.repo / 'bench/rhat/graph.rh').read_bytes())
        suite.run([suite.compiler, 'format', path])
        text = path.read_text(encoding='utf-8')
        assert '    *tail += 1' in text, text
        suite.run([suite.compiler, 'format-check', path])
    suite.case('graph_example_dereference_spacing', graph_example)

    def scene_example():
        path = fixture('scene_example')
        source = (suite.repo / 'bench/rhat/scene.rh').read_text(encoding='utf-8')
        path.write_text(source, encoding='utf-8')
        suite.run([suite.compiler, 'format', path])
        def initializer(text):
            start = text.index('    pool.push(Node{')
            end = text.index('    })', start) + len('    })')
            return text[start:end]
        assert initializer(path.read_text(encoding='utf-8')) == initializer(source)
        suite.run([suite.compiler, 'format-check', path])
    suite.case('scene_example_preserves_nested_initializer_indent', scene_example)

    def sexp_example():
        path = fixture('sexp_example')
        path.write_bytes((suite.repo / 'bench/rhat/sexp.rh').read_bytes())
        suite.run([suite.compiler, 'format', path])
        text = path.read_text(encoding='utf-8')
        assert ('let^parse_node = p^text:read^u8^[..], c:ref^Cursor, pool:ref^Node[grow^], '
                'symbols:ref^u8^[grow^],\n'
                '    prev:u32^-> ParseResult {') in text, text
        suite.run([suite.compiler, 'format-check', path])
    suite.case('sexp_example_wraps_parameters_before_array_types', sexp_example)

    def live():
        path = fixture('lsp', 'indent_size=3\nindent_style=space\nmax_line_length=off\n')
        path.write_text('disk stays untouched', encoding='utf-8')
        client = Client(Path(suite.compiler))
        try:
            caps = client.request('initialize', dict(rootUri=path.parent.as_uri(), capabilities={}))
            assert caps['capabilities']['documentFormattingProvider'] is True
            client.send('initialized', {})
            source = 'let^f=p^{\r\nprint^("🙂", unknown())\r\n}\r\n'
            expected = 'let^f = p^{\r\n   print^("🙂", unknown())\r\n}\r\n'
            client.open(path, source)
            client.diagnostics(path, 1)
            params = dict(textDocument=dict(uri=path.as_uri()), options=dict(tabSize=8, insertSpaces=False))
            edits = client.request('textDocument/formatting', params)
            assert len(edits) == 1 and edits[0]['newText'] == expected, edits
            assert edits[0]['range'] == dict(start=dict(line=0, character=0), end=dict(line=3, character=0)), edits
            assert path.read_text() == 'disk stays untouched'
            client.change(path, expected, 2)
            client.diagnostics(path, 2)
            assert client.request('textDocument/formatting', params) == []
            client.change(path, 'let^ = (', 3)
            client.diagnostics(path, 3)
            assert client.request('textDocument/formatting', params) is None
            client.change(path, source, 4)
            client.diagnostics(path, 4)
            assert client.request('textDocument/formatting', params)[0]['newText'] == expected
            catch_source = 'print^(read(-1) catch^42)\ndo^{\nprint^(1)\n}\nprint^(2)\n'
            client.change(path, catch_source, 5)
            client.diagnostics(path, 5)
            edits = client.request('textDocument/formatting', params)
            assert edits[0]['newText'] == ('print^(read(-1) catch^42)\ndo^{\n'
                                            '   print^(1)\n}\nprint^(2)\n'), edits
            method_source = ('def^V<T>{\nself^{x:T}\n'
                             'op^+ =f^self^,b:Self^ ->Self^{\nSelf^{x=self^.x+b.x}\n}\n'
                             'get=f^self^ ->T {\nself^.x\n}\n}\nlet^F=V<i32^>\n')
            client.change(path, method_source, 6)
            assert client.diagnostics(path, 6) == []
            formatted = client.request('textDocument/formatting', params)[0]['newText']
            assert ('   op^+ = f^self^, b:Self^-> Self^{\n'
                    '      Self^{x = self^.x + b.x}\n   }\n'
                    '   get = f^self^-> T {\n      self^.x\n   }\n}\n') in formatted, formatted
            match_source = 'let^f=f^status:i32^ {\nfor^status:\nwhen^1: 10\nother^: 0;\n}\n'
            client.change(path, match_source, 7)
            assert client.diagnostics(path, 7) == []
            formatted = client.request('textDocument/formatting', params)[0]['newText']
            assert formatted == ('let^f = f^status:i32^{\n   for^status:\n'
                                 '   when^1: 10\n   other^: 0;\n}\n'), formatted
            # The CLI sees the same options and produces the same text.
            path.write_bytes(source.encode())
            suite.run([suite.compiler, 'format', path])
            assert path.read_bytes() == expected.encode()
        finally:
            client.close()
    suite.case('lsp_unsaved_settings_ranges_and_recovery', live)

    def examples():
        root = fixture('project_sources').parent
        for source in [*Path(suite.repo / 'examples').glob('*.rh'), *Path(suite.repo / 'stdlib').rglob('*.rh')]:
            path = root / source.relative_to(suite.repo)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(source.read_bytes())
            suite.run([suite.compiler, 'format', path])
            suite.run([suite.compiler, 'format-check', path])
        if not suite.args.checks_only:
            path = root / 'examples/generic_vec2.rh'
            exe = root / 'vector.exe'
            suite.run([suite.compiler, 'build', path, '--clang', suite.clang, '-O2', '-o', exe])
            assert suite.run([exe]).stdout == '3.0 4.0 18.0 1 0\n'
        lines = (root / 'examples/generic_vec2.rh').read_text(encoding='utf-8').splitlines()
        for name in ('op^+', 'op^dot^'):
            at = next(i for i, line in enumerate(lines) if line.lstrip().startswith(name))
            assert len(lines[at]) - len(lines[at].lstrip()) == 4, lines
            assert len(lines[at + 1]) - len(lines[at + 1].lstrip()) == 8, lines
            assert lines[at + 2] == '    }', lines
        hanoi = (root / 'examples/hanoi.rh').read_text(encoding='utf-8')
        assert '...[0]' in hanoi and '... [' not in hanoi, hanoi
    suite.case('project_sources_and_formatted_program_execution', examples)
    suite.finish()


if __name__ == '__main__':
    main()
