# ThreadMonitor trace

## Make target

実機からThreadMonitorを取得して解析する場合は、リポジトリルートで次を実行します。

```sh
make APP_TARGET=sample-ai thread-monitor
```

このターゲットはsample-aiをビルドし、ELFの
`__sample_ai_thread_monitor_start__`／`__sample_ai_thread_monitor_end__`から領域を
自動検出します。その後、ST-LINKのHot Plug接続でCPUを一時停止して
`build/thread_monitor.bin`へ128 KiBを取得し、CPUを再開してから
`build/thread_monitor.json`と`build/thread_monitor.png`を生成し、モデル別の解析結果を
表示します。PNG生成には`userspace/sample-ai/tools/pyproject.toml`の`matplotlib`を
`uv run`で使用します。

生バイナリだけを取得する場合は次を使います。

```sh
make APP_TARGET=sample-ai thread-monitor-dump
```

出力先やCPUクロックはMake変数で変更できます。

```sh
make APP_TARGET=sample-ai thread-monitor \
  THREAD_MONITOR_DUMP=/tmp/thread_monitor.bin \
  THREAD_MONITOR_JSON=/tmp/thread_monitor.json \
  THREAD_MONITOR_PNG=/tmp/thread_monitor.png \
  THREAD_MONITOR_CPU_HZ=600000000
```

`ThreadMonitor` samples the task that owns `NpuRuntime` every 100 ms and
stores fixed-size records in the linker-reserved internal APP RAM region:

```text
address: __sample_ai_thread_monitor_start__ (see the .map file)
size:    0x00020000 (128 KiB)
record:  64 bytes
```

The region is a ring buffer. It contains approximately 102 seconds of samples
before old records are overwritten. The header and records are flushed from
the CPU data cache after each update, so a debugger can read the region after
halting the target. The region is volatile and is not guaranteed to survive a
power cycle. A warm reset may retain it and increments `boot_count` when the
format is still valid.

When an inference runs, the following phase records are queued:

```text
model_selection
input_preparation
input_preparation_wait
npu_execution
output_preparation
output_decoding
result_conversion
```

Trace type pipeline_stage (type 6) records the operation-level runtime
stages. Its phase_id field is interpreted as the internal pipeline::Stage ID:

```text
copy, resize, letterbox, input_cache, submit,
irq_wait, epoch_continue, output_cache, decode, convert, finalize
```

`irq_wait` は NPU 所有タスクのステータス確認・必要時の IRQ 待機を含みます。別の CPU 入力タスクが `copy` / `resize` / `letterbox` を実行する間も NPU 所有タスクは待機できるため、可視化では `irq_wait` 全体を CPU 稼働時間として合算しません。これらの CPU 前処理は現在 stage 単位のトレースを出さず、`input_preparation` phase で計測します。`epoch_continue` は未完了時のみ実行する NPU 所有タスクの `ContinueRun` 呼び出しです。後処理の ID は旧トレース互換として残りますが、現行計画には含めません。

Pipeline stages also carry DWT end and elapsed cycles in the existing raw
record fields. This keeps CPU work below one millisecond visible; the host
plot converts those cycles with `--cpu-hz` instead of displaying them as zero.

Input stages prepared by the prefetch callback are recorded at their actual
execution time. The later inference only records the handoff, so the graph
does not duplicate a prefetched CPU interval as a zero-length active-stage
bar.

Firmware trace version 4 also records dense `npu_epoch` entries for every
generated epoch block. These entries come from the four ST Edge AI runtime
callbacks and use the Cortex-M DWT cycle counter, so short pure-SW and hybrid
blocks remain measurable below the millisecond resolution. The callback stage
is encoded as `callback_stage` by the decoder:

```text
cpu_start = PRE_START -> POST_START
npu       = POST_START -> PRE_END
cpu_end   = PRE_END -> POST_END
```

The `npu` stage is the NPU/ATON execution and wait interval. For these records,
`epoch_index`, `epoch_flags`, and `epoch_address` identify the block, while
`cycle_elapsed` is the measured duration of that stage. Flags identify
`pure_hw`, `pure_sw`, `hybrid`, `blob`, and `internal` blocks.

Phase records contain the phase end timestamp, the exact elapsed time in
`npu_elapsed_ms`, and the selected model's numeric `model_kind_id`. Epoch
records use `cycle_elapsed` for the callback interval and retain the same
numeric `model_kind_id`. The
`input_preparation` interval is only the model's input preparation operation.
`input_preparation_wait` starts when that preparation completes and ends just
before the input is submitted to the NPU; it includes time spent waiting for
the previous inference and input handoff/cache setup. For `npu_execution`,
the interval starts immediately before the asynchronous ST.AI `Run` call and
ends when the NPU reports done (or an error/timeout). The host reconstructs
every phase interval as:

```text
start_ms = timestamp_ms - npu_elapsed_ms
end_ms   = timestamp_ms
```

After a frame is submitted, the runtime may prepare the next queued frame
while the current NPU run is still active. Therefore an input-preparation bar
can overlap the preceding model's NPU bar in the Gantt panel. The frame keeps
the model ID selected for that preparation, so the later model switch remains
consistent with the prepared input.

The current model IDs are `0=person`, `1=segmentation`, and `2=face`.

Periodic `sample` records still contain the latest NPU timing for convenient
correlation with task state. The trace header and records also expose the
numeric `monitored_task_id` and `monitor_task_id`.

The raw dump can be decoded on the host with:

```text
python3 userspace/sample-ai/tools/decode_thread_monitor.py trace.bin --pretty
```

To compare the measured profile density across person, segmentation, and
face, use:

```text
python3 userspace/sample-ai/tools/analyze_npu_trace.py trace.bin --top 20
```

Pass `--cpu-hz` to print cycle durations as microseconds as well.

The decoder emits the records in chronological order and ignores records
whose commit marker indicates an incomplete write.

For a graph, install the host-side plotting dependency in the local `uv`
environment and run:

```text
uv run --project userspace/sample-ai/tools \
  python userspace/sample-ai/tools/visualize_thread_monitor.py \
  trace.bin --output thread_monitor.png
```

The graph has two panels. The first is an execution timeline with separate
CPU-stage and NPU/ATON lanes for each model. It contains the operation-level
stage-colored CPU active bars and NPU/ATON bars, and preserves overlap on the
`timestamp_ms / 1000` time axis. Wait/control intervals are retained in the
raw trace but omitted from this focused graph. The second panel shows the
average CPU active time and NPU/ATON execution time as separate bars. CPU active
averages use cycle-accurate pipeline stages when available, so direct-input
models such as person do not incorrectly appear to have zero CPU work. The bars
are deliberately not stacked because CPU-side work may overlap NPU work.

The dense `npu_epoch` records remain available in the decoded JSON and in the
trace analysis tool; the two-panel graph is intentionally focused on the
CPU/NPU execution relationship and its average decomposition.
The `task_id` shown in the raw trace header identifies the RTOS task that owns
`NpuRuntime`; it is context, not the model identifier, and is intentionally
omitted from the model-comparison title.

This monitor currently observes the task that owns `NpuRuntime`; it does not
yet enumerate every µT-Kernel task. Therefore the Gantt task row is explicitly
labeled with that task ID.
