# NPU（Neural-ART / STAI）

## HWの役割

STM32N6のNeural-ART NPUは、STEdgeAIで生成されたモデルを実行します。ドライバーはSTAIの実行プロトコル、NPU割り込み待ち、epochの継続、実行時間の記録を担当します。画像の前処理、モデル固有の出力decode、結果の配信は[ai_runtime](../middleware/ai_runtime.md)やアプリの責任です。

公開ヘッダー: [npu_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/npu_driver/npu_driver.hpp)、[npu_network.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/npu_driver/npu_network.hpp)。名前空間は`uai::ai::npu`です。

## 初期化とAPI

1. `NpuDriver::InitializeMemory()`でNPUとNPU用RAMのクロックを用意します。
2. [キャッシュ](cache.md)、[RIF](rif.md)、外部メモリなどを準備します。
3. アプリの割り込み登録経路で`NPU0_IRQHandler`を`tk_def_int()`に登録します。[μT-Kernel](../kernel/utkernel.md)を参照してください。
4. `NpuManagement::Initialize(model)`でSTAI runtimeと最初のモデルを初期化します。
5. 追加モデルは`Preload(model)`、実行するモデルは`SelectModel(model)`で選びます。

`NpuNetwork`は生成モデルへのadapterです。`Initialize`、`Shutdown`、`GetInfo`、`SetInput`、`GetOutputs`、`SetOutputs`、`Run`、`ContinueRun`、`GetRunStatus`、`NewInference`、`SetEpochTraceCallback`を実装します。生成された`stai_network_*`関数を包み、モデルオブジェクトの寿命はロード中のDriverより短くしないでください。

### 管理API

| API | 戻り値・動作 |
| --- | --- |
| `NpuManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize(model)` | 最初のモデルとruntimeを初期化。`npu::Status` |
| `Preload(model)` | 追加の生成モデルを初期化し、後の切り替えに備える |
| `SelectModel(model)` | ロード済みモデルへ切り替える |
| `GetInfo(&info)` / `GetOutputs(outputs, &count)` | 選択中モデルの情報 / 出力ポインターを取得 |
| `SetEpochTraceModelKindId(id)` | epoch診断に付けるモデルIDを設定 |
| `Acquire(&accessor, timeout = TMO_FEVR)` | 実行中に保持する所有権を取得。`common::Error` |
| `NpuDriver::InitializeMemory()` | NPU用メモリ・クロックの準備。`common::Error` |
| `NpuDriver::KeepMemoryClocksOnSleep()` | NPU用メモリなどのsleep時クロックを維持 |

### Accessor経由のDriver API

以下の状態操作には最後の引数としてWriterを渡します。`PollRun()` / `WaitRun()`ではWriterの後に任意のprogress callbackとcontextを渡せます。

| API | 内容 |
| --- | --- |
| `Initialize(model, writer)` / `Preload(model, writer)` / `SelectModel(model, writer)` | 所有権を保持したままモデルを準備・選択 |
| `GetInfo(info, writer)` | 選択中モデルのtensor情報などを取得 |
| `SetInput(input, size, writer)` | 入力バッファを設定。sizeの契約は生成モデルに従う |
| `SetOutputs(outputs, count, writer)` / `GetOutputs(outputs, count, writer)` | 出力バッファを設定 / 取得。countは出力数 |
| `StartRun(writer)` | 非同期実行をsubmit。成功は完了を意味しない |
| `PollRun(writer, progress = nullptr, context = nullptr)` | STAI状態を確認。待機もepoch継続も行わない |
| `WaitForIrq(writer)` | NPUのイベントフラグだけを待つ |
| `ContinueRun(writer)` | タスク文脈で生成モデルの次のepochを継続 |
| `WaitRun(writer, progress = nullptr, context = nullptr)` | Poll、IRQ待ち、epoch継続をまとめて完了まで処理 |
| `Run(writer)` | `StartRun()`と`WaitRun()`の同期版 |
| `NewInference(writer)` | 生成モデルを次の推論に備えた状態に戻す |
| `Shutdown(writer)` | モデルとruntimeを終了 |
| `SetEpochTraceObserver(observer, context, writer)` | epoch traceの受け取り先を設定 |
| `SetEpochTraceModelKindId(id, writer)` | 所有権を保持したまま診断モデルIDを設定 |
| `IsLoaded(model)` / `Initialized()` / `LastExecution()` | ロード状態、初期化状態、最後の実行記録を参照 |

