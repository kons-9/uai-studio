# task

`kernel/middleware/task`は、μT-Kernelのタスクを扱う定型をまとめた`common::Task`です。OSのタスクAPIに依存するため[共通基盤（foundation）](../kernel/common.md)とは分けていますが、名前空間は[buffer](buffer.md)と同じ`uai::ai::common`です。

| 関数 | 内容 |
| --- | --- |
| `Start(monitor, entry, stack, priority, name)` | `TA_USERBUF`で呼び出し側のスタックを使ってタスクを作り、`monitor.RegisterTask()`に名前を登録して起動します。登録の失敗はWarnログ、作成と起動の失敗は`Halt()`します |
| `RunForever(monitor, name, wait_for_event, process_one)` | 待機と1回分の処理を繰り返す常駐ループです。`process_one`の時間を`BeginTaskLoop()`と`RecordTaskLoop()`で記録します（[cpu_task_monitor](cpu_task_monitor.md)） |
| `Now()` | `tk_get_otm()`のミリ秒（下位32 bit） |
| `Halt(message)` | エラーを出力して、そのタスクを永久に待たせます |

```cpp
#include "middleware/task/task.hpp"

common::StableAlignedBytes<4096U> stack;  // タスクより長生きする場所に置く
common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack, 5, "camera");

common::Task::RunForever(monitor, "camera",
                         [] { /* イベントを待つ。この時間は計測しない */ },
                         [] { /* 1回分の処理 */ });
```

`monitor`は`RegisterTask()`、`BeginTaskLoop()`、`RecordTaskLoop()`を持つ型であればよく、ホストテストではモックに置き換えます。`RunForever()`はタスクの寿命そのものなので終わりませんが、`wait_for_event`や`process_one`の中で行う待機や再試行は、タイムアウトか回数で打ち切ってエラーにしてください。

## ホストテスト

`kernel/utkernel/linux`のμT-Kernelモックで、呼び出し側のスタックを使った起動と、待機してから処理する順序を確認します。

```sh
make -C kernel/middleware/task/tests test
```