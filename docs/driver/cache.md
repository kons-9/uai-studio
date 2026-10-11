# キャッシュ（D-cache / CACHEAXI）

## HWの役割

Cortex-M55のD-cacheはCPUのメモリアクセスを高速化し、CACHEAXIはNPU側のキャッシュを担います。CPUが見ているキャッシュの内容と、DMA・DCMIPP・LTDC・NPUが読み書きするメモリは、自動的に一致するとは限りません。バッファを受け渡す前後にキャッシュ整合を行います。

公開ヘッダー: [cache_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/cache_driver/cache_driver.hpp)。名前空間は`uai::ai::cache`です。

## 初期化とAPI

`NpuDriver::InitializeMemory()`の後で`CacheManagement::Initialize()`を呼びます。CPUのD-cacheを有効化し、CACHEAXIのクロック・リセット・有効化を行います。

| API | 戻り値・動作 |
| --- | --- |
| `CacheManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize()` | `common::Error`。再初期化は`kAlreadyInitialized` |
| `PrepareForPeripheralRead(buffer)` | CPUが書いた内容をcleanし、周辺機能が読めるようにする |
| `PrepareForDmaWrite(buffer)` | cleanしてからinvalidateし、DMA/NPUによる書き込みに備える |
| `PrepareForCpuRead(buffer)` | invalidateし、DMA/NPUが書いた内容をCPUで読めるようにする |
| `Acquire(&accessor, timeout = TMO_FEVR)` | 所有権を取得。Driverの操作には`accessor.Ownership()`を渡す |
| `KeepClocksOnSleep()` | CACHEAXIとCACHEAXIRAMのsleep時クロックを維持 |
| `CacheDriver::Clean(address, bytes)` | 生のアドレス範囲をclean。静的メソッド、`common::Error` |
| `CacheDriver::CleanInvalidate(address, bytes)` | 生のアドレス範囲をclean/invalidate |
| `CacheDriver::Invalidate(address, bytes)` | 生のアドレス範囲をinvalidate |

`PrepareFor*()`が受け付けるのは、有効な`buffer::Buffer`で、regionが`kCapture`、`kDisplay`、`kInference`のものです。未初期化は`kNotInitialized`、無効な範囲・regionは`kInvalidArgument`です。

## ユースケース: CPUで作った推論入力をNPUへ渡す

CPUによる入力生成が終わった後でcleanします。NPUが出力を書き終えた後には、CPUで読む前にinvalidateします。書き込み開始前の準備だけでは、完了後のinvalidateを代替できません。

```cpp
#include "driver/cache_driver/cache_driver.hpp"

namespace ai = uai::ai;

ai::common::Error PublishCpuInput(const ai::buffer::Buffer &input)
{
    return ai::cache::CacheManagement::Instance().PrepareForPeripheralRead(input);
}

ai::common::Error ReadCompletedOutput(const ai::buffer::Buffer &output)
{
    return ai::cache::CacheManagement::Instance().PrepareForCpuRead(output);
}
```

## 制約と診断

- `PrepareFor*()`は対象範囲を32byteのキャッシュライン境界へ外側に丸めます。隣接データを巻き込まないよう、バッファをキャッシュライン単位で分離してください。
- 静的な`Clean*()` / `Invalidate()`は丸めません。先頭と長さが32byte境界、長さが非ゼロで`INT32_MAX`以下、アドレス加算がoverflowしないことが必要です。これらは所有権や初期化状態の検査を行いません。
- キャッシュ操作はDMA完了待ちやバッファの排他を行いません。HWが書き込み中のバッファをCPUで読まないでください。
- 画像の一部が古い、推論結果が前回のままなら、実行順序とバッファの受け渡しを確認します。[DMA2D](dma2d.md)は自身で転送前後のキャッシュ操作を行います。

関連: [buffer](../middleware/buffer.md)、[memory_manager](../middleware/memory_manager.md)、[NPU](npu.md)。