# C 関数の呼び出し（externC^）

```rhat
externC^ hypot:f^ x:f64^, y:f64^ ->f64^;
externC^ frexp:p^ x:f64^, exponent:ref^ i32^ ->f64^;
externC^ memset:p^ s:u8^[:], c:i32^, n:u64^;
externC^ puts:p^ s:string^ ->i32^;

var^ exponent:i32^=0
print^(hypot(3.0, 4.0), frexp(8.0, ref^ exponent), exponent)   # 5 0.5 4
```

`externC^ 名前:署名;` は、本体のない関数を C の関数へ結び付けます。名前がそのまま C のシンボル名になり、
呼び出しは C の呼び出し規約による直接呼び出しになります。データスタックの位置や結果の書き込み先は渡しません。

R^ 側の名前とシンボル名を分けるときは、関数リテラルの代わりに `externC^ シンボル:署名;` を束縛します。
裸の `externC^ 名前:署名;` は、シンボル名と同じ名前で束縛する省略形です。

```rhat
public^ let^ sqrt = externC^ sqrtf:f^ x:f32^ ->f32^;
public^ overload^ let^ sqrt = externC^ sqrt:f^ x:f64^ ->f64^;
```

トップレベルの関数と同じく、同じ名前の二つ目以降の宣言には `overload^` を付けます（[オーバーロード](overloads.md)）。

署名は関数リテラルと同じく `f^` / `p^`、名前付きの引数、`->` の後の結果型で書き、末尾の `;` で終わります。
すべての引数に型注釈が必要です。結果を省略すると C の `void` です。`template^` と本体は書けません。
どちらの形もトップレベルにだけ置けます（関数本体の中や式の途中には書けません）。ライブラリのモジュールにも書けます。`public^` を付けると他のモジュールへ公開します。
通常の関数と同じく名前解決とオーバーロードに参加します。

C 以外の呼び出し規約が必要になった場合は、文字列で規約を指定する形ではなく、
`externSystem^` のようなハット識別子を追加します。

## 渡せる型

- 引数・結果: `bool^`、`i8^`〜`i64^`、`u8^`〜`u64^`、`f32^`、`f64^`。`bool^` と 8・16 ビット整数は C と同じく拡張して渡します。
- 引数・結果: フラットな `def^`（スカラー・フラットな `def^`・それらの固定長配列だけをフィールドに持つ、空でない構造体）の値。
- 引数・結果: スカラー・フラットな `def^`・不透明な C 型への `ref^ T` はそのポインタ。`ref^ T | nil^` は NULL になりうるポインタで、
  NULL は `nil^` と相互に変換します。C が返した参照の寿命は検査できないため、C 側の約束を信頼します。
