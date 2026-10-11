# 5. ドライバ初期化

## 目的

初期化タスクで、NPU用メモリ、キャッシュ、バッファプール、RIF、PSRAM、NOR、LCD、カメラをこの順に初期化し、ほかのタスクへ「外部メモリが使える」ことを知らせます。

## 追加するコード

`src/task/initialize_task.cpp`の`InitializeDrivers()`です。

```cpp
common::Error InitializeDrivers(AppContext &app)
{
    common::Error status = npu::NpuDriver::InitializeMemory();   // 1. NPUとNPU RAMのクロック
    if (!Initialized(status)) return status;
    status = app.cache.Initialize();                             // 2. Dキャッシュ、CACHEAXI
    if (!Initialized(status)) return status;
    status = app.memory.Initialize();                            // 3. バッファプール
    if (!status.Ok()) return status;
    status = app.rif.Initialize();                               // 4. XSPI1/2のアクセス権
    if (!Initialized(status)) return status;
    if (!app.psram.Initialize()) return {common::ErrorCode::kHardware};   // 5. PSRAM
    app.external_nor_ready = app.nor.Initialize() == 0;          // 6. NOR（失敗しても続ける）
    status = app.lcd.Initialize(app.memory, app.cache);          // 7. LCD
    if (!Initialized(status)) return status;
    status = app.camera.Initialize(app.memory, app.cache);       // 8. カメラ
    if (!Initialized(status)) return status;

    app.cache.KeepClocksOnSleep();                               // スリープ中もクロックを維持
    app.psram.KeepClocksOnSleep();
    if (app.external_nor_ready) {
        nor::NorManagement::Accessor nor_accessor;
        status = app.nor.Acquire(&nor_accessor);
        if (!status.Ok()) return status;
        nor_accessor.KeepClocksOnSleep();
    }
    npu::NpuDriver::KeepMemoryClocksOnSleep();
    app.lcd.KeepClocksOnSleep();
    app.camera.KeepClocksOnSleep();
    return {};
}
```

`Initialized()`は`kOk`と`kAlreadyInitialized`を成功扱いにする小さな関数です。

`Run()`はHALのtickを再開し、割り込み優先度を整えてから初期化します。

```cpp
void InitializeTask::Run()
{
    AppContext &app = App();
    HAL_ResumeTick();
    driver::board::ConfigureReferenceInterruptPriorities();

    const common::Error status = InitializeDrivers(app);
    if (!status.Ok()) {
        status.LogStatus("driver");
        common::Task::Halt("mini: driver initialization failed\n");
    }
    UAI_LOG_INFO("mini: driver init done nor=%u\n", static_cast<unsigned int>(app.external_nor_ready));

    app.cpu_task_monitor.InitializeTraceBuffer();
    (void)tk_set_flg(app.external_memory_ready, kExternalMemoryReady);

    CameraTask::Instance().Start(app.cpu_task_monitor);
    InferenceTask::Instance().Start(app.cpu_task_monitor);

    common::Task::RunForever(app.cpu_task_monitor, "initialize",
                             [] { tk_dly_tsk(1000); },
                             [&] { app.cpu_task_monitor.Report(); });
}
```

## なぜそうするか

- 順序の理由は[ドライバーの初期化の順序](../driver/common.md)にあります。コールドブート直後のXSPI1/2はRIFで保護されているので外部メモリより先にRIF、PSRAMの初期化はXSPIMをリセットするのでNORより先、LCDとカメラはバッファプール（`memory`）が要るので最後です。
- NORが初期化できなくてもカメラ表示は動かします。`external_nor_ready`を見て推論タスクだけが止まります。このフラグはNORの読み出しとメモリマップの成功を示すだけで、重みやcommand blobの内容は検証しません。未書き込みでも`nor=1`になり得るため、推論を動かす前に`ai-load`が必要です。
- 初期化は深い呼び出しになるため、μT-Kernelの初期タスク（`usermain()`）ではなく専用スタックのタスクで行います。
- `KeepClocksOnSleep()`は、タスクがすべて待ちに入ってCPUがスリープしたときに周辺機能のクロックが止まらないようにします。カメラのDMAやLTDCの走査はCPUが寝ていても続きます。

## 確認

```sh
make -C userspace/mini-ai-app monitor    # 別端末。すでに開いていれば追加しない
make -C userspace/mini-ai-app ram-run
```

UARTに`mini: driver init begin`→`mini: driver init done nor=1`が出れば完了です。NOR初期化に失敗した場合は`nor=0`と`mini: NOR unavailable; inference disabled`が出ますが、カメラの初期化は続きます。ほかの初期化で失敗した場合は`error: component=driver ...`の`code`を[エラーコード](../kernel/common.md)で引いてください。
