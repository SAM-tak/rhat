# pop 展開・型による別領域情報を追加する前の測定

コミット `1446808` のコンパイラによる全16種・48条件の測定を保持する。
全比較実装を再ビルドし、2回のウォームアップと3回の本測定を行った。
チェックサム・実行ファイル・元の全比較バリアントを検査した結果は
[verification.json](verification.json)、一覧は [results.md](results.md)。

47条件は合格したが、lru 小サイズが C++ 比 0.773895x のため目標は未達。
当初未達だった15条件はすべて合格した。この未達結果を除外・上書きせず、履歴として保持する。
保存時点で `bench/all08/report.py` は元の配置 `bench/all08/current` と
`bench/all08/current_bintrees` で検証しており、JSON 内の報告パスもその当時の値である。
生成物は `build/all08-workloads` と `build/all08-trees` に保存した。

その後の測定は親ディレクトリの current / current_bintrees に保存する。
