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

    expect('hat_spacing', 'let^ x = self^ + 1\n', 'let^x = self^+ 1\n')
    expect('space_after_hat', 'let^x=self^+1\n', 'let^ x = self^ + 1\n', 'rhat_space_after_hat=true\n')
    expect('calls_and_operators', 'let^x  =  foo ( 1 ,2 )+bar[ 3 ]\n',
           'let^x = foo(1, 2) + bar[3]\n')
    expect('unary_minus', 'let^x = - 1 * ( -y )\n', 'let^x = -1 * (-y)\n')
    expect('dereferences', 'print^( * r, ** rr, 2*3 )\n', 'print^(*r, **rr, 2 * 3)\n')
    expect('inline_records', 'let^a={x=1,y=2}\nlet^b={ x=1 }\nlet^c={ }\n',
           'let^a = {x = 1, y = 2}\nlet^b = { x = 1 }\nlet^c = {}\n')
    expect('indent_and_braces', 'let^f=p^x\n{\nif^x>1\n{\nprint^(1)\nel^:\nprint^(2)\n}\n}\n',
           'let^f = p^x {\n    if^x>1 {\n        print^(1)\n    el^:\n        print^(2)\n    }\n}\n')
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
    expect('generics_and_refs', 'def^V<T>{\nself^{x:T,y:ref^T}\nZERO=Self^{x=T.ZERO}\n}\nlet^F=V<f32^>\n',
           'def^V<T>{\n    self^{x:T, y:ref^T}\n    ZERO = Self^{x = T.ZERO}\n}\nlet^F = V<f32^>\n')
    expect('match_expression', 'let^x=for^mode:\nwhen^1: "one"\nother^: "other"\n;\n',
           'let^x = for^mode:\n    when^1: "one"\n    other^: "other"\n;\n')
    expect('loop_sections', 'repeat^while^true^{\nmain^:\nprint^(1)\nlast^:\nprint^(2)\n}\nprint^(3)\n',
           'repeat^while^true^{\nmain^:\n    print^(1)\nlast^:\n    print^(2)\n}\nprint^(3)\n')
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
    suite.case('project_sources_and_formatted_program_execution', examples)
    suite.finish()


if __name__ == '__main__':
    main()
