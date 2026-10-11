# RIF（Resource Isolation Framework）

## HWの役割

RIFは周辺機能やメモリへのアクセスを、secure/privileged属性やCID（コンパートメントID）で制御する仕組みです。CPUから読めるメモリでも、DCMIPPやNPUなど別のバスマスターからのアクセスは拒否されることがあります。

共有ドライバーは起動時にアプリの画像処理・推論に必要な設定を行います。汎用的な権限管理APIではなく、現在のボード構成を前提とした初期化です。

公開ヘッダー: [rif_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/rif_driver/rif_driver.hpp)。名前空間は`uai::ai::rif`です。

## 初期化とAPI

| API | 戻り値・動作 |
| --- | --- |
| `RifManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize()` | `common::Error`。RIFSC、RISAF、IACのクロックとアクセス設定を準備。再初期化は`kAlreadyInitialized` |
| `Acquire(&accessor, timeout = TMO_FEVR)` | 所有権取得の共通I/F。通常のアプリ利用では不要 |

実行時にアクセス権を書き換える公開操作や`KeepClocksOnSleep()`はありません。`Initialize()`を[RIF実装](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/rif_driver/rif_driver.cpp)が直接制御します。

| 設定対象 | 役割 |
| --- | --- |
| RISAF2〜7 | 内部メモリ領域のアクセス設定 |
| RISAF11 | XSPI1のPSRAM領域。カメラ書き込みとLCD表示で使用 |
| RISAF12 | XSPI2のNOR領域。モデル参照で使用 |
| NPU、DMA2D、DCMIPP、LTDCのマスター属性 | CIDとsecure/privileged属性を設定 |
| XSPI、CSI、DCMIPP、LTDC、NPUなどのスレーブ属性 | 周辺機能へのアクセス属性を設定 |
| IAC | 対象アクセス違反の割り込みを有効化 |

## ユースケース: コールドブート後に外部メモリを使う

カーネル起動後の初期化タスクから、PSRAM/NORの初期化前に呼びます。

```cpp
#include "driver/rif_driver/rif_driver.hpp"

namespace ai = uai::ai;

ai::common::Error ConfigureMemoryAccess()
{
    const auto status = ai::rif::RifManagement::Instance().Initialize();
    if (status.Code() == ai::common::ErrorCode::kAlreadyInitialized) {
        return {};
    }
    return status;
}
```

## 制約と診断

- 外部メモリ初期化前に設定することが重要です。NOR側のRISAF12だけを設定しても、PSRAM側のRISAF11が未設定ならDCMIPPの書き込みを許可できません。
- secure/privileged/CID設定はボード全体の設計に関わります。非secure実行など別の構成へ移行する場合、現在の固定設定をそのまま使わないでください。
- `Initialize().Ok()`はソフトウェア上の初期化完了を示すもので、すべてのバスマスターの実アクセスが成功することの実機証明ではありません。
- 外部メモリでfaultする、カメラがフレームを出さない、NPUが応答しない場合は、[ボード診断](board.md)のRIFSC/RISAF/IACダンプと、[カメラ診断](camera.md)を組み合わせます。

関連: [共通ルールと初期化順序](common.md)、[PSRAM](psram.md)、[NOR](nor.md)。