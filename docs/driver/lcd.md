# LCD（LTDC / 800x480表示）

## HWの役割

LTDCはメモリ上のRGB565画像を読み、STM32N6570-DKの800x480 LCDへ表示します。`LcdManagement`は表示バッファの確保、カメラ画像のコピー、検出枠・マスク・UIの合成と、LTDCへの受け渡しをまとめます。

公開ヘッダー: [lcd_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/lcd_driver/lcd_driver.hpp)。名前空間は`uai::ai::lcd`です。LTDCにバッファを渡す低レベルAPIは[display_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/lcd_driver/display_driver.hpp)にあります。

## 初期化とAPI

RIF、PSRAM、キャッシュ、MemoryManagerを先に用意して`Initialize(memory, cache)`を呼びます。GT911とLCDはリセット線を共有するため、[タッチ](touch.md)はLCDの後で初期化します。

| `LcdManagement`のAPI | 戻り値・動作 |
| --- | --- |
| `Instance()` / `Acquire(&accessor, timeout = TMO_FEVR)` | 管理インスタンス / 所有権を取得 |
| `Initialize(memory, cache)` | 表示HWと合成用の依存を初期化。`common::Error` |
| `KeepClocksOnSleep()` | 表示関連のsleep時クロックを維持 |
| `ShowInitialFrame(boxes, coordinate_pattern = false)` | カメラ開始前の画面を用意。座標確認パターンも選択可能 |
| `GenerateCoordinatePattern(destination)` | 指定の`buffer::Buffer`へ座標確認パターンを生成 |
| `ComposeAndPresent(capture, boxes, overlay = nullptr, log_copy_crc = false)` | カメラ画像をコピーし、検出枠・マスク・UIを重ねて表示を予約 |
| `ComposeInferenceAndPresent(frame)` | Pipe2の推論入力画像を確認する表示。`frame.buffer`を参照 |
| `PresentOverlay(overlay)` | カメラ画像をコピーせず、`ui::Painter`だけで画面を作る |
| `SynchronizeCurrentFrame()` | LTDCの表示切り替えを同期し、MemoryManagerのバッファ受け渡しを完了 |
| `SetTimingDiagnostics(enabled)` | コピー時間などの診断ログを有効化。戻り値は`void` |

表示・合成操作は`common::Error`を返します。`Accessor`経由のDriver操作ではWriterを渡します。`ComposeAndPresent()`のDriver版は`capture, boxes, writer, overlay, log_copy_crc`の引数順です。

### 低レベル表示API

自前のRGB565表示バッファを使う場合は`DisplayManagement`を利用できます。

| API | 動作 |
| --- | --- |
| `DisplayManagement::Instance()` / `Acquire(&accessor, timeout)` | 管理インスタンス / 所有権を取得 |
| `Initialize(initial)` | 初期バッファを設定してLTDCを初期化 |
| `Present(frame)` | 次の表示バッファへの切り替えを予約 |
| `Synchronize()` | 予約した表示切り替えの完了を待つ |
| `Initialize(initial, writer)` / `Present(frame, writer)` / `Synchronize(writer)` | 所有権を保持したまま対応操作を実行 |

表示サイズは`kDisplayWidth = 800`、`kDisplayHeight = 480`、容量は`kDisplayBytes = 768000`と完全一致、regionは`kDisplay`、先頭は32byte境界が必要です。この経路はカメラ画像のコピー、UI合成、MemoryManagerの受け渡しを行いません。表示前のcache cleanはAPIが行いますが、描画中・表示中の排他とバッファの寿命は呼び出し側の責任です。

## ユースケース: カメラ画像に検出結果とUIを重ねる

以下は初期化済みLCDへ、取得済みのPipe1フレームを渡す例です。`boxes`に検出結果、`overlay`にUIのPainterを渡します。UIを使わないときは`nullptr`を渡せます。

```cpp
#include "driver/lcd_driver/lcd_driver.hpp"

namespace ai = uai::ai;

ai::common::Error ShowDetection(const ai::pipeline::CaptureFrame &capture,
                               const ai::inference::BoxSet &boxes,
                               const ai::ui::Painter *overlay)
{
    auto &lcd = ai::lcd::LcdManagement::Instance();
    const auto status = lcd.ComposeAndPresent(capture, boxes, overlay);
    if (!status.Ok()) {
        return status;
    }
    return lcd.SynchronizeCurrentFrame();
}
```

各合成APIは次の表示バッファを確保する前にも前回の表示切り替えを同期します。上のように明示的に同期すると、単発の表示後も受け渡し完了を確認できます。

## ユースケース: カメラを使わないメニュー画面

全画素を描く`ui::Painter`を用意して`PresentOverlay(painter)`を呼びます。前の画面の背景はコピーされないため、ラベルやボタンだけを描くPainterでは未描画部分が残ります。背景から描く例は[ui](../middleware/ui.md)を参照してください。

## 制約と診断

- LTDCが読んでいるバッファを直接変更・再利用しないでください。表示切り替え予約と、実際にVBlankで切り替わった時点は別です。
- 表示バッファの取得・返却とキャッシュ操作は合成APIが担当します。通常の利用でアプリが表示バッファを別途返却する必要はありません。
- Pipe2の表示APIはフレームを返却しません。使用後は呼び出し側で`ReleaseInferenceBuffer()`を行います。
- 現在の`ComposeAndPresent()`はCPUの`memcpy`でコピーします。[DMA2D](dma2d.md)ドライバーを初期化しても、自動的にDMA2D合成へ切り替わるわけではありません。
- `LcdManagement`と`DisplayManagement`は同じ所有権を共有します。一方のAccessorを持ったまま、他方の所有権取得付きAPIを呼ばないでください。
- コピー不一致には`log_copy_crc`、処理時間には`SetTimingDiagnostics(true)`、表示位置にはcoordinate patternを使います。

関連: [カメラ](camera.md)、[memory_manager](../middleware/memory_manager.md)、[キャッシュ](cache.md)。