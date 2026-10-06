# 共通基盤（foundation）

`kernel/middleware/foundation`は、ドライバー、ミドルウェア、アプリが共有する小さな部品です。名前空間は`uai::ai::common`で、すべてヘッダだけで構成されています。

| ヘッダ | 内容 |
| --- | --- |
| `error.hpp` | 戻り値の`common::Error`とエラーコード、エラーのログ出力 |
| `log.hpp` | レベル付きのログマクロ`UAI_LOG_*` |
| `task.hpp` | μT-Kernelタスクの起動、ループ、停止の共通処理`common::Task` |
| `stable_aligned_bytes.hpp` | スタックやメッセージバッファ用の、固定アドレスで8 byte整列したバイト領域 |
| `fixed_message_slots.hpp`、`message_channel.hpp` | 型付きメッセージバッファ`common::MessageChannel` |
| `owned_buffer.hpp` | 固定長の値バッファ`common::OwnedBuffer` |

## common::Error

`error.hpp`の`common::Error`をドライバーとミドルウェアの戻り値に使います。例外は使いません。

```cpp
struct Error {
    ErrorCode code = ErrorCode::kOk;
    std::uint32_t detail = 0U;     // HALやST.AIの戻り値など
    const char *operation = "ok";  // 失敗した操作名
    constexpr bool Ok() const;
    constexpr bool IsRoutine() const;          // kNoFrame、kNoBuffer、kQueueFull
    void LogStatus(const char *component) const;
};
```

| `ErrorCode` | 意味 |
| --- | --- |
| `kOk` | 成功 |
| `kInvalidArgument` | 引数が不正 |
| `kNotInitialized`、`kAlreadyInitialized` | 初期化前、または二重初期化 |
| `kHardware`、`kCache` | HALやキャッシュ操作の失敗。`detail`にHALの戻り値が入ります |
| `kNoFrame`、`kNoBuffer`、`kQueueFull` | 今は処理対象がない。次のループで再試行すれば済むことがほとんどです |
| `kBufferOverflow` | 固定長バッファやキューに入りきらない。`OwnedBuffer::CopyFrom()`や、ai-appの結果キューが満杯のときに返します |
| `kTimeout` | NPUの応答待ちなどのタイムアウト |
| `kModel`、`kNpu` | STEdgeAIの生成コードやNPUの失敗。`detail`に`stai_return_code`が入ります |
| `kOwnership` | 所有権の不一致（別のタスクが保持している、古いトークンを渡した） |
| `kInvalidState` | 操作できる状態にない |

使うときの指針です。

- 初期化を二重に呼んだときは`kAlreadyInitialized`を返すので、成功と同じに扱えます。
- `IsRoutine()`が真になる`kNoFrame`、`kNoBuffer`、`kQueueFull`はエラーログを出さず、次のループへ進みます。
- 失敗を記録するときは`LogStatus("component")`を呼びます。`IsRoutine()`なら`UAI_LOG_DEBUG`、それ以外は`UAI_LOG_ERROR`で、操作名、コード名、`detail`を1行に出します。成功時は何もしないため、戻り値にそのまま付けられます。

```cpp
common::Error status = camera.Start();
status.LogStatus("camera");
// error: component=camera operation=camera.start code=hardware(4) detail=1
```

## ログ

`log.hpp`はレベル付きのログマクロを提供します。文字列は`const char*`で渡し、出力先のT-Monitorが要求する`UB*`への変換はログ層の中だけで行います。

```cpp
UAI_LOG_INFO("ai: model registered=%s\n", name);
```

| マクロ | 用途 |
| --- | --- |
| `UAI_LOG_ERROR` | 処理を続けられない失敗 |
| `UAI_LOG_WARN` | 復旧できた異常（カメラの再起動など） |
| `UAI_LOG_INFO` | 起動時の状態、1秒ごとの統計 |
| `UAI_LOG_DEBUG`、`UAI_LOG_TRACE` | 調査用。フレーム単位のログはここに置きます |

