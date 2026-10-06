# ドライバー

`kernel/driver`はSTM32N6570-DKの周辺機能をタスクから安全に使うためのドライバーです。CMakeターゲットは`uai::drivers`と`uai::driver_overrides`です（[ビルド構成](kernel/build.md)）。

## 共通の形

各ドライバーは`<Name>Driver`と`<Name>Management`の組で構成されます。

- `<Name>Management::Instance()`で唯一のインスタンスを取得します。
- `Initialize()`でハードウェアを初期化します。二回目以降は`kAlreadyInitialized`を返します。
- ドライバーの状態を変える操作には所有権（`Writer`）が必要です。`Writer`は`ResourceManagement`が持つミューテックスを表し、スコープを抜けると解放されます。

操作には2つの呼び方があります。

```cpp
// 1. Managementの簡易メソッド。呼び出しごとに所有権を取って解放する
auto &camera = camera::CameraManagement::Instance();
camera.Start();

// 2. Accessorで所有権を保持し続ける。連続した操作をまとめたいときに使う
npu::NpuManagement::Accessor npu;
common::Error status = npu::NpuManagement::Instance().Acquire(&npu);
if (status.Ok()) {
    npu->Run(npu.Ownership());
}
```

`Accessor`（`driver::ResourceAccessor<Driver>`）はムーブのみ可能で、破棄すると所有権を返します。`Accessor`を持ったまま同じドライバーの`Management`の簡易メソッドを呼ぶと、所有権を待ち続けて止まります。ai-appのNPUタスクのように1つのタスクがドライバーを占有する場合は、起動時に`Accessor`を取り、そのタスクからだけ操作します。

各ドライバーは`KeepClocksOnSleep()`を持ちます。CPUがスリープしても周辺機能のクロックを止めないように、初期化後に呼んでください。

## 初期化の順序

外部メモリとRIFの依存関係があるため、次の順で初期化します。ai-appでは初期化タスク（`userspace/ai-app/src/task/application_initialize_task.cpp`の`InitializeDrivers()`）がこの順で呼んでいます。その前に`HAL_ResumeTick()`と`driver::board::ConfigureReferenceInterruptPriorities()`を呼び、HALのtickと割り込み優先度を整えます（[μT-Kernel](kernel/utkernel.md)）。

| 順 | 呼び出し | 理由 |
| --- | --- | --- |
| 1 | `npu::NpuDriver::InitializeMemory()` | NPUとNPU用RAMのクロックを有効にします |
| 2 | `cache::CacheManagement::Instance().Initialize()` | Dキャッシュと、NPUが使うCACHEAXIを有効にします |
| 3 | `memory_manager::MemoryManager::Initialize()` | バッファプールを用意します（[memory_manager](middleware/memory_manager.md)） |
| 4 | `rif::RifManagement::Instance().Initialize()` | XSPI1/XSPI2へのアクセス権を設定します。外部メモリより先に必要です |
| 5 | `psram::PsramManagement::Instance().Initialize()` | PSRAMをメモリマップします。戻り値は`bool` |
| 6 | `nor::NorManagement::Instance().Initialize()` | NOR Flashをメモリマップします。戻り値は成功で`0` |
| 7 | `lcd::LcdManagement::Instance().Initialize(memory, cache)` | LCDを初期化します |
| 8 | `camera::CameraManagement::Instance().Initialize(memory, cache)` | カメラを初期化します |

コールドブート直後のXSPI1/XSPI2はRIFで保護されているため、外部メモリより先にRIFを設定します。PSRAMの初期化はXSPIMをリセットするため、NORより先に行います。

## キャッシュ

`cache::CacheManagement`は、DMAやNPUとCPUの間でバッファを受け渡すときのキャッシュ操作を提供します。対象は`buffer::Buffer`で、`Region`が`kCapture`、`kDisplay`、`kInference`のものだけを受け付けます。

| メソッド | 操作 | 使う場面 |
| --- | --- | --- |
| `PrepareForCpuRead(buffer)` | invalidate | DMAやNPUが書いた内容をCPUで読む前 |
| `PrepareForPeripheralRead(buffer)` | clean | CPUが書いた内容をDMAやNPUに読ませる前 |
| `PrepareForDmaWrite(buffer)` | clean、invalidate | DMAやNPUに書かせる前 |

## カメラ

