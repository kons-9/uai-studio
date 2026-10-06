# 4. タスクと共有資源

## 目的

3つのタスク（初期化、カメラ、推論）と、タスク間で受け渡すものを決めます。この章ではタスクの骨格と`usermain()`を書き、ドライバの中身は次章以降で埋めます。

## 追加するファイル

```text
src/app_config.hpp        優先度、スタックサイズ、キュー深さ、結果保持時間
src/app_context.hpp       AppContext: ドライバ参照、MemoryManager、チャネル、フラグ
src/task/channels.hpp     InferenceFrameChannel、InferenceResultChannel
src/task/initialize_task.hpp / .cpp
src/task/camera_task.hpp / .cpp
src/task/inference_task.hpp / .cpp
src/main.cpp              usermain()
```

### 共有資源: AppContext

タスクが共有するものを1つの構造体に集め、`App()`で取り出します。ドライバは`<Name>Management::Instance()`のシングルトンなので参照を持つだけです。

```cpp
struct AppContext {
    memory_manager::MemoryManager memory{};
    middleware::cpu_task_monitor::CpuTaskMonitor cpu_task_monitor{};
    cache::CacheManagement &cache = cache::CacheManagement::Instance();
    psram::PsramManagement &psram = psram::PsramManagement::Instance();
    rif::RifManagement &rif = rif::RifManagement::Instance();
    nor::NorManagement &nor = nor::NorManagement::Instance();
    lcd::LcdManagement &lcd = lcd::LcdManagement::Instance();
    camera::CameraManagement &camera = camera::CameraManagement::Instance();

    InferenceFrameChannel frames{memory};
    InferenceResultChannel results{};

    volatile bool external_nor_ready = false;   // 初期化タスクが1回だけ書く
    ID external_memory_ready = -1;              // イベントフラグ
};
```

### チャネル

カメラ→推論は`InferenceFrame`（Pipe2のバッファ所有権付き）、推論→カメラは`BoxSet`（検出枠）です。どちらも「新しいものを優先し、古いものは捨てる」`LatestValueChannel`を使います。

```cpp
class InferenceFrameChannel final {
public:
    void Send(const pipeline::InferenceFrame &frame)
    {
        const common::Error status = channel_.SendReplacingOldest(
            frame, [this](const pipeline::InferenceFrame &discarded) {
                memory_.ReleaseInferenceBuffer(discarded).LogStatus("memory");
            });
        if (status.Ok()) return;
        memory_.ReleaseInferenceBuffer(frame).LogStatus("memory");
    }
    common::Error Receive(pipeline::InferenceFrame *frame)
    { return channel_.ReceiveBlocking(frame); }
    ...
};
```

フレームを捨てるときは必ず`ReleaseInferenceBuffer()`でバッファを返します。返さないとPipe2のDMAが書き込み先を失います（[memory_manager](../middleware/memory_manager.md)）。

結果側は`SendReplacingOldestOnce()`で送り、受け手は`DrainLatest()`で溜まった中から最新だけを取ります。

### タスクの形

3タスクとも同じ形です。スタックはタスクより長生きするメンバに置き、`Task::Start()`で起動します。

```cpp
class CameraTask final {
public:
    static CameraTask &Instance() { static CameraTask task; return task; }
    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
    {
        common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_,
                            kCameraTaskPriority, "camera");
    }
private:
    static void Entry() { Instance().Run(); }
    [[noreturn]] void Run();
    common::StableAlignedBytes<kCameraTaskStackSize> stack_;
};
```

`Run()`の本体は`Task::RunForever(monitor, name, wait, process)`で、`wait`（計測しない）と`process`（計測する）を分けます。

| タスク | wait | process |
| --- | --- | --- |
| initialize | `tk_dly_tsk(1000)` | `cpu_task_monitor.Report()` |
| camera | `tk_dly_tsk(1)` | `Process()`、Pipe2転送、結果取得、表示 |
| inference | フレームチャネルの`Receive()`（永久待ち） | 1フレームの推論 |

### usermain()

μT-Kernelが呼ぶ入口です。割り込みの登録、イベントフラグとチャネルの作成、初期化タスクの起動だけを行い、あとは眠ります。

```cpp
extern "C" INT usermain(void)
{
    AppContext &app = App();
    app.cpu_task_monitor.Start();
    app.cpu_task_monitor.RegisterTask(tk_get_tid(), "usermain");

    T_DINT npu_interrupt = {};               // NPU完了割り込み（NPUドライバが実装）
    npu_interrupt.intatr = TA_HLNG;
    npu_interrupt.inthdr = reinterpret_cast<FP>(NPU0_IRQHandler);
    tk_def_int(static_cast<UINT>(NPU0_IRQn), &npu_interrupt);
    T_DINT iac_interrupt = {};               // 不正アクセス（RIF違反）の通知
    iac_interrupt.intatr = TA_ASM;
    iac_interrupt.inthdr = reinterpret_cast<FP>(IAC_IRQHandler);
    tk_def_int(static_cast<UINT>(IAC_IRQn), &iac_interrupt);

    T_CFLG flag = {};
    flag.flgatr = TA_TFIFO | TA_WMUL;
    app.external_memory_ready = tk_cre_flg(&flag);
    app.frames.Create();
    app.results.Create();

    InitializeTask::Instance().Start(app.cpu_task_monitor);
    for (;;) tk_slp_tsk(TMO_FEVR);
}
```

## なぜそうするか

- 表示を止めないことを最優先にします。カメラタスクは推論の結果を待たず、結果がなければ前回の枠（または枠なし）で表示します。推論タスクが遅ければフレームチャネルが古いフレームを捨てます。
- 割り込みハンドラはCubeMXのベクタテーブルではなく`tk_def_int()`で登録します。μT-Kernelが起動時にベクタテーブルをRAM上の自分のものに置き換えるためです（[μT-Kernel](../kernel/utkernel.md)）。
- `CpuTaskMonitor`は`ENABLE_CPU_TASK_MONITOR=0`のときは空実装になるので、計測の有無に関わらず同じコードで動きます。

## 確認

この章の終わりでは、ドライバ初期化を空にしたまま3タスクが起動し、`RunForever`に入ることを確認できます（UARTには何も出ないので、`cpu_task_monitor.Report()`の1秒ごとの出力か、各タスク先頭に一時的な`UAI_LOG_INFO`を置いて確認します）。章5で初期化を埋めると`mini: driver init done`が出ます。
