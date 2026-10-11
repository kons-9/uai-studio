# DMA2D（画像転送・合成）

## HWの役割

DMA2Dはメモリ上の画像をコピー・形式変換・塗りつぶし・ブレンドする周辺機能です。CPUで全画素を処理する代わりにHWへ要求を渡します。共有ドライバーは同期APIで、転送が完了するか停止するまで所有権を保持します。

公開ヘッダー: [dma2d_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/dma2d_driver/dma2d_driver.hpp)。名前空間は`uai::ai::dma2d`です。要求の型は[image_processing](../middleware/image_processing.md)の`Request` / `Image`を使います。

## 初期化とAPI

カーネル起動後、HALのtick、キャッシュと転送先メモリのRIF設定を用意してから初期化します。`Initialize()`はDMA2Dのsecure/privileged属性、CID、通常・sleep時クロックを設定します。

| API | 戻り値・動作 |
| --- | --- |
| `Dma2dManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize()` | `common::Error`。再初期化は`kAlreadyInitialized` |
| `Transfer(request, timeout_ms = 100)` | 要求を検査して所有権を取得し、同期転送。`common::Error` |
| `Run(request, timeout_ms)` | 転送が成功したかを`bool`で返す。middleware検証器のI/F |
| `Acquire(&accessor, timeout = TMO_FEVR)` | 連続操作の所有権を取得 |
| `accessor->Transfer(request, timeout_ms, accessor.Ownership())` | 既に取得した所有権で同期転送 |
| `KeepClocksOnSleep()` | DMA2Dのsleep時クロックを維持 |
| `Dma2dDriver::ValidateRequest(request, timeout_ms)` | HALとキャッシュに触れず、要求が有効かを`bool`で返す |

### 要求の形式

`Image`は`data`、`bytes`（実際の容量）、`width`、`height`、`stride`（バイト単位）、`format`を持ちます。RGB565はlittle-endian、RGB888はR/G/B順です。

| `Request`の演算 | 入力 | 動作 |
| --- | --- | --- |
| `Operation::kBlit` | source、destination | 同寸法のコピー、RGB565/RGB888相互変換 |
| `Operation::kFill` | destination、color | RGB24色による塗りつぶし |
| `Operation::kBlend` | source、background、destination、alpha | 前景を8bit alphaで背景に合成 |
| `Operation::kResize` | 非対応 | `kInvalidArgument`で拒否 |

## ユースケース: RGB888バッファを塗りつぶす

初期化条件を満たしたタスクから呼ぶ例です。64byteの容量に、8x2画素、1行24byteの画像を置きます。画素外の余りは塗りつぶしません。

```cpp
#include "driver/dma2d_driver/dma2d_driver.hpp"

namespace ai = uai::ai;

ai::common::Error FillTestImage()
{
    alignas(32) static std::uint8_t pixels[64];
    ai::image_processing::Request request{};
    request.operation = ai::image_processing::Operation::kFill;
    request.color = 0x12ab34;
    request.destination = {pixels, sizeof(pixels), 8, 2, 24,
                           ai::image_processing::Format::kRgb888};
    auto &dma = ai::dma2d::Dma2dManagement::Instance();
    const auto initialized = dma.Initialize();
    if (!initialized.Ok() &&
        initialized.Code() != ai::common::ErrorCode::kAlreadyInitialized) {
        return initialized;
    }
    return dma.Transfer(request, 100);
}
```

## ユースケース: CPU参照実装と画素結果を比較する

`image_processing::Verification::Run()`のdeviceに初期化済みの`Dma2dManagement::Instance()`を渡します。検証器には25ケースあり、約25KiBの内蔵バッファを使うため静的領域などへ置きます。driverがキャッシュ操作を行うので、通常は検証器の追加cache hookは不要です。

## 制約と診断

- 入出力の先頭と容量は32byte境界、アドレス範囲は32bit内、容量は`INT32_MAX`以下が必要です。バッファの実在やDMAからのアクセス可否は呼び出し側で保証します。
- strideは画素サイズの倍数です。幅と行末offsetは14bit、高さは16bitに制限します。行末paddingへは書きません。
- 入出力の全容量範囲の重複、容量不足、不正stride、未対応形式、24bitを超えるfill色などは転送前に`kInvalidArgument`です。
- 入力・背景をcleanし、出力をclean/invalidateして開始します。完了または停止後に出力をinvalidateします。通常の転送では呼び出し側で追加のキャッシュ操作は不要です。
- timeoutは1〜1000msです。Managementのmutex待ちとHAL転送待ちは、それぞれ指定時間を上限にします。abortの時間もあるため、関数全体の厳密な時間上限ではありません。
- 初期化・layer設定の失敗時はresetします。開始・完了待ちの失敗時はabortし、abort失敗ならresetして、DMAがバッファへアクセスしなくなってから所有権を返します。途中失敗時の出力内容は保証しません。
- LCDやカメラが使用中のバッファを変更したり、BSP/HALから同じDMA2Dを並行操作したりしないでください。既存のLCD合成やResize APIはこのDriverへ自動で切り替わりません。
- ホストテストはHAL fixtureの設定・拒否・キャッシュ順序・timeout・abort/resetを確認するものです。実DMAの画素結果や実機キャッシュ整合は別途実機で確認します。