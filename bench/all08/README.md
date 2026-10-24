# 全16種を 0.8x 以上にする検証

[targets.json](targets.json) に全48条件を固定した。元の未達は tree、interp、sexp、scene、calc、respond の15条件。
前回改善した4種も含め、全16種・48条件について C++／Rust の両比率が0.8以上であることを確認する。
平均や丸めた値では判定せず、各言語の元の全バリアントから選んだ最速値と比較する。

今回の変更はコンパイラのコード生成であり、この段階でベンチの入力・処理回数・構築・走査を省略していない。
前段で導入した graph CSR と bintrees の重複カウンタ除去は維持する。
元の連結版・カウンタ付き版は rhat/variants に残し、テストも維持する。

- 列挙値のタグ一致、浮動小数点から整数への範囲検査を LLVM IR に置き、失敗時だけ従来の診断へ渡す。
- 16バイト以下の非所有値を LLVM の値として直接受け渡す。所有値と大きい集約値の構築先渡しは維持する。
- 文字列リテラル直後の追記は容量を確認したコピーへ展開する。一般の重なる追記は memmove を維持する。
- clear は論理長だけを0にし、保存領域の先端は維持する。

元の全 C++／Rust バリアントも再ビルド・再測定する。2回ウォームアップ＋3回本測定の最小値、
実行順は反転しながら交互に実行する。元のサイズ、ソース、バイナリ、LLVM IR、
コンパイラ・ランタイムのハッシュ、全回のチェックサムと比較バリアントの集合を検査する。

```powershell
python bench/run_bench.py --out bench/all08/current_bintrees --work build/all08-trees
python bench/run_workloads.py --out bench/all08/current --work build/all08-workloads
python bench/all08/report.py
```

[結果](results.md)、[機械判定](verification.json)、[全比較バリアント](current/workloads_results.md)、
[bintrees](current_bintrees/results.md) に生データを保持する。
未達があればスクリプトは失敗し、目標を完了とは判定しない。
