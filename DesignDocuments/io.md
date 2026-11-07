# 入出力とストリーム

`std.io` はストリームの抽象、`std.fs` はファイルです。オブジェクトのメソッドで読み書きするのが基本で、
`fs.read(path)` などはその上の便利な入口です。

```rhat
let^fs = import^std.fs
let^io = import^std.io

do^{
    with^file = try^fs.open("a.txt") {    # どの経路で抜けても閉じる
        var^data = u8^[grow^]{}
        try^file.read_all(ref^data)
    }
catch^:
    print^("failed")                       # 外側なので open の失敗も受ける
}

# 型ごとに具体化する
let^copy = p^src:ref^io.Reader, dst:ref^io.Writer { ... }
# 実行時に型を混ぜる
let^log = p^out:ref^dyn^io.Writer, s:string^{ try^out.write(s.bytes) }
```

## with^

`with^ h = e` を一つ以上並べ、必ずブロックを続けます。ブロックを抜けるときに、定義の逆順で `h.dispose()` を呼びます。

```rhat
with^r = try^fs.open("a.txt")
with^w = try^fs.create("b.txt") {
    try^copy(ref^r, ref^w)
}
```

- ブロックのない形はエラーです（`with^ needs a block`）。資源の寿命をブロックの形で見せるためで、C# の `using var` のような宣言形は採りません。複数の資源は並べて書けるので入れ子になりません。
- 通常の終了・`return^`・`break^`・`next^`・`try^` の伝播のすべてで呼びます。`panic^` と実行時エラーでは呼びません（巻き戻しはしません）。
- `dispose` は `p^self^` で、引数も戻り値もありません。**失敗しません。** 自動で呼ばれる位置には誤りを返す先がないからです。失敗しうる後始末は `flush()` や `close()` を明示的に呼びます。
- `h` は書き換えられません。`dispose` が別の値を見ることになるからです。名前のまま外へ持ち出す返却・代入も拒否します。ただし中のメソッドは `p^` でも呼べます。
- `f^` の本体には書けません。
- 見出しでは `if^` と同じく `R{...}` の構築は `{` と紛らわしいので、`(R{...})` と括弧で囲みます。

### catch^ との関係

L^ と同じです（L^ の `04-errors.md` の「どのブロックにも書ける」）。`with^` のブロックに書いた `catch^` は、
**波括弧の中の `try^` だけ**を受けます。見出しの `e` は波括弧の外なので受けません。

```rhat
do^{
    with^file = try^fs.open("a.txt") {     # 開けなければ外側の catch^ へ
        try^file.read_all(ref^data)        # 読めなければ内側の catch^ へ
    catch^:
        try^file.close()                   # file は開いたままで、見える
    }                                      # ここで dispose
catch^:
    print^("cannot open")
}
```

- 内側の `catch^` からは `with^` の名前が見えます。名前は見出しで束縛されるので、腕に来たときには必ず束縛済みです。ブロックの中で作った名前は見えません。
- `dispose` は内側の `catch^` の**後**です。
- 内側の `catch^` の中の `try^` は外へ伝わり、そのときも `dispose` してから移ります。
- 外側の `catch^` へ移るときは、束縛済みの資源をすべて `dispose` してからです。2つ目の `e` が失敗したら、1つ目だけを閉じます。

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

`File` は `read`・`write`・`flush`・`close`・`dispose` を持つ普通の値で、コピーできます。
ハンドルは `FILE*` ではなく、ランタイムの表の位置と世代です。閉じた後の使用や二重の `close` は `io.Error.Closed` になります。コピーも同じです。
位置が再利用されても世代が違うので、別のファイルを誤って操作することはありません。

`File` はバッファを持ちません。持つと所有値になり、開いている間データスタックを一本使い、再帰の中で保持できなくなるからです。バッファリングは C の `FILE` に任せます。
