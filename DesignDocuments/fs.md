# ファイル入出力（std.fs）

ファイル全体の読み書きです。ここの関数は [File とストリーム](io.md) の上の便利な入口です。
開く・読む・書く・閉じる・消すはランタイム（`rhat_runtime.c`）の `rh_fs_*` が行います。
パスは UTF-8 で、Windows では UTF-16 に変換して `_wfopen_s` / `_wremove` で開くので、日本語などのパスも使えます。

```rhat
let^fs = import^std.fs

do^{
    var^wad = try^fs.read("freedoom1-e1m1.wad")              # 全体を新しい配列で受け取る
    try^fs.read("more.wad", ref^ wad)                        # 既存の配列の末尾に追加

    fs.write("out.bin", wad[0 .. 16])                        # 作成または置換
    fs.append("log.bin", wad[0 .. 4])                        # 末尾に追加（無ければ作成）
catch^:
    print^("cannot read")
}
```

## 関数

- `read(path:string^) -> u8^[grow^] | Error.OpenFailed | io.Error`:
  ファイル全体を新しい配列で返します。配列の所有者は呼び出し元です（所有値は呼び出し元の保存先に直接構築します）。
- `read(path:string^, out:ref^ u8^[grow^]) -> nil^ | Error.OpenFailed | io.Error`:
  ファイル全体を `out` の末尾へ追加します。失敗したときは途中まで入っていることがあります。
- `write(path, data:u8^[..]) -> nil^ | Error.OpenFailed | io.Error`: 作成または置換します。
- `append(path, data:u8^[..])`: 末尾へ追加します。戻り値は `write` と同じです。
- `exists(path) -> bool^`: 読み取りで開けるか。
- `delete(path) -> bool^`: 削除できたか。

`data` には `a.bytes`（[配列のバイト列表示](arrays.md#配列のバイト列表示)）で数値配列も渡せます。

## 制約

- NUL を含むパスや UTF-8 でないパスは開けません（`OpenFailed`）。
- 書き込みは直接で、一時ファイル経由の原子的な置換ではありません。
- ディレクトリの操作・ストリーム読み書き・標準入出力は未対応です。
