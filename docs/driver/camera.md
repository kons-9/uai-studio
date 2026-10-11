# カメラ（IMX335 / CSI / DCMIPP / ISP）

## HWの役割

IMX335センサーの画像をCSI経由でDCMIPPへ取り込み、ISPで露出・ホワイトバランスなどを処理します。DCMIPPの2つの出力系統を使い、LCD表示とAI推論を並行して進めます。

公開ヘッダー: [camera_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/camera_driver/camera_driver.hpp)、[capture_configuration.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/camera_driver/capture_configuration.hpp)。名前空間は`uai::ai::camera`です。

| 通常のMemoryManager構成 | 出力 | アプリでの受け取り |
| --- | --- | --- |
| Pipe1 | RGB565、800x480 | `pipeline::CaptureFrame`。LCD表示用 |
| Pipe2 | RGB888、480x480。800x480画像を縮小してletterbox | `pipeline::InferenceFrame`。推論用 |

上記は共有MemoryManagerを使う通常経路です。`Initialize(CaptureConfiguration)`では出力サイズ・形式・バッファをユーザーが指定します。

## 初期化とAPI

[共通の初期化順序](common.md)に従い、RIF、PSRAM、キャッシュ、MemoryManagerを先に用意します。

| `CameraManagement`のAPI | 戻り値・動作 |
| --- | --- |
| `Instance()` / `Acquire(&accessor, timeout = TMO_FEVR)` | 管理インスタンス / 所有権を取得 |
| `Initialize(memory, cache)` | 通常の画像パイプラインを初期化。`common::Error` |
| `Initialize(configuration)` | ユーザー指定のPipe1/2バッファ、形式、geometryで初期化 |
| `Start()` / `Stop()` | センサーと取り込みを開始 / 停止 |
| `Running()` | 所有権を取得して稼働状態を`bool`で確認 |
| `Process()` | ISPの更新とフレーム停止時の復旧処理。タスクから継続的に呼ぶ |
| `Recover()` | センサーとDCMIPPの復旧を明示的に実行 |
| `KeepClocksOnSleep()` | カメラ関連のsleep時クロックを維持 |
| `TakeCompletedCapture(&frame)` | 通常経路のPipe1完了フレームを取得。未到着は`kNoFrame` |
| `TakeCompletedInference(&frame)` | 通常経路のPipe2完了フレームと`capture_sequence`を取得。未到着は`kNoFrame` |
| `SnapshotInferenceSource(&frame)` | Pipe2の有効画像とpaddingを`frame.source`へコピーし、`source_valid`を立てる |
| `SetNotifications(notifications)` | capture / inference / vsync / anomalyの`EventNotification`を設定 |
| `GetServiceWait(&remaining_ms)` | 次のサービス処理までの待ち時間を取得。通知待ちループに利用 |
| `GetDiagnostics()` | フレーム数、ドロップ、timeout、CSI/DCMIPP/ISPエラー、復旧の診断値 |

状態変更・取得メソッドの多くは`common::Error`を返します。`Accessor`経由ではDriverの対応メソッドに`accessor.Ownership()`を渡します。

### 画角・露出・色のAPI

| API | 引数・用途 |
| --- | --- |
| `GetGeometry(&geometry)` / `Configure(geometry)` | `Geometry`のfps、左右/上下反転、センサーcropを取得 / 設定 |
| `ReadState(&state)` / `ApplyState(state)` | 露出・ゲイン・ホワイトバランス・統計領域の状態を取得 / 適用 |
| `AutoExposure(enabled)` | 自動露出を有効 / 無効化 |
| `Compensation(half_stops)` | 半段単位の露出補正。-4〜4（-2〜+2段） |
| `Manual(exposure_us, gain_mdB)` | 手動露出時間（µs）とゲイン（mdB）。先に`AutoExposure(false)`が必要 |
| `Statistics(rectangle)` | ISP統計の対象領域。座標系はセンサー側であり、LCD座標ではない |
| `WhiteBalance(temperature)` | 0は自動、非ゼロは指定色温度によるホワイトバランス |
| `ListWhiteBalance(temperatures, capacity, &count)` | 利用できる色温度を取得 |

