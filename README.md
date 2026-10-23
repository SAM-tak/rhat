# R^ / rhat

R^ は L^ の構文を基にした、GC を使わないシステム言語の実装です。
現在は **0.1 の実行可能な言語サブセット**です。構文木の表示だけではなく、
名前解決、公称型、合成、テンプレート具体化、寿命検査、中間表現、テキスト LLVM IR、
Clang によるネイティブ実行ファイル生成まで実装しています。

コンパイラは C11 + CMake 製で、LLVM ライブラリはリンクしません。
R^ のソースを C に変換する処理もありません。生成した `.ll` と小さな C ランタイムを
Clang でコンパイル・リンクします。Windows x86-64 で検証しています。

## ビルドと実行

Windows では Visual Studio の C++ ビルド環境、Clang ツール、CMake、Python 3 が必要です。
次のスクリプトは PATH または Visual Studio インストール先から Clang を探します。

```powershell
cd C:\Users\Owner\source\repos\rhat
.\scripts\build.ps1 -Test
.\build\Debug\rhat.exe run examples/layout.rh
.\build\Debug\rhat.exe run examples/regions.rh
```

スクリプトは現在の PowerShell プロセスに `RHAT_CLANG` を設定します。
別のターミナルで使う場合は Clang を PATH に置くか、`--clang` に実行ファイルを指定します。

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
`--emit-llvm PATH` で保存先を変更できます。`--dump-ir PATH` は R^ 中間表現と配置を出力します。
`--clang PATH` / `RHAT_CLANG` は Clang、`--runtime PATH` / `RHAT_RUNTIME` はランタイムの指定です。
通常の開発ビルドはソースツリーのランタイムを使用します。配布時はランタイムも同梱してください。

## 動く例

```lhat
def^ A {
    self^{ a : i32 = 0 }
}
def^ B {
    self^{ abstract^ b : i32 }
}
def^ C : A, B {
    self^{ c : i32 = 12, b : i32 = 42 }
}

let^ main = p^ {
    let^ value = C.new()
    print(value.a, value.b, value.c)
}
```

出力は `0 42 12`。`C` のフィールド配置は `A.a @0`, `B.b @4`, `C.c @8` です。
抽象フィールドの席は最初の宣言で確定し、実装を与えても移動しません。

- [examples/layout.rh](examples/layout.rh): 抽象フィールドを含む合成と配置。
- [examples/nominal_template.rh](examples/nominal_template.rh): 明示的な公称の適合、デフォルト実装、private、構造によるテンプレート検査。
- [examples/regions.rh](examples/regions.rh): 呼び出し側への配列構築、伸長後も有効な参照、スコープでの領域回収。

## 実装されている範囲

- `def^ Name : Base1, Base2` による公称型と順序付きの合成。
- `self^{ ... }` のフィールド、`abstract^` フィールド・メソッド、デフォルト実装、`override^`。
- `private^` の宣言元に基づくアクセス検査。合成先やテンプレートからも無条件にはアクセスできません。
- 通常の型付き引数は、同じ公称型または明示的な合成先の実型を受け取ります。
  合成先の配置を保持して関数を特殊化し、基底型へのポインタキャストは行いません。
- `template^ let^ name = f^ ...` / `p^ ...`。無注釈引数には呼び出し時の実型を代入し、本体全体を検査します。
  型注釈がある引数はテンプレートでも公称の制約を守ります。
- `bool`, `u8`, `i32`, `i64`, `f64`, `string^`、固定長配列 `T[N]`、伸長配列 `T[>..]`、参照 `&T`。
  基本型の末尾の `^` は省略可能です。
- `Type.new()`、`Type{ field = expr }`、型の分かる位置の `{ field = expr }`、配列リテラル `[ ... ]`。
- `let^`, `var^`, `:=`, `return^`, ブロック、`if^ ... { ... } else^ { ... }`, `while^`。
- 型の一致を要求する算術・比較、短絡評価する `and^` / `or^`、`&value`, `*ref`、メンバと添字。
- `print`, `assert`, 配列の `.len`, 伸長配列の `.push^()`。
- ファイル・行・列を含むコンパイル診断と、行番号付きの実行時検査。

詳しい規則・検査・制約は [docs/design.md](docs/design.md) にあります。

## 現在の制約

これは L^ 全体の互換実装でも、Goose の完全な再実装でもありません。
再帰、クロージャ（非脱出のものも含む）、コルーチン、モジュール、列挙・エラー値、`super^`、
演算子の多重定義、型推論による返り値、動的ディスパッチ、任意の C FFI、L^ バインド生成は未実装です。
未対応の操作を動的実行へ逃がすフォールバックはありません。

伸長配列は固定サイズ・参照なしの要素に限定し、縮小・削除・全体への再代入を提供していません。
所有値はコピーできません。参照を含む値の寿命検査には保守的な制約があり、
安全でも受理しないプログラムがあります。0.1 の領域方式は値ごとの OS 仮想メモリ領域です。
Goose の可変サイズ値の連続インライン配置や、少数のデータスタックへの静的な割り当ては今後の実装です。

コンパイラ自身は通常のヒープを使います。生成プログラムの所有配列には `malloc` / `free` / GC を使いません。
C 標準ライブラリの入出力内部まで動的確保を禁止する freestanding 環境は、現在の対象外です。
