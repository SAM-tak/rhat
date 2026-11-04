# 列挙体（enum^）

`enum^` は整数の定数に名前を付けた、閉じた集合の型です。C の `enum` と同じ配置で、`exportC^` / `externC^` の
境界でもそのまま整数として渡せます。

```rhat
enum^ Mode { Idle, Walk, Dash = 10, Stop }     # Idle=0, Walk=1, Dash=10, Stop=11
enum^ Small : u8^ { A, B = 255 }               # 基底型を指定
enum^ Sign : i16^ { Minus = -1, Zero, Plus }

let^ m = Mode.Dash
print^(m, m = Mode.Dash, i32^(m))              # Dash true 10
```

## 宣言

- 名前は必須で、公称型です。同じ綴りの別の宣言とは別の型になります。`public^` で公開できます。
- 基底型は `: u8^` のように built-in の整数型（128 ビットを除く）で指定します。省略すると `i32^` です。
- メンバの値は整数リテラル（`0x` / `0o` / `0b`、`_` 区切り、負数を含む）です。書かないメンバは直前の値＋1
  （先頭は 0）になります。
- メンバ名の重複、メンバ値の重複、基底型に収まらない値、メンバが無い宣言はエラーです。
- メンバは `Mode.Idle` と書きます。実体は `Mode.Idle` という名前のモジュール定数で、型は `Mode` です。

## 演算と変換

- `=` と `!=` で比べられます。順序比較（`<` など）と算術は使えません。
- 基底型の値が求められる所では、暗黙に基底型の整数になります。代入・初期化・引数・戻り値・配列要素と、
  基底型の値との二項演算です。`enum^ Block : u8^` なら `let^ b:u8^ = Block.Lamp`、`cells[i] > Block.Lamp`
  （`cells:u8^[]`）が書けます。基底型以外の整数型へは暗黙に変換しません（`i32^(m)` と明示します）。
  enum 同士の順序比較と算術、別の enum との比較はこれまでどおりできません。
- 整数から enum への変換は `Mode(n)` です。`n` は基底型の値に限り（`Block(u8^(i))`）、整数リテラルは
  基底型として読みます。値が一致するメンバか、どのメンバとも一致しなければ `std.sys.Error.CastFail` を
  返します（型は `Mode | std.sys.Error.CastFail`）。
  `catch^` / `try^` で処理してから使います。`std.sys` は `enum^` を宣言したモジュールが見えない形で import します。

  ```rhat
  let^ block_at = f^ i:i64^ -> Block { Block(cells[i]) catch^ Block.Air }
  ```

  列挙体の値は宣言されたメンバに限られるので、網羅検査が成り立ちます。
- `print^` はメンバの名前を書きます（C から渡された、メンバにない値は数値を書きます）。

## パターンマッチの網羅

`for^ m { when^ Mode.Idle: ... }` の腕でメンバをすべて名指しすれば、`other^` は要りません。
一つでも欠けると、式形はエラーです。文形は網羅を検査せず、当たらなければ何もしません。

```rhat
let^ weight = f^ m:Mode -> i32^ {
    for^ m: when^ Mode.Idle: 1 when^ Mode.Walk: 2 when^ Mode.Dash, Mode.Stop: 3;
}
```

網羅の規則は [構文](syntax.md#パターンマッチの網羅) にまとめています。

## C との境界

- `exportC^` / `externC^` の引数と結果に使えます。C 側では基底型の整数（既定は `int32_t`）です。
- C が列挙体にないメンバ値を渡すと、`print^` は数値を書き、比較はどのメンバとも一致しません。
  網羅した式形の `other^` なしの match でそれに当たると、`match fell through` で終了します。
- `exportL^` では使えません（L^ は任意の数を渡せるため）。

## 未対応

- メンバを型として使うこと（`fits^ Mode.Idle`）、`.value`、`.tostring()`。
- `private^` なメンバ、メンバごとの付随データ（それは `errordef^` の領域です）。