`camera::CameraManagement`はIMX335とDCMIPPを制御し、2系統のフレームを出します。

| 系統 | 形式 | 取得 |
| --- | --- | --- |
| Pipe1 | RGB565 800x480 | `TakeCompletedCapture(&capture)` |
| Pipe2 | RGB888 480x480（800x480をletterbox） | `TakeCompletedInference(&frame)` |

使い方は次のとおりです。フレームがない場合は`kNoFrame`を返します。

```cpp
auto &camera = camera::CameraManagement::Instance();
camera.Start();
for (;;) {
    camera.Process();  // ISPの更新と、フレームが止まったときの自動復旧

    pipeline::InferenceFrame pipe2{};
    if (camera.TakeCompletedInference(&pipe2).Ok()) {
        camera.SnapshotInferenceSource(&pipe2);  // 推論用に不変なコピーを作る
        // 推論タスクへ渡す。使い終わったらmemory.ReleaseInferenceBuffer(pipe2)
    }

    pipeline::CaptureFrame capture{};
    if (camera.TakeCompletedCapture(&capture).Ok()) {
        lcd.ComposeAndPresent(capture, boxes);
    }
}
```

- `Process()`はカメラタスクのループで毎回呼びます。2秒間フレームが来ないとセンサーとDCMIPPを再起動します（再試行は5秒間隔）。
- Pipe2のフレームは推論バッファ（`InferenceFrame`）として受け取ります。受け取ったフレームは必ず`MemoryManager::ReleaseInferenceBuffer()`で返します。
- `SnapshotInferenceSource()`はPipe2の有効領域を`frame.source`へコピーします。DMAが次のフレームを書いても推論入力が変わらないようにするためです。
- `GetDiagnostics()`でフレーム数、ドロップ数、CSIエラー数などを読めます。
- `camera_driver/camera_diagnostics.hpp`の`ReadSensorDiagnostics()`はIMX335の露出とゲインを、`DumpCaptureRegisters()`はDCMIPPとCSIのレジスタをログに出します。フレームが来ない、映像が暗いといった調査に使います。
- センサーのテストパターンやPipe2のフレームレートは`kernel/driver/config/ai_board_config.hpp`の`config::kCamera`で設定します。

## LCD

`lcd::LcdManagement`はLTDCへの表示を担当します。

| メソッド | 内容 |
| --- | --- |
| `ShowInitialFrame(boxes)` | カメラ開始前に最初の画面を出します |
| `ComposeAndPresent(capture, boxes)` | Pipe1のフレームを表示バッファへコピーし、`inference::BoxSet`の枠とマスクを重ねて表示します |
| `ComposeInferenceAndPresent(frame)` | 推論入力を確認するための表示です |
| `SynchronizeCurrentFrame()` | LTDCの表示切り替えと同期し、表示バッファの受け渡しを完了します |
| `SetTimingDiagnostics(enabled)` | 合成と表示にかかった時間のログを有効にします。ai-appでは`DiagnosticsConfig::display_timing`から設定します |

表示バッファの確保と受け渡しは内部で`MemoryManager`を使います。

## NPU

`npu::NpuManagement`はNeural-ART NPUでSTEdgeAIの生成モデルを実行します。

### モデルの用意

ドライバーは`npu::NpuNetwork`インターフェース（`driver/npu_driver/npu_network.hpp`）を通してモデルを呼びます。生成された`stai_network_*`関数をこのインターフェースで包みます。

```cpp
class NpuNetwork {
public:
    virtual stai_return_code Initialize() = 0;
    virtual stai_return_code Shutdown() = 0;
    virtual stai_return_code GetInfo(stai_network_info *info) = 0;
    virtual stai_return_code SetInput(stai_ptr input, stai_size size) = 0;
    virtual stai_return_code GetOutputs(stai_ptr *outputs, stai_size *count) = 0;
    virtual stai_return_code SetOutputs(const stai_ptr *outputs, stai_size count) = 0;
    virtual stai_return_code Run(stai_run_mode mode) = 0;
    virtual stai_return_code ContinueRun() = 0;
    virtual stai_return_code GetRunStatus() = 0;
    virtual stai_return_code NewInference() = 0;
    virtual stai_return_code SetEpochTraceCallback(EpochTraceCallback callback,
                                                   void *context) = 0;
};
```

