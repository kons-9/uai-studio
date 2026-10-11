# cpu_task_monitor

## 概要

`host_app/cpu_task_monitor`は、タスク別CPU使用率、割込み使用率、タスクループ時間を集計するツールです。実機から取得したraw dumpやUARTログを読み込み、PNG/SVGの図とJSON/CSVの集計結果を生成します。

実機側の記録器は[kernel/middlewareのcpu_task_monitor](../middleware/cpu_task_monitor.md)です。ai-appではPSRAMの`0x91C48000`から512 KiBのリングに記録します。AIの推論区間を見る[ai_model_monitor](ai_model_monitor.md)と組み合わせると、推論の経過時間とCPUの使用状況を比較できます。

### 入力と出力

| 入力 | 利用できる情報 |
| --- | --- |
| CPUモニタのraw dump | タスク・割込みの使用率、ループ統計。新しい形式ではループの実行時刻も利用 |
| UARTログ | `cpu: period=...`、`cpu: task=...`、`cpu: loop ...`行のCPU・ループ統計 |
| スケジュール統計を含むUARTログ | 上記に加え、`cpu: schedule name=...`行の発火・完了・遅延などの統計 |
| `-` | 標準入力からUARTログを読む |

minicomの制御シーケンスは自動で除去します。出力JSONは解析結果であり、入力として読み直す形式ではありません。

| 出力 | 内容 |
| --- | --- |
| PNG / SVG | CPU使用率、ループのガント図、平均・最大ループ時間。ログに応じてスケジュール統計も表示 |
| JSON | レポートごとのCPU・ループ情報。スケジュール統計は`reports[].schedules`に格納 |
| CSV | 集計をlong-form形式で出力。スケジュール統計は`schedule_*`列にも出力 |

## 使い方

コマンドはリポジトリルートで実行します。図生成には`matplotlib`が必要なので、[共通のセットアップ](usage.md#準備)を済ませてください。

### UARTログを解析する

付属サンプルから図と集計ファイルを作ります。

```sh
uv run --project host_app python host_app/cpu_task_monitor/cpu_task_monitor.py \
  host_app/cpu_task_monitor/sample/uart.log \
  -o /tmp/cpu-uart.png \
  --json /tmp/cpu-uart.json --csv /tmp/cpu-uart.csv \
  --cpu-hz 600000000
```

`--cpu-hz`を指定すると、CPU情報の横軸を秒で表示します。省略時はレポート番号です。実機のクロックが異なる場合は値を変更してください。

### raw dumpからSVGを作る

```sh
uv run --project host_app python host_app/cpu_task_monitor/cpu_task_monitor.py \
  host_app/cpu_task_monitor/sample/cpu_task_monitor.bin \
  -o /tmp/cpu-raw.svg --cpu-hz 600000000 \
  --top-tasks 0 --gantt-repeats 3
```

出力ファイルの拡張子でPNGまたはSVGを選びます。`--top-tasks 0`は全タスクを描画します。

### 実機のデータを取得する

実行中のai-appとST-LINK接続がある場合は、次のターゲットでdump取得から可視化まで行えます。

```sh
make -C userspace/ai-app cpu-task-monitor
```

raw dump、JSON、CSV、PNGを既定で`build-ai-app-person/`へ出力します。dumpだけが必要なら`cpu-task-monitor-dump`を使います。リングは揮発性なので、リセット前に取得してください。

出力先を変更する場合は次の変数を指定します。

```sh
make -C userspace/ai-app cpu-task-monitor \
  CPU_TASK_MONITOR_DUMP=/tmp/cpu-live.bin \
  CPU_TASK_MONITOR_JSON=/tmp/cpu-live.json \
  CPU_TASK_MONITOR_CSV=/tmp/cpu-live.csv \
  CPU_TASK_MONITOR_PNG=/tmp/cpu-live.png
```

スケジュール統計も調べる場合はUARTログを保存し、上のUARTログ解析の入力に指定します。raw dumpには追加のスケジュール統計が含まれません。

## 結果の読み方

図は上から、タスク別の積み上げCPU使用率、ガント図、タスクごとの平均・最大ループ時間です。

- CPU使用率では、どのタスクや割込みがCPU時間を使っているかを確認します。
- ガント図は`Task::RunForever()`が計測したループ本体の区間です。イベント待ちは含みません。UARTログとversion 2以前のdumpにはループ時刻がないため、ガント図は描きません。
- 平均と最大ループ時間を比較し、一部のループだけが長くなっていないかを確認します。

ai-appは5秒ごとに、`touch`、`pipe2`、`submit`、`results`、`exposure`、`display`のスケジュール統計をUARTへ出します。発火・開始・完了・失敗、間引き・ドロップ理由、最大遅延、空転起床回数を記録します。

この行を含むログでは、図に最大遅延・完了率のパネルを追加します。統計行だけのログも描画できます。カウンタは32bitの累積値、遅延はmsで、統計パネルの横軸は`--cpu-hz`指定時もレポート番号です。

`submit.completed`は入力キューへの送信成功数です。推論完了数ではないため、推論の実行・完了は[AIモニタ](ai_model_monitor.md)と合わせて確認してください。

## 主なオプションと注意点

| オプション | 内容 |
| --- | --- |
| `-o` / `--output` | PNGまたはSVGの出力先 |
| `--json`、`--csv` | 集計結果の追加出力先 |
| `--cpu-hz` | 横軸を秒へ換算するためのCPUクロック |
| `--top-tasks` | 描画するタスク数。既定12、`0`で全件 |
| `--gantt-repeats` | 各タスクについてガント図に含める直近の実行回数。既定3 |

`matplotlib`の設定ディレクトリに書けない場合は`MPLCONFIGDIR=/tmp/uai-matplotlib`を付けて実行します。ホストのlibstdc++と衝突する場合の対処例は[ツールのREADME](https://github.com/kons-9/uai-studio/blob/main/host_app/cpu_task_monitor/README.md)を参照してください。

詳細なヘルプは`python3 host_app/cpu_task_monitor/cpu_task_monitor.py --help`で確認できます。