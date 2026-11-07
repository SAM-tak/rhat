# HashMap と HashSet

`std.collections` は `HashMap<K, V>` と `HashSet<T>`、`std.hash` はそのハッシュ関数を提供します。

```rhat
let^c = import^std.collections
import^std.text

var^pool = std.text.Builder{}
var^keys:c.HashSet<string^> = {}
var^counts:c.HashMap<string^, i32^> = {}
let^word = c.intern(ref^ keys, ref^ pool, "the")
counts.set(word, (counts.get(word) ?? 0) + 1)
print^(counts.get("the") ?? 0)
```

## API

`HashMap<K, V>`:

- `set(key, value)`: 追加または上書き。
- `get(key) -> V | nil^`: 値のコピー。
- `get_mut(key) -> ref^ V | nil^`: 値への参照。
- `has(key) -> bool^`・`remove(key) -> bool^`・`len() -> i64^`・`clear()`
- `stored_key(key) -> K | nil^`: 保持しているキー。`string^` なら保存済みの本体を指すビュー。
- `for^k, v in^ m`: キーと値のコピーを回します。削除するまでは挿入順です。
- `items() -> read^Entry<K, V>[..]`: 全エントリのスライス。`key`・`value` を読みます。結果を局所変数に持つ間は map を変更できません。

`HashSet<T>` は `add(value) -> bool^`（新規なら `true^`）・`has`・`get`・`remove`・`len`・`clear`・`items` を持ち、`for^v in^ s` で回せます。
中身は `HashMap<T, bool^>` です。

## 文字列のキー

`string^` はビューなので、キーの本体は呼び出し側が持ちます。Goose の `dictionary<u8[:], V>` と同じ形です。
本体はキーを持つ map より長生きする必要があり、[引数をまたぐ参照の流れ](design.md)の検査で保証します。

```rhat
var^m:c.HashMap<string^, i32^> = {}
do^{
    var^b = std.text.Builder{}
    m.set(b.add("x"), 1)     # 誤り: reference would outlive its owner
}
```

本体の置き場には `std.text.Builder` を使えます。`add(s) -> string^` は追記した部分のビューを返し、`grow^` の領域は動かないので、
追記を続けても有効です。本体には区切りがなく、境界はビューだけが知っています。

**重複の排除**は `intern(keys, pool, s)` で行います。`keys` にあれば保存済みのビューを返し、なければ `pool` に追記します。
同じ文字列の本体は一つだけになります。集合と pool は別の変数です。所有フィールドは構造体に一つだけなので、一つの型にまとめられません。

`map.set(pool.add(s), v)` は、キーが既にあっても pool に追記します。pool を増やしたくないときは `intern` を通すか、先に `has` で確かめます。

本体を書き換えてはいけません。`pool.clear()` は参照が残っていても拒否されないので、キーの中身が変わり、保存済みのハッシュと食い違います。
メモリ安全性は保たれますが、検索結果は不定です。

## 内部表現

一本の `grow^` 配列だけで表の全体を持ちます（連鎖法）。

```text
entries: [key, value, code, head, next] ...
```

- エントリは先頭から隙間なく並びます。空きスロットがないので、キーや値に既定値を要求しません。
- バケット `b` の先頭のエントリ番号は、`entries[b].head` に置きます。バケット数は2の累乗で、常にエントリ数以下なので、必ず置き場所があります。
- 挿入は末尾に追加して連鎖に繋ぎます。負荷率が3を超えるとバケット数を倍にし、繋ぎ直します。別の配列は要りません。
- 削除は末尾のエントリを空いた位置へ移し、配列を縮めます。`head` は位置に属するので移しません。
  エントリ数がバケット数を下回ると半分にします。この閾値の差で、境界での再構築の連発を防ぎます。
- ハッシュの32ビットを `code` に持ち、比較と再構築で再計算しません。

`grow^` の領域は動かないので、`get_mut` の参照は `set` をまたいでも有効です。ただし `remove` は別のエントリをその位置へ移します（メモリ安全ですが、別のキーの値が見えます）。
`clear` や `remove` で領域は返しません。`flex^` は参照を保持している間は縮められないので使いません。

### 採らなかった形

- オープンアドレス（Goose の `dictionary`）: 未使用スロットに既定値が要り、拡張で要素が動くので参照が保てません。負荷率が低く、速度はこちらが有利な可能性があります。
- 索引の配列とエントリの配列の二本構成: 所有フィールドは構造体に一つだけなので作れません。

## ハッシュ

`std.hash.of(x)` は `for^x:` と `when^fits^T:` で型ごとに振り分けます。到達しない分岐は検査されないので、制約節は要りません。合併型は実行時のタグで振り分けます。

- `string^`（FNV-1a の後に混合）、整数（128ビットを除く）、`char^`、`bool^` は組み込みで扱います。
- それ以外は `x.hash()` を呼びます。利用者の型には `hash` メソッドと `op^=` を書きます。

```rhat
let^hash = import^std.hash
def^Point {
    self^{ x:i32^, y:i32^ }
    op^= = f^self^, other:Point -> bool^ { self^.x = other.x and^ self^.y = other.y }
    hash = f^self^ -> u64^ { hash.combine(hash.of(self^.x), hash.of(self^.y)) }
}
```

- 浮動小数点はキーにできません。`NaN` は自身と等しくなく、`-0.0` は `0.0` と等しいからです。
- 構造体の自動導出はしません。`.bytes` はパディングの内容が不定で、参照を含む型には使えません。
- シードは固定です。同じ入力なら実行ごとに同じ順序とハッシュになります。意図的な衝突攻撃（HashDoS）への対策はありません。

## 現在の制限

- 構造体に置ける map は一つで、末尾のフィールドだけです。固定容量版には `def^` の値パラメータが要ります。
- 値に伸長配列は入れられません。別の配列への添字を値にします。
- `op^[]` はないので、`m[k]` は書けません。
- `for^k, v in^ m` の値はコピーです。書き換えるには `get_mut` を使います。反復中の変更は文書上の禁止で、検査はしません。
- `enum^` のキーは未対応です。
