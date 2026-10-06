# ai_runtime

推論1回分を前処理CPU、NPU、後処理CPUの3つのレーンに分けて実行するパイプラインです。NPUがあるモデルを推論している間に、CPUは別のモデルの前処理や後処理を進められます。ヘッダは`middleware/ai_runtime/pipeline_dispatcher.hpp`です。

```text
Scheduler::Submit(future)
  -> kPreprocessCpu キュー -> Dispatcher(kPreprocessCpu).RunOnce() -> future.Evaluate()
  -> kNpu キュー           -> Dispatcher(kNpu).RunOnce()           -> future.Evaluate()
  -> kPostprocessCpu キュー -> Dispatcher(kPostprocessCpu).RunOnce() -> future.Evaluate()
  -> 完了コールバック
```

| クラス | 役割 |
| --- | --- |
| `PipelineRuntime` | 3つのキューと実行中の推論（最大`kCapacity`=8）を管理します |
| `Scheduler` | 新しい推論の入口です。`Submit()`で前処理レーンへ入れます |
| `Dispatcher` | 1つのレーンを担当します。`RunOnce()`はキューから1件取り出して1ステップ実行し、ブロックしません |
| `AiFuture` | モデル側が実装する推論1回分の状態です |

ランタイム自身はタスクを作りません。レーンごとにμT-Kernelのタスクを用意し、その中で`Dispatcher::RunOnce()`を回すのはアプリの役割です。

## AiFutureの実装

`Evaluate()`は1ステップだけ実行し、次に実行するレーンを返します。

```cpp
class MyFuture final : public ai_runtime::AiFuture {
public:
    ai_runtime::AiModelId model_id() const override { return kMyModelId; }
    std::uint32_t step_id() const override { return static_cast<std::uint32_t>(phase_); }
    bool is_ready() const override { return true; }

    ai_runtime::AiRuntimeResult Evaluate() override
    {
        common::Error status{};
        switch (phase_) {
        case Phase::kPreprocess:
            status = Preprocess();
            if (!status.Ok()) break;
            phase_ = Phase::kNpu;
            return {{}, {ai_runtime::ExecutionContext::kNpu}, false};
        case Phase::kNpu:
            status = Infer();
            if (!status.Ok()) break;
            phase_ = Phase::kPostprocess;
            return {{}, {ai_runtime::ExecutionContext::kPostprocessCpu}, false};
        case Phase::kPostprocess:
            return {Postprocess(), {}, true};  // completed = true
        }
        return {status, {}, false};
    }
};
```

| 戻り値 | 意味 |
| --- | --- |
| `error`が失敗 | 推論を打ち切り、完了コールバックにエラーを渡します |
| `completed = true` | 推論が終わりました |
| `next.context` | 次に実行するレーン |
| `next.wait_flags`、`next.wait_mode` | 次のステップの前に待つイベント。`kNone`ならすぐにキューへ入ります |

`is_ready()`が`false`の場合、そのステップは同じレーンの末尾へ戻されます（`DispatchResult::kNotReady`）。

ai-appでは`userspace/ai-app/src/models/<model>/future.*`がこの実装です。前処理で`image_resizer`による縮小とキャッシュ操作、NPUステップで`NpuDriver::Run()`、後処理でSTの後処理ライブラリによるデコードを行います。

## 非同期イベントを待つ

NPU完了割り込みなどを待ってから次へ進めたい場合は、`next.wait_flags`に`WaitBitFlag::kNpuCompletion`や`kExternal`を指定し、イベント発生時に`PipelineRuntime::Signal(future, flags)`を呼びます。待っている間はどのキューにも入らないため、他の推論を妨げません。`WaitMode::kAll`は全ビット、`kAny`はいずれか1ビットで再開します。`Evaluate()`の実行中に届いた`Signal()`も失われません。

ai-appでは`NpuDriver::Run()`がNPUレーンの中で完了まで待つため、`Signal()`は使っていません。

