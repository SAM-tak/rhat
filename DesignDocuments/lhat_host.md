# L^ のホスト用ライブラリ（lib/lhat）

R^ で L^ のホストアプリを書くためのライブラリです。L^ の `std.task` と `std.async` を R^ の std.task の Pool と
std.async の待ちの表で提供し、ホストが持ち続ける L^ の値を安全に包みます。

```rhat
includeC^"host.h"          # L^ のプログラムを組み立てるホストの C（lhat.h を読む）
import^lhat.task
import^lhat.async
```

```c
// ホストの C: lhat_program_check より前に
rhat_lhat_register_lhat_task(program, "std.task");
rhat_lhat_register_lhat_async(program, "std.async");
```

- モジュールは `lhat.l`（L^ の C の型）・`lhat.task`・`lhat.async`・`lhat.value` です。`--module-path <rhat>/lib` で読みます。
- 各モジュールの exportL^ を、ホストが選んだ L^ のモジュール名で登録します（[ffi.md](ffi.md#l-に-r-を公開するexportl)）。
  L^ の標準の `std.task` / `std.async` の代わりに登録し、同じ名前・同じ署名で L^ のスクリプトから使えます。
  L^ の `std.sys` のエラー（`std.sys.Error.OutOfMemory`）が署名に出るので、L^ の `std.sys` も登録します（L^ の標準の
  モジュールのどれかを登録すれば一緒に入ります）。
- ビルドには L^ のヘッダー（`include`・`build/release/include`・リポジトリの根。`stdlib/carry.h` を読むため）と
  ライブラリ（`lhatstdlib`・`lhat`・`lhatport`・`lhatthread`）が要ります。Windows では `--crt-dll` を付けます。
  L^ の `stdlib` ディレクトリは `math.h` を持つので、include のパスには足しません。

## lhat.task（L^ の std.task）

L^ の `std.task` と同じ振る舞いです: `start()` / `start(n)`・`stop()`・`workers()`・`async(job)`（未開始のコルーチンか、
引数のない閉包）・`await(task)`、Task の `done()`・`failed()`・`awaitable()`・`dispose`。

- Task の `join()` は `await^` が委譲するコルーチン（`c^{f^ -> number^ -> self^.T0|std.task.TaskError}`）です。
  終わるまで待ちの id を yield^ し、終わったら `await` と同じ結果（失敗は `TaskError`。Failed の message は失敗の内容）で終わります。
  `std.task.await` は終わるまでスレッドを寝かせるので、ジョブの中で待つときは `try^ await^ t.join()` を使います。
  ジョブは最初に載ったワーカーから動かないため、ジョブの中の `await` は同じワーカー上の相手を待つと進まなくなります。

- ジョブは R^ の std.task の Pool（`rh_pool_*`）に、関数ポインタの組 `std.task.JobKind` で積みます。ジョブごとに専用の L^ の機械を作り
  （フレーム 64・スロット 2048）、1 歩で 20000 ターン（`lhat_machine_set_budget`）だけ走らせて他のジョブに譲ります。
- ジョブが整数を yield^ し、それがそのワーカーの std.async の待ち（`std.async.waiting`）なら、待ちが済むまでジョブを休ませ、
  ワーカーは他のジョブを進めます。待ちでない数は L^ と同じく「止まっただけ」で、すぐ続けます。
- 終わったジョブの答えは `lhat_machine_detach_result` で機械から外し、`await` が `lhat_machine_adopt_result` で呼んだ機械に移します。
  失敗は `TaskError.Failed`、機械から出せない答えは `TaskError.Refused`、2 度目の await は `TaskError.Taken` です。
  失敗の内容（状態・値・行・トレースバック）は `failed()` が答えます。
- Task は `exportL^ ref^ externC^ def^`（C のヒープ）で、`retain`・`let_go` の契約により機械の間で受け渡せます（channel で別の
  ジョブに渡すなど）。参照数が 0 になると Pool の Task を手放します。`type_argument` が結果の型を答えるので、
  `std.task.Task<number^>` のような実行時の型検査も通ります。
- Pool の handle は C の静的変数に置きます（R^ のモジュール変数はワーカーの入口から読めないため）。
- L^ の標準の std.task との違い: ジョブの結果が 1 スロットに収まるかの静的検査（`task_check_async`）は登録しません。
  `OutOfMemory` は `TaskError.Failed` で答えます。`await` はデバッガの一時停止点を作りません。

## lhat.async（L^ の std.async）

`timer`・`drop`・`wait`・`next`・`pending` を L^ と同じ署名で R^ の std.async に渡します。待ちの表はスレッドごとなので、
L^ のスクリプトは自分を走らせているスレッド（ホストの本体、または Pool のワーカー）の表で待ちます。
`external` は提供しません。外から済ませる待ちは R^ の `std.async.External` で作り、トークンで済ませます。

## lhat.value（値の包み）

- `Held`（Disposable）: 値を機械の host root の表（`lhat_machine_host_root`）に負の整数の鍵で載せ、GC から守ります。
  `get()` で読み、dispose で外します。ホスト自身が host root に置く鍵とは負の整数で分けます。
- `Carried`（Disposable）: `lhat_carry` で機械から外した値です。`into(machine)` で何度でも、どの機械にも組み立て直せ、
  dispose で `lhat_carried_free` します。

## C の補助（lhat_host.h）

R^ は C のポインタを所有して持ち回れない（構造体に入れられない・中断をまたげない）ため、機械を動かす部分は
`lhat_host.h` の `static inline` 関数です: ジョブ（機械・コルーチン・直前の実行結果）の 1 歩・終わりの処理・解放、
Task の参照数と結果の受け取り、host root と carry の薄い包み。ランタイムの `rh_` 関数（R^ の外からは呼べない名前）も
ここで `lhost_` の名前に包みます。L^ に何を見せるか、Pool への積み方、待ちの判定、契約、エラーの選び方は R^ 側です。

## テスト

`tests/lhat_host.py` は、本物の L^ をリンクしたホストで L^ のスクリプトを走らせます。CMake は `RHAT_LHAT_DIR`
（既定 `../lhat`）に `include/lhat.h` とビルド済みのライブラリがあるときだけ登録します。