- 引数・結果: `externC^ f^ T, U -> R;` 型は C の関数ポインタ（[下記](#c-の関数ポインタ)）。`| nil^` で NULL を許します。
- 引数だけ: スカラーかフラットな `def^` のスライス `T[:]` と `string^` は先頭要素へのポインタ。

スライスと文字列の長さは渡しません。C が長さを必要とするなら別の引数で明示します。
NUL 終端を保証するのは文字列リテラルだけです。部分スライスなど、それ以外の終端は呼び出し側の責任です。

```rhat
def^ LLDiv { self^{ quot:i64^ = 0, rem:i64^ = 0 } }
externC^ lldiv:f^ n:i64^, d:i64^ -> LLDiv;
let^ q = lldiv(17, 5)            # q.quot = 3, q.rem = 2
```

### 構造体の値渡しと C の中継コード

LLVM IR は構造体を C の ABI どおりに渡す処理をしないため、`def^` を値で受け取るか返す外部関数には、
コンパイラが C の中継関数 `rh_shim_<シンボル>` を生成します。R^ 側は構造体をポインタで渡し、
中継関数が C の呼び出し規約どおりに本物の関数を呼びます（ABI の処理は Clang に任せます）。
スカラーだけの外部関数は従来どおり直接呼びます。

中継コードは各構造体を C の構造体として定義し、`_Static_assert` で大きさ・アラインメント・各フィールドの位置が
R^ の配置と一致することを C のコンパイル時に検証します。ヘッダーは読み込まず、外部関数の宣言も
R^ の署名から作ります（ポインタは型なしの `void *`）。中継コードはオブジェクトのキャッシュに置いて Clang でコンパイルし、
`rhat build` / `rhat run` がリンクします。`rhat emit` の単一 IR には含まれません。

### C のヘッダーと型（includeC^ / externC^ def^）

```rhat
includeC^ "lhat.h"                                          # このモジュールの中継コードが読むヘッダー
externC^ def^ LhatMachine;                                  # 不透明な C 型: ref^ としてだけ使える
externC^ def^ LhatValue { self^{ as:u64^ = 0, tag:i32^ = 0 } } # C の LhatValue と同じ配置
externC^ lhat_integer:f^ i:i64^ -> LhatValue;               # static inline 関数も呼べる
externC^ lhat_machine_new:p^ -> ref^ LhatMachine | nil^;
```

- `includeC^ "file"` を書いたモジュールの `externC^` は、すべてそのヘッダーを読む中継コード経由で呼びます。
  ヘッダーのプロトタイプが本物の宣言になるため、ヘッダーにしか実体のない `static inline` 関数も呼べ、
  ポインタと整数の取り違えや存在しない関数は C コンパイラが検出します（スカラー同士の暗黙の変換は C の規則どおり通ります）。
  ヘッダーはモジュールのディレクトリと `--c-include DIR` から探します。`public^` にはできません。
- `externC^ def^ Name { ... }` は同名の C 型と同じ配置の構造体です。中継コードが R^ のフィールド名で、
  構造体の大きさ・アラインメントと各フィールドの位置・大きさを `_Static_assert` で検査します。
  C の共用体のフィールドは、同じ大きさのスカラー（`u64^` など）として宣言できます（中身は C の補助関数で扱います）。
- `externC^ def^ Name;` は不透明な C 型です。`ref^ Name` / `ref^ Name | nil^` としてだけ使え、構築・値としての保持はできません。
- ヘッダーを読むモジュールで構造体を値で受け渡すには、その型を `externC^ def^` で宣言する必要があります。

固定長配列の値渡し、合併型・文字列・参照を含む構造体（C の共用体はスカラーとして宣言）、`i128^` / `u128^`、
`f8^` / `f16^`、スライスの結果、可変長引数は未対応です。

## C から R^ を呼ぶ（exportC^）

```rhat
exportC^ let^ r_twice = f^ x:i64^ -> i64^ { x * 2 }       # C からは int64_t r_twice(int64_t)
exportC^ let^ r_mid = f^ a:Point, b:Point -> Point { ... } # 構造体の値渡しも可
```

- `exportC^` を付けたトップレベルの関数は、宣言した名前（モジュールの修飾を除く）の C 関数として呼べます。
  引数と結果は `externC^` と同じ C の形です。C は長さを渡さないため、スライスと文字列は引数にできません。
  `f^` は結果型の注釈が必要です。`template^`・可変長・メソッド・オーバーロード・既存の C シンボルとの重複は拒否します。
- C 側の関数は中継コードに生成するラッパーで、C の ABI は Clang が処理します。ラッパーは LLVM 側の入口
  `rh_entry_<名前>` を呼び、入口が R^ の具体化を呼びます。

### データスタックの受け渡し

R^ から `externC^` を呼ぶ直前に、呼び出し元がその時点で使っているデータスタックの次の番号を
ランタイムの `rh_callback_sp` に記録し、戻ったら元に戻します。`exportC^` の入口はそこから上のスタックを使うため、
中断中の R^ の呼び出しの所有値と衝突しません。

- スタックの総数は、すべての C 呼び出しの位置から `exportC^` 関数が 1 段呼び返されても足りるように計画し、
  さらに 7 段分を足します（スタックは予約した仮想アドレスで、触れたページだけが物理メモリを使います）。
- 入れ子がそれを超えると、入口が「C callbacks nest deeper than the data stacks allow」と診断して終了します。
  呼び出し元が C を呼ぶ時点で所有値を抱えていなければスタックを消費しないため、何段でも入れ子にできます。
- `rh_callback_sp` はスレッドごとではありません。`exportC^` 関数は、R^ を実行しているスレッドから、
  R^ が呼んだ C 関数の中で呼ばれる場合に限ります。

### C の関数ポインタ

```rhat
externC^ qsort:p^ base:i32^[:], n:u64^, size:u64^, compare:externC^ f^ ref^ i32^, ref^ i32^ -> i32^;;
exportC^ let^ descending = f^ a:ref^ i32^, b:ref^ i32^ -> i32^ { *b - *a }
qsort(xs[0 :: 5], 5, 4, descending)        # 名前を呼ばずに書くと C のアドレス

externC^ lhat_program_new:p^ strict:bool^,
    load:externC^ p^ ref^ LhatContext | nil^, string^, ref^ u64^ -> ref^ u8^ | nil^;,
    context:ref^ LhatContext | nil^ -> ref^ LhatProgram;
let^ program = lhat_program_new(true^, lhat_load_file, nil^)
```

- 型の位置で関数の署名（`f^ ... ;` / `p^ ... ;`）の前に `externC^` を付けると C の関数ポインタ型になります。
  中身はアドレスだけなので、`let^` に束縛したり引数として渡したりできます。
- この型の値になれるのは C の ABI を持つ関数、つまり `externC^` 宣言と `exportC^` 関数だけです。通常の R^ 関数は
  データスタックの隠れた引数を取り、捕捉も持ちうるため C のアドレスを持ちません。
- 署名（`f^` / `p^`、引数と結果の型）は完全に一致する必要があります。C 側のポインタ型（`void *`・`char *` など
  R^ に無い型を含む）とは照合せず、中継コードでは `void *` として渡します。署名の正しさは `externC^` 宣言と同じく信頼境界です。
- `exportC^` 関数を渡した場合も、呼ばれてよいのは R^ が呼んでいる C 関数の中からだけです
  （[データスタックの受け渡し](#データスタックの受け渡し)）。C 側が保存して後から別スレッドで呼ぶ使い方は検査できません。
- R^ から関数ポインタを呼ぶことはまだできません。

## L^ に R^ を公開する（exportL^）

```rhat
includeC^ "lhat.h"
externC^ def^ LhatProgram;
externC^ rhat_lhat_register:p^ program:ref^ LhatProgram, module:string^ -> bool^;
exportL^ let^ add = f^ a:i64^, b:i64^ -> i64^ { a + b }      # L^ からは rhat.add(40, 2)
exportL^ let^ greet = f^ name:string^ -> string^ { name }
exportL^ let^ shout = p^ text:string^ { print^("R^ says:", text) }

rhat_lhat_register(program, "rhat")    # lhat_program_check より前に
```

- `exportL^` を付けた関数ごとに、中継コードが L^ の `LhatHostFn` ラッパー `rh_lhat_<名前>` を生成し、
  `bool rhat_lhat_register(LhatProgram *program, const char *module)` が全部を `lhat_register_func` で
  `module` の下に登録します。R^ からは上のように `externC^` で宣言して呼びます。
- 署名は R^ の型から決まります。数値型は `number^`、`bool^` は `bool^`、`string^` は `string^`、
  `exportL^ def^` は `<module>.<名前>` です（例: `f^number^, number^ -> number^;`）。
  それ以外の型は引数にも結果にもできません。
  `f^` / `p^` と結果型の注釈、`template^` などの制限は `exportC^` と同じです。
- 整数型の引数に整数でない数が渡ると「exportL^ NAME needs a whole number」で終了します。浮動小数点型は
  整数も受け取ります。文字列の引数は呼び出しの間 L^ の文字列を借り、結果の文字列は
  `lhat_machine_make_string` で L^ 側に複製します。
- 呼び出しは `exportC^` と同じ入口を通るため、L^ の `lhat_run` を R^ から呼んでいる間に限ります
  （データスタックの受け渡しは上と同じ）。
- `exportL^` には `includeC^ "lhat.h"` を書いたモジュールが必要です。L^ のライブラリは MSVC で
  `/MD`（DLL 版 CRT）でビルドされるため、Windows でリンクするときは `--crt-dll` を付けます。

### 値として渡す型（exportL^ def^）

```rhat
exportL^ def^ Vec3 {
  self^{ x:f32^ = 0.0, y:f32^ = 0.0, z:f32^ = 0.0 }
  length = f^ self^ -> f64^ { ... }                 # L^ からは v.length()
}
exportL^ let^ sum = f^ a:Vec3, b:Vec3 -> Vec3 { ... } # L^ からは rhat.sum(a, b)
```

- `exportL^ def^` は L^ の host value 型（`lhat_register_hostvalue_type`）として登録します。L^ はその値を
  バイト列としてスタックに持ち、寿命の管理はありません。フラットな `def^` で、248 バイト以下に限ります。
- `f32^` / `f64^` / `i8^`〜`i64^` / `u8^`〜`u32^` のフィールドは `lhat_register_hostvalue_field` で登録し、
  L^ から `v.x` で直接読み書きできます。`bool^`・`u64^`・入れ子の `def^`・配列・`private^` のフィールドは
  L^ からは見えず、メソッドを通して扱います。
- `private^` でないメソッドはすべて `lhat_register_hostvalue_member` で登録します（受け手は `self^`）。
  署名は関数と同じ規則で、L^ は値の複製を渡すため `mutable^ self^` のメソッドは拒否します
  （L^ から外すには `private^` にします）。
- 引数の値は L^ のバイト列を複製して受け取り、結果は `lhat_make_hostvalue` で返します。
  別の host value 型が渡されると「exportL^ received another host value type」で終了します。
- 参照として渡す型（`exportL^ ref^ def^`、L^ の host data）は未対応です。

## 効果と安全性

`f^` の宣言は副作用がないことの約束です。引数がすべてスカラーの `f^` 宣言には LLVM の `memory(none)` を付けるため、
同じ引数での呼び出しはまとめられたり省かれたりします。状態を持つ関数や、出力を伴う関数は `p^` で宣言します。
通常の関数と同じく、`f^` から `p^` の宣言は呼べません。

C は受け取ったポインタを通して書き込みうるものとして扱い、呼び出し後は引数の参照元について絞り込んだ事実を捨てます。
C が借りた引数を呼び出し後も保持すると、データスタックの再利用で宙づりになります。これは R^ から検査できないため、
`externC^` 宣言そのものを信頼境界とします。

## シンボル

シンボルは ASCII の C 識別子に限ります。`main` は使えません。
`rh_` で始まるシンボルはランタイム用で、標準ライブラリ（`std.*`）だけが宣言できます。
同じシンボルを複数の場所で宣言できますが、署名（`f^` / `p^` を含む）が一致している必要があります。
LLVM の宣言は IR ごとに一度だけ出力します。

## リンク

C 標準ライブラリと、Windows 以外での `libm` は常にリンクします。その他のライブラリは
`rhat build` / `rhat run` の `--link-lib NAME` で指定します（`-lNAME` として Clang に渡します。複数指定可）。
パス（`/` や `.lib` などを含むもの）はそのまま渡します。
Windows で DLL 版 CRT（`/MD`）でビルドされたライブラリをリンクするときは `--crt-dll` を付けます。

## 標準ライブラリでの利用

`std.random` は `nextafter` と OS の乱数取得（`rh_random_seed`）を非公開の `externC^` 宣言で呼びます。
以前の専用の組み込み関数（`__sqrt^` など）は廃止しました。

標準ライブラリは同じ形の `externLLVM^ llvm.名前:署名;` で LLVM 組み込み関数も束縛できます（`std.math` が使用）。
利用者のコードからは使えません。
