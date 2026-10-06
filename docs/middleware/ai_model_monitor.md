# ai_model_monitor

AIパイプラインの各ステップ（前処理、NPU、後処理）の開始と終了を、モデルと推論IDつきでPSRAM上のリングに記録します。ホストで読み出すと、どのモデルがいつCPUとNPUを使っていたかをタイムラインで見られます。

![AI model monitorの出力例](https://raw.githubusercontent.com/kons-9/uai-studio/main/host_app/ai_model_monitor/sample/ai_model_monitor.png)

上段がモデルごとのCPU（青）とNPU（橙）の処理区間、下段が各段階の平均時間です。

## 仕組み

`PipelineRuntime`が内部に持ち、`Dispatcher::RunOnce()`が`Evaluate()`を呼ぶ前後で記録します。アプリ側で計測コードを書く必要はありません。記録先は`Key::kThreadMonitor`の領域（ai-appではPSRAMの32 KiB）で、満杯になると古い記録から上書きします。

CPUの時間は`Evaluate()`の開始と終了の差なので、同じレーンの他の推論を待つ時間は含みませんが、割り込みや高優先度タスクによる中断は含みます。

ステップのイベントは127件まで一時キューに保持し、監視タスクがPSRAMへ書き出します。送信処理はSTM32では短い割り込み禁止区間、ホストテストではmutexで直列化します。開始時刻は推論IDとステップIDごとに最大16件保持し、満杯なら最古の記録を置き換えます。開始イベントの欠落や置き換えで終了イベントと対応づけられない場合、`kTraceFlagTimingValid`は立ちません。この場合の`npu_elapsed_ms = 0`は計測値ではありません。対応づけられた実際の0 msには有効フラグが付きます。

## アプリがすること

1. NPUレーンのタスクで`pipeline.RegisterModelName(id, "name")`を呼び、モデル名を登録する（最大16モデル、名前は27 byteまで）。
2. 同じタスクで`pipeline.StartAiModelMonitor()`を呼ぶ。監視用のタスクが作られ、呼んだタスクの状態を定期的に記録します。

```cpp
// NPUレーンのタスク
pipeline.RegisterModelName(kPersonModelId, "person");
pipeline.RegisterModelName(kFaceModelId, "face");
pipeline.StartAiModelMonitor();
```

モデル名を登録しておくと、ホスト側のツールを変更せずに新しいモデルを表示できます。ホストビルド（テスト）では何もしない実装になります。

## 取得と可視化

```sh
make -C userspace/ai-app thread-monitor
```

ST-LINKのHot Plug接続でCPUを一時停止してリングを読み出し、再開してからJSONとPNGを作ります。UARTの帯域は使いません。リングは揮発性のため、リセット前に取得してください。

| 出力 | 既定の場所 |
| --- | --- |
| raw dump | `build-ai-app-person/thread_monitor.bin` |
| JSON | `build-ai-app-person/thread_monitor.json` |
| PNG | `build-ai-app-person/thread_monitor.png` |

JSONにはモデルごとのステップ時間の集計が含まれるため、スクリプトやAIエージェントがボトルネックを探す入力にも使えます。CLIのオプションとトレース形式は[host_app/ai_model_monitor/README.md](https://github.com/kons-9/uai-studio/blob/main/host_app/ai_model_monitor/README.md)を参照してください。

## ホストテスト

最小限のμT-Kernel、HAL、メモリ配置、T-Monitorモックと実物の`foundation/log.hpp`で監視の実装本体をリンクし、推論の交錯と複数スレッドからの記録を確認します。

```sh
make -C kernel/middleware/ai_model_monitor/tests test
make -C kernel/middleware/ai_model_monitor/tests tsan
```

通常テストでは交錯した推論の所要時間、開始イベント欠落時の有効フラグ、時計の周回、並行送受信とキュー周回後の満杯判定を確認します。公開APIでイベントを入力し、タスクモックに登録された入口と待機フックを使って監視処理を1周期ずつ実行します。割り込み制御はHALモックで置き換え、本体にテスト専用の排他処理は持ちません。実時間でのスケジューリングや実機のキャッシュ動作は対象外です。送信キュー以外の監視状態の共有には既存の競合が残っており、TSANでの検証は通常テストとは別に必要です。
