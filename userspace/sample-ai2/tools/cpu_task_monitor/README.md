# sample-ai2 CPU task monitor

`cpu_task_monitor` は、ファームウェアがUARTへ出力する
`cpu: period=...` / `cpu: task=...` 行を読み取り、タスク別CPU使用率と割込み使用率を
PNGまたはSVGにします。PSRAMへ保存したCPU task monitorのバイナリリングも直接読み取れます。
minicomのカーソル制御シーケンスも自動的に除去します。

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
`period` cycleを累積した秒数になります。直近だけを見る場合は `--window 60`、
描画するタスク数を制限する場合は `--top-tasks 8` を指定します。

実機のPSRAMリングを取得して可視化する場合は、次を実行します。

```sh
make -C userspace/sample-ai2 cpu-task-monitor
```

ダンプだけが必要な場合は`cpu-task-monitor-dump`を使用します。リングは
`0x91C48000`の32 KiBで、リセット前に取得してください。

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

上段はタスク別の積み上げCPU使用率と割込み使用率、下段はタスク別使用率の
ヒートマップです。`--csv` はレポートを表形式にするため、後処理や比較にも使えます。
