# std.async と std.net（待ちとネットワーク）

コルーチン（[coroutines.md](coroutines.md)）で書くネットワーク処理の土台です。
L^ の `std.async`（`timer`・`wait`）と `std.net`（UDP）を踏襲し、TCP を足しています。

## 実装状況

- 実装済み: `std.async` の `timer`・`readable`・`writable`・`wait`・`delay`・`Loop<T>`。
- 実装済み: `std.net` の `listen`・`connect`・`TcpListener`・`TcpStream`・`udp`・`Udp`・`Address`・`Datagram`。
- サンプル: [examples/echo_server.rh](../examples/echo_server.rh)。
- テスト: [tests/net.py](../tests/net.py)（ループバックで TCP の往復・UDP の往復・遅延の順序・`Pending`・`Closed`）。

## 待ちは id を yield する

コルーチンは、待つものの id を `yield^` して止まります。`Loop` は id と準備のできた事象を突き合わせて、そのコルーチンを再開します。
L^ の `sample/async.lh` と同じ形で、waker を持たない Goose の形（待つものを値で伝える）とも合います。

```rhat
let^async = import^std.async

let^tick = p^name:string^ {
    var^k:i64^= 0
    repeat^while^k < 3 {
        await^async.delay(0.1)      # yields a timer id; the Loop resumes it when due
        print^(name, k)
        k += 1
    }
    return^nil^
}
```

| 呼び出し | 答え |
| --- | --- |
| `async.timer(seconds)` | `i64^`（その時間が経つと準備のできる待ち） |
| `async.readable(socket)` / `async.writable(socket)` | `i64^`（ソケットが読める・書ける、または閉じたら準備のできる待ち） |
| `async.wait(timeout)` | `i64^ \| nil^`（準備のできた待ち。`timeout` 秒待っても無ければ `nil^`） |
| `await^ async.delay(seconds)` | — |

- 待ちの表はスレッドごとです（`_Thread_local`）。thread-per-core のプログラムでは、ワーカーごとに `Loop` を回します。
- `wait` は期限の来たタイマーのうち一番早いものを先に答えます。ソケットは `poll`（Windows は `WSAPoll`）で待ちます。
- id は 1 から配ります。準備ができて答えた待ちは表から消えます。答える前にコルーチンが捨てられた待ちは表に残ります（v1）。

## Loop

```rhat
var^a = tick("a")
var^b = tick("b")
var^loop = async.Loop<nil^>{}
loop.add(ref^a)
loop.add(ref^b)
loop.run()
```

- `Loop<T>` は `dynamic^ std.sys.CoroutineNR<i64^, T>` の一覧を持ちます。フレームの実体は呼び出し側が持ち、Loop は参照するだけです。
- `run` は起きているタスクを `start`/`resume` し、出てきた id を待ちとして覚えます。全員が待っていれば `wait` で眠ります。
  0 を `yield^` したタスクは起きたままで、他のタスクに順番を譲るだけです。
- 終わったタスクは一覧から外れます。結果はフレームの持ち主が `result()` で読みます。
- `T` は各タスクの最終結果を受け止められる型です。フレームは、yield する型と最終結果の型が広がる先のコルーチン型を満たします
  （たとえば `Error.Reset | … | nil^` を返すフレームは `CoroutineNR<i64^, nil^ | net.Error>` を満たします）。

## std.net

すべてノンブロッキングのソケットです。各操作に、すぐ答える `try_` 形と、待ちを `yield^` する yieldable な形があります。

```rhat
let^worker = p^listener:ref^net.TcpListener {
    var^buf:u8^[1024] = {}
    repeat^{
        var^conn = try^await^listener.accept()
        repeat^{
            let^n = try^await^conn.read(buf[..])
            if^n = 0 { break^}
            try^await^conn.write(buf[0 .. n])
        }
    }
}
```

