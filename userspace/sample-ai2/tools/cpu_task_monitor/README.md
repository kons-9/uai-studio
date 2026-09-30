# sample-ai2 CPU task monitor

`cpu_task_monitor` は、ファームウェアがUARTへ出力する
`cpu: period=...` / `cpu: task=...` / `cpu: loop ...` 行を読み取り、タスク別CPU使用率、
割込み使用率、タスク名、1ループの平均・最大時間をPNGまたはSVGにします。PSRAMへ保存した
CPU task monitorのバイナリリングも直接読み取れます。minicomのカーソル制御シーケンスも
自動的に除去します。CPU monitor v3のバイナリでは、`Task::RunForever`が計測した
ループ本体の開始・終了時刻を記録し、ガント図に表示します。

## 使い方

UARTログを保存してから、リポジトリルートで実行します。

```sh
python3 userspace/sample-ai2/tools/cpu_task_monitor/cpu_task_monitor.py \
  uart.log \
  --output build/cpu_task_monitor.png \
  --csv build/cpu_task_monitor.csv \
  --json build/cpu_task_monitor.json \
  --cpu-hz 1000000000
```

`--cpu-hz` を省略すると、横軸はレポート番号になります。指定すると、各レポートの
`period` cycleを累積した秒数になります。CPU使用率、ループ平均・最大時間、CSV、JSONは
常に入力の全レポートを使います。描画するタスク数を制限する場合は `--top-tasks 8` を
指定します。

実機のPSRAMリングを取得して可視化する場合は、次を実行します。

```sh
make -C userspace/sample-ai2 cpu-task-monitor
```

ダンプだけが必要な場合は`cpu-task-monitor-dump`を使用します。リングは
`0x91C48000`の512 KiBで、リセット前に取得してください。v3のガント図は
イベント待ちを除いたループ本体の経過区間です。ループ本体内でブロックした時間や
他タスクによるプリエンプトは区間に含まれます。UARTログやv2以前のダンプには
各ループの時刻がないため、CPU使用率と平均・最大時間のみ表示します。

matplotlibは既存の `userspace/sample-ai2/tools/pyproject.toml` に定義されています。
環境を使う場合は次のように実行できます。

```sh
UV_CACHE_DIR=/tmp/uai-uv-cache \
MPLCONFIGDIR=/tmp/sample-ai2-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project userspace/sample-ai2/tools \
  python userspace/sample-ai2/tools/cpu_task_monitor/cpu_task_monitor.py \
  uart.log -o build/cpu_task_monitor.png
```

上段は全レポートのタスク別積み上げCPU使用率と割込み使用率、中段はガント図、下段は
全レポートのタスクごとのループ平均・最大時間です。ガント図は、計測区間がある各タスクの
直近3回が入るように表示期間を決めます。回数は `--gantt-repeats 5` のように変更できます。
CPUクロック指定時は100 ms以下の短い隙間をつないで稼働バーストとしてまとめます。
タスク行には登録名とIDを表示します。
`--csv` はCPU使用率と集計ループ時間を同じレポート行へ出力します。
