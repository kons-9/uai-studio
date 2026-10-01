# cpu_task_monitor

μT-Kernelのディスパッチフックと割り込みフック（[フックAPI](../kernel/utkernel.md)）でタスクごとのCPUサイクルを数え、タスク別のCPU使用率、割り込みの使用率、タスクループの時間をPSRAM上のリングに記録します。

![CPU task monitorの出力例](https://raw.githubusercontent.com/kons-9/uai-studio/main/host_app/cpu_task_monitor/sample/cpu_task_monitor.png)

上段がタスク別のCPU使用率、中段がタスクの実行状況（ガント図）、下段がループ時間です。

## 仕組み

- `td_hok_dsp`の`exec`と`stop`で、タスクが実行された区間のサイクル数（DWT CYCCNT）をタスクIDごとに積算します。
- `td_hok_int`の`enter`と`leave`で割り込み処理の時間を数え、割り込まれたタスクの時間から除きます。
- `Report()`を呼ぶと、前回からの各タスクの使用率を1レコードとして記録し、カウンタをリセットします。
- `BeginTaskLoop()`と`RecordTaskLoop()`でループ1回分の時間を記録すると、ガント図に使われます。

CMakeオプション`UAI_CPU_TASK_MONITOR`（Makeでは`ENABLE_CPU_TASK_MONITOR=1`）が無効のときは、同じAPIの空実装になり、フックも組み込まれません。ai-appでは既定で有効です。

## 使い方

```cpp
middleware::cpu_task_monitor::CpuTaskMonitor monitor;

monitor.Start();                          // usermain()の最初で呼ぶ。フックを登録する
monitor.RegisterTask(tk_get_tid(), "usermain");
monitor.InitializeTraceBuffer();          // PSRAMの初期化後に呼ぶ
monitor.Report();                         // 1秒ごとに呼ぶと使用率を記録する
```

- `Start()`はフックを登録するだけなので、PSRAMの初期化前でも呼べます。記録は`InitializeTraceBuffer()`以降に始まります。
- `RegisterTask()`でタスクIDに名前を付けます。付けていないタスクはIDで表示されます。
- `Report()`はどのタスクから呼んでも構いません。ai-appでは初期化タスクが1秒周期で呼んでいます。

ループ時間を記録するには、ループ本体を`BeginTaskLoop()`と`RecordTaskLoop()`で囲みます。ai-appの`Task::RunForever()`（`userspace/ai-app/src/task/task.hpp`）がこれを行う雛形です。

```cpp
Task::RunForever(monitor, "camera",
                 [] { /* イベントを待つ。この時間は計測しない */ },
                 [] { /* 1回分の処理 */ });
```

## 取得と可視化

```sh
make -C userspace/ai-app cpu-task-monitor
```

記録先は`Key::kCpuTaskMonitor`の領域（ai-appではPSRAMの512 KiB）です。ST-LINKで読み出し、JSON、CSV、PNGを`build-ai-app-person/`へ出力します。UARTログの`cpu:`行からも同じ図を作れます。CLIのオプションは[host_app/cpu_task_monitor/README.md](https://github.com/kons-9/uai-studio/blob/main/host_app/cpu_task_monitor/README.md)を参照してください。

## 読み方の例

ai-appの3モデル同時推論では、カメラ表示タスク（`camera`）がCPU時間の大半を使い、前処理と後処理のタスクはNPUの完了を待つ間に短く走ることが分かります。NPUレーンのタスクの使用率が低いのに推論が遅い場合は、CPUではなくNPUの処理時間がボトルネックなので、[ai_model_monitor](ai_model_monitor.md)でモデルごとのNPU時間を確認します。