`kLogLevel`より詳細なレベルはコンパイル時に除かれ、引数の評価も行われません。`kLogLevel`は`log.hpp`で変更します（既定は`kInfo`）。レベルを実行時の値で選ぶときは`UAI_LOGF(level, ...)`、出力の前に重い処理を省きたいときは`common::IsLogEnabled(level)`を使います。T-Monitorの出力は1文字ずつ送るため、周期的なログは1秒程度の間隔にとどめてください。

## タスク

`task.hpp`の`common::Task`は、μT-Kernelのタスクを扱う定型をまとめたものです。

| 関数 | 内容 |
| --- | --- |
| `Start(monitor, entry, stack, priority, name)` | `TA_USERBUF`で呼び出し側のスタックを使ってタスクを作り、`monitor.RegisterTask()`に名前を登録して起動します。失敗したら`Halt()`します |
| `RunForever(monitor, name, wait_for_event, process_one)` | 待機と1回分の処理を繰り返す無限ループです。`process_one`の時間を`BeginTaskLoop()`と`RecordTaskLoop()`で記録します（[cpu_task_monitor](../middleware/cpu_task_monitor.md)） |
| `Now()` | `tk_get_otm()`のミリ秒（下位32 bit） |
| `Halt(message)` | エラーを出力して、そのタスクを永久に待たせます |

```cpp
common::StableAlignedBytes<4096U> stack;  // タスクより長生きする場所に置く
common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack, 5, "camera");

common::Task::RunForever(monitor, "camera",
                         [] { /* イベントを待つ。この時間は計測しない */ },
                         [] { /* 1回分の処理 */ });
```

`monitor`は`RegisterTask()`、`BeginTaskLoop()`、`RecordTaskLoop()`を持つ型であればよく、ホストテストではモックに置き換えます。

## 固定領域とメッセージチャネル

μT-Kernelにスタックやメッセージバッファの領域を渡すときは、アドレスが動かず、整列が保証されたメモリが必要です。`StableAlignedBytes<Bytes>`はコピーとムーブを禁止した8 byte整列の配列で、`size_bytes()`と`data()`だけを持ちます。

`MessageChannel<Message, Depth>`は、`Depth`件分の`Message`が入るメッセージバッファ（`tk_cre_mbf`、`TA_USERBUF`）を領域ごと所有し、型付きで送受信します。`Message`はtrivially copyableである必要があります。

```cpp
common::MessageChannel<pipeline::InferenceFrame, kFrameQueueDepth> frames;
frames.Create();                            // 戻り値はメッセージバッファID
frames.Send(frame, TMO_POL);                // 満杯なら E_TMOUT
pipeline::InferenceFrame received{};
if (frames.Receive(&received, TMO_FEVR) == sizeof(received)) { /* ... */ }
```

領域の大きさはμT-Kernelのメッセージヘッダを含めて`FixedMessageSlots`が計算するため、`Depth`件を必ず収容できます。ai-appではPipe2のフレームと推論結果の受け渡しに使い、満杯のときにフレームを返却する、最古の結果を捨てて再送する、といった方針はチャネルを包むクラス（`userspace/ai-app/src/task/pipeline_task.hpp`）に置いています。

## OwnedBuffer

`OwnedBuffer<Type, Capacity>`はポインタの代わりに値として持ち回る固定長バッファです。`CopyFrom(source, length)`は`Capacity`を超えると`kBufferOverflow`を返します。ai-appではセグメンテーションの20x20マスクを推論結果（`inference::SegmentationSet::mask`）に値として含めるため、推論バッファが再利用されても表示中のマスクは変わりません。

## ホストテスト

`common::Error`のログ分類、`Task`の起動とループ、`MessageChannel`の容量と送受信、`OwnedBuffer`のあふれ検出を、`kernel/utkernel/linux`のμT-Kernelモックで確認します。

```sh
make -C kernel/middleware/foundation/tests test
```
