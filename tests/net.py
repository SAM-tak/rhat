"""std.async waits and Loop, and std.net TCP and UDP over loopback."""
from harness import Suite


def main():
    suite = Suite('net')
    positive, negative = suite.positive, suite.negative

    # a's second wait is registered only after its first one fired, so it always comes due
    # after b's: the order holds however late the timers fire.
    positive('delay_order', '''
let^ async = import^ std.async
let^ work = p^ name:string^, pause:f64^, n:i64^ {
    var^ k:i64^ = 0
    repeat^ while^ k < n {
        await^ async.delay(pause)
        print^(name, k)
        k += 1
    }
    return^ nil^
}
let^ run = p^ {
    var^ a = work("a", 0.1, 2)
    var^ b = work("b", 0.15, 1)
    var^ loop = async.Loop<nil^>{}
    loop.add(ref^ a)
    loop.add(ref^ b)
    loop.run()
    print^("done")
}
run()
''', 'a 0\nb 0\na 1\ndone\n')

    # A dropped wait leaves the table and never comes back from wait.
    positive('async_drop', '''
let^ async = import^ std.async
let^ run = p^ {
    let^ a = async.timer(0.0)
    let^ b = async.timer(0.0)
    print^(async.pending(), async.waiting(a), async.waiting(b))
    async.drop(a)
    print^(async.pending(), async.waiting(a), async.waiting(b))
    let^ got = async.wait(1.0)
    if^ got fits^ i64^ { print^(got = b) }
    print^(async.pending(), async.waiting(b), async.wait(0.0) = nil^)
}
run()
''', '2 true true\n1 false true\ntrue\n0 false true\n')

    positive('tcp_echo', '''
let^ async = import^ std.async
let^ net = import^ std.net
let^ serve = p^ listener:ref^ net.TcpListener {
    var^ buf:u8^[64] = {}
    var^ conn = try^ await^ listener.accept()
    repeat^ {
        let^ n = try^ await^ conn.read(buf[..])
        if^ n = 0 { return^ nil^ }
        try^ await^ conn.write(buf[0 .. n])
    }
}
let^ client = p^ port:i64^ {
    var^ conn = try^ await^ net.connect("127.0.0.1", port)
    var^ buf:u8^[64] = {}
    var^ round:i64^ = 0
    repeat^ while^ round < 3 {
        try^ await^ conn.write("hello".bytes)
        var^ got:i64^ = 0
        repeat^ while^ got < 5 { got += try^ await^ conn.read(buf[got .. 64]) }
        let^ text = string^(buf[0 .. got])
        if^ text fits^ string^ { print^(text, round) }
        round += 1
    }
    conn.close()
    return^ nil^
}
let^ run = p^ -> nil^ | net.Error {
    var^ listener = try^ net.listen("127.0.0.1", 0)
    var^ s = serve(ref^ listener)
    var^ c = client(listener.port())
    var^ loop = async.Loop<nil^ | net.Error>{}
    loop.add(ref^ s)
    loop.add(ref^ c)
    loop.run()
    if^ s.result() fits^ nil^ and^ c.result() fits^ nil^ { print^("done") }
    return^ nil^
}
let^ r = run()
''', 'hello 0\nhello 1\nhello 2\ndone\n')

    positive('tcp_pending_and_closed', '''
let^ net = import^ std.net
let^ run = p^ -> nil^ | net.Error {
    var^ listener = try^ net.listen("127.0.0.1", 0)
    let^ first = try^ listener.try_accept()
    if^ first fits^ net.Pending { print^("pending") }
    listener.close()
    let^ again = listener.try_accept()
    if^ again fits^ net.Error.Closed { print^("closed") }
    return^ nil^
}
let^ r = run()
''', 'pending\nclosed\n')

    positive('udp_round_trip', '''
let^ async = import^ std.async
let^ net = import^ std.net
let^ server = p^ sock:ref^ net.Udp {
    var^ buf:u8^[64] = {}
    let^ got = try^ await^ sock.receive(buf[..])
    print^("server got", got.length)
    try^ sock.send_to("pong".bytes, ref^ got.from)
    return^ nil^
}
let^ client = p^ sock:ref^ net.Udp, port:i64^ {
    let^ to = try^ net.Address.resolve("127.0.0.1", port)
    try^ sock.send_to("ping!".bytes, ref^ to)
    var^ buf:u8^[64] = {}
    let^ got = try^ await^ sock.receive(buf[..])
    let^ text = string^(buf[0 .. got.length])
    if^ text fits^ string^ { print^("client got", text, got.from.port() = port) }
    return^ nil^
}
let^ run = p^ -> nil^ | net.Error {
    var^ a = net.udp()
    try^ a.bind("127.0.0.1", 0)
    var^ b = net.udp()
    var^ s = server(ref^ a)
    var^ c = client(ref^ b, a.port())
    var^ loop = async.Loop<nil^ | net.Error>{}
    loop.add(ref^ s)
    loop.add(ref^ c)
    loop.run()
    var^ host:u8^[grow^] = {}
    var^ address = try^ net.Address.resolve("localhost", 80)
    address.write_host(ref^ host)
    print^(host.len > 0)
    return^ nil^
}
let^ r = run()
''', 'server got 5\nclient got pong true\ntrue\n')

    negative('resolve_failure', '''
let^ net = import^ std.net
let^ run = p^ -> nil^ | net.Error {
    let^ a = try^ net.Address.resolve("", 99999)
    return^ nil^
}
let^ r = run()
let^ x:i32^ = r
''', "expected 'i32^'")
    suite.finish()


if __name__ == '__main__':
    main()
