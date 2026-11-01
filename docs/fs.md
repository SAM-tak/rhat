# ファイル入出力（std.fs）

ファイル全体の読み書きです。開く・読む・書く・閉じる・消すはランタイム（`rhat_runtime.c`）の `rh_fs_*` が行います。
パスは UTF-8 で、Windows では UTF-16 に変換して `_wfopen_s` / `_wremove` で開くので、日本語などのパスも使えます。

```rhat
let^ fs = import^ std.fs

var^ wad:grow^ u8^[] = []
let^ loaded = fs.read("freedoom1-e1m1.wad", ref^ wad)       # 全体を wad の末尾に追加
for^ loaded { when^ fits^ fs.Error: print^("cannot read") other^: print^(wad.len) }

fs.write("out.bin", wad[0 .. 16])                            # 作成または置換
fs.append("log.bin", wad[0 .. 4])                            # 末尾に追加（無ければ作成）
```

## 関数

- `read(path:string^, out:ref^ grow^ u8^[]) -> nil^ | Error.OpenFailed | Error.ReadFailed`:
  ファイル全体を `out` の末尾へ追加します。失敗したときは途中まで入っていることがあります。
- `write(path, data:u8^[:]) -> nil^ | Error.OpenFailed | Error.WriteFailed`: 作成または置換します。
- `append(path, data:u8^[:])`: 末尾へ追加します。戻り値は `write` と同じです。
- `exists(path) -> bool^`: 読み取りで開けるか。
- `delete(path) -> bool^`: 削除できたか。

`data` には `a.bytes`（[配列のバイト列表示](arrays.md#配列のバイト列表示)）で数値配列も渡せます。

## 制約

- NUL を含むパスや UTF-8 でないパスは開けません（`OpenFailed`）。
- 書き込みは直接で、一時ファイル経由の原子的な置換ではありません。
- ディレクトリの操作・ストリーム読み書き・標準入出力は未対応です。
