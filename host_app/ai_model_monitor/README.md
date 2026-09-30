# ai_model_monitor

ai-appのAIパイプライン実行トレースを読み取るツールです。ファームウェアは`kernel/middleware/ai_model_monitor`でPSRAM上のリング（`0x91C40000`、32 KiB）に記録します。rawデータの形式名はThreadMonitorです。

## 実機から取得する

```sh
make -C userspace/ai-app thread-monitor
```

ST-LINKのHot Plug接続でCPUを一時停止してリングを読み出し、CPUを再開してからJSONとPNGを作ります。出力先は`THREAD_MONITOR_DUMP`、`THREAD_MONITOR_JSON`、`THREAD_MONITOR_PNG`で変更できます。ダンプだけが必要な場合は`thread-monitor-dump`を使います。リングは揮発性のため、リセット前に取得してください。

## CLI

`ai_model_monitor.py`のサブコマンドは次の4つです。入力はraw dumpとdecode済みJSONのどちらでも構いません。

| サブコマンド | 内容 |
| --- | --- |
| `decode` | raw dumpをJSONへ変換します。`-o`省略時は標準出力へ出します。 |
| `analyze` | モデルごとのCPU/NPUステップ時間を集計します。 |
| `visualize` | タイムラインPNGを作ります。 |
| `all` | 上の3つをまとめて実行します。 |

```sh
python3 host_app/ai_model_monitor/ai_model_monitor.py \
  decode host_app/ai_model_monitor/sample/ai_model_monitor.bin -o /tmp/trace.json

MPLCONFIGDIR=/tmp/uai-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project host_app python host_app/ai_model_monitor/ai_model_monitor.py \
  all host_app/ai_model_monitor/sample/ai_model_monitor.bin \
  --json /tmp/trace.json --png /tmp/trace.png
```

`decode`と`analyze`は標準ライブラリだけで動きます。`visualize`と`all`はmatplotlibを使うため`uv run`で実行します。`LD_PRELOAD`はホストのlibstdc++とmatplotlibが衝突する環境でだけ必要です。

主なオプション:

| オプション | 内容 |
| --- | --- |
| `--cpu-hz` | サイクル値を時間へ換算するCPUクロック。既定は600000000 |
| `--max-inferences` | 図に描く直近の推論数。既定は12、`-1`で全件 |
| `--json`、`--png` | `all`の出力先。省略時は入力の隣に作ります |

タイムラインはモデルごとにCPUとNPUの2行で描き、各区間に推論IDを付けます。CPUの時間は`Evaluate()`の開始と終了の差で、スケジューリング待ちを含みます。

## トレース形式

version 5では、ヘッダーの直後にモデル名テーブル（最大16モデル、名前は27 byteまで）を置きます。ファームウェアは`PipelineRuntime::RegisterModelName()`で登録するため、モデルを追加してもツール側の変更は不要です。version 2から4のdumpも読めますが、モデル名はIDで表示します。

## サンプル

[sample/](sample/)には実機のai-appから採取したraw dump、JSON、PNGがあります。更新する場合:

```sh
make -C userspace/ai-app thread-monitor \
  THREAD_MONITOR_DUMP="$PWD/host_app/ai_model_monitor/sample/ai_model_monitor.bin" \
  THREAD_MONITOR_JSON="$PWD/host_app/ai_model_monitor/sample/ai_model_monitor.json" \
  THREAD_MONITOR_PNG="$PWD/host_app/ai_model_monitor/sample/ai_model_monitor.png"
```
