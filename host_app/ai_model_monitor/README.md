# ai-app AI model monitor

このディレクトリには、ai-appのAIモデル実行状況を確認するツールを
まとめています。エントリーポイントは `ai_model_monitor.py` です。
内部のrawデータ形式はThreadMonitorですが、用途に合わせてツール名を
`ai_model_monitor` としています。

サンプルデータは [`sample/`](sample/) にあります。

- `sample/ai_model_monitor.bin`: raw dump
- `sample/ai_model_monitor.json`: decode済みJSON
- `sample/ai_model_monitor.png`: 実機データのタイムライン図

サンプルは2026-09-30にSTM32N6570-DKへai-appをRAM runして採取したものです。
ThreadMonitor version 5の32 KiBリングから取得し、JSONとPNGも同じraw dumpから生成しています。
リングは循環式のため、ヘッダーの`dropped_count`は上書きされた古いレコード数です。
raw dumpには最新のリング容量分のレコードが入っています。

実機からサンプル一式を更新する場合:

```sh
make -C userspace/ai-app thread-monitor \
  THREAD_MONITOR_DUMP="$PWD/host_app/ai_model_monitor/sample/ai_model_monitor.bin" \
  THREAD_MONITOR_JSON="$PWD/host_app/ai_model_monitor/sample/ai_model_monitor.json" \
  THREAD_MONITOR_PNG="$PWD/host_app/ai_model_monitor/sample/ai_model_monitor.png"
```

## トレースヘッダー

trace version 5では、固定ヘッダー直後にモデル名テーブルを保持します。
各エントリーは `model_kind_id -> name` で、ファームウェア初期化時に
`PipelineRuntime::RegisterModelName()` から登録します。raw dumpを読むツールは
このテーブルを使って表示名を解決するため、モデル追加時にPython側のID一覧を
更新する必要はありません。テーブルは最大16モデル、名前は最大27 byteです。

旧version 2〜4のdumpもデコードできますが、旧形式には名前テーブルがありません。
その場合はIDで表示します。以前に生成したJSONにモデル名が含まれる場合は、表示ツール側で
その名前も引き続き読み取れます。

decode済みJSONではモデル名をヘッダーに一度だけ保持し、各レコードのphase、callback、
pipeline stage、laneはIDで表します。表示ツールはIDから必要なラベルを解決します。

## CLI

リポジトリのルートから実行する場合:

```sh
python3 host_app/ai_model_monitor/ai_model_monitor.py \
  decode host_app/ai_model_monitor/sample/ai_model_monitor.bin \
  --output /tmp/ai_model_monitor.json
```

サブコマンドは次の4つです。

### decode

raw dumpをJSONに変換します。`--output`を省略すると標準出力に出力します。

```sh
python3 host_app/ai_model_monitor/ai_model_monitor.py \
  decode dump.bin -o trace.json
```

### analyze

JSONまたはraw dumpからCPU/NPUのステップ経過時間を集計します。
ai_runtimeのCPU値は `Evaluate()` の開始・終了時刻差（`tk_get_otm()`）で、
DWTによるCPU実行サイクルではありません。スケジューリング待ちを含み、
システム時刻の分解能に丸められます。

```sh
python3 host_app/ai_model_monitor/ai_model_monitor.py \
  analyze trace.json --cpu-hz 600000000
```

### visualize

JSONまたはraw dumpからタイムラインのPNGを生成します。matplotlibを使うため、
プロジェクトの `uv` 環境で実行します。

```sh
MPLCONFIGDIR=/tmp/ai-app-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project host_app \
  python host_app/ai_model_monitor/ai_model_monitor.py \
  visualize trace.json -o trace.png --cpu-hz 600000000
```

AIタイムラインはモデルごとにCPU/NPUの2行で表示します。3モデルの場合は6行になり、
各CPU/NPU区間のそばに推論IDを表示して、同じモデルの実行順を追えるようにしています。

### all

raw dumpのdecode、解析、PNG生成を一度に実行します。

```sh
MPLCONFIGDIR=/tmp/ai-app-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project host_app \
  python host_app/ai_model_monitor/ai_model_monitor.py \
  all \
  host_app/ai_model_monitor/sample/ai_model_monitor.bin \
  --json /tmp/ai_model_monitor.json \
  --png /tmp/ai_model_monitor.png \
  --cpu-hz 600000000
```

`--json` と `--png` を省略した場合は、入力ファイルの隣に同じbasenameで
出力します。`--max-inferences`で可視化する推論数を指定でき、デフォルトは
直近12件です。

## Makefileからの実行

ボードからのダンプ取得も含めて実行する場合は、リポジトリルートで次を実行します。

```sh
make -C userspace/ai-app thread-monitor
```

このターゲットはAIモデルのトレースを取得して可視化し、
`THREAD_MONITOR_JSON` と `THREAD_MONITOR_PNG` に指定したファイルを生成します。
CPU task monitorは別のPNGとして、`make -C userspace/ai-app cpu-task-monitor`から
取得・可視化できます。
