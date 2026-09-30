# ミドルウェア

`kernel/middleware`はドライバーの上で動く共通処理です。CMakeターゲットは`uai::middleware`です。

| モジュール | 内容 |
| --- | --- |
| [ai_runtime](#ai_runtime) | 推論を前処理CPU、NPU、後処理CPUの3レーンで実行するパイプライン |
| [memory](#memory) | 生成したメモリ配置へのアクセスと、バッファの型 |
| [memory_manager](#memory_manager) | キャプチャ、表示、推論バッファの所有権管理 |
| [pipeline](#pipeline) | フレームと画像形式の型 |
| [ai_model_monitor](#ai_model_monitor) | AIパイプラインの実行トレース |
| [cpu_task_monitor](#cpu_task_monitor) | タスク別CPU使用率とループ時間 |
| [image_resizer](#image_resizer) | 画像縮小のハードウェア選択とCPU縮小 |

## ai_runtime

ヘッダは`middleware/ai_runtime/pipeline_dispatcher.hpp`です。推論1回分を`AiFuture`として投入し、3つのレーンを順に進めます。

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

### AiFutureの実装

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

### 非同期イベントを待つ

NPU完了割り込みなどを待ってから次へ進めたい場合は、`next.wait_flags`に`WaitBitFlag::kNpuCompletion`や`kExternal`を指定し、イベント発生時に`PipelineRuntime::Signal(future, flags)`を呼びます。待っている間はどのキューにも入らないため、他の推論を妨げません。`WaitMode::kAll`は全ビット、`kAny`はいずれか1ビットで再開します。`Evaluate()`の実行中に届いた`Signal()`も失われません。

ai-appでは`NpuDriver::Run()`がNPUレーンの中で完了まで待つため、`Signal()`は使っていません。

### RTOSタスクへの組み込み

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

// レーンごとのワーカータスク
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
- `RegisterModelName()`と`StartAiModelMonitor()`はNPUレーンのタスクから呼びます。ai_model_monitorはそのタスクを監視対象にします。

### テスト

ホストPCでビルドして実行できます。

```sh
sh kernel/middleware/ai_runtime/tests/run.sh
```

### 推論結果の型

`middleware/ai_runtime/inference_result_types.hpp`の`inference::BoxSet`は、後処理からLCDへ渡す結果です。person、faceの`DetectionSet`（最大`kMaxBoxes`=16個の枠）と、segmentationのマスク情報を持ち、それぞれ`*_valid`で有効かを示します。

## memory

ビルド時に`host_app/auto_static_memory_layout`がメモリ配置を解決し、次のヘッダを`<build>/generated/middleware/memory/generated/`へ生成します。

| ヘッダ | 内容 |
| --- | --- |
| `static_memory_layout/key.hpp` | 領域のキー`static_memory_layout::Key` |
| `static_memory_layout/raw.hpp` | リンカシンボルから作った領域の表 |
| `memory_config.hpp` | `kMemoryConfig`（アライメント、モデル出力サイズ）と各バッファ数（`kCaptureBufferCount`、`kInferenceBufferCount`など） |

領域は`static_memory_layout.hpp`の`Region::GetRegionFromKey()`で取得します。

```cpp
#include "middleware/memory/static_memory_layout.hpp"

const auto region = static_memory_layout::Region::GetRegionFromKey(
    static_memory_layout::Key::kThreadMonitor);
void *base = reinterpret_cast<void *>(region.address());
std::size_t size = region.size();
```

キーは`config/application_memory.json`の`reservations`の`key`から作られます（`kCapture0`、`kInference0`、`kInferenceScratch`、`kThreadMonitor`、`kCpuTaskMonitor`など）。専用の領域を追加するときは、`reservations`に名前、キー、配置先のメモリ（ベースのリンカスクリプトの`MEMORY`名）、セクション名、サイズを追加します。

`memory/buffer_types.hpp`の`memory_allocator::Buffer`は、アドレス、サイズ、アライメント、スロット番号、`Region`（`kCapture`、`kDisplay`、`kInference`）を持つバッファ記述子です。キャッシュ操作やフレーム型はこの型でバッファを受け渡します。

## memory_manager

`memory_manager::MemoryManager`は、キャプチャ、表示、推論の各バッファをどのコンポーネントが使っているかを管理します。アプリは1つのインスタンスを作り、ドライバーより先に`Initialize()`して、LCDとカメラの`Initialize()`に渡します。

キャプチャと表示のバッファはカメラとLCDのドライバーが内部で扱います。アプリが直接扱うのは推論バッファです。

```text
カメラのDMAが書き込む
  -> TakeCompletedInference()   kReadyForAi
  -> ClaimInferenceBuffer()     kInUseByAi（推論側が使用中）
  -> ReleaseInferenceBuffer()   kFree（DMAが再利用できる）
```

- 受け取った`InferenceFrame`は、使っても使わなくても必ず`ReleaseInferenceBuffer()`で返します。返さないとPipe2のDMAが書き込み先を失い、フレームが落ちます。
- `ClaimInferenceBuffer()`と`ReleaseInferenceBuffer()`はフレームの`capture_sequence`と`lease_token`を照合します。キューに残った古いメッセージで再利用後のスロットを操作すると`kOwnership`を返します。
- 推論バッファには入力画像の後ろにモデル出力領域があり、`frame.outputs[]`で参照できます。`frame.source`は`SnapshotInferenceSource()`のコピー先、`frame.scratch`は共有の作業領域です。

## pipeline

`middleware/pipeline/`はフレームと画像形式の型だけを定義します。

| 型 | 内容 |
| --- | --- |
| `pipeline::CaptureFrame` | Pipe1のフレーム。`buffer`と`sequence` |
| `pipeline::DisplayBuffer` | LCDの表示バッファ |
| `pipeline::InferenceFrame` | Pipe2のフレームと推論用の付随バッファ（`source`、`scratch`、`outputs[]`） |
| `pipeline::kCaptureFormat` | 800x480、2 byte/pixel |
| `pipeline::kInferenceFormat` | 480x480、3 byte/pixel |
| `pipeline::kInferenceContentFormat` | 480x288、3 byte/pixel（letterboxの有効領域） |

## ai_model_monitor

`PipelineRuntime`が内部に持ち、全ステップの開始と終了を記録します。アプリがするのは次の2つだけです。

1. NPUレーンのタスクで`pipeline.RegisterModelName(id, "name")`を呼び、モデル名を登録する（最大16モデル）。
2. 同じタスクで`pipeline.StartAiModelMonitor()`を呼ぶ。監視用のタスクが作られ、呼んだタスクの状態を定期的に記録します。

記録先は`Key::kThreadMonitor`の領域（ai-appではPSRAMの32 KiB）です。取得と可視化は[host_app/ai_model_monitor](https://github.com/kons-9/uai-studio/tree/main/host_app/ai_model_monitor)を参照してください。ホストビルドでは何もしない実装になります。

## cpu_task_monitor

µT-Kernelのディスパッチフックと割り込みフックでタスクごとのCPUサイクルを数えます。CMakeオプション`UAI_CPU_TASK_MONITOR`（Makeでは`ENABLE_CPU_TASK_MONITOR=1`）が無効のときは、同じAPIの空実装になります。

```cpp
middleware::cpu_task_monitor::CpuTaskMonitor monitor;

monitor.Start();                          // usermain()の最初で呼ぶ
monitor.RegisterTask(tk_get_tid(), "usermain");
monitor.InitializeTraceBuffer();          // PSRAMの初期化後に呼ぶ
monitor.Report();                         // 1秒ごとに呼ぶと使用率を記録する
```

ループ時間を記録するには、ループ本体を`BeginTaskLoop()`と`RecordTaskLoop()`で囲みます。ai-appの`Task::RunForever()`（`userspace/ai-app/src/task/task.hpp`）がこれを行う雛形です。

```cpp
Task::RunForever(monitor, "camera",
                 [] { /* イベントを待つ。この時間は計測しない */ },
                 [] { /* 1回分の処理 */ });
```

記録先は`Key::kCpuTaskMonitor`の領域（ai-appではPSRAMの512 KiB）です。取得と可視化は[host_app/cpu_task_monitor](https://github.com/kons-9/uai-studio/tree/main/host_app/cpu_task_monitor)を参照してください。

## image_resizer

画像縮小に使うハードウェアを選ぶ方針層と、CPUによる縮小を提供します。バッファの確保とキャッシュ操作は呼び出し側が行います。

### ハードウェアの選択

```cpp
image_resizer::Request request{};
request.input = image_resizer::InputKind::kCameraPipe;
request.input_width = 800U;
request.input_height = 480U;
request.output_width = 128U;
request.output_height = 77U;

image_resizer::Selection selection{};
common::Error status = image_resizer::Select(request, &selection);
// selection.hardware == Hardware::kDcmipp、selection.dcmipp_decimationに1/2/4/8
```

| 入力 | 選ばれるハードウェア |
| --- | --- |
| `kCameraPipe` | DCMIPP。decimation（1/2/4/8）の後、downsizeで最大8:1まで縮小します。拡大はしません |
| `kRgb565Memory` | CPU |

DMA2DとGPU2D（NeoChrom）は未実装で、暗黙に選ばれることもありません。GPDMA、HPDMAは画素の補間ができないため候補にしていません。

### CPUによる縮小

いずれもnearest-neighborで、メモリを確保しません。

| 関数 | 内容 |
| --- | --- |
| `ResizeRgb565ToRgb888(source, crop_x, crop_y, crop_w, crop_h, destination)` | RGB565の一部を切り出してRGB888へ縮小 |
| `ResizeRgb888(source, destination)` | RGB888を`destination`の大きさへ縮小 |
| `ResizeRgb888Letterbox(source, destination, content_w, content_h, pad)` | RGB888を縦横比を保って縮小し、余白を埋める |
| `FillRgb888LetterboxPadding(destination, content_w, content_h, pad)` | letterboxの余白だけを埋める |

ai-appではface（128x128）とsegmentation（320x320）の前処理で、letterbox済みの480x480の入力（`frame.source`）を`ResizeRgb888()`で縮小しています。
