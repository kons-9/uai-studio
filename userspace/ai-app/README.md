# ai-app

STM32N6570-DKでカメラ映像をLCDに表示しながら、person、face、segmentationの3モデルをNeural-ART NPUで推論するアプリです。セットアップ、ビルド、書き込みの手順は[ルートのREADME](../../README.md)を参照してください。

## 処理の流れ

```text
IMX335
  +-- DCMIPP Pipe1 (RGB565 800x480) --> CameraRenderTask --> LCD
  +-- DCMIPP Pipe2 (RGB888 480x480) --> frame_queue --> PipelineTask
        FrameEntry        モデルを選び、Futureを投入する
        PreprocessEntry   入力を準備する（face、segmentationはCPUで縮小）
        NpuEntry          NPUで推論する
        PostprocessEntry  出力をデコードし、結果をbox_queueへ送る
CameraRenderTask は最新の結果をPipe1のフレームへ合成してLCDへ出す
```

- Pipe2は800x480全体を縦横比を保って480x480へletterboxします。有効領域は480x288です。
- モデルはperson、face、segmentation、faceの順に投入します。空きFutureがないモデルは飛ばします。
- 推論の初期化に失敗してもカメラ表示は継続します。
- 前処理、NPU、後処理の各タスクは`kernel/middleware/ai_runtime`のDispatcherを1つずつ持ちます。

## モデル

| モデル | 元モデル | 入力 | 重みのアドレス | command blobのアドレス |
| --- | --- | --- | --- | --- |
| person | ST YOLOX Nano | 480x480 | `0x70380000` | `0x70500000` |
| segmentation | DeepLabV3 MobileNetV2 | 320x320 | `0x70600000` | `0x70560000` |
| face | BlazeFace Front | 128x128 | `0x70800000` | `0x70580000` |

起動時に3モデルのcommand blobをRAMへ展開し、モデル切り替え時は再ロードしません。NPUのactivation領域はモデル間で共有します。モデルの取得と生成は[models/README.md](models/README.md)を参照してください。

## ソース構成

| パス | 内容 |
| --- | --- |
| `src/main.cpp` | `usermain()`。割り込み登録、カーネルオブジェクト作成、初期化タスク起動 |
| `src/task/application_initialize_task.cpp` | ドライバー初期化と各タスクの起動 |
| `src/task/camera_render_task.cpp` | カメラフレームの取得とLCD表示 |
| `src/task/pipeline_task.cpp` | モデル登録と3レーンのパイプライン実行 |
| `src/task/task_context.*` | タスク間で共有する資源、キュー、診断設定 |
| `src/task/task.hpp` | タスクループの共通骨格`Task::RunForever()` |
| `src/models/<model>/` | 生成コードのラッパー（`*_model_runtime.c`、`c_wrapper.h`）、`NpuNetwork`実装（`npu_model.*`）、`AiFuture`実装（`future.*`） |
| `models/` | モデル生成スクリプト、NPUメモリプール設定、生成物の出力先 |
| `config/` | CubeMX IOC、HAL設定、メモリ配置の入力 |
| `third_party/` | ST vision-models post-processing（[third_party/README.md](third_party/README.md)） |
| `fsbl/` | Flash起動用FSBL（[fsbl/README.md](fsbl/README.md)） |

新しいモデルを追加する場合は、`src/models/<model>/`に同じ3種類のファイルを用意し、`pipeline_task.cpp`に登録します。実装の要点は[docs/middleware/ai_runtime.md](../../docs/middleware/ai_runtime.md)にあります。

## 設定

| 設定 | 場所 |
| --- | --- |
| 推論モード（`kNpu`、`kCopyOnly`、`kDisabled`）、表示診断モード | `src/task/task_context.hpp` |
| UART診断（`DiagnosticsConfig`） | `src/task/task_context.hpp` |
| カメラの診断設定、Pipe2のフレームレート | `kernel/driver/config/ai_board_config.hpp` |
| ログレベル | `kernel/middleware/foundation/log.hpp`の`kLogLevel` |
| メモリ配置 | `config/board_memory.json`、`config/application_memory.json`、`config/model_layout.json` |

`DiagnosticsConfig`は既定で`inference_fps`だけが有効です。T-MonitorのUART出力は遅いため、フレーム単位の診断（`inference_trace`、`inference_input`など）は調査時だけ有効にしてください。`inference_input_display`を有効にすると、LCDにNPUへ渡す入力画像を表示します。

メモリ配置はビルド時に`host_app/auto_static_memory_layout`が解決し、リンカスクリプトと`static_memory_layout`用ヘッダを`build-ai-app-person/generated/`へ生成します。

## 主な生成物

`build-ai-app-person/userspace/ai-app/`に次を出力します。

| ファイル | 内容 |
| --- | --- |
| `ai-app.elf`、`ai-app.bin` | RAMへロードするアプリ本体 |
| `network_blobs_<model>.hex` | モデルごとのcommand blob（絶対アドレス付き） |
| `ai-app.map` | リンクマップ |

## トレース

| ターゲット | 内容 | 保存先 |
| --- | --- | --- |
| `thread-monitor` | AIパイプラインの実行トレースを取得してPNG化 | PSRAM `0x91C40000`（32 KiB） |
| `cpu-task-monitor` | タスク別CPU使用率とループ時間を取得してPNG化 | PSRAM `0x91C48000`（512 KiB） |

どちらも実行中のボードからST-LINKで読み出します。PSRAMは揮発性のため、リセット前に取得してください。解析ツールは[host_app/README.md](../../host_app/README.md)を参照してください。

## 関連文書

- [DESIGN.md](DESIGN.md): 表示優先とバッファ所有権の設計方針
- [VERIFICATION_CHECKLIST.md](VERIFICATION_CHECKLIST.md): 実機確認の項目
