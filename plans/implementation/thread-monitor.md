# 通常タスクと NPU のスレッドモニタ

**優先度:** 高。依存: [トレーシング基盤](tracing.md)。既存の [experiment-ai ThreadMonitor](../../userspace/experiment-ai/src/npu_runtime/thread_monitor/README.md) は NPU 所有タスクの周期観測＋推論フェーズ記録であり、全タスクスイッチの実測ではない。

## 作るもの

- 同一時刻軸上に、init／camera render／inference／postprocess／monitor の task lane、CPU running lane、NPU submit→完了 lane、Pipe1/Pipe2 frame lane を並べる。task ID と role の対応は起動時に一度記録する。ISR の占有は計測できた範囲だけ別レーンに置く。
- タスク状態は `RUNNING`／`READY`／`WAITING(reason, object)`／`DORMANT`／`UNKNOWN`。標本から補完した区間は明示して実行区間に数えない。優先度、スイッチ回数、実測可能な CPU 占有率、queue 待ち、ブロック時間を分けて出す。
- NPU はモデル ID・推論 ID・frame ID ごとに入力準備、submit、IRQ 待ち／epoch continuation、完了、後処理を結ぶ。`submit→done` は wall time であり純粋な NPU 演算時間ではない。prefetch と先行 NPU 動作の重複を保持する。

## 取得方法

1. [TaskContext](../../userspace/experiment-ai/src/task/task_context.cpp) の task 作成／開始、queue 送受、[InferenceTask](../../userspace/experiment-ai/src/task/inference_task.cpp) と [PostprocessTask](../../userspace/experiment-ai/src/task/inference_postprocess_task.cpp) の既知の待ちと handoff を静的に観測。フレームが捨てられた地点と理由も残す。
2. [NpuRuntime](../../userspace/experiment-ai/src/npu_runtime/npu_runtime.cpp) の既存フェーズ・epoch callback を取り込む。デコーダは従来ダンプとの対応を host で検証してから統合する。
3. 正しい RUNNING／READY 区間が必要なら µT-Kernel の dispatch／wait／wake 境界に**最小**イベントを追加する。アプリ側 queue フックだけでは別の task に横取りされた時刻は分からない。dispatch と割込みの切替コストを測定し、未導入時には CPU 利用率の正確さを主張しない。

## 容量・劣化時の設計

余裕がない構成では task の低頻度サンプルとアプリの queue／NPU 境界だけ取得し、状態変化の間を `UNKNOWN` と表示。余裕が得られれば dispatch イベントと短時間 epoch を有効にする。NPU 内部の大量 callback を全て記録して欠落するより、用途別に event mask と集約を選ぶ。周期 task の追加 stack が APP を圧迫するなら、既存 monitor task の共有・廃止を含めてメモリ量と障害監視機能を比較する。

## 完了条件

カメラ表示中に NPU 待ちと CPU 上の別タスク実行が重なる実例が説明できる。CPU 実行区間は同一コア上で重ならず、CPU／NPU が重なることは許容される。queue 滞留、idle、sample の欠落、モデル切替を注入・検出し、[共通の実機ゲート](README.md#共通の検証ゲート)を通す。