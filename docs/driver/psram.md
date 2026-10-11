# PSRAM（APS256XX / XSPI1）

## HWの役割

STM32N6570-DKのAPS256XXは、XSPI1経由で接続された外部PSRAMです。共有ドライバーで初期化すると、`0x90000000`を基点とするメモリマップ領域として使えます。カメラのフレームやLCDの表示バッファなど、内部RAMだけでは容量が足りないデータの置き場になります。

公開ヘッダー: [psram_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/psram_driver/psram_driver.hpp)。名前空間は`uai::ai::psram`です。

## 初期化とAPI

カーネル起動後、HALのtickを使用できる状態で、[RIF](rif.md)を先に初期化します。PSRAMのBSP初期化はXSPIMをリセットするため、[NOR](nor.md)より先に行います。

| API | 戻り値・動作 |
| --- | --- |
| `PsramManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize()` | `bool`。成功・初期化済みは`true`、初期化や所有権取得の失敗は`false` |
| `Acquire(&accessor, timeout = TMO_FEVR)` | `common::Error`。sleep設定などに使う所有権を取得 |
| `KeepClocksOnSleep()` | XSPI1のsleep時クロックを維持 |
| `accessor->KeepClocksOnSleep(accessor.Ownership())` | 保持中の所有権でsleep設定を行う |

公開APIに`Read()` / `Write()`はありません。初期化後はメモリマップ領域を通常のメモリとして使用します。

## ユースケース: 画像バッファ用の外部RAMを準備する

次の関数は初期化タスクから呼ぶ例です。この後にNOR、LCD、カメラを初期化します。

```cpp
#include "driver/psram_driver/psram_driver.hpp"
#include "driver/rif_driver/rif_driver.hpp"

namespace ai = uai::ai;

ai::common::Error InitializeExternalRam()
{
    const auto status = ai::rif::RifManagement::Instance().Initialize();
    if (!status.Ok() && status.Code() != ai::common::ErrorCode::kAlreadyInitialized) {
        return status;
    }
    auto &ram = ai::psram::PsramManagement::Instance();
    if (!ram.Initialize()) {
        return {ai::common::ErrorCode::kHardware};
    }
    ram.KeepClocksOnSleep();
    return {};
}
```

アプリで画像領域を確保するときは、任意の固定アドレスへ書くのではなく、[memory_manager](../middleware/memory_manager.md)とリンカのメモリ配置に従います。

## 制約と診断

- メモリマップ領域への直接アクセスは`Accessor`で排他されません。バッファの寿命と所有権は上位層で管理します。
- DMA/DCMIPP/LTDCも同じPSRAMへアクセスします。[キャッシュ](cache.md)の操作とRIFのアクセス権設定が必要です。
- アドレスが合っていても、未初期化、sleep時クロック停止、RIFでの拒否があれば正常に使えません。[ボード診断](board.md)でクロックやRIFの状態を確認し、XSPIの初期化結果と併せて調べます。
- 部品設定は[aps256xx_conf.h](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/config/aps256xx_conf.h)にあります。