# 10. 次のステップ

mini-ai-appは、ai-appから「1モデル・同期推論・UIなし」に削ったものです。ここでは、ai-appが追加で持っているものと、その読み先をまとめます。

## ai-appとの違い

| 項目 | mini-ai-app | ai-app | 読み先 |
| --- | --- | --- | --- |
| モデル | person 1つ | person、face、segmentationの3つを1バイナリに。フレームごとにスケジュール表で切り替え | [ai-app README](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/README.md) |
| 推論の実行 | 推論タスク1つで前処理→NPU→後処理を順に | `ai_runtime`の3レーン（前処理CPU、NPU、後処理CPU）を3タスクで回し、NPU実行中に別フレームの前処理・後処理を進める | [ai_runtime](../middleware/ai_runtime.md)、`src/task/pipeline_task.cpp` |
| モデルごとの処理 | `PersonDecoder` | `AiFuture`派生の`Future`（`Preprocess`/`Infer`/`Postprocess`を`Evaluate()`で1ステップずつ） | `src/models/<model>/future.cpp` |
| 前処理 | なし（Pipe2の480x480をそのまま入力） | face（128x128）とsegmentation（320x320）はCPUで縮小。`SnapshotInferenceSource()`で不変なコピーを作ってから | [image_processing](../middleware/image_processing.md)、[image_resizer](../middleware/image_resizer.md) |
| 結果の表示 | 人物の枠 | 人物・顔の枠、セグメンテーションのマスク | `lcd_driver.cpp`の`DrawBoxes`/`DrawMask` |
| UI | なし | GT911タッチと`ui::ButtonPanel`（BOXESトグル）。配置は`ui_designer`で生成 | [ui](../middleware/ui.md)、[ui_designer](https://github.com/kons-9/uai-studio/blob/main/host_app/ui_designer/README.md) |
| 計測 | cpu_task_monitor | cpu_task_monitorに加え、`ai_model_monitor`でステップ単位のNPU/CPUタイムライン | [ai_model_monitor](../middleware/ai_model_monitor.md) |
| 診断モード | なし | `task_config.hpp`の`DisplayDiagnosticMode`/`InferenceMode`/`DiagnosticsConfig`で、静的パターン表示、Pipe2直接表示、フレーム単位のトレースなど | `src/task/task_config.hpp` |
| Flash起動 | RAM実行のみ | FSBL＋署名イメージで外部NORから起動（`make program`） | [起動の流れ](../kernel/boot.md) |

## 機能を足すときの順番

1. **2つ目のモデル**: `models/generate_model.sh <name>`で生成し、`model_layout.json`と`board_memory.json`（command blobの枠）に追加、`<name>_network.c`で名前を付け替えて取り込みます。推論タスクで`npu.Preload()`→`SelectModel()`で切り替えます（[NPU](../driver.md)）。入力サイズが480x480でなければ前処理（CPU縮小とcleanキャッシュ）が要ります。
2. **パイプライン化**: 推論が複数モデルになり、NPU待ちの間にCPUが遊ぶのが気になったら`ai_runtime`へ移します。`PersonDecoder`は後処理ステップにそのまま使えます。
3. **UI**: `ui_designer`でボタンを置き、`ComposeAndPresent()`の`overlay`に`ButtonPanel`を渡します。タッチは`TouchManagement`をLCDの後に初期化します。
4. **計測の拡張**: `ai_model_monitor`は`ai_runtime`のトレースフックから記録するので、2の後に有効になります。

## 削ったものを戻す場所

| mini-ai-appにないもの | ai-appでの場所 |
| --- | --- |
| フレームの時刻記録（`input_preparation_*_ms`） | `InferenceFrame`、各`Future::Preprocess()` |
| 推論結果の統合（複数モデルの枠を1つの`BoxSet`に） | `pipeline_task.cpp`の`PublishBoxes()` |
| NORが読めないときのモデル単位の無効化 | `external_nor_ready`と`g_app.enabled` |
| IACハンドラのRISAF詳細ログ | `main.cpp`（mini-ai-appにも同じものがあります） |

## 関連資料

- [ドライバー](../driver.md): カメラ、LCD、NPU、PSRAM、NOR、RIF、キャッシュの使い方
- [ミドルウェア](../middleware/index.md): `memory_manager`、`message_channel`、`task`、`ai_runtime`、モニタ
- [カーネル](../kernel/index.md): ビルド構成、起動の流れ、μT-Kernel
