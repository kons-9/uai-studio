# Host側ツール

Host側の解析・可視化ツールと、ai-appのメモリ配置生成ツールをまとめています。
Python 3.10以上と `uv` を使います。依存パッケージを用意するには、リポジトリの
ルートから次を実行します。

```sh
UV_CACHE_DIR=/tmp/uai-uv-cache uv sync --project host_app --locked
```

## AIモデル実行トレース

同梱サンプルをJSONへ変換する例です。

```sh
python3 host_app/ai_model_monitor/ai_model_monitor.py \
  decode host_app/ai_model_monitor/sample/ai_model_monitor.bin \
  --output /tmp/ai_model_monitor.json
```

サンプルのPNG可視化を含めて全処理を実行する場合:

```sh
UV_CACHE_DIR=/tmp/uai-uv-cache \
MPLCONFIGDIR=/tmp/uai-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project host_app python \
  host_app/ai_model_monitor/ai_model_monitor.py all \
  host_app/ai_model_monitor/sample/ai_model_monitor.bin \
  --json /tmp/ai_model_monitor.json \
  --png /tmp/ai_model_monitor.png
```

リポジトリに同梱している入力は
[`ai_model_monitor/sample/`](ai_model_monitor/sample/) に置いています。
2026-09-30に実機から採取したrawデータ、JSON、タイムラインPNGを同梱しています。
採取条件と更新手順は[`ai_model_monitor/README.md`](ai_model_monitor/README.md)にあります。
ボードから取得したraw dumpも同じ形式なので、例として
`host_app/ai_model_monitor/data/` に保存します。ファームウェアのThreadMonitorリングを
取得するには、実機をRAM runした後に次を実行します。

```sh
make -C userspace/ai-app thread-monitor-dump
mkdir -p host_app/ai_model_monitor/data
cp build-ai-app-person/thread_monitor.bin \
  host_app/ai_model_monitor/data/thread_monitor.bin
```

以後はコマンドの入力パスを `data/thread_monitor.bin` に変えます。同梱サンプルは
`sample/` に残し、取得データは `data/`、解析結果は `/tmp/` や
`build-ai-app-person/` などへ置きます。

## CPUタスクモニタ

UARTログをテキストファイルに保存してから、次のようにPNGと集計データを作成します。

```sh
mkdir -p host_app/cpu_task_monitor/data
UV_CACHE_DIR=/tmp/uai-uv-cache \
MPLCONFIGDIR=/tmp/uai-cpu-task-monitor \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project host_app python \
  host_app/cpu_task_monitor/cpu_task_monitor.py \
  host_app/cpu_task_monitor/data/uart.log \
  --output /tmp/cpu_task_monitor.png \
  --csv /tmp/cpu_task_monitor.csv \
  --json /tmp/cpu_task_monitor.json
```

ログファイルは `host_app/cpu_task_monitor/data/` に保存できます。ai-app内蔵のPSRAM
リングを使う場合は、起動前にUARTモニターを開き、実行中のファームウェアから次を実行します。

```sh
make -C userspace/ai-app cpu-task-monitor-dump
```

ダンプは既定で `build-ai-app-person/cpu_task_monitor.bin` に保存されます。
取得したリングをPNGと集計データにするには、次を実行します。

```sh
UV_CACHE_DIR=/tmp/uai-uv-cache \
MPLCONFIGDIR=/tmp/uai-cpu-task-monitor \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project host_app python \
  host_app/cpu_task_monitor/cpu_task_monitor.py \
  build-ai-app-person/cpu_task_monitor.bin \
  --output /tmp/cpu_task_monitor.png \
  --csv /tmp/cpu_task_monitor.csv \
  --json /tmp/cpu_task_monitor.json
```

UARTテキストログを使う場合は、ログを
`host_app/cpu_task_monitor/data/uart.log` に保存して、最初の可視化例の入力パスを
そのファイルにします。`cpu_task_monitor/sample/`には実機のCPU trace、UARTログと
生成済みのJSON・CSV・PNGがあります。取得条件と更新方法は
[`cpu_task_monitor/README.md`](cpu_task_monitor/README.md)を参照してください。
各ツールの全オプションは[`ai_model_monitor/README.md`](ai_model_monitor/README.md)と
[`cpu_task_monitor/README.md`](cpu_task_monitor/README.md)を参照してください。

## ai-appメモリ配置生成

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
modifies the source tree or the build support files.

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

The ai-app CMake target runs `all` into the selected CMake build directory's
`generated/` folder (by default, `build-ai-app-person/generated/`). The kernel
components include generated headers from there; they do not include source
files from `userspace/ai-app`.

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
