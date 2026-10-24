# 組み込みIR化・import導入後の再計測

元の16種・48条件を、現在のR^とC++／Rustで再ビルド・再計測する。
C++／Rustは、直前の全変種測定 `bench/all08` で条件ごとに最速だった構成を選ぶ。
今回は全比較変種の探索は繰り返さない。現在の表に過去の比較時間は流用しない。

2回ウォームアップ＋3回本測定の最小値。実行順は交互に反転し、各実行の前後で
実行ファイルのSHA-256とチェックサムを照合する。元の入力サイズ・処理回数を維持する。
ソース、Clang/MSVC/Rust、R^コンパイラ、モジュールIR・ネイティブオブジェクトも識別する。
実測に使ったハーネスは [measured_harness.py](measured_harness.py) にそのまま保存し、
生データの `source_snapshots` から参照する。表示用リンクの修正後も実測時のコードを照合できる。

```powershell
. .\scripts\devshell.ps1 -Configuration Release
python bench/modules_refresh/measure.py
```

完了した結果を上書きしないよう、同じセッションでの再計測は拒否する。
再生成は `python bench/modules_refresh/measure.py --report-only` または
`python bench/report_suite.py`。旧表は `bench/pre_modules_results.md` と
元の `bench/all08` に保持する。

前回からは組み込み処理のIR化と分割コンパイルなど複数の変更がある。
前後差は異なるセッションの観測値であり、import単独の効果や小さな差の有意性は主張しない。
