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

## 推論結果に応じた露出制御

[src/exposure_control/controller.hpp](src/exposure_control/controller.hpp)が領域選択・履歴・座標変換と表示値の整形を担当し、[runtime.hpp](src/exposure_control/runtime.hpp)がカメラ設定の適用・読み戻しを担当します。`PipelineTask`は累積した表示用結果とは別に、今回の推論結果だけを専用キューへ送ります。`CameraRenderTask`が結果を消費し、同じタスクからカメラを操作します。

- 顔、人物、segmentation前景の順に選び、顔・人物は最高信頼度の枠を1.5倍に拡張します。枠は800x480の表示座標、マスクは正方形入力の座標としてセンサ領域へ変換します。
- モデルごとに最後の有効な結果を保持します。空の結果では期限を延長せず、最後の被写体から1秒後に次の候補または全画面へ戻ります。小さな領域変化は抑制します。
- 設定と読み戻しは250ms間隔です。手動露出中は統計領域を操作せず、設定失敗とカメラ復旧後の設定消失は次回に再試行します。
- LCD上端の2行目に制御対象・実測露出・ゲイン、またはエラーを表示します。要求領域と適用領域は別に保持し、要求値を実測値として表示しません。
- `kAiExposureControl`はビルド時の機能ガードです。既定では有効で、設定画面の`AI EXPOSURE`ボタンから実行時にON/OFFを切り替えられます。OFFでは全画面AEに戻し、再ON時は古い推論結果を捨てます。枠表示ON/OFFとは独立です。手動のPI制御は追加していません。

領域変更時はUARTに`exposure: source=... area=... exposure_us=... gain_mdB=... ae=...`を出します。sourceは0=全画面、1=顔、2=人物、3=前景です。

ホスト確認は`cmake --build build/middleware-tests --target exposure_control_test frame_channels_test ui_layout_test`と対応するctestで行います。実機ではUARTを先に開き、起動とPipe1/2の開始に加え、UIからのON/OFF・領域変更・被写体消失時の全画面復帰・逆光での輝度改善・AWBへの影響・CPU時間と推論FPSを確認してください。未確認の判定基準は[VERIFICATION_CHECKLIST.md](VERIFICATION_CHECKLIST.md)にあります。ホストテストとARM構文チェックは実機確認を代替しません。

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
| `src/shell/` | UART行編集・コマンド登録・所有タスクへの要求/応答。コマンドごとに別の`.cpp`を配置 |
| `src/task/task_context.hpp` | 共有資源とタスク参照の保持、各タスク用コンテキストの組み立て |
| `src/task/application_initialize_task.hpp`、`src/task/camera_render_task.hpp`、`src/task/pipeline_task.hpp` | タスクごとに必要な依存を列挙するコンテキストとスタック。パイプラインのフレーム解放と結果選別 |
| `src/task/task_config.hpp` | 動作モード、診断設定、キュー・スタックのサイズ |
| `src/ui/ui_layout.hpp`、`src/ui/ui_layout_images.hpp` | 画面（ページ）と各ウィジェットの表、ロゴのRGB565ビットマップ。`config/ui_layout.json`と`config/ui/*.png`から[host_app/ui_designer](../../host_app/ui_designer/README.md)が生成（`make ui-layout`） |
| `src/ui/app_ui.cpp` | 画面の実行時状態とハンドラ。画面遷移、モデル・AI露出の有効/無効、枠表示と信頼度しきい値、ステータスラベルの更新 |
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

UIの機能IDと操作IDは`config/ui_feature_catalog.json`に定義します。`config/ui_layout.json`の`feature`/`operation`はこれと照合され、`make ui-layout`でC++のバインディングを生成します。MakeまたはCMakeからのビルド時には`ui-layout-check`で生成物の一致も確認します。カタログはID一覧であり、実行時の制約は[shell/owner.hpp](src/shell/owner.hpp)の`CheckConstraints`で判定します。

画面は2つあります。カメラ画面の上端にモデル別FPSと露出の読み戻しを表示し、右上のメニューボタンで設定画面へ移ります。設定画面の`PERSON`・`FACE`・`SEG`、`INFERENCE OVERLAY`、`AI EXPOSURE`はカメラ所有タスクの`ApplyTouch`からシェルと同じ`Apply`へ渡し、確定した状態をボタンへ反映します。confidenceのスライダーと更新周期のダイヤルはUI内の値を変更します。左上の矢印でカメラ画面へ戻ります。

AI露出ON中はAEの手動切替・手動値設定・独自統計領域を拒否します。手動値は`camera ae off`後だけ受け付けます。AI露出がビルドで利用不可ならボタンを暗くしてタッチを無効化し、シェルからの有効化も拒否します。読み戻しで失敗した露出操作は確定せず、直前の制御値を再適用します。復元失敗は状態不明として通知するため、`camera status`で実値を確認してから再設定してください。設定画面の`APPLIED` / `APPLY ERROR <code>`は最後の操作結果であり、定期統計更新では消しません。

