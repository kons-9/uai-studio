# sample-ai2 AI model monitor

このディレクトリには、sample-ai2のAIモデル実行状況を確認するツールを
まとめています。エントリーポイントは `ai_model_monitor.py` です。
内部のrawデータ形式はThreadMonitorですが、用途に合わせてツール名を
`ai_model_monitor` としています。

サンプルデータは [`sample/`](sample/) にあります。

- `sample/ai_model_monitor.bin`: raw dump
- `sample/ai_model_monitor.json`: decode済みJSON

## CLI

リポジトリのルートから実行する場合:

```sh
python3 userspace/sample-ai2/tools/ai_model_monitor/ai_model_monitor.py \
  decode userspace/sample-ai2/tools/ai_model_monitor/sample/ai_model_monitor.bin \
  --output /tmp/ai_model_monitor.json
```

サブコマンドは次の4つです。

### decode

raw dumpをJSONに変換します。`--output`を省略すると標準出力に出力します。

```sh
python3 userspace/sample-ai2/tools/ai_model_monitor/ai_model_monitor.py \
  decode dump.bin -o trace.json
```

### analyze

JSONまたはraw dumpからCPU/NPUの実行時間を集計します。

```sh
python3 userspace/sample-ai2/tools/ai_model_monitor/ai_model_monitor.py \
  analyze trace.json --cpu-hz 600000000
```

### visualize

JSONまたはraw dumpからタイムラインのPNGを生成します。matplotlibを使うため、
プロジェクトの `uv` 環境で実行します。

```sh
MPLCONFIGDIR=/tmp/sample-ai2-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project userspace/sample-ai2/tools \
  python userspace/sample-ai2/tools/ai_model_monitor/ai_model_monitor.py \
  visualize trace.json -o trace.png --cpu-hz 600000000
```

### all

raw dumpのdecode、解析、PNG生成を一度に実行します。

```sh
MPLCONFIGDIR=/tmp/sample-ai2-matplotlib \
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
uv run --project userspace/sample-ai2/tools \
  python userspace/sample-ai2/tools/ai_model_monitor/ai_model_monitor.py \
  all \
  userspace/sample-ai2/tools/ai_model_monitor/sample/ai_model_monitor.bin \
  --json /tmp/ai_model_monitor.json \
  --png /tmp/ai_model_monitor.png \
  --cpu-hz 600000000
```

`--json` と `--png` を省略した場合は、入力ファイルの隣に同じbasenameで
出力します。`--max-inferences`で可視化する推論数を指定でき、デフォルトは
直近12件（person/segmentation/faceの約4サイクル）です。

## Makefileからの実行

ボードからのダンプ取得も含めて実行する場合は、リポジトリルートで次を実行します。

```sh
make -C userspace/sample-ai2 thread-monitor
```

このターゲットは、ダンプ取得後にこのディレクトリの `all` コマンドを呼び出し、
`THREAD_MONITOR_JSON` と `THREAD_MONITOR_PNG` に指定したファイルを生成します。
