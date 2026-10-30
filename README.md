# The Programming Language R^

![R^ logo](./media/rhat-logo.png)

R^ `(rhat)` は L^ の構文を基にした、GC を使わないシステム言語の実装です。

[Goose(プログラミング言語)](https://github.com/aardappel/goose)の強い影響を受けて誕生しました。
無注釈型安全を実現するアイデア（データスタックによるメモリモデル）はほぼ `Goose` からの借用です。

現在は **0.1 の実行可能な言語サブセット**です。構文解析・名前解決・公称型・合成・テンプレート具体化・
型推論・寿命検査・データスタック計画・テキスト LLVM IR の生成から、
Clang によるネイティブ実行ファイルの生成までを実装しています。

コンパイラは C11 + CMake 製で、LLVM ライブラリはリンクしません。
R^ のソースを C に変換する処理もありません。生成した LLVM IR と小さな C ランタイムを
Clang でコンパイル・リンクします。Windows x86-64 で検証しています。

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

詳しい規則・検査・制約は [docs/design.md](docs/design.md)、L^ の構文との対応は [docs/syntax.md](docs/syntax.md) にあります。

- **型**:
  - `bool^`、符号なし整数 `u8^`〜`u128^`、符号付き整数 `i8^`〜`i128^`、浮動小数点 `f8^` / `f16^` / `f32^` / `f64^`
    （`f8^` は E4M3FN、他は IEEE）、文字 `char^`、UTF-8 の `string^`。
    組み込み型の末尾の `^` は必須で、通常の `bool` や `u8` はユーザー定義名として使えます。
  - 固定長配列 `T[N]`、伸長配列 `grow^ T[]`、伸長と縮小ができる `flex^ T[]`、借用スライス `T[:]`、参照 `ref^ T`。
    [配列](docs/arrays.md)、[範囲 `range^`](docs/ranges.md)、[文字列と文字](docs/strings.md)。
- **公称型と合成**: `def^ Name : Base1, Base2` の順序付き合成、`self^{ ... }` のフィールド、
  `abstract^`、デフォルト実装、`override^`、`private^` の宣言元に基づくアクセス検査。
  通常の型付き引数は、同じ公称型または明示的な合成先の実型を受け取ります。
  合成先の配置を保持して関数を特殊化し、基底型へのポインタキャストは行いません。
- **合併型と絞り込み**:
  - `def^ Expr = Num | Add | Mul | Neg` の公称の合併型と、`T | nil^` などの無名合併型。
    タグと最大ペイロードをインライン配置します。
  - `value fits^ Type` と `when^ fits^ Type:` による適合判定と、両側の型の絞り込み。
  - `?` / `?.` / `?[]` / `??` / `?:=` などのニル伝播。
  - [`fits^`](docs/fits.md)、[合併型とニル伝播](docs/nil.md)。
- **列挙体**: `enum^ Mode : u8^ { Idle, Walk, Dash = 10 }`。C 互換の整数で、`=` / `!=` で比べ、`print^` はメンバ名を表示します。
  [列挙体](docs/enums.md)。
- **パターンマッチ**: `for^ value { when^ 1,2: ... when^ 3 to^ 5: ... other^: ... }` と式形。
  `other^` を省くマッチは、`bool^`・`enum^`・合併型のすべての値を腕で尽くさなければなりません（[網羅](docs/syntax.md#パターンマッチの網羅)）。
- **エラー**: `errordef^` のエラー値（付随フィールド・生成位置・`message()`）、`try^` による伝播、
  遅延代替の `catch^`、型付き・既定の `catch^` 節。[エラー値](docs/errors.md)。
- **複数の返り値**: `-> (A, B)`、分解束縛 `let^ a, b = f()`。エラーや `nil^` との合併も使えます。[複数の返り値](docs/results.md)。
- **可変長引数**: `...:T` と `expr...`、コマンド引数。[可変長引数](docs/variadic.md)。
- **関数とテンプレート**:
  - 返り値の型推論、無注釈引数の呼び出し形ごとの具体化、`let^ name = template^ f^ ...`。
  - キャプチャなしの無名関数、`this^` / `this^^` による再帰。
  - 型によるオーバーロード、交差型 `&`、`op^` による演算子定義（[オーバーロード](docs/overloads.md)）。
  - 動的ディスパッチ `ref^ dyn^ D`（[動的ディスパッチ](docs/dyn.md)）。
- **文と制御構造**: `let^` / `var^`、`:=` と複合代入、並列代入、`if^` / `elseif^` / `else^`、
  `for^` / `repeat^` と `prolog^` 〜 `epilog^` の各節、`break^` / `next^` と多段脱出。
- **数値**: 型の一致を要求する算術・比較、`bitand^` / `bitor^` / `bitxor^` / `bitshift^` / `bitnot^`、
  `i32^(value)` の明示的な変換、文字列から数値への `i32^.parse(s)`。
  範囲外・NaN などは実行時に診断します。
- **文字列**: UTF-8 の保証、`char^`、補間 `$"...{x}..."`、`std.text.Builder`。
- **C との連携**: `externC^` で C の関数を呼び、`exportC^` で R^ の関数を C に公開し、`exportL^` で L^ に公開します。
  [C 連携](docs/ffi.md)。
- **モジュール**: `module^ lib.example`、`public^`、`import^`。標準ライブラリは `std.random`・`std.math`・`std.text`・`std.sys`。
  ビルドは全依存ソースを検査した後、モジュール別にオブジェクトを生成し、
  既定の `<出力先ディレクトリ>/.rhat-cache` で再利用します。
  [分割コンパイル](docs/separate_compilation.md)、[モジュールと分割コンパイル](docs/design.md#モジュールと分割コンパイル)。
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
未保存の編集と import 依存も検査します。[設定・実装範囲](docs/lsp.md)。

ビルドのたびに `rhat` は `.rhat-ls/` にもコピーされます。エディターにはこのコピーを起動させると、
サーバーの実行中でもビルドできます。VS Code 拡張は `rhat-vscode-extension` にあります。

## 文書

- [設計](docs/design.md): 処理の流れ、型・合成・領域と寿命・数値・モジュール・エラー・検証。
- [構文対応](docs/syntax.md): L^ の構文との対応と、採用しなかったもの。
- [配列](docs/arrays.md)・[範囲](docs/ranges.md)・[文字列](docs/strings.md)・[可変長引数](docs/variadic.md)。
- [fits^](docs/fits.md)・[ニル伝播](docs/nil.md)・[エラー](docs/errors.md)・[複数の返り値](docs/results.md)・[列挙体](docs/enums.md)。
- [オーバーロード](docs/overloads.md)・[動的ディスパッチ](docs/dyn.md)。
- [C 連携](docs/ffi.md)・[分割コンパイル](docs/separate_compilation.md)・[言語サーバー](docs/lsp.md)。

Goose のベンチマークを R^・C++・Rust で測り直す手順と結果は
[bench/README.md](bench/README.md) にあります。現在は `bintrees`・`sum`・`blur`・`particles`・`tree`・`graph`・`push`・`lru`・`scene`・`strlist`・`words`・`respond`・`records`・`interp`・`sexp`・`calc` の全16種を移植しています。
採用した元の変種とメモリ表現の違いを、各レポートに記載しています。
計測バイナリの衝突があった旧 R^ 行は無効とし、言語別ファイル名と実行前後の SHA-256 検査で全16種を再測定し、結果を訂正しました。