`Rect`は`x`、`y`、`width`、`height`を持ちます。`State`には自動制御の設定だけでなく`reported_exposure_us` / `reported_gain_mdB`などの取得値もあります。サポート範囲はセンサー・ISPの制約に従います。失敗時は戻り値を確認し、設定後の状態を`ReadState()`で読み直してください。

### ユーザー指定バッファ

`CaptureConfiguration`は`pipe1` / `pipe2`の`CaptureOutput`、`geometry`、`automatic_recovery`を持ちます。各出力はバッファ、幅、高さ、RGB565/RGB888形式を指定します。

- 先頭と容量は32byte境界、容量は全画素分以上、Pipe1/2のバッファ範囲は重複不可です。
- geometryのfpsは10〜30の5刻み、cropは2592x1944のセンサー領域内で、両出力サイズ以上が必要です。
- この経路はアプリ所有の固定バッファに取り込みます。通常経路の`TakeCompleted*()` / `SnapshotInferenceSource()`用MemoryManagerは設定されないため、同じ取得APIは使えません。
- バッファの寿命・キャッシュ操作・取り込み中の読み取り同期は呼び出し側で管理します。

## ユースケース: カメラ画像をLCDへ表示する

カメラとLCDを初期化し、`ShowInitialFrame()`、`camera.Start()`を成功させた後に、描画タスクのループから次の関数を呼びます。`kNoFrame`は通常の未到着状態として扱います。

```cpp
#include "driver/camera_driver/camera_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"

namespace ai = uai::ai;

ai::common::Error PollCameraPreview(const ai::inference::BoxSet &boxes)
{
    auto &camera = ai::camera::CameraManagement::Instance();
    const auto status = camera.Process();
    if (!status.Ok()) {
        return status;
    }
    ai::pipeline::CaptureFrame frame{};
    const auto captured = camera.TakeCompletedCapture(&frame);
    if (!captured.Ok()) {
        return captured;
    }
    return ai::lcd::LcdManagement::Instance().ComposeAndPresent(frame, boxes);
}
```

## ユースケース: Pipe2を推論タスクへ渡す

1. `TakeCompletedInference(&frame)`で受け取ります。
2. `SnapshotInferenceSource(&frame)`で入力画像を固定します。失敗した場合も取得したフレームを返却します。
3. 推論タスクへ所有権を移し、`frame.source`を前処理の入力として使用します。
4. 推論が終わった、送信に失敗した、処理を取り消した場合は`MemoryManager::ReleaseInferenceBuffer(frame)`で返します。

`capture_sequence`は結果と撮影画像を対応付ける情報です。タスク間転送や結果生成で失わないようにします。詳細は[ai_runtime](../middleware/ai_runtime.md)と[memory_manager](../middleware/memory_manager.md)を参照してください。

## 制約と診断

- `Process()`は通知や周期待ちと組み合わせ、継続的に実行します。通常は2秒のフレームtimeoutで自動復旧し、復旧再試行は5秒間隔です。`automatic_recovery`を無効にすると自動再起動しません。
- CSIエラーだけでは再起動せず、実際のフレーム停止を復旧条件にします。
- `GetDiagnostics()`の`pipe2_drop_count`、各pipeのframe age、`last_anomaly`、`recovery_error_count`から停止箇所を絞り込みます。
- [camera_diagnostics.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/camera_driver/camera_diagnostics.hpp)の`ReadSensorDiagnostics()`、`DumpCaptureRegisters()`で露出・ゲインやDCMIPP/CSIレジスタを調べます。
- センサーのテストパターン、Pipe2のフレームレートなどは[ai_board_config.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/config/ai_board_config.hpp)で設定します。
- 実機確認ではUARTの`camera: pipe1=started pipe2=started`を確認します。これは開始確認であり、継続的なフレーム到着は診断カウンターでも確認します。