# 共通ルールと初期化順序

## レイヤーとリンク

共有ドライバーはSTM32N6570-DK向けです。アプリからは公開C++ APIを使い、HAL/BSPやレジスタの詳細はDriver内へ閉じ込めます。画像の前処理、推論結果、UIの状態などはmiddleware・アプリで管理します。

| 層・ターゲット | 役割 |
| --- | --- |
| `<Name>Management` | 唯一のDriverと所有権管理を持つ |
| `<Name>Driver` | HWの初期化・状態遷移・操作を実行 |
| `registers/`、ボード実装、HAL/BSP | レジスタと部品固有の操作 |
| `uai::drivers` | 共有ドライバーの静的ライブラリ |
| `uai::driver_overrides` | HAL時刻とDCMIPP callbackのstrong実装を直接リンクするOBJECTターゲット |

ビルド定義は[CMakeLists.txt](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/CMakeLists.txt)、詳細は[ビルド構成](../kernel/build.md)です。汎用周辺ドライバーは`UAI_DRIVER_PERIPHERAL_TESTS`構成で追加され、この構成ではNPUドライバーのソースが除外されます。

## 所有権と呼び出し方

`driver::ResourceManagement`は[resource_ownership](../middleware/resource_ownership.md)の`ResourceOwnership<MicroTKernelMutexBackend>`の別名です。`Writer`はHWを操作する権利で、対応するμT-Kernel mutexを保持します。

通常の呼び方は2つです。

1. Managementの簡易APIを使う。呼び出しごとに所有権を取得し、終了時に解放します。
2. `Acquire(&accessor, timeout)`で所有権を保持し、DriverのAPIに`accessor.Ownership()`を渡します。

```cpp
#include "driver/camera_driver/camera_driver.hpp"

namespace ai = uai::ai;

ai::common::Error StartCaptureOwned()
{
    ai::camera::CameraManagement::Accessor accessor;
    const auto status = ai::camera::CameraManagement::Instance().Acquire(&accessor);
    if (!status.Ok()) {
        return status;
    }
    return accessor->Start(accessor.Ownership());
}
```

この例はカメラ初期化済みのタスクから呼びます。Accessorはムーブのみ可能で、スコープ終了時に所有権を返します。`Acquire()`のtimeoutはμT-Kernelの`TMO`で、既定値`TMO_FEVR`は無期限待ちです。HW操作の`timeout_ms`とは別です。

### 排他の注意

- Accessor保持中に同じDriverのManagement簡易APIを呼ばないでください。非再帰mutexの再取得で待ち続けます。保持中はWriter付きDriver APIを使います。
- `LcdManagement`と`DisplayManagement`は同じ所有権を共有します。クラス名が違っても再取得を避けます。
- 複数Driverの所有権を同時に持つ場合は、取得順序をアプリ全体で統一し、他タスクとの循環待ちを防ぎます。
- 所有権はHW操作を排他するもので、渡したバッファの寿命や他DriverからのDMAアクセスまで保証しません。
- ISRからブロックする簡易APIや所有権取得を呼ばず、IRQはタスクへ通知し、操作はタスクで行います。

### 共通形の例外

| 対象 | 違い |
| --- | --- |
| PSRAM | `Initialize()`は`bool`。初期化済みも`true` |
| NOR | `Initialize()`は`int`。成功・初期化済みは`0`。専用Accessorで`Read()`などを直接呼ぶ |
| NPU | 多くの操作が`npu::Status`。実行はAccessor経由のDriver APIを使う |
| 汎用周辺 | `Initialize()`なし。`Acquire()`で管理を用意し、Accessorの`Close()`でTIM2/RTCも解放 |
| RIF、タッチ、コンソール、汎用周辺 | `KeepClocksOnSleep()`は公開されていない |
| ボード共通 | Singleton/Accessorではなく関数API |

## 初期化の順序

ai-appの[初期化タスク](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/src/task/application_initialize_task.cpp)を基準にした順序です。先にカーネルを起動し、`HAL_ResumeTick()`と`driver::board::ConfigureReferenceInterruptPriorities()`でHALのtickと割り込み優先度を整えます。

| 順序 | 呼び出し | 依存・役割 |
| --- | --- | --- |
| 1 | `NpuDriver::InitializeMemory()` | NPUとNPU用RAMのクロックを準備 |
| 2 | `CacheManagement::Initialize()` | D-cacheとCACHEAXIを有効化 |
| 3 | `MemoryManager::Initialize()` | バッファプールと管理情報を用意 |
| 4 | `RifManagement::Initialize()` | 外部メモリとバスマスターのアクセス権を設定 |
| 5 | `PsramManagement::Initialize()` | XSPI1 PSRAMをメモリマップ |
| 6 | `NorManagement::Initialize()` | 推論に必要なXSPI2 NORをメモリマップ |
| 7 | `LcdManagement::Initialize(memory, cache)` | 表示HWを初期化 |
| 8 | `CameraManagement::Initialize(memory, cache)` | センサーと画像パイプラインを初期化 |
| 9 | `TouchManagement::Initialize()` | LCDと共有するリセット線を使うためLCDの後 |

この表は`Instance()`呼び出しを省略しています。コールドブート後はXSPI1/2がRIFで保護されるため、外部メモリより先にRIFが必要です。PSRAM初期化はXSPIMをリセットするため、NORより先に行います。MemoryManagerの初期化だけではPSRAM HWを初期化できません。

ai-appはNORが使えない場合に推論を無効化し、タッチが使えない場合に画面操作を無効化します。必要な機能をアプリ単位で決め、初期化失敗を一律に成功扱いしないでください。

初期化後、公開されているDriverの`KeepClocksOnSleep()`と`NpuDriver::KeepMemoryClocksOnSleep()`を設定します。NORはAccessorから呼びます。DMA2D、コンソール、汎用周辺は用途に応じて別途準備し、この表だけで全機能の初期化が完了するわけではありません。

## エラーとバッファ

通常の戻り値は`common::Error`です。`Ok()`と`Code()`を確認し、必要なら`LogStatus()`で記録します。多くのDriverは再初期化時に`kAlreadyInitialized`を返しますが、上表の例外に注意してください。

| エラー | 主な意味 |
| --- | --- |
| `kNotInitialized` | 管理・HW・必要な依存が未準備 |
| `kInvalidArgument` | null、不正な寸法・範囲・alignmentなど |
| `kInvalidState` | 現在の実行状態では操作できない |
| `kNoFrame` | カメラフレームやコンソール入力がまだない |
| `kNoBuffer` | 利用できるバッファや表示資源がない |
| `kTimeout` / `kHardware` | 待機上限 / HW処理の失敗 |

CPUとDMA/NPU間の受け渡しには[キャッシュ操作](cache.md)が必要です。Pipe2推論フレームの返却とLCDの表示受け渡しは[memory_manager](../middleware/memory_manager.md)に従います。timeoutだけでHWがバッファへのアクセスを止めたと判断せず、各Driverの停止・復旧契約を確認してください。

## experimentとの関係

`userspace/experiment-*`は共有driver/middlewareとは独立した実装です。このドキュメントの対象は`kernel/driver`であり、experimentのローカルAPIや実行手順は各READMEを参照します。共有APIを使うために、experimentから別userspaceや共有driverの実装へ依存させないでください。