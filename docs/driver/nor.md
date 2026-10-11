# NOR Flash（MX66UW1G45G / XSPI2）

## HWの役割

STM32N6570-DKのMX66UW1G45GはXSPI2に接続された128MiBの外部NOR Flashです。AIモデルの重みやcommand blobを保持し、メモリマップ時は`0x70000000`を基点とする領域から参照します。PSRAMと違い、通常のポインター書き込みで内容を変更する用途には使いません。

公開ヘッダー: [nor_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/nor_driver/nor_driver.hpp)。名前空間は`uai::ai::nor`です。

## 初期化とAPI

[RIF](rif.md)、[PSRAM](psram.md)の順で初期化した後に使用します。PSRAMの初期化によるXSPIMリセットより先にNORを初期化しないでください。

| API | 戻り値・動作 |
| --- | --- |
| `NorManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize()` | `int`。成功・初期化済みは`0`、失敗は`-1`。BSP初期化、16byteのprobe読み出し、メモリマップ有効化を行う |
| `Acquire(&accessor, timeout = TMO_FEVR)` | `common::Error`。初期化済みのNORに対する所有権を取得 |
| `OpenReadOnly(&accessor, timeout = TMO_FEVR)` | 所有権と読み取り用の初期化を用意する。メモリマップ有効化は行わない |
| `accessor.Valid()` | 有効なDriverと所有権を持っているか |
| `accessor.Read(offset, output, bytes)` | `common::Error`。Flash先頭からのoffsetを指定して読み出す |
| `accessor.Diagnostics()` | 初期化・読み出し診断の`NorDriver::Diagnostic`を取得 |
| `accessor.KeepClocksOnSleep()` | XSPI2のsleep時クロックを維持 |

NORの`Accessor`は専用クラスです。他のドライバーのような`accessor->`や`Ownership()`ではなく、上表のメソッドを直接使います。`Acquire()`だけではHWを初期化しません。

## ユースケース: モデルをメモリマップで参照する

起動タスクで`Initialize()`が`0`を返したことを確認します。その後、リンカやモデルパッケージが指定するアドレスからモデルを参照できます。`config::kModelNorProbeOffset`のprobeは読み出し可否の確認であり、モデル全体の内容や形式を保証する検証ではありません。

ai-appではNORが使えない場合、推論を無効化してカメラ表示を継続します。詳細は[初期化タスク](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/src/task/application_initialize_task.cpp)を参照してください。

## ユースケース: ヘッダーやデータを読み出す

以下はRIFとPSRAMの準備が済んだ状態での例です。`offset`は`0x70000000`から始まるCPUアドレスではなく、Flash内の相対位置です。

```cpp
#include "driver/nor_driver/nor_driver.hpp"

namespace ai = uai::ai;

ai::common::Error ReadNorData(std::uint32_t offset, std::uint8_t *output,
                             std::size_t bytes)
{
    auto &nor = ai::nor::NorManagement::Instance();
    ai::nor::NorManagement::Accessor accessor;
    const auto status = nor.OpenReadOnly(&accessor);
    if (!status.Ok()) {
        return status;
    }
    return accessor.Read(offset, output, bytes);
}
```

## 制約と診断

- `Read()`は非nullの出力、非ゼロの長さ、128MiB内に収まる範囲を要求します。不正な要求は`kInvalidArgument`、読み取り未準備は`kNotInitialized`、BSPの失敗は`kHardware`です。
- `Accessor`を保持中に同じ管理インスタンスの初期化や再取得を行わないでください。メモリマップをモデルが使っている間の再設定も避けます。
- 書き込み・消去APIはありません。モデルの更新はSTM32CubeProgrammerとExternal Loaderで行います。
- 設定は[mx66uw1g45g_conf.h](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/config/mx66uw1g45g_conf.h)と[ai_board_config.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/config/ai_board_config.hpp)です。アクセス拒否の調査には[ボード診断](board.md)を使います。