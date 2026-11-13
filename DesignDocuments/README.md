# 設計文書

R^ の設計理由、実装方針、現状の制約、検討中の仕様を記録する開発向けの文書です。
全体像は [design.md](design.md)、構文の対応は [syntax.md](syntax.md) を参照してください。

未実装の提案を含むため、各文書に記した実装状況を確認してください。
将来のユーザー向けマニュアル・リファレンスは、リポジトリ直下の `docs/` に分けて配置します。

- [コルーチンと非同期処理](coroutines.md): L^ の `yield^`・`await^` を引き継ぐ stackless な値フレーム。
- [std.async と std.net](net.md): 待ちの id を yield するコルーチンと Loop、ノンブロッキングの TCP・UDP。
- [スレッドと Channel の実装プラン](threads.md): スレッドごとのデータスタックシャード、値の転送、共有 Channel と終了処理（同期 API を実装済み）。