### 戻り値と非同期処理

`npu::Status`は`error`（`common::Error`）、`execution`（`ExecutionSnapshot`）、`action`（`RunAction`）を持ちます。`Ok()`でエラーを判定し、非同期実行の完了は`action == kCompleted`で判定します。

`PollRun()`の結果に応じて、`kWaitForIrq`なら`WaitForIrq()`、`kContinueEpoch`なら`ContinueRun()`を呼び、再びpollします。`WaitRun()`はこの処理を内部で行います。progress callbackはIRQではなくタスクから呼ばれ、次フレームの準備などに使えます。

`ExecutionSnapshot`には状態、STAI status、run ID、開始・終了・経過時間、poll回数、IRQ待ち時間、epoch継続時間があります。時間を参照する際は`timing_valid`も確認します。

## ユースケース: 所有権を保持して1回の推論を実行する

モデルの初期化・preloadと入力のキャッシュcleanを済ませた状態で呼びます。バッファサイズ・出力数はモデルのtensor情報に一致させます。関数が成功した後、出力をCPUで読む前にinvalidateしてください。

```cpp
#include "driver/npu_driver/npu_driver.hpp"

namespace ai = uai::ai;

ai::npu::Status RunModel(ai::npu::NpuNetwork &model, stai_ptr input,
                        stai_size input_size, const stai_ptr *outputs,
                        stai_size output_count)
{
    ai::npu::NpuManagement::Accessor accessor;
    const auto acquired = ai::npu::NpuManagement::Instance().Acquire(&accessor);
    if (!acquired.Ok()) {
        return {acquired};
    }
    auto &driver = *accessor.Get();
    const auto &writer = accessor.Ownership();
    auto status = driver.SelectModel(model, writer);
    if (!status.Ok()) {
        return status;
    }
    status = driver.SetInput(input, input_size, writer);
    if (!status.Ok()) {
        return status;
    }
    status = driver.SetOutputs(outputs, output_count, writer);
    if (!status.Ok()) {
        return status;
    }
    const auto completed = driver.Run(writer);
    if (!completed.Ok()) {
        return completed;
    }
    const auto next = driver.NewInference(writer);
    return next.Ok() ? completed : next;
}
```

ai-appのようにNPUを専用タスクが占有する場合、タスク開始時にAccessorを取得し、毎回取り直さずそのWriterを使い続けます。

## ユースケース: 複数モデルを切り替える

最初のモデルを`Initialize()`、追加モデルを`Preload()`で準備してから、フレームごとに`SelectModel()`します。現在のロード管理は最大3モデルです。activation領域を共有する構成では同時実行できるモデルは1つです。実行が終わる前にモデルや入力・出力を切り替えないでください。

生成関数名の衝突は、ai-appの`src/models/<model>/<model>_model_runtime.c`のように生成コードをモデル別の名前で包んで解決します。RAMへのcommand blob展開は生成adapterの初期化経路が担当し、preload後の選択では再初期化しません。

## 制約と診断

- 入出力はアプリが用意します。この構成ではモデルを`--no-inputs-allocation --no-outputs-allocation`で生成します。[キャッシュ](cache.md)とバッファの寿命管理も必要です。
- IRQ待ちの上限は実装の`kTimeoutTicks = 5000`です。`WaitRun()`には5000回のprotocol step上限もありますが、推論全体を厳密に5秒で打ち切るdeadlineではありません。
- 失敗時は`Status.error`に加えて`execution.state` / `stai_status`を確認します。timeout後にHWが安全に停止したと決めつけてバッファを再利用せず、アプリの復旧方針に従ってください。
- `Accessor`保持中にManagementの所有権取得付きAPIを呼ぶと再取得待ちになります。Driver版を使ってください。
- epoch別の解析は[ai_model_monitor](../middleware/ai_model_monitor.md)、CPUタスクとの重なりは[cpu_task_monitor](../middleware/cpu_task_monitor.md)、HW状態は[ボード診断](board.md)を使います。