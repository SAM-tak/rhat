# 入出力とストリーム

`std.io` はストリームの抽象、`std.fs` はファイルです。オブジェクトのメソッドで読み書きするのが基本で、
`fs.read(path)` などはその上の便利な入口です。

```rhat
let^fs = import^std.fs
let^io = import^std.io

do^{
    var^file = try^fs.open("a.txt")        # どの経路で抜けても閉じる
    var^data = u8^[grow^]{}
    try^file.read_all(ref^data)
catch^:
    print^("failed")                       # open の失敗も read の失敗も受ける
}

# 型ごとに具体化する
let^copy = p^src:ref^io.Reader, dst:ref^io.Writer { ... }
# 実行時に型を混ぜる
let^log = p^out:ref^dyn^io.Writer, s:string^{ try^out.write(s.bytes) }
```

## ストリーム

```rhat
public^def^Reader {
    abstract^read:p^self^, out:ref^u8^[grow^], max:i64^-> i64^ | Error;
    read_all = ...
}
public^def^Writer {
    abstract^write:p^self^, data:u8^[..]-> nil^ | Error;
    flush = ...
}
public^def^Stream : Reader, Writer {}
```

`read` は最大 `max` バイトを `out` の末尾へ追加し、その数を返します。0 は終端です。
新しい種類のストリームは、これらを合成して `read`・`write` を書くだけです。通信ストリームも同じ形で足せます。

`io.MemoryReader.over(bytes)` はバイト列のビューを読み、`io.MemoryWriter` は書かれたものを集めます。

### よくある形と違う点

R^ の制約に合わせています。

- **`read(buf:u8^[..])` ではない**: スライスは読み取り専用で、R^ で書いたストリームから書き込めません。伸長配列へ追加する形なら、ファイルもメモリも同じように書けます。読み直すバッファは `clear()` して再利用します。
- **`dyn^` 経由で所有値を返せない**: 基本の操作は `out` 引数です。`-> u8^[grow^]` の便利なメソッドは静的な呼び出しでだけ使えます。
- **所有する `dyn^`（`Box<dyn Trait>`）はない**: `fs.open` は具体型 `File` を返します。「設定でファイルかメモリかを選んで返す」関数は合併型 `File | io.MemoryReader` で返します。
- **参照を保持するメソッドは `dyn^` に載らない**（[引数をまたぐ参照の流れ](design.md)）: `write` は渡されたデータをコピーし、保持しません。

## File

`fs.open(path)`・`fs.create(path)`、または `File.for_appending(path)` で開きます。`-> File | fs.Error.OpenFailed` です。

`File` は `read`・`write`・`flush`・`close` を持つ [`Disposable`](dispose.md) で、スコープの終わりに閉じます。**コピーできません。**
閉じた後の使用や二重の `close` は `io.Error.Closed` になります。書き込みの失敗を確かめたいときは `close()` を明示的に呼びます。その後の解放は何もしません。

複数のファイルを持つなら `File[grow^]` に入れ、ほかからは `ref^` で持ちます。`HashMap<string^, ref^fs.File>` のように書きます。

`File` はバッファを持ちません。持つと所有値になり、開いている間データスタックを一本使い、再帰の中で保持できなくなるからです。バッファリングは C の `FILE` に任せます。
