# コピー不可の型 noncopy^

`noncopy^def^` の値はコピーできません。一時値を格納先へ直接入れるか、`ref^` で借ります。

```rhat
noncopy^def^Device {
    self^{ private^raw:i64^, private^closed:bool^ = false^ }
    close = p^self^ {
        if^self^.closed {
            print^("closed twice")
            return^
        }
        self^.closed := true^
        rh_device_close(self^.raw)
    }
    dispose = p^self^ { self^.close() }
}
```

## 目的

二重に解放するとクラッシュする資源を包む型の設計者の負担を減らします。

状態がコピーごとに分かれると、値の中のフラグでは二重解放を防げません。コピーを許す `fs.File` はランタイムのハンドル表で防いでいますが、
R^ のメソッドは大域の表を使えないので、同じ形を R^ だけでは書けません。
コピーを禁じれば値は一つに定まるので、フラグだけで足ります。`close` の後に `with^` の `dispose` が走っても安全です。

`dispose` を持つ型を自動でコピー不可にはしません。`File` のように共有しても安全な型まで制限してしまうからです。

## 規則

`noncopy^` は合成の基底から、また値として持つ側（フィールド・固定長配列・上限付き配列・合併）へ波及します。`ref^` とスライスは波及しません。

**許すもの**（名前を持たない一時値なので、ビットコピーしても他に残りません）:

```rhat
var^a = make()
a := make()
var^h = Holder{d = make()}
files.push(make())
files[0] := make()
with^b = try^open(path) { ... }
return^make()
```

**拒否するもの**（場所が値を持ち続けるので、コピーになります）:

- 変数・フィールド・要素・参照先からの束縛・代入・`push`・リテラルの要素・`return^`。
- 値引数と値の仮引数。`ref^` を使います。
- `repeat^`・`source...`・`append`。要素を複製します。
- `for^x in^xs`。`x` が要素のコピーになるので、添字で回します。

合併に入っている場合は、絞り込んだ名前がその場所を指します。`if^u fits^Device { u.close() }` は合併の中の値を操作します。
スライスの要素もその場所を読み取り専用で見せます。

## コンテナ

**配列が所有し、ほかからは `ref^` で持ちます。** `HashMap<K, Device>` は作れません。`set` の値引数と `get` の返り値がコピーになるからです。
`get` のたびにコピーを返す動作は `noncopy^` の型に合わないので、これは制約というより設計です。

```rhat
var^devices = Device[grow^]{}
var^names:c.HashMap<string^, ref^Device> = {}

devices.push(try^open(path))
names.set("main", ref^devices[devices.len - 1])
```

- 配列より長生きする map は `reference would outlive its owner` で拒否されます。
- `grow^` の領域は動かないので、`push` をまたいでも参照は有効です。
- ユーザー定義型も同じです。値で持つならリテラルに一時値を渡し、持たないなら `ref^` のフィールドにします。

## 制約

- **配列と map を一つの型にまとめられません。** どちらも所有領域を持つので、`intern` の `keys` と `pool` と同じく別の変数にします。
- **`pop` の後の参照:** 残った参照は次に `push` された要素を指します。メモリ安全ですが、別の資源を操作します。
  要素は閉じたまま残し、`files[i] := try^open(path)` で再利用する形を勧めます。空き番号の管理は利用者の仕事です。
- **解放は保証しません。** `with^` で受けない値、`pop` の結果を捨てた場合、`:=` で上書きされた古い値、配列の要素には `dispose` が走りません。
  資源は残りますが、二重解放は起きません。
- **名前付きの局所変数も返せません。** 一時値を返します。`var^f = open(); f.setup(); return^f` の形はムーブにあたるので見送りました。

ムーブは Goose の可変長配列の `move` が入った時点で、その実装を参考に改めて検討します。