複数のモデルをリンクする場合、生成コードの関数名が衝突します。ai-appでは`src/models/<model>/<model>_model_runtime.c`で`#define stai_network_init person_stai_network_init`のように名前を変えてから生成コードを`#include`し、command blobを`.network_blobs_<model>`セクションへ置いています。

### 実行

```cpp
auto &npu = npu::NpuManagement::Instance();

// 起動時: 1つ目はInitialize、2つ目以降はPreload
npu.Initialize(person_model);
npu.Preload(face_model);
npu.SelectModel(person_model);

npu::NpuManagement::Accessor accessor;
npu.Acquire(&accessor);
npu::NpuDriver *driver = accessor.Get();
const auto &writer = accessor.Ownership();

// 推論ごと
driver->SelectModel(face_model, writer);
driver->SetInput(input, input_bytes, writer);
driver->SetOutputs(outputs, output_count, writer);
npu::Status status = driver->Run(writer);
driver->NewInference(writer);
```

- `Preload()`はcommand blobをRAMへ展開します。以降の`SelectModel()`は再ロードせずにモデルを切り替えます。activation領域はモデル間で共有するため、同時に実行できるモデルは1つです。
- `Run()`は`StartRun()`と`WaitRun()`をまとめたものです。NPUの割り込みを待ち、epochを進め、完了まで戻りません。応答がない場合は`kTimeout`を返します。細かく制御したい場合は`StartRun()`、`PollRun()`、`WaitForIrq()`、`ContinueRun()`を使います。
- 戻り値の`npu::Status`は`error`（`common::Error`）と実行時間などの`execution`を持ちます。
- 入出力バッファはアプリが用意します。モデルは`--no-inputs-allocation --no-outputs-allocation`で生成してください。入力は`PrepareForPeripheralRead()`、出力は読む前に`PrepareForCpuRead()`します。
- NPUの割り込みハンドラ`NPU0_IRQHandler`は、アプリが`tk_def_int()`で登録します（[μT-Kernel](kernel/utkernel.md)）。
- `SetEpochTraceObserver()`で生成コードのepochごとのコールバックを受け取れます。`SetEpochTraceModelKindId()`で記録に付けるモデルIDを切り替えます。

## PSRAM、NOR、RIF

これらは初期化だけで使います。初期化後はアドレスで直接アクセスできます。

| ドライバー | 初期化後の状態 |
| --- | --- |
| `rif::RifManagement` | XSPI1、XSPI2へのアクセスが許可されます |
| `psram::PsramManagement` | PSRAM（APS256XX）が`0x90000000`以降にメモリマップされます |
| `nor::NorManagement` | NOR Flash（MX66UW1G45G）が`0x70000000`以降にメモリマップされます。モデルの重みとcommand blobを読めるかを確認します |

外部NORへの書き込みはボード上では行わず、STM32CubeProgrammerとExternal Loaderで行います。

## ボード共通（board）

`kernel/driver/board`はドライバーに属さないボード全体の処理です。名前空間は`uai::ai::driver::board`です。

| ファイル | 内容 |
| --- | --- |
| `hal_time.c` | `HAL_GetTick()`と`HAL_Delay()`をμT-Kernelの時刻で置き換えます（`uai::driver_overrides`） |
| `interrupt_priority.hpp` | `ConfigureReferenceInterruptPriorities()`。すべての周辺割り込みをSysTickと同じ優先度にそろえます |
| `register_diagnostics.hpp` | `DumpCoreRegisters(stage)`と`DumpPeripheralRegisters(stage)`。Cortex-M55のコアレジスタとベクタ、RCC、キャッシュ、RIF（IAC、RIFSC、RISAF）、NPUのレジスタをログに出します。ログレベルが`kDebug`以上のときだけ出力します |

レジスタダンプはai-appの`DiagnosticsConfig::register_dump`で有効にします。起動が途中で止まる、外部メモリが読めないといった調査に使います。

## 設定ファイル

| ファイル | 内容 |
| --- | --- |
| `config/ai_board_config.hpp` | カメラの診断設定、Pipe2のフレームレート、NOR確認用オフセット |
| `config/stm32n6xx_hal_conf.h` | HALの有効モジュール |
| `config/aps256xx_conf.h`、`config/mx66uw1g45g_conf.h` | 外部メモリ部品の設定 |
| `board/include/stm32n6570_discovery_conf.h` | BSPの設定 |
