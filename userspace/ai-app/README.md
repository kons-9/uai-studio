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
| `src/task/camera_render_task.cpp` | カメラフレームの取得とLCD表示。タッチをポーリングし、画面上のボタンを処理 |
| `src/task/pipeline_task.cpp` | モデル登録と3レーンのパイプライン実行 |
| `src/task/task_context.hpp` | 共有資源とタスク参照の保持、各タスク用コンテキストの組み立て |
| `src/task/application_initialize_task.hpp`、`src/task/camera_render_task.hpp`、`src/task/pipeline_task.hpp` | タスクごとに必要な依存を列挙するコンテキストとスタック。パイプラインのフレーム解放と結果選別 |
| `src/task/task_config.hpp` | 動作モード、診断設定、キュー・スタックのサイズ |
| `src/ui/ui_layout.hpp`、`src/ui/ui_layout_images.hpp` | 画面（ページ）と各ウィジェットの表、ロゴのRGB565ビットマップ。`config/ui_layout.json`と`config/ui/*.png`から[host_app/ui_designer](../../host_app/ui_designer/README.md)が生成（`make ui-layout`） |
| `src/ui/app_ui.cpp` | 画面の実行時状態とハンドラ。画面遷移、モデルの有効/無効、枠表示と信頼度しきい値、ステータスラベルの更新 |
| `src/task/model_control.hpp` | モデルマスクとパイプライン統計のインターフェース。`PipelineTask`が実装し、UIが参照 |
| `kernel/middleware/foundation/error_code.hpp` | ログ・OS非依存のエラーコードとコード名 |
| `kernel/middleware/foundation/error.hpp` | ログ・OS非依存のエラー構造体と判定。`LogStatus()`の実装は`error.cpp`に配置 |
| `kernel/middleware/task/task.hpp` | μT-Kernelタスクの起動、ループ、停止の共通処理 |
| `kernel/middleware/buffer/stable_aligned_bytes.hpp` | サイズと8バイト整列を保証し、コピー・移動を禁止する固定アドレスのバイト領域 |
| `kernel/middleware/message_channel/message_channel.hpp` | メッセージバッファの領域所有、生成と型付き送受信 |
| `src/models/<model>/` | 生成コードのラッパー（`*_model_runtime.c`、`c_wrapper.h`）、`NpuNetwork`実装（`npu_model.*`）、`AiFuture`実装（`future.*`） |
| `models/` | モデル生成スクリプト、NPUメモリプール設定、生成物の出力先 |
| `config/` | CubeMX IOC、HAL設定、メモリ配置の入力、画面レイアウト（`ui_layout.json`） |
| `third_party/` | ST vision-models post-processing（[third_party/README.md](third_party/README.md)） |
| `fsbl/` | Flash起動用FSBL（[fsbl/README.md](fsbl/README.md)） |

新しいモデルを追加する場合は、`src/models/<model>/`に同じ3種類のファイルを用意し、`pipeline_task.cpp`に登録します。実装の要点は[docs/middleware/ai_runtime.md](../../docs/middleware/ai_runtime.md)にあります。

## 設定

| 設定 | 場所 |
| --- | --- |
| 推論モード（`kNpu`、`kCopyOnly`、`kDisabled`）、表示診断モード | `src/task/task_config.hpp` |
| UART診断（`DiagnosticsConfig`） | `src/task/task_config.hpp` |
| カメラの診断設定、Pipe2のフレームレート | `kernel/driver/config/ai_board_config.hpp` |
| ログレベル | `kernel/middleware/foundation/log.hpp`の`kLogLevel` |
| メモリ配置 | `config/board_memory.json`、`config/application_memory.json`、`config/model_layout.json` |
| 画面上のボタン、タッチのポーリング周期 | `config/ui_layout.json`（`make ui-designer`で編集）、`src/task/task_config.hpp`の`kTouchPollPeriod` |

`task_config.hpp`は「何を選び、いくつ確保するか」を定義し、`TaskContext`は起動後に共有するタスクへの参照と資源を保持します。例えば`kFrameQueueDepth`は設定、`context.pipeline_task.InferenceFrames()`はその深さの保存領域とキューを持つ実体へのアクセサです。推論結果はパイプラインタスクが保持し、カメラタスクが`TryGetLatestResult()`で待たずに最新の有効な結果を取得します。`DiagnosticsConfig`は診断の設定項目と既定値を定義し、`context.diagnostics`は起動中に参照するその設定値です。

実行時は`ApplicationInitializeContext`、`CameraRenderContext`、`PipelineFrameContext`、`PipelineWorkerContext`を各タスクへ渡し、タスクが使わない資源は含めません。これらはタスクのヘッダに定義し、起動入口で`TaskContext`から組み立てます。

セグメンテーションの20x20 maskは推論結果の固定長バッファに値として含めるため、推論出力が再利用されても表示中のmaskは変わりません。結果キューが満杯なら`kBufferOverflow`を返し、パイプラインタスクが最古の結果を捨てて再送します。再送できなかった場合は失敗をログに残します。

`DiagnosticsConfig`は既定で`inference_fps`だけが有効です。T-MonitorのUART出力は遅いため、フレーム単位の診断（`inference_trace`、`inference_input`など）は調査時だけ有効にしてください。`inference_input_display`を有効にすると、LCDにNPUへ渡す入力画像を表示します。

メモリ配置はビルド時に`host_app/auto_static_memory_layout`が解決し、リンカスクリプトと`static_memory_layout`用ヘッダを`build-ai-app-person/generated/`へ生成します。

画面は2つあります。カメラ画面では下段の`PERSON`、`FACE`、`SEG`ボタンが各モデルの推論を有効/無効にし（有効なモデルは色付き）、`BOXES`は検出枠とマスクの表示を切り替えます。上端のラベルはモデルごとの推論レート（`PERSON 7.5  FACE 7.3  SEG --  FPS`）、右側の数値表示`DET`は直近の検出数です。右上の丸いハンバーガーをタップすると単色背景の設定画面に切り替わり、スライダーで表示する枠の最小信頼度（`MIN CONFIDENCE %`）、ダイヤルでステータスの更新周期（`STATUS MS`、100〜2000 ms）を変えられます。`MODELS`ホイールを上下にドラッグするとモデルの組み合わせ（`ALL`、`PERSON`、`FACE`、`SEG`、`PERSON+FACE`）を一度に選べ、カメラ画面のボタンと互いに同期します。`PERSON FPS`は人物検出のレートを数値で出し、右上にはロゴ画像（`config/ui/logo.png`）を表示します。左上の丸い矢印でカメラ画面に戻ります。操作はUARTに`ui: tap id=4 models=6`、`ui: screen=1`、`ui: min confidence=35%`、`ui: wheel=FACE models=2`のように出ます。タッチコントローラ（GT911、I2C2）の初期化に失敗しても起動は続行し、`touch: controller unavailable; on-screen UI disabled`を出します。ウィジェットや画面の追加・変更は`make -C userspace/ai-app ui-designer`（または`host_app/ui_designer`のCLI）で行い、`make -C userspace/ai-app ui-layout`でヘッダを再生成します。ハンドラは`src/ui/app_ui.cpp`にあります（[docs/middleware/ui.md](../../docs/middleware/ui.md)）。

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
