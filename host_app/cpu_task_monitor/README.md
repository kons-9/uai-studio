# cpu_task_monitor

タスク別CPU使用率、割込み使用率、タスクループ時間を集計し、PNGまたはSVGにするツールです。ファームウェアは`kernel/middleware/cpu_task_monitor`でPSRAM上のリング（`0x91C48000`、512 KiB）に記録します。UARTログの`cpu: period=...`、`cpu: task=...`、`cpu: loop ...`行も入力にできます。

## 実機から取得する

```sh
make -C userspace/ai-app cpu-task-monitor
```

raw dump、JSON、CSV、PNGを`build-ai-app-person/`へ出力します。出力先は`CPU_TASK_MONITOR_DUMP`、`CPU_TASK_MONITOR_JSON`、`CPU_TASK_MONITOR_CSV`、`CPU_TASK_MONITOR_PNG`で変更できます。ダンプだけが必要な場合は`cpu-task-monitor-dump`を使います。リングは揮発性のため、リセット前に取得してください。

## CLI

```sh
MPLCONFIGDIR=/tmp/uai-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project host_app python host_app/cpu_task_monitor/cpu_task_monitor.py \
  build-ai-app-person/cpu_task_monitor.bin \
  -o /tmp/cpu_task_monitor.png \
  --csv /tmp/cpu_task_monitor.csv \
  --json /tmp/cpu_task_monitor.json \
  --cpu-hz 600000000
```

入力にはraw dump、UARTログのテキスト、`-`（標準入力）を指定できます。minicomの制御シーケンスは自動で除去します。

| オプション | 内容 |
| --- | --- |
| `-o`、`--output` | PNGまたはSVGの出力先 |
| `--csv`、`--json` | 集計結果の出力先 |
| `--cpu-hz` | 指定すると横軸を秒にします。省略時はレポート番号 |
| `--top-tasks` | 描画するタスク数。既定は12、`0`で全件 |
| `--gantt-repeats` | ガント図に含める各タスクの直近実行回数。既定は3 |

図は上から、タスク別の積み上げCPU使用率、ガント図、タスクごとのループ平均・最大時間です。ガント図は`Task::RunForever()`が計測したループ本体の区間で、イベント待ちは含みません。UARTログとversion 2以前のdumpにはループ時刻がないため、ガント図は描きません。

## スケジュール統計

ai-appは5秒ごとに`cpu: schedule name=...`をUARTへ出します。`touch`、`pipe2`、`submit`、`results`、`exposure`、`display`ごとの発火・開始・完了・失敗・間引き・ドロップ理由・最大遅延と、空転起床回数を記録します。`submit.completed`は入力キューへの送信成功で、推論完了数はAIモニタで確認します。

この行を含むUARTログは、JSONの`reports[].schedules`とCSVの`schedule_*`列へ出力し、グラフに最大遅延・完了率のパネルを追加します。統計行だけのログも描画できます。カウンタは32bitの累積値、遅延はmsです。通常CPU情報の横軸と異なり、統計パネルの横軸はレポート番号です。

旧binary ringのrecord・版は変更していないため、raw dumpには追加スケジュール統計は含まれません。統計も必要な場合はUARTログを入力にします。

## サンプル

[sample/](sample/)には実機のai-appから採取したraw dump、UARTログ、JSON、CSV、PNGがあります。更新する場合:

```sh
make -C userspace/ai-app cpu-task-monitor \
  CPU_TASK_MONITOR_DUMP="$PWD/host_app/cpu_task_monitor/sample/cpu_task_monitor.bin" \
  CPU_TASK_MONITOR_JSON="$PWD/host_app/cpu_task_monitor/sample/cpu_task_monitor.json" \
  CPU_TASK_MONITOR_CSV="$PWD/host_app/cpu_task_monitor/sample/cpu_task_monitor.csv" \
  CPU_TASK_MONITOR_PNG="$PWD/host_app/cpu_task_monitor/sample/cpu_task_monitor.png"
```
