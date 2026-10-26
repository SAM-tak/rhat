# The Programming Language R^

R^ `(rhat)` は L^ の構文を基にした、GC を使わないシステム言語の実装です。

[Goose(プログラミング言語)](https://github.com/aardappel/goose)の強い影響を受けて誕生しました。
無注釈型安全を実現するアイデアはほぼ `Goose` からの借用です。

現在は **0.1 の実行可能な言語サブセット**です。構文木の表示だけではなく、
名前解決、公称型、合成、テンプレート具体化、寿命検査、中間表現、テキスト LLVM IR、
Clang によるネイティブ実行ファイル生成まで実装しています。

コンパイラは C11 + CMake 製で、LLVM ライブラリはリンクしません。
R^ のソースを C に変換する処理もありません。生成した `.ll` と小さな C ランタイムを
Clang でコンパイル・リンクします。Windows x86-64 で検証しています。

## モジュールと標準乱数

```rhat
import^ std.random

var^ rng = std.random.new(12345)
print(rng.next())
let^ result = rng.rangeint(1, 6)
if^ result fits^ i64^ { print(result) }
```

独立したファイルは `module^ lib.example` で名乗り、`public^` の付いた宣言を公開します。
`import^ lib.example` または `let^ alias = import^ lib.example` で取り込みます。
`--module-path DIR` は繰り返し指定できます。`require^` はありません。

ビルドは全依存ソースを型・寿命・データスタック検査した後、モジュール別にオブジェクトを生成します。
既定の `<出力先ディレクトリ>/.rhat-cache` でコンパイル済みオブジェクトを再利用します。
`--cache-dir DIR`、`--no-cache`、`--verbose` で場所・再利用・詳細表示を指定できます。
標準ライブラリはコンパイラに同梱され、ベンチの生成ソースを別の場所にコピーしても使用できます。
詳細は [モジュールと分割コンパイル](docs/design.md#モジュールと分割コンパイル) を参照してください。

## ビルドと実行

Windows では Visual Studio の C++ ビルド環境、Clang ツール（compiler-rt builtins を含む）、CMake、Python 3 が必要です。
次のスクリプトは PATH または Visual Studio インストール先から Clang を探します。

```powershell
cd C:\Users\Owner\source\repos\rhat
.\scripts\build.ps1 -Test
.\build\Debug\rhat.exe run examples/layout.rh
.\build\Debug\rhat.exe run examples/regions.rh
```

新しいターミナルでは、L^ と同様に開発環境を読み込めます。
このスクリプトは現在のセッションの PATH に R^・Clang・MSVC を追加し、`INCLUDE`・`LIB` も設定します。
システムの永続的な環境変数は変更しません。`build.ps1` からも同じ設定を読み込みます。

```powershell
. .\scripts\devshell.ps1
rhat build examples/layout.rh -o build/layout.exe -O2
.\build\layout.exe
```

Release 版には `-Configuration Release`、Clang の指定には `-Clang PATH` を使えます。

ビルド時に見つけた Clang のパスを実行ファイルに記録するので、別のターミナルでもそのまま使えます。
Clang の選択順は `--clang`、`RHAT_CLANG`、ビルド時に検出したパスです。
ビルド時に見つからなかった場合は PATH の `clang` を利用します。

一般的な CMake の手順も利用できます。単一構成ジェネレーターでは実行ファイルは
`build/rhat` に生成されます。POSIX 用ランタイムもありますが、この環境では未検証です。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CLI:

```powershell
rhat check examples/layout.rh
rhat emit examples/layout.rh -o build/layout.ll --dump-ir build/layout.rir
rhat build examples/layout.rh -o build/layout.exe -O2
rhat run examples/regions.rh
```

`build` / `run` は既定で `<実行ファイル名>.ll` を保持します。
同じ出力先への再ビルドでは上書きするため、実行ファイルや `.ll` を手動で消す必要はありません。
`--emit-llvm PATH` で保存先を変更できます。`--dump-ir PATH` は R^ 中間表現と配置を出力します。
`--clang PATH` / `RHAT_CLANG` は Clang、`--runtime PATH` / `RHAT_RUNTIME` はランタイムの指定です。
通常の開発ビルドはソースツリーのランタイムを使用します。配布時はランタイムも同梱してください。

## 動く例

```lhat
def^ A {
    self^{ a : i32^ = 0 }
}
def^ B {
    self^{ abstract^ b : i32^ }
}
def^ C : A, B {
    self^{ c : i32^ = 12, b : i32^ = 42 }
}

let^ value = C.new()
print(value.a, value.b, value.c)
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
- [examples/numeric.rh](examples/numeric.rh): 組み込み型と同じ通常識別子、128ビット整数、低精度浮動小数点。

## 実装されている範囲

- `def^ Name : Base1, Base2` による公称型と順序付きの合成。
- `def^ Expr = Num | Add | Mul | Neg` による公称の合併型。タグと最大ペイロードをインライン配置します。
  `value fits^ Num` で判定し、真の分岐では `value.value` のように選択肢のメンバを使えます。
- `T | nil^` の無名合併型、存在判定 `?`、ニル伝播 `?.` / `?[]`、遅延代替 `??`、
  条件付き代入 `?:=` / `?+=` など。[型・評価順・寿命の詳細](docs/nil.md)。
- `value fits^ Type` と `when^ fits^ Type:` による適合判定・両側の型の絞り込み。
  静的に決まる判定は定数化し、一部だけが適合する合併型ではタグを検査します。[詳細](docs/fits.md)。
- `try^` によるエラー伝播、遅延代替の `catch^`、型付き・既定の `catch^` 節。
  返り値推論・スコープの回収と連携します。[詳細](docs/errors.md)。
- `self^{ ... }` のフィールド、`abstract^` フィールド・メソッド、デフォルト実装、`override^`。
- `private^` の宣言元に基づくアクセス検査。合成先やテンプレートからも無条件にはアクセスできません。
- 通常の型付き引数は、同じ公称型または明示的な合成先の実型を受け取ります。
  合成先の配置を保持して関数を特殊化し、基底型へのポインタキャストは行いません。
- `let^ name = template^f^ ...` / `template^p^ ...`。`template^` は関数・手続きリテラルの修飾子で、無名のままでも呼び出せます。無注釈引数には呼び出し時の実型を代入し、本体全体を検査します。
  型注釈がある引数はテンプレートでも公称の制約を守ります。
- `bool^`, `string^`, `void^`、符号なし整数 `u8^` / `u16^` / `u32^` / `u64^` / `u128^`、
  符号付き整数 `i8^` / `i16^` / `i32^` / `i64^` / `i128^`、浮動小数点 `f8^` / `f16^` / `f32^` / `f64^`。
  組み込み型の末尾の `^` は必須です。通常の `bool` や `u8` はユーザー定義名として使用できます。
  `f8^` は E4M3FN、`f16^` / `f32^` / `f64^` は IEEE binary16 / binary32 / binary64 です。
  固定長配列 `T[N]`、伸長配列 `T[>..]`、読み取り用の借用スライス `T[:]`、参照 `ref^ T` にもこれらの型を使えます。
- `Type.new()`、`Type{ field = expr }`、配列リテラル `[ ... ]`。
- `let^`, `var^`, `:=`, `+=` / `-=` / `*=` / `/=` / `%=`、並列代入、`return^`、単独ブロック `do^{...}`。
  引数など期待型のある位置では `{field=value}` で公称構造体を構築できます。[詳細](docs/syntax.md#ブロックと文脈付きの構築)。
- 分岐は `if^ condition { ... elseif^ condition: ... else^: ... }`。波括弧は分岐全体で一組です。
- `for^ var^ i=0 while^ i<8 next^ i+=1 { ... }`、`for^ i from^ 1 to^ 8 { ... }`、`downto^` / `step^`。
- `repeat^ n { ... }`、`repeat^ while^ condition { ... }`、`repeat^ until^ condition { ... }`、無限 `repeat^ { ... }`。
- `prolog^` / `pre^` / `first^` / `main^` / `last^` / `epilog^`、`break^` / `next^` と多段脱出。
- 値マッチ `for^ value { when^ 1,2: ... when^ 3 to^ 5: ... other^: ... }` と式形。
- 詳細と、型・所有権の判断を残した構文は [構文対応](docs/syntax.md) を参照。
- `f^`・`p^` の返り値の型推論、式一つの `f^` の暗黙の返却、`if^ condition: value el^: value;`。
- キャプチャなしの無名関数の即時呼び出し、`this^` による自己再帰、`this^^` / `this^^^` による外側の関数の呼び出し、名前付き関数間の再帰。
  通常の `f^` / `p^` も、無注釈引数を同一モジュール内の呼び出し形ごとに具体化します。
  型付きの呼び先・返却先・ローカル変数・真偽条件など、本体の要求からも引数型を推論します。
  公開関数も本体だけで型が決まれば注釈を省略可能。未確定の公開引数には注釈か `template^`、
  未確定のローカル引数には呼び出し形が必要です。`x+1` だけでは数値の幅を固定しません。
- 型の一致を要求する算術・比較、短絡評価する `and^` / `or^`、`ref^ value`, `*ref`、メンバと添字。
- 整数の `bitand^` / `bitor^` / `bitxor^` / `bitshift^`、前置 `bitnot^`。
  L^ と同じ優先順位で、`bitshift^` は正で左、負で論理右シフトし、型の幅以上ならゼロになります。
- `i32^(value)` などの明示的な数値変換。整数間は下位ビットの保持と符号・ゼロ拡張、
  浮動小数点から整数はゼロ方向への切り捨てです。NaN・無限大・変換先範囲外は実行時診断します。
- `print`, `assert`, 配列の `.len`, 伸長配列の `.push()`・`.pop()`・`.clear()`・`.truncate()`。組み込みメンバの名前に `^` は付けません。
  `.push(value)` は追記した要素への `ref^ T` を返し、後続の追記でも参照は有効です。
  `.slice(start, count)` は配列・文字列の一部を借用し、`.append(view)` は伸長配列へまとめてコピーします。
  文字列の添字は `u8^` のバイト値を返します。ビューの寿命は元の保存領域に束縛されます。
  `.pop()` は末尾を返して長さを減らし、`.clear()` は長さをゼロにします。保存領域と既存のスロット参照は維持し、再追記で同じスロットを上書きします。
  `(ref^ T)[>..]` / `(ref^ T)[N]` は参照の配列、`ref^ T[>..]` は配列全体への参照です。
- ファイル・行・列を含むコンパイル診断と、行番号付きの実行時検査。

詳しい規則・検査・制約は [docs/design.md](docs/design.md) にあります。

Goose のベンチマークを R^・C++・Rust で測り直す手順と結果は
[bench/README.md](bench/README.md) にあります。現在は `bintrees`・`sum`・`blur`・`particles`・`tree`・`graph`・`push`・`lru`・`scene`・`strlist`・`words`・`respond`・`records`・`interp`・`sexp`・`calc` の全16種を移植しています。
採用した元の変種とメモリ表現の違いを、各レポートに記載しています。
計測バイナリの衝突があった旧 R^ 行は無効とし、言語別ファイル名と実行前後の SHA-256 検査で全16種を再測定し、結果を訂正しました。

## 現在の制約

これは L^ 全体の互換実装でも、Goose の完全な再実装でもありません。
キャプチャを持つクロージャ、関数値の保存・受け渡し、コルーチン、`enum^`、エラーの付随情報、`super^`、
演算子の多重定義、動的ディスパッチ、任意の C FFI、L^ バインド生成は未実装です。
再帰の返り値に型を決める根拠がない場合は、明示的な返り値型が必要です。
`number^` は未実装であり、数値の表現は `i64^` などで指定します。
`f128^` は binary128 の演算・入出力を含む対応を保留しています。別の精度への置き換えは行いません。
未対応の操作を動的実行へ逃がすフォールバックはありません。

伸長配列の要素は、所有領域を含まない固定サイズの値です。参照や参照を含む構造体も保存でき、
参照先が保存先より短命になる書き込みを拒否します。途中の要素の削除・全体への再代入は提供していません。
所有値はコピーできません。参照を含む値の寿命検査には保守的な制約があり、
安全でも受理しないプログラムがあります。データスタック数と配置はコンパイラが静的に決定し、
スコープ終了時に先端を巻き戻して再利用します。添字アクセスは LLVM IR 内で境界検査・アドレス計算を行い、
範囲外の経路だけがランタイムを呼びます。単純な増加ループは全反復の範囲を検証した高速経路を使い、
保証できなければ元の検査付き経路を使います。内部関数のスカラー・参照は値として直接受け渡します。
push も容量の割り算を避け、証明できる箇所では先端を直接更新します。`rhat check source.rh --stacks` で必要数を表示できます。
同じ再帰成分への呼び出しをまたいでローカル所有値を保持するコードは拒否します。
外部所有配列を借用する再帰は可能です。Goose の可変サイズ値の連続インライン配置は今後の実装です。

コンパイラ自身は通常のヒープを使います。生成プログラムの所有配列には `malloc` / `free` / GC を使いません。
C 標準ライブラリの入出力内部まで動的確保を禁止する freestanding 環境は、現在の対象外です。

## 言語サーバー

`rhatls` を同時にビルドします。エディターの LSP クライアントから
`build/Debug/rhatls.exe --stdio` を起動すると、診断・ホバー・定義移動・参照検索・補完・
シグネチャヘルプ・文書シンボル・semantic tokens を利用できます。
未保存の編集と import 依存も検査します。[設定・実装範囲](docs/lsp.md)。

`-DRHAT_BUILD_LSP=OFF` でコンパイラのみをビルドできます。
