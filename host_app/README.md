# ai-app memory layout generator

`auto_static_memory_layout/` resolves the memory layout from three
inputs:

- `config/board_memory.json`: physical memory regions, model weight addresses,
  command-blob sections, and linker entry point.
- `config/application_memory.json`: runtime image sizes/counts and fixed
  reservations for capture, display, inference, scratch, and diagnostics.
- `config/model_layout.json` plus the generated model directory: model order,
  weight files, `stai_network.h` tensor metadata, NPU pool comments, and
  ECBLOB command arrays.

The resolved JSON is the canonical intermediate representation. YAML is a
human-readable export of that JSON; it is not an input required by the build.
The same resolved document generates `raw.hpp`, `memory_config.hpp`, and the
linker script. All output paths are explicit, so invoking the tool never
modifies the source tree or the `tools` directory.

```sh
python3 host_app/auto_static_memory_layout all \
  --board userspace/ai-app/config/board_memory.json \
  --application userspace/ai-app/config/application_memory.json \
  --models-dir userspace/ai-app/models \
  --model-config userspace/ai-app/config/model_layout.json \
  --output-dir /tmp/ai-app-memory-layout \
  --linker-base userspace/ai-app/stm32n6570-dk-npu-ram.ld
```

For pipeline use, the subcommands are:

```sh
auto_static_memory_layout resolve ...
auto_static_memory_layout generate_yml --input memory_layout.json --output memory_layout.yml
auto_static_memory_layout generate_cpp --input memory_layout.json \
  --linker-base stm32n6570-dk-npu-ram.ld \
  --key-header generated/middleware/memory/generated/static_memory_layout/key.hpp \
  --raw-header generated/middleware/memory/generated/static_memory_layout/raw.hpp \
  --memory-config generated/middleware/memory/generated/memory_config.hpp \
  --linker generated/stm32n6570-dk-npu-ram.ld
```

The ai-app CMake target runs `all` into
`userspace/ai-app/generated/` and adds that generated include directory
before `src/`. The checked-in C++ headers remain available as compatibility
copies for source browsing, but are not the build source of truth.

The public C++ API is
`kernel/middleware/memory/static_memory_layout.hpp`. The generated `key.hpp`
contains the layout-specific keys and is included by the public header.
The generated `raw.hpp`
contains the static-memory keys and linker-backed layout instance.

The ai-app ThreadMonitor ring is placed in the dedicated `PSRAM_TRACE`
region (`0x91C40000`, 32 KiB), rather than APP RAM or the model NOR. This keeps
the NPU's memory-mapped weight reads uninterrupted and leaves the trace
available to `make -C userspace/ai-app thread-monitor-dump` while the CPU is halted. The ring is
volatile PSRAM; dump it before resetting the board if the trace must be kept.

The CPU task monitor uses a separate `PSRAM_CPU_TRACE` ring at
`0x91C48000` (512 KiB). It records one-second usage reports and each measured
`Task::RunForever` loop interval. Use `make -C userspace/ai-app
cpu-task-monitor` to dump and visualize the reports and loop intervals.

AI model monitor tools are collected under
[`ai_model_monitor/`](ai_model_monitor/).  See
[`ai_model_monitor/README.md`](ai_model_monitor/README.md) for the CLI and
visualization usage.

CPU task monitor tools are collected under
[`cpu_task_monitor/`](cpu_task_monitor/).  They parse the UART output from
`td_hok_dsp`/`td_hok_int` and generate a task-usage plot. See
[`cpu_task_monitor/README.md`](cpu_task_monitor/README.md) for the CLI.
