# 0.8x 目標の検証

元の完全測定で C++ または Rust 比が 0.5x 以下だった8条件を [targets.json](targets.json) に固定。
対象は graph の全3サイズ、blur の全3サイズ、lru small、bintrees large。
後の部分測定で改善済みの条件も対象から外さず、対象4種の全12条件を確認する。

比率は各言語の最速時間 / R^ の時間。C++／Rust の両方について、丸める前の比率が
0.8 以上であることを [report.py](report.py) が検査する。平均で合格させることはしない。

比較言語の元の全バリアントを再ビルド・再測定する。2回ウォームアップ、3回本測定、
実行順は反転しながら交互に実行する。各回のチェックサムとバイナリ SHA-256、
コンパイラ・ランタイム・ソース・LLVM IR の識別、元のサイズと比較バリアントの集合を検査する。

graph は連結辺をたどる版に加えて CSR を実装した。辺の生成、次数集計、累積和、
連続した辺への配置の全コストを含み、C++ VARIANT 1／Rust CSR と同じ構築方式を使う。
辺、乱数、4回の BFS、距離のチェックサムは変えない。旧版は variants に保存している。

bintrees は重複していたノードカウンタを除き、配列長を利用する。
木の構築と走査、ノード構造、反復数を維持する。コンパイラは縮小操作がない閉じたプログラムで、
借用配列の push も先端の直接更新にできる。縮小があり得る場合は保守的な更新を維持する。

開発シェルで、統合テストを通してから実行する。

```powershell
python bench/run_bench.py --out bench/target08/current_bintrees --work build/target08-trees
python bench/run_workloads.py --workloads graph blur lru --out bench/target08/current --work build/target08-workloads
python bench/target08/report.py
```

検証結果は [results.md](results.md)、機械判定は [verification.json](verification.json)。
未達条件があればスクリプトは失敗し、結果にも未達と記録する。
元の完全測定は固定したまま保持し、[results.md](../results.md) には今回の対象4種を更新して反映する。