`conn`・`buf` はワーカー自身のローカルで、`await^` した内側のフレームに貸し出されます（[再貸し](coroutines.md#再貸し)）。

| 呼び出し | 答え |
| --- | --- |
| `net.listen(host, port)` | `TcpListener \| net.Error`（port 0 なら空いているものを選ぶ） |
| `await^ net.connect(host, port)` | `TcpStream \| net.Error` |
| `l.try_accept()` / `await^ l.accept()` | `TcpStream \| Pending \| Error` / `TcpStream \| Error` |
| `s.try_read(into)` / `await^ s.read(into)` | 読んだバイト数。0 は相手が閉じた |
| `s.try_write(data)` / `await^ s.write(data)` | 書けたバイト数 / 全部書くまで待つ |
| `net.udp()` | `Udp`（OS のソケットは最初の `bind`・`set_peer`・`send_to` でその族で作る。L^ と同じ） |
| `u.bind(host, port)`・`u.set_peer(host, port)`・`u.send(data)`・`u.send_to(data, ref^address)`・`u.set_broadcast(on)` | `nil^ \| Error` |
| `u.try_receive(into)` / `await^ u.receive(into)` | `Datagram{length, from}` |
| `net.Address.resolve(host, port)` | `Address \| Error`（`getaddrinfo`、同期。最初の答えを使う） |
| `a.port()`・`a.write_host(ref^out)` | ポート・数値表記のホスト |

- `Pending{wait}` は「今はできない」の答えで、`wait` を `yield^` すれば、できそうになったときに再開されます。`try_` 形で細かく制御するときに使います。
- `TcpStream`・`TcpListener`・`Udp` は `Disposable` で、スコープを出ると閉じます。値渡しはできないので、接続はそれを使うコルーチンの中で
  `accept` して持ちます（サンプルのように、ワーカーが listener を共有して順に接続を受けます）。
- 誤りは `net.Error`（`AddressInUse`・`Resolve`・`Unreachable`・`Refused`・`Reset`・`TooLarge`・`Closed`・`Failed`）。
  閉じたソケットへの操作は `Closed` です。
- POSIX では書き込みに `MSG_NOSIGNAL`（または `SO_NOSIGPIPE`）を使い、SIGPIPE を起こしません。Windows の UDP は `SIO_UDP_CONNRESET` を切ります。
- UDP の送信バッファが一杯で送れないデータグラムは、UDP が落としてよいものとして誤りにしません（L^ と同じ）。

## 実装の置き場

- [runtime/rhat_net.c](../runtime/rhat_net.c): ソケットと待ちの表。ハンドルは OS のソケット + 1（0 は「無い」）、負の値は誤りの種類か `Pending`（-100）。
- [stdlib/std/async.rh](../stdlib/std/async.rh)・[stdlib/std/net.rh](../stdlib/std/net.rh): `externC^` で上を呼ぶ R^ の層。
- `std.net` か `std.async` を読み込んだプログラムだけ、`rhat_net.c` をコンパイルしてリンクします（Windows は `ws2_32`）。`std.random` と同じ扱いです。

## L^ との差

- TCP があります（L^ は UDP のみ）。
- L^ の `receive` は待たずに `nil^` を答えますが、R^ は `try_receive` が `Pending` を答え、`await^ receive` で待てます。
- `Udp` を作る `udp()` は誤りを返しません（ソケットは最初の宛先で作るので、作る時点で失敗し得るのはそちら）。
- `send_to` は解決済みの `Address` を取ります。名前から送るときは `Address.resolve` を先に呼びます。

## 今後

- 待ちの取り消し（捨てたコルーチンの待ちを表から外す）。
- `epoll`・`kqueue`・IOCP。インターフェースは変わりません。
- Channel の `await^ ch.take()` と `std.task`（[coroutines.md](coroutines.md) の手順 5）。
- 再貸しの入れ子（受け取った参照をさらに `await^` に渡す）。これができると `read_exact` のような補助を重ねられます。
