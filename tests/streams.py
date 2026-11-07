"""with^ and dispose(), and the streams of std.io and std.fs."""

from harness import Suite

RESOURCE = '''
errordef^E { Bad }
def^R {
    self^{ name:string^ }
    dispose = p^self^ { print^("dispose", self^.name) }
    use = p^self^ { print^("use", self^.name) }
}
let^make = p^name:string^, ok:bool^ -> R | E {
    if^!ok { return^E.Bad{} }
    return^R{name = name}
}
let^res = p^name:string^ -> R { return^R{name = name} }
let^fail = p^ -> nil^ | E { return^E.Bad{} }
'''


def main():
    suite = Suite('streams')
    positive, negative = suite.positive, suite.negative

    positive('with_every_way_out', RESOURCE + '''
with^a = res("a")
with^b = res("b") {
    a.use()
    b.use()
}
let^early = p^ -> i32^ {
    with^r = res("return") {
        return^1
    }
}
print^(early())
for^i from^0 to^1 {
    with^r = res("loop") {
        if^i = 0 { next^ }
        break^
    }
}
let^propagates = p^ -> nil^ | E {
    with^r = res("try") {
        try^fail()
    }
    print^("never")
    return^nil^
}
print^(propagates() fits^ E)
with^outer = res("outer") {
    with^inner = res("inner") {
        print^("body")
    }
    print^("between")
}
''', 'use a\nuse b\ndispose b\ndispose a\ndispose return\n1\ndispose loop\ndispose loop\n'
     'dispose try\ntrue\nbody\ndispose inner\nbetween\ndispose outer\n')

    positive('with_catch', RESOURCE + '''
do^{
    with^r = try^make("body", true^) {
        print^("in")
        try^fail()
        print^("never")
    }
catch^:
    print^("caught")
}
do^{
    with^r = try^make("none", false^) {
        print^("never")
    }
catch^:
    print^("caught open")
}
do^{
    with^a = try^make("first", true^)
    with^b = try^make("second", false^) {
        print^("never")
    }
catch^:
    print^("caught second")
}
do^{
    with^r = try^make("fine", true^) {
        print^("in")
    }
catch^:
    print^("never")
}
print^("end")
''', 'in\ndispose body\ncaught\ncaught open\ndispose first\ncaught second\nin\ndispose fine\nend\n')

    positive('with_catch_inside', RESOURCE + '''
with^r = res("body") {
    try^fail()
    print^("never")
catch^:
    print^("arm", r.name)
    r.use()
}
do^{
    with^r = try^make("head", false^) {
        try^fail()
    catch^:
        print^("never")
    }
catch^:
    print^("head goes outside")
}
do^{
    with^a = res("a")
    with^b = try^make("b", false^) {
        try^fail()
    catch^:
        print^("never")
    }
catch^:
    print^("second head goes outside")
}
do^{
    with^r = res("rethrown") {
        try^fail()
    catch^:
        print^("arm")
        try^fail()
        print^("never")
    }
catch^:
    print^("outer")
}
let^answer = p^ -> i32^ {
    with^r = res("returned") {
        try^fail()
        return^1
    catch^:
        return^2
    }
}
print^(answer())
with^r = res("recovered") {
    try^fail()
catch^:
    print^("arm")
}
print^("end")
''', 'arm body\nuse body\ndispose body\nhead goes outside\ndispose a\nsecond head goes outside\n'
     'arm\ndispose rethrown\nouter\ndispose returned\n2\narm\ndispose recovered\nend\n')

    positive('with_shadow_and_owned', RESOURCE + '''
def^Owner {
    self^{ data:i32^[grow^] = {} }
    dispose = p^self^ { print^("owner", self^.data.len) }
}
with^o = Owner{} {
    o.data.push(1)
}
with^r = res("named") {
    do^{
        let^r = 1
        print^(r)
    }
}
''', 'owner 1\n1\ndispose named\n')

    positive('with_literal_head', '''
def^D<T> {
    self^{ n:T }
    dispose = p^self^ { print^("dispose", self^.n) }
}
def^N {
    self^{ n:i32^ }
    dispose = p^self^ { print^("dispose", self^.n) }
}
with^a = N{n = 1}
with^b = N{n = 2} {
    print^(a.n + b.n)
}
with^c = D<i32^>{n = 3} {
    print^(c.n)
}
with^d = (N{n = 4}) {}
''', '3\ndispose 2\ndispose 1\n3\ndispose 3\ndispose 4\n')
    negative('with_qualified_literal', 'let^io = import^std.io\nwith^m = io.MemoryWriter{} {}\n',
             "with^ needs a value with a dispose() method, found 'std.io.MemoryWriter'")

    negative('with_needs_a_block', RESOURCE + 'with^r = res("x")\nprint^(r.name)\n', 'with^ needs a block')
    negative('with_no_dispose', 'def^N {}\nwith^n = (N{}) {}\n', "with^ needs a value with a dispose() method, found 'N'")
    negative('with_number', 'with^n = 1 {}\n', "with^ needs a value with a dispose() method, found 'i32^'")
    negative('with_dispose_argument', 'def^N { dispose = p^self^, x:i32^ {} }\nwith^n = (N{}) {}\n', 'dispose must be a p^self^ method')
    negative('with_dispose_function', 'def^N { dispose = f^self^ {} }\nwith^n = (N{}) {}\n', 'dispose must be a p^self^ method')
    negative('with_dispose_result', 'def^N { dispose = p^self^ -> i32^ { return^1 } }\nwith^n = (N{}) {}\n', 'dispose must not return a value')
    negative('with_write', RESOURCE + 'with^r = res("x") { r := res("y") }\n', 'a with^ name cannot be written')
    negative('with_colon_equals', RESOURCE + 'with^r := res("x") {}\n', 'with^ binds with =')
    negative('with_return', RESOURCE + 'let^f = p^ -> R {\n with^r = res("x") { return^r }\n}\nf()\n', 'it cannot leave the block')
    negative('with_copy', RESOURCE + 'with^r = res("x") { let^s = r }\n', 'it cannot leave the block')
    negative('with_in_function', RESOURCE + 'let^f = f^ -> i32^ {\n with^r = (R{name = "x"}) { return^1 }\n}\nprint^(f())\n', 'with^ needs p^')
    negative('with_arm_cannot_see_body_names', RESOURCE + 'with^r = res("x") {\n let^n = 1\n try^fail()\ncatch^:\n print^(n)\n}\n', "'n'")
    negative('with_name_in_catch', RESOURCE + 'do^{\n with^r = try^make("x", true^) {\n print^(1)\n }\ncatch^:\n print^(r.name)\n}\n', "'r.name'")

    FILE = '''
let^fs = import^std.fs
let^io = import^std.io
'''
    positive('file_read_write', FILE + '''
let^path = "streams_file.tmp"
do^{
    with^w = try^fs.create(path) {
        try^w.write("hello ".bytes)
        try^w.write("world".bytes)
        try^w.flush()
    }
    with^r = try^fs.open(path) {
        var^data:u8^[grow^] = {}
        print^(try^r.read(ref^ data, 5), try^r.read(ref^ data, 0))
        try^r.read_all(ref^ data)
        print^(string^(data[..]) catch^ "bad", data.len, try^r.read(ref^ data, 5))
    }
catch^:
    print^("failed")
}
print^(fs.delete(path))
''', '5 0\nhello world 11 0\ntrue\n')

    positive('file_after_close', FILE + '''
let^path = "streams_closed.tmp"
let^closed = f^r:nil^ | io.Error -> bool^ {
    if^r fits^ io.Error { return^r fits^ io.Error.Closed }
    return^false^
}
let^closed_read = f^r:i64^ | io.Error -> bool^ {
    if^r fits^ io.Error { return^r fits^ io.Error.Closed }
    return^false^
}
do^{
    var^a = try^fs.create(path)
    var^copy = a
    try^a.write("x".bytes)
    try^a.close()
    print^(closed(a.close()), closed(copy.close()))
    print^(closed(copy.write("y".bytes)), closed(copy.flush()))
    var^b = try^fs.open(path)
    var^data:u8^[grow^] = {}
    print^(closed_read(a.read(ref^ data, 1)))
    print^(try^b.read(ref^ data, 1), data.len)
    try^b.close()
    do^{
        try^b.close()
        print^("never")
    catch^ io.Error.Closed:
        print^("caught closed")
    }
catch^:
    print^("failed")
}
print^(fs.delete(path))
''', 'true true\ntrue true\ntrue\n1 1\ncaught closed\ntrue\n')

    # A file with^ closes has been written out, with no flush or close written here.
    positive('with_closes_the_file', FILE + '''
let^path = "streams_disposed.tmp"
do^{
    with^w = try^fs.create(path) {
        try^w.write("kept".bytes)
    }
    let^back = try^fs.read(path)
    print^(string^(back[..]) catch^ "bad")
    print^(fs.delete(path))
catch^:
    print^("failed")
}
''', 'kept\ntrue\n')

    positive('file_open_failure', FILE + '''
do^{
    with^r = try^fs.open("no_such_dir/no_such.tmp") {
        print^("never")
    }
catch^ fs.Error.OpenFailed:
    print^("open failed")
}
''', 'open failed\n')

    positive('streams_static_and_dynamic', FILE + '''
let^path = "streams_copy.tmp"
let^copy = p^src:ref^io.Reader, dst:ref^io.Writer -> nil^ | io.Error {
    var^chunk:u8^[grow^] = {}
    repeat^{
        chunk.clear()
        if^(try^src.read(ref^ chunk, 3)) = 0 { return^nil^}
        try^dst.write(chunk[..])
    }
}
let^copy_any = p^src:ref^dyn^io.Reader, dst:ref^dyn^io.Writer -> nil^ | io.Error {
    var^chunk:u8^[grow^] = {}
    repeat^{
        chunk.clear()
        if^(try^src.read(ref^ chunk, 3)) = 0 { return^nil^}
        try^dst.write(chunk[..])
    }
}
let^log = p^out:ref^dyn^io.Stream, s:string^-> nil^ | io.Error {
    try^out.write(s.bytes)
    return^out.flush()
}
let^text = "memory to file and back"
do^{
    var^source = io.MemoryReader.over(text.bytes)
    with^w = try^fs.create(path) {
        try^copy(ref^ source, ref^ w)
        try^log(ref^ w, "!")
    }
    with^r = try^fs.open(path) {
        var^back = io.MemoryWriter{}
        try^copy(ref^ r, ref^ back)
        print^(string^(back.data()) catch^ "bad")
    }
    var^dynamic = io.MemoryReader.over(text.bytes)
    var^out = io.MemoryWriter{}
    try^copy_any(ref^ dynamic, ref^ out)
    print^(out.data().len)
    with^r = try^fs.open(path) {
        out.clear()
        try^copy_any(ref^ r, ref^ out)
        print^(out.data().len)
    }
catch^:
    print^("failed")
}
print^(fs.delete(path))
''', 'memory to file and back!\n23\n24\ntrue\n')

    positive('file_crosses_chunks', FILE + '''
let^path = "streams_big.tmp"
do^{
    var^big:u8^[grow^] = {}
    for^i in^0 .. i64^(200000) { big.push(u8^(i % 251)) }
    try^fs.write(path, big[..])
    try^fs.append(path, big[.. 10])
    let^back = try^fs.read(path)
    print^(back.len, back[199999], back[200009])
    var^empty_file = try^fs.create(path)
    try^empty_file.close()
    let^nothing = try^fs.read(path)
    print^(nothing.len)
catch^:
    print^("failed")
}
print^(fs.delete(path))
''', '200010 203 9\n0\ntrue\n')

    suite.finish()


if __name__ == '__main__':
    main()
