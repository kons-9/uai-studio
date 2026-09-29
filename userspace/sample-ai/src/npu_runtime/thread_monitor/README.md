# ThreadMonitor trace

`ThreadMonitor` samples the task that owns `NpuRuntime` every 100 ms and
stores fixed-size records in the linker-reserved internal APP RAM region:

```text
address: __sample_ai_thread_monitor_start__ (see the .map file)
size:    0x00010000 (64 KiB)
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

Each record contains the phase end timestamp, the exact elapsed time in
`npu_elapsed_ms`, and the selected model's numeric `model_kind_id`. The
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

The decoder emits the records in chronological order and ignores records
whose commit marker indicates an incomplete write.

For a graph, install the host-side plotting dependency in the local `uv`
environment and run:

```text
uv run --project userspace/sample-ai/tools \
  python userspace/sample-ai/tools/visualize_thread_monitor.py \
  trace.bin --output thread_monitor.png
```

The graph's first panel is a Gantt-style timeline with one row per model;
phase is represented by color. Its horizontal axis is
`timestamp_ms / 1000`, where `timestamp_ms` is the µT-Kernel monotonic system
time (milliseconds since boot), not a sample index. The second panel has one
stacked average-duration bar per model, with the same phase colors, and its
horizontal axis is average duration in milliseconds. The `task_id` shown in
the raw trace header identifies the RTOS task that owns `NpuRuntime`; it is
context, not the model identifier, and is intentionally omitted from the
model-comparison title.

This monitor currently observes the task that owns `NpuRuntime`; it does not
yet enumerate every µT-Kernel task. Therefore the Gantt task row is explicitly
labeled with that task ID.