## RTOSタスクへの組み込み

ai-appの`userspace/ai-app/src/task/pipeline_task.cpp`の構成です。

```cpp
ai_runtime::PipelineRuntime pipeline{};
ai_runtime::Scheduler scheduler{pipeline};

// タスク開始前に一度だけ設定する
pipeline.SetCriticalSection(&EnterCritical, &LeaveCritical, nullptr);  // 割り込み禁止
pipeline.SetObserver(&OnDone, &context);        // 完了時にバッファを返す
pipeline.SetTrace(&OnTrace, &context, &Now, &context);
pipeline.SetWakeCallback(&Wake, nullptr);       // レーンに仕事が入ったら起こす

// Wake: レーンごとのイベントフラグのビットを立てる
void Wake(void *, ai_runtime::ExecutionContext lane)
{
    tk_set_flg(work_flag, 1U << static_cast<UINT>(lane));
}

// レーンごとのワーカータスク（ai-appではcommon::Task::RunForever()で待機と処理に分けている）
ai_runtime::Dispatcher dispatcher(pipeline, lane);
for (;;) {
    tk_wai_flg(work_flag, 1U << static_cast<UINT>(lane), TWF_ANDW | TWF_BITCLR,
               &pattern, TMO_FEVR);
    while (dispatcher.RunOnce() == ai_runtime::DispatchResult::kRan) {
        tk_rot_rdq(TPRI_RUN);
    }
}
```

- 複数のタスクや割り込みから使う場合、`SetCriticalSection()`は必須です。キューの操作だけを保護し、`Evaluate()`やコールバックはその外で実行します。
- `AiFuture`と入出力バッファは完了コールバックが呼ばれるまで利用側が保持します。ai-appではモデルごとに推論バッファ数と同じ数の`Future`を静的に持ち、空いているものを使います。
- 同時に投入できる推論は8件です。超えると`Submit()`は`kQueueFull`を返します。
- `RegisterModelName()`と`StartAiModelMonitor()`はNPUレーンのタスクから呼びます（[ai_model_monitor](ai_model_monitor.md)）。
- カメラタスクからのフレームと、後処理からLCDへの結果は、[message_channel](message_channel.md)の`message_channel::MessageChannel`で受け渡します。ai-appではフレームの送信失敗時に古いフレームを返却してから入れ直し、結果キューが満杯（`kBufferOverflow`）なら最古の結果を捨てて再送します。カメラタスクは待たずに最新の結果だけを取り出します（`userspace/ai-app/src/task/pipeline_task.hpp`）。

## 推論結果の型

`middleware/ai_runtime/inference_result_types.hpp`の`inference::BoxSet`は、後処理からLCDへ渡す結果です。person、faceの`DetectionSet`（最大`kMaxBoxes`=16個の枠）と、segmentationの`SegmentationSet`を持ち、それぞれ`*_valid`で有効かを示します。`SegmentationSet::mask`は20x20（`kSegmentationMaskWidth` x `kSegmentationMaskHeight`）の`common::OwnedBuffer`で、マスクをポインタではなく値として持ちます。このため`BoxSet`はメッセージバッファでそのまま送れ、推論バッファが次の推論に再利用されても表示中のマスクは変わりません。`LcdDriver::ComposeAndPresent()`がこの型を受け取り、カメラ映像に重ねます。

## テスト

ホストPCでビルドして実行できます。μT-Kernelやドライバーには依存していません。

```sh
make -C kernel/middleware/ai_runtime/tests test
```

並列投入と3レーンの実行もホストスレッドで確認します。データ競合を調べる場合は`make -C kernel/middleware/ai_runtime/tests tsan`を使用します。CMake、GoogleTest、C++コンパイラが必要です。組み込み側の割り込み禁止やAIモデル監視（ホストではスタブ）は、このテストでは再現しません。
