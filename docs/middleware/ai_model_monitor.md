# ai_model_monitor

AIパイプラインの各ステップ（前処理、NPU、後処理）の開始と終了を、モデルと推論IDつきでPSRAM上のリングに記録します。ホストで読み出すと、どのモデルがいつCPUとNPUを使っていたかをタイムラインで見られます。

![AI model monitorの出力例](https://raw.githubusercontent.com/kons-9/uai-studio/main/host_app/ai_model_monitor/sample/ai_model_monitor.png)

上段がモデルごとのCPU（青）とNPU（橙）の処理区間、下段が各段階の平均時間です。

## 仕組み

`PipelineRuntime`が内部に持ち、`Dispatcher::RunOnce()`が`Evaluate()`を呼ぶ前後で記録します。アプリ側で計測コードを書く必要はありません。記録先は`Key::kThreadMonitor`の領域（ai-appではPSRAMの32 KiB）で、満杯になると古い記録から上書きします。

CPUの時間は`Evaluate()`の開始と終了の差なので、同じレーンの他の推論を待つ時間は含みませんが、割り込みや高優先度タスクによる中断は含みます。

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
