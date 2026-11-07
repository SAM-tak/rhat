# The Programming Language R^

![R^ logo](./media/rhat-logo.png)

R^ `(rhat)` は [L^](https://github.com/SAM-tak/lhat) の構文を基にした、GC を使わないシステム言語の実装です。

[Goose(プログラミング言語)](https://github.com/aardappel/goose)の強い影響を受けて誕生しました。
無注釈型安全を実現するアイデア（データスタックによるメモリモデル）はほぼ `Goose` からの借用です。

また**関数**と**手続き**を区別し、関数は関数型言語の関数を初等的な制御構文で書き下せることを目指して
設計され、関数とは別に手続きが存在することでモナドなどのような概念を持ち込むことなく副作用のある処理を
関数型言語の利点を損なうことなく自然に書ける言語、を目標に設計されています。

現在は **v0.6.0 の実行可能な言語サブセット**です。構文解析・名前解決・公称型・合成・テンプレート具体化・
型推論・寿命検査・データスタック計画・テキスト LLVM IR の生成から、
Clang によるネイティブ実行ファイルの生成までを実装しています。

コンパイラは C11 + CMake 製で、LLVM ライブラリはリンクしません。
R^ のソースを C に変換する処理もありません。生成した LLVM IR と小さな C ランタイムを
Clang でコンパイル・リンクします。Windows x86-64 で検証しています。

- [ゲームのサンプル実装](https://github.com/SAM-tak/rhat-gfx)
- [VSCode拡張（言語サポート）](https://marketplace.visualstudio.com/items?itemName=SAMtak.rhat)

## 特徴

### 予約語が存在しない

`if^` `else^` `while^` `import^` など、 R^ の構文キーワードは必ず末尾に `^` が付きます。（ハット識別子）  
奇妙に感じるでしょうが、これによって R^ ではいわゆる**予約語が存在しません**。  
数字から始まらず空白文字と記号以外で構成された名前は、すべてが自身のプログラム内の名前として使用可能です。

また言語設計の側から見ても、ユーザーの自由を奪う懸念なしに最もふさわしいと思うキーワードを追加できます。  
予約語を増やさないようにと既存のキーワードを別の文脈では別の意味で使い回す、というようなことを一切せずにすんでいます。  
`f^`（関数） `p^`（手続き）など、多用されるキーワードには（他の言語では許されないであろう）
極端に短い名前を割り当てる事ができていますし、Goose における `[>..]`（拡張する配列） `[>..<]`（伸縮する配列）
`[..10]`（最大数が10の配列）のような記号による記述を避け `[grow^]` `[flex^]` `[max^10]` のようなより覚えやすい
記法を採用できています。

`^if` と前置にしなかったのは `^` を前置した上でさらに空白がないとキーワードを区切れないこと（後置なら
`^` 自体が区切りになるので空白は必須ではありません）、インテリセンスでの補間で一番打鍵を省略したい `^` の打鍵が
必須となってしまうこと（後置なら `i` を打った時点で `if^` が候補に現れます）、の二点から後置が最適と判断しました。

### 関数型と手続き型が区別される混成関数型言語

R^ では、**関数**（`f^`）と**手続き**（`p^`）を別のものとして区別します。  
関数は値の計算や自分の局所的な状態の更新を行えますが、外部の状態を書き換えたり、
手続きを呼び出したりすることは型検査で禁止されます。  
手続きは状態変更や入出力などを担当し、関数と手続きの両方を呼び出せます。

こうすることで R^ の関数（`f^`）は関数型言語の関数のように参照透過性を持ち副作用による
予期しないバグの検討範囲から外すことができ、将来のアグレッシブな最適化を期待できます。
かつ、副作用を伴う処理は普通に手続きとして書き下せるので、モナドのような難解な概念を
必要とせずに関数型言語の恩恵を得ることが出来ます。

> ![NOTE]
> 既存の関数型言語では、関数に書けるのは式のみ、とすることで副作用が存在しないことを保証していますが、
> 式しか許されないとなると再帰呼び出しやパターンマッチングを駆使して望みの動作をする関数を組み立てる、
> ちょっとしたパズルを解くことを強制されます。  
> R^ の関数はローカル変数への書き換えしか許さない、というような追加の規則を導入することで、
> 通常の制御構文を使って、外から観測できる副作用のない関数型の関数、を書くことが出来ます。

R^ は非純粋関数型言語なのではなく手続き型とのハイブリッドなのです。

> ![NOTE]
> R^ の `f^`(関数) は C++ で言うところの const メンバ関数に近いです。C++ ではさらに参照/ポインターで渡される
> 引数の書き換えの許可・不許可も個別に `const T *arg` と `const` をつけるか付けないかで指定できるわけですが、
> R^ ではそれは過剰な自由度であるとみなし、const 関数 であり参照引数もすべて const である `f^` と、
> そうではない `p^` の２種類で十分である、という整理をしました。  
> `p^` に渡す参照引数は既定で mutable ですが、 `read^` を指定することで immutable にも指定できます。  
> Rust に例えるなら `f^` は `fn foo(&self)` の形、`p^` は `fn foo(&mut self)` に対応します。
>
> もっとも、R^ の `f^` はメンバメソッドだけでなく自由関数としても存在できるので、値を書き換えない操作として
> より強い保証を提供します。C++ の const メンバ関数、Rust の 既定でimmutable &self のメソッドも、
> 自身への変更だけを拒否して他のインスタンスには既定では何も保証しませんが、R^ ではあらゆる `p^` の呼び出しが
> 禁止されます。
>
> ゼロ注釈メモリ安全と並んで、初等的な記述で関数型言語の特徴を取り込むことは、 R^ の重要な目標です。

### 注釈無しでメモリ安全性を得られる

Goose のメモリモデルを採用することで、R^ もまた一切ライフタイムアノテーションを書くことなくメモリ安全性を達成しています。

### かつ、オブジェクト指向プログラミング言語

しかし、 Goose は最近のトレンドを汲んでオブジェクト指向プログラミング機能のサポートにやや消極的です。（メンバメソッド
という概念がなく、単に 第一引数の型が一致するなら、メソッド呼び出しの構文を採用することもできる、という程度に留まっている）

R^ ははっきりとオブジェクト指向プログラミング言語です。クラスベースではありませんがメンバメソッドを定義でき、演算子の
オーバーロードができ、順序付きミックスインと委譲によって安全で強力なオブジェクト指向機能を提供します。

```rhat
def^Vec2<T> {
    self^{ x:T, y:T }
    ZERO = Self^{ x = T.ZERO, y = T.ZERO }
    ONE = Self^{ x = T.ONE, y = T.ONE }
    RIGHT = Self^{ x = T.ONE, y = T.ZERO }
    LEFT = Self^{ x = -T.ONE, y = T.ZERO }
    UP = Self^{ x = T.ZERO, y = T.ONE }
    DOWN = Self^{ x = T.ZERO, y = -T.ONE }
    op^+ = f^self^, other:Self^-> Self^{
        Self^{ x = self^.x + other.x, y = self^.y + other.y }
    }
    op^- = f^self^, other:Self^-> Self^{
        Self^{ x = self^.x - other.x, y = self^.y - other.y }
    }
    op^* = f^self^, scale:T -> Self^{
        Self^{ x = self^.x * scale, y = self^.y * scale }
    }
    op^dot^= f^self^, other:Self^-> T {
        self^.x * other.x + self^.y * other.y
    }
}
```

## ビルドと実行

Windows では Visual Studio の C++ ビルド環境、Clang ツール（compiler-rt builtins を含む）、CMake、Python 3 が必要です。
次のスクリプトは PATH または Visual Studio インストール先から Clang を探します。

```powershell
cd C:\Users\Owner\source\repos\rhat
.\scripts\build.ps1 -Test
.\build\Release\rhat.exe run examples/layout.rh
.\build\Release\rhat.exe run examples/regions.rh
```

新しいターミナルでは、開発環境を読み込めます。
このスクリプトは現在のセッションの PATH に R^・Clang・MSVC を追加し、`INCLUDE`・`LIB` も設定します。
システムの永続的な環境変数は変更しません。`build.ps1` からも同じ設定を読み込みます。

```powershell
. .\scripts\devshell.ps1
rhat build examples/layout.rh -o build/layout.exe -O2
.\build\layout.exe
```

既定は Release 版です。Debug 版には `build.ps1 -Config Debug` / `devshell.ps1 -Configuration Debug`、Clang の指定には `-Clang PATH` を使えます。
テストは既定で実行ファイルを `-O2` だけでビルドします。`-O0` も確かめるには CMake に `-DRHAT_TEST_ALL_OPTS=ON` を渡します。
各テストスイートは全コアで並列に動くため、`ctest` に `-j` は付けません。

ビルド後に integration の型チェック中心の検証だけを行うには、次を使います。
`-Test` の追加は不要で、両方指定した場合もこちらが優先されます。

```powershell
.\scripts\build.ps1 -Test-ChecksOnly
```

局所修正では `ctest --test-dir build -C Release -R '^(inference|lsp)$' --output-on-failure`
のように関連スイートを選びます。integration のケースも、開発シェルで次のように絞れます。

```powershell
python tests/run.py --compiler build/Release/rhat.exe --clang "$env:RHAT_CLANG" --work build/tests --filter '^numeric_' --timings
```

`--checks-only` は正常系も `rhat check` で検査し、ネイティブビルド・実行結果・IR の検査を省きます。
局所修正ではこれと関連スイートを使い、仕上げに全スイートを実行します。
`--timings` は実時間と外部コマンドの回数・累積時間を表示します（並列実行のため累積時間は実時間を超えます）。
ベンチマークの境界値は `--filter '_edges$'` で選択でき、各ワークロードを一度ビルドして複数サイズを検証します。
整数除算と添字の型別ケースは `--filter '_widths$'` で選択でき、各群を一度ビルドして全型の出力とIRを検証します。

ビルド時に見つけた Clang のパスを実行ファイルに記録するので、別のターミナルでもそのまま使えます。
Clang の選択順は `--clang`、`RHAT_CLANG`、ビルド時に検出したパスです。
ビルド時に見つからなかった場合は PATH の `clang` を利用します。

一般的な CMake の手順も利用できます。単一構成ジェネレーターでは実行ファイルは
`build/rhat` に生成されます。POSIX 用ランタイムもありますが、この環境では未検証です。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### CI とリリース

GitHub Actions が、`main` への push とプルリクエストで全テストを実行します（Windows x86-64 が必須、Linux は試験的で失敗しても全体は失敗になりません）。
`v*` のタグを push すると、Windows x86-64 版をテストしたうえで、`cmake --install` の結果（`bin/rhat.exe`、`share/rhat/` のランタイム・標準ライブラリ・ライセンス）を
`rhat-win-x64.zip` にして GitHub のリリースへ添付します。Clang は同梱しないので、利用側に Clang が必要です。

### コマンド

```powershell
rhat check examples/layout.rh
rhat emit examples/layout.rh -o build/layout.ll --dump-ir build/layout.rir
rhat build examples/layout.rh -o build/layout.exe -O2
rhat run examples/regions.rh
rhat lsp --stdio
```

- `check`: 型・寿命・データスタックまで検査します。`--stacks` でデータスタックの必要数を表示します。
- `emit`: 依存全体を含む単一の LLVM IR を書きます。
- `build` / `run`: 実行ファイルを作ります（`run` は続けて実行します）。`.ll` は残しません。
  `--emit-llvm PATH` を付けたときだけ、単一の LLVM IR を `PATH` に書きます。
- `lsp`: 言語サーバーです（[言語サーバー](#言語サーバー)）。
- `format` / `format-check`: ソースの整形と、書き換えない整形チェックです。LSP の全文整形と同じエンジン・EditorConfig を使います（[整形](DesignDocuments/formatting.md)）。
- 共通: `--dump-ir PATH` は R^ 中間表現と配置を出力します。
  `--clang PATH` / `RHAT_CLANG` は Clang、`--runtime PATH` / `RHAT_RUNTIME` はランタイムの指定です。
  通常の開発ビルドはソースツリーのランタイムを使用します。配布時はランタイムも同梱してください。
- モジュール: `--module-path DIR`（繰り返し可）、`--cache-dir DIR`、`--no-cache`、`--verbose`。
- C 連携: `--link-lib NAME`、`--c-include DIR`、Windows で `/MD` の C ライブラリにリンクするときの `--crt-dll`。

## 動く例

```rhat
def^A {
    self^{ a : i32^ = 0 }
}
def^B {
    self^{ abstract^ b : i32^ }
}
def^C : A, B {
    self^{ c : i32^ = 12, b : i32^ = 42 }
}

let^value = C.new()
print^(value.a, value.b, value.c)
```

出力は `0 42 12`。`C` のフィールド配置は `A.a @0`, `B.b @4`, `C.c @8` です。
抽象フィールドの席は最初の宣言で確定し、実装を与えても移動しません。

`main` 関数の宣言は不要です。トップレベルの文を記述順に実行します。
空のファイルや定義だけのファイルも有効です。`main` という名前の関数を定義しても
自動では呼び出さず、通常の関数として扱います。

- [examples/layout.rh](examples/layout.rh): 抽象フィールドを含む合成と配置。
- [examples/nominal_template.rh](examples/nominal_template.rh): 明示的な公称の適合、デフォルト実装、private、構造によるテンプレート検査。
- [examples/generic_vec2.rh](examples/generic_vec2.rh): `Vec2<T>` の型パラメータ、型別名、`ZERO`・`ONE` とベクトル演算。
- [examples/regions.rh](examples/regions.rh): 呼び出し側への配列構築、伸長後も有効な参照、スコープでの領域回収。
- [examples/inference.rh](examples/inference.rh): 無注釈引数の呼び出し形ごとの具体化と返り値推論。
- [examples/factorial.rh](examples/factorial.rh): 無名関数の即時呼び出し、返り値の推論、`if^` 式、`this^` による再帰。10! を `i64^` で計算します。
- [examples/errors.rh](examples/errors.rh): エラー値、伝播、遅延代替、型付きの捕捉。
- [examples/fits.rh](examples/fits.rh), [examples/nil.rh](examples/nil.rh): 型の絞り込みと `nil^`。
- [examples/numeric.rh](examples/numeric.rh): 組み込み型と同じ通常識別子、128ビット整数、低精度浮動小数点。
- [examples/overloads.rh](examples/overloads.rh): 型によるオーバーロード、交差型、`op^`。
- [examples/hanoi.rh](examples/hanoi.rh): 文字列から数値への変換とコマンド引数。
- [examples/random.rh](examples/random.rh): 標準の乱数。

## 言語の範囲

詳しい規則・検査・制約は [DesignDocuments/design.md](DesignDocuments/design.md)、L^ の構文との対応は [DesignDocuments/syntax.md](DesignDocuments/syntax.md) にあります。

- **型**:
  - `bool^`、符号なし整数 `u8^`〜`u128^`、符号付き整数 `i8^`〜`i128^`、浮動小数点 `f8^` / `f16^` / `f32^` / `f64^`
    （`f8^` は E4M3FN、他は IEEE）、文字 `char^`、UTF-8 の `string^`。
    組み込み型の末尾の `^` は必須で、通常の `bool` や `u8` はユーザー定義名として使えます。
  - 固定長配列 `T[N]`、構築時に長さを決める所有配列 `T[]`、伸長配列 `T[grow^]`、伸長と縮小ができる `T[flex^]`、上限付き配列 `T[max^N]`、構築時に容量を決める `T[max^]`、借用スライス `T[..]`、参照 `ref^T`。
    [配列](DesignDocuments/arrays.md)、[範囲 `range^`](DesignDocuments/ranges.md)、[文字列と文字](DesignDocuments/strings.md)。
- **公称型と合成**: `def^ Name : Base1, Base2` の順序付き合成、`self^{ ... }` のフィールド、
  `abstract^`、デフォルト実装、`override^`、`private^` の宣言元に基づくアクセス検査。
  通常の型付き引数は、同じ公称型または明示的な合成先の実型を受け取ります。
  合成先の配置を保持して関数を特殊化し、基底型へのポインタキャストは行いません。
- **合併型と絞り込み**:
  - `def^ Expr = Num | Add | Mul | Neg` の公称の合併型と、`T | nil^` などの無名合併型。
    タグと最大ペイロードをインライン配置します。
  - `value fits^ Type` と `when^ fits^ Type:` による適合判定と、両側の型の絞り込み。
  - `?` / `?.` / `?[]` / `??` / `?:=` などのニル伝播。
  - [`fits^`](DesignDocuments/fits.md)、[合併型とニル伝播](DesignDocuments/nil.md)。
- **列挙体**: `enum^ Mode : u8^ { Idle, Walk, Dash = 10 }`。C 互換の整数で、`=` / `!=` で比べ、`print^` はメンバ名を表示します。基底型の値が求められる所では暗黙にその整数になり、基底型の整数からは `Mode(n)` で変換します（メンバにない値は `std.sys.Error.CastFail`）。
  [列挙体](DesignDocuments/enums.md)。
- **パターンマッチ**: `for^ value { when^ 1,2: ... when^ 3 to^ 5: ... other^: ... }` と式形。
  `other^` を省く式形のマッチは、`bool^`・`enum^`・合併型のすべての値を腕で尽くさなければなりません。文形は網羅を検査しません（[網羅](DesignDocuments/syntax.md#パターンマッチの網羅)）。
- **エラー**: `errordef^` のエラー値（付随フィールド・生成位置・`message()`）、`try^` による伝播、
  遅延代替の `catch^`、型付き・既定の `catch^` 節。[エラー値](DesignDocuments/errors.md)。
- **複数の返り値**: `-> (A, B)`、分解束縛 `let^ a, b = f()`。エラーや `nil^` との合併も使えます。[複数の返り値](DesignDocuments/results.md)。
- **ファイル入出力**: `std.fs` で全体の読み書き。[std.fs](DesignDocuments/fs.md)。
- **可変長引数**: `...:T` と `expr...`、コマンド引数。[可変長引数](DesignDocuments/variadic.md)。
- **関数とテンプレート**:
  - 返り値の型推論、無注釈引数の呼び出し形ごとの具体化、`let^ name = template^ f^ ...`。
  - キャプチャなしの無名関数、`this^` / `this^^` による再帰。
  - 型によるオーバーロード、交差型 `&`、`op^` による演算子定義（[オーバーロード](DesignDocuments/overloads.md)）。
  - 動的ディスパッチ `ref^ dyn^ D`（[動的ディスパッチ](DesignDocuments/dyn.md)）。
- **文と制御構造**: `let^` / `var^`、`:=` と複合代入、並列代入、`if^` / `elseif^` / `else^`、
  `for^` / `repeat^` と `prolog^` 〜 `epilog^` の各節、`break^` / `next^` と多段脱出。
- **数値**: 型の一致を要求する算術・比較、`bitand^` / `bitor^` / `bitxor^` / `bitshift^` / `bitnot^`、
  `i32^(value)` の明示的な変換、文字列から数値への `i32^.parse(s)`。
  範囲外・NaN などは実行時に診断します。
- **文字列**: UTF-8 の保証、`char^`、補間 `$"...{x}..."`、`std.text.Builder`。
- **C との連携**: `externC^` で C の関数を呼び、`exportC^` で R^ の関数を C に公開し、`exportL^` で L^ に公開します。
  [C 連携](DesignDocuments/ffi.md)。
- **モジュール**: `module^ lib.example`、`public^`、`import^`。標準ライブラリは `std.random`・`std.math`（`std.math.vector2`・`vector3`・`vector4`・`complex`・`quaternion`）・`std.text`・`std.sys`。
  ビルドは全依存ソースを検査した後、モジュール別にオブジェクトを生成し、
  既定の `<出力先ディレクトリ>/.rhat-cache` で再利用します。
  [分割コンパイル](DesignDocuments/separate_compilation.md)、[モジュールと分割コンパイル](DesignDocuments/design.md#モジュールと分割コンパイル)。
- **診断**: ファイル・行・列を含むコンパイル診断と、行番号付きの実行時検査。

```rhat
import^std.random

var^rng = std.random.new(12345)
print^(rng.next())
let^result = rng.rangeint(1, 6)
if^result fits^ i64^ { print^(result) }
```

## 現在の制約

これは L^ 全体の互換実装でも、Goose の完全な再実装でもありません。
キャプチャを持つクロージャ、動的な関数ポインタの保存、コルーチン、`super^`、L^ バインド生成は未実装です。
再帰の返り値に型を決める根拠がない場合は、明示的な返り値型が必要です。
`number^` は未実装であり、数値の表現は `i64^` などで指定します。
`f128^` は binary128 の演算・入出力を含む対応を保留しています。別の精度への置き換えは行いません。
未対応の操作を動的実行へ逃がすフォールバックはありません。
各機能の未対応の範囲は、それぞれの文書の「未対応」に書いています。

メモリモデルの要点:

- 伸長配列の要素は、所有領域を含まない固定サイズの値です。参照や参照を含む構造体も保存でき、
  参照先が保存先より短命になる書き込みを拒否します。
- 所有値はコピーできません。参照を含む値の寿命検査には保守的な制約があり、安全でも受理しないプログラムがあります。
- データスタック数と配置はコンパイラが静的に決定し、スコープ終了時に先端を巻き戻して再利用します。
  `rhat check source.rh --stacks` で必要数を表示できます。
- 添字アクセスは LLVM IR 内で境界検査・アドレス計算を行い、範囲外の経路だけがランタイムを呼びます。
  単純な増加ループは全反復の範囲を検証した高速経路を使い、保証できなければ元の検査付き経路を使います。
- 同じ再帰成分への呼び出しをまたいでローカル所有値を保持するコードは拒否します。
  外部所有配列を借用する再帰は可能です。Goose の可変サイズ値の連続インライン配置は今後の実装です。

コンパイラ自身は通常のヒープを使います。生成プログラムの所有配列には `malloc` / `free` / GC を使いません。
C 標準ライブラリの入出力内部まで動的確保を禁止する freestanding 環境は、現在の対象外です。

## 言語サーバー

言語サーバーは `rhat lsp` です。別の実行ファイルはありません。
エディターの LSP クライアントから `rhat lsp --stdio` を起動すると、
診断・ホバー・定義移動・参照検索・補完・シグネチャヘルプ・文書シンボル・semantic tokens を利用できます。
未保存の編集と import 依存も検査します。[設定・実装範囲](DesignDocuments/lsp.md)。

ビルドのたびに `rhat` は `.rhat-ls/` にもコピーされます。エディターにはこのコピーを起動させると、
サーバーの実行中でもビルドできます。VS Code 拡張は `rhat-vscode-extension` にあります。

## 設計文書

[DesignDocuments/](DesignDocuments/) に、設計理由・実装方針・検討中の仕様をまとめています。
将来のユーザー向けマニュアル・リファレンスは `docs/` に置き、設計文書と分けます。

- [設計](DesignDocuments/design.md): 処理の流れ、型・合成・領域と寿命・数値・モジュール・エラー・検証。
- [構文対応](DesignDocuments/syntax.md): L^ の構文との対応と、採用しなかったもの。
- [配列](DesignDocuments/arrays.md)・[範囲](DesignDocuments/ranges.md)・[文字列](DesignDocuments/strings.md)・[可変長引数](DesignDocuments/variadic.md)。
- [型パラメータ・型別名・型定数](DesignDocuments/generics.md)。
- [fits^](DesignDocuments/fits.md)・[ニル伝播](DesignDocuments/nil.md)・[エラー](DesignDocuments/errors.md)・[複数の返り値](DesignDocuments/results.md)・[列挙体](DesignDocuments/enums.md)。
- [オーバーロード](DesignDocuments/overloads.md)・[動的ディスパッチ](DesignDocuments/dyn.md)。
- [C 連携](DesignDocuments/ffi.md)・[分割コンパイル](DesignDocuments/separate_compilation.md)・[言語サーバー](DesignDocuments/lsp.md)。

Goose のベンチマークを R^・C++・Rust で測り直す手順と結果は
[bench/README.md](bench/README.md) にあります。現在は `bintrees`・`sum`・`blur`・`particles`・`tree`・`graph`・`push`・`lru`・`scene`・`strlist`・`words`・`respond`・`records`・`interp`・`sexp`・`calc` の全16種を移植しています。
採用した元の変種とメモリ表現の違いを、各レポートに記載しています。
計測バイナリの衝突があった旧 R^ 行は無効とし、言語別ファイル名と実行前後の SHA-256 検査で全16種を再測定し、結果を訂正しました。
