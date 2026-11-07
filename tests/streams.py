"""The streams of std.io and std.fs."""

from harness import Suite


def main():
    suite = Suite('streams')
    positive, negative = suite.positive, suite.negative

    FILE = '''
let^fs = import^std.fs
let^io = import^std.io
'''
    positive('files_in_an_array', FILE + '''
let^c = import^std.collections
let^path = "streams_array.tmp"
do^{
    try^fs.write(path, "x".bytes)
    var^files = fs.File[grow^]{}
    var^names:c.HashMap<string^, ref^fs.File> = {}
    files.push(try^fs.open(path))
    files.push(try^fs.open(path))
    names.set("first", ref^files[0])
    var^data:u8^[grow^] = {}
    let^named = names.get("first")
    if^named fits^ref^fs.File { print^(try^named.read(ref^ data, 1)) }
    print^(try^files[1].read(ref^ data, 1), data.len)
catch^:
    print^("failed")
}
print^(fs.delete(path))
''', '1\n1 2\ntrue\n')

    positive('file_read_write', FILE + '''
let^path = "streams_file.tmp"
do^{
    do^{
        var^w = try^fs.create(path)
        try^w.write("hello ".bytes)
        try^w.write("world".bytes)
        try^w.flush()
    }
    var^r = try^fs.open(path)
    var^data:u8^[grow^] = {}
    print^(try^r.read(ref^ data, 5), try^r.read(ref^ data, 0))
    try^r.read_all(ref^ data)
    print^(string^(data[..]) catch^ "bad", data.len, try^r.read(ref^ data, 5))
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
    try^a.write("x".bytes)
    try^a.close()
    print^(closed(a.close()))
    print^(closed(a.write("y".bytes)), closed(a.flush()))
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
''', 'true\ntrue true\ntrue\n1 1\ncaught closed\ntrue\n')

    # A file its scope closes has been written out, with no flush or close written here.
    positive('scope_closes_the_file', FILE + '''
let^path = "streams_disposed.tmp"
do^{
    do^{
        var^w = try^fs.create(path)
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
    var^r = try^fs.open("no_such_dir/no_such.tmp")
    print^("never")
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
    do^{
        var^w = try^fs.create(path)
        try^copy(ref^ source, ref^ w)
        try^log(ref^ w, "!")
    }
    do^{
        var^r = try^fs.open(path)
        var^back = io.MemoryWriter{}
        try^copy(ref^ r, ref^ back)
        print^(string^(back.data()) catch^ "bad")
    }
    var^dynamic = io.MemoryReader.over(text.bytes)
    var^out = io.MemoryWriter{}
    try^copy_any(ref^ dynamic, ref^ out)
    print^(out.data().len)
    var^r = try^fs.open(path)
    out.clear()
    try^copy_any(ref^ r, ref^ out)
    print^(out.data().len)
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