タッチコントローラ（GT911、I2C2）の初期化に失敗しても起動は続行し、`touch: controller unavailable; on-screen UI disabled`を出します。レイアウト変更は`make -C userspace/ai-app ui-designer`で編集し、`make -C userspace/ai-app ui-layout`でヘッダを再生成します。このコマンドは`clang-format-21`も使います（[docs/middleware/ui.md](../../docs/middleware/ui.md)）。

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

ST-LINKからの読み出しに加え、シェルの`trace ai` / `trace cpu`でUART経由の転送もできます。PSRAMは揮発性のため、リセット前に取得してください。解析ツールは[host_app/README.md](../../host_app/README.md)を参照してください。

## UARTシェル

ボードのST-LINK VCPを115200 bps、8N1で開きます（`make -C userspace/ai-app monitor`）。モデル変更後は先に`make -C userspace/ai-app ai-load`を実行し、**UARTモニターを開いたまま別の端末で**`make -C userspace/ai-app ram-run`を実行します。起動時の`camera: pipe1=started pipe2=started`と`shell ready; type help`を確認し、コマンドを改行で送ってください。CR/LF、Backspaceに対応します。受信エラーや長すぎる行は破棄されます。

| コマンド | 内容 |
| --- | --- |
| `help`、`uptime`、`tasks`、`memory` | コマンド一覧、稼働時間、タスク状態、静的に予約したバッファ容量（実使用ヒープ量は取得不可） |
| `log [error\|warn\|info\|debug\|trace]` | ログレベルの取得・変更 |
| `camera [status\|ae on/off\|comp -4..4\|manual us mdB\|stats x y w h\|fps 10..30\|flip h v\|crop x y w h]` | 状態・診断値の取得とカメラ設定。`manual`は`ae off`後、`stats`はAI露出OFF時だけ受け付ける |
| `models [none\|person\|face\|seg\|person+face\|all\|0..7]` | 現在のモデル選択の表示・切り替え |
| `ui [status\|boxes on/off\|exposure on/off]` | UI状態と枠/AI露出の切り替え |
| `diag <frame\|brightness\|input\|input_display\|inference\|fps\|display\|timing> [on/off]` | 実行時診断の取得・変更 |
| `trace <ai\|cpu> [pause\|resume\|status]` | 指定なしなら転送中だけ停止する。`pause`は記録停止を保持し、`resume`までUART転送後もリングを固定する。`status`は保持状態を表示する |

カメラとUIの変更は表示タスクへ要求し、適用失敗時にはエラーコードを返します。応答がタイムアウトした場合も要求が後から適用され得るため、`camera status`または`ui status`で確認してください。`trace cpu`は512 KiBをHEXで転送するので115200 bpsでは数分かかり、転送中はCPUトレースの記録が停止します。転送ログはminicomのキャプチャ（Ctrl-A、L）等で保存し、次のように検証・復元します。

```sh
python3 host_app/ai_model_monitor/decode_uart_trace.py /tmp/ai-uart.log /tmp/ai-trace.bin
python3 host_app/ai_model_monitor/ai_model_monitor.py decode /tmp/ai-trace.bin -o /tmp/ai-trace.json
python3 host_app/ai_model_monitor/decode_uart_trace.py /tmp/cpu-uart.log /tmp/cpu-trace.bin
uv run --project host_app python host_app/cpu_task_monitor/cpu_task_monitor.py /tmp/cpu-trace.bin --json /tmp/cpu-trace.json --csv /tmp/cpu-trace.csv
```

UARTとST-LINKの同一スナップショット比較では、シェルで`trace ai pause`、`trace ai`を送り、`@TRACE END`までログを保存します。停止を保持したまま、別端末で起動中FWと一致するビルドから`thread-monitor-dump`を取得し、次を実行します。

```sh
make -C userspace/ai-app thread-monitor-dump THREAD_MONITOR_DUMP=/tmp/ai-swd.bin
python3 host_app/ai_model_monitor/decode_uart_trace.py /tmp/ai-uart.log /tmp/ai-trace.bin --compare /tmp/ai-swd.bin
```

取得後はシェルで`trace ai resume`を送ってください。CPUも`trace cpu pause`・`trace cpu`と`cpu-task-monitor-dump`で同様に取得し、最後に`trace cpu resume`します。比較中にリセット・書込み・記録再開を行わず、失敗や中断時にも明示的に再開します。記録停止はカメラ・推論の停止ではありません。

新しいコマンドは[src/shell/engine.hpp](src/shell/engine.hpp)の固定容量レジストリへ登録します。[src/shell/commands.hpp](src/shell/commands.hpp)の`RegisterAll`に登録関数を追加し、`src/shell/<command>.cpp`に引数検証と応答を実装してCMakeへ追加してください。カメラなど状態変更を伴う操作はシェルから直接触らず、[src/shell/mailbox.hpp](src/shell/mailbox.hpp)の要求を表示タスクへ渡します。

## 関連文書

- [TODO.md](TODO.md): 実機確認の手順・合格条件・記録欄
