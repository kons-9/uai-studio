# ai_model_monitor

## 概要

`host_app/ai_model_monitor`は、実機で記録したAIパイプラインの実行トレースを解析するツールです。raw dumpのデコード、モデル別のCPU/NPUステップ時間の集計、タイムラインPNGの生成を行います。保存済みのデータがあれば実機なしで利用できます。

実機側の記録器は[kernel/middlewareのai_model_monitor](../middleware/ai_model_monitor.md)です。raw形式の名前はThreadMonitorで、ai-appではPSRAMの`0x91C40000`から32 KiBのリングに記録します。

### 入力と出力

| 処理 | 入力 | 出力 |
| --- | --- | --- |
| `decode` | ThreadMonitorのraw dump | デコード済みJSON。出力先省略時は標準出力 |
| `analyze` | raw dumpまたはデコード済みJSON | モデル別の時間と遅いepoch群の集計を標準出力に表示 |
| `visualize` | raw dumpまたはデコード済みJSON | タイムラインPNG |
| `all` | raw dumpまたはデコード済みJSON | JSON、集計の標準出力、タイムラインPNG |

version 5のdumpにはモデル名テーブルが含まれます。最大16モデル、名前は27 byteまでで、実機側の`PipelineRuntime::RegisterModelName()`で登録した名前を表示します。version 2から4も読み込めますが、モデル名はIDで表示します。

## 使い方

コマンドはリポジトリルートで実行します。`decode`と`analyze`はPython標準ライブラリだけで動きます。`visualize`と`all`は`matplotlib`が必要なので、[共通のセットアップ](usage.md#準備)を済ませてください。

### 1. raw dumpをJSONへ変換する

付属サンプルから、解析内容を確認しやすい整形済みJSONを作ります。

```sh
python3 host_app/ai_model_monitor/ai_model_monitor.py decode \
  host_app/ai_model_monitor/sample/ai_model_monitor.bin \
  -o /tmp/ai-trace.json --pretty
```

`-o`を省略するとJSONを標準出力へ出します。`decode`の入力はraw dumpで、JSONを再入力する場合は他のサブコマンドを使います。

### 2. ステップ時間を集計する

```sh
python3 host_app/ai_model_monitor/ai_model_monitor.py analyze \
  /tmp/ai-trace.json --cpu-hz 600000000 --top 12
```

raw dumpを直接指定しても集計できます。サイクル値を時間へ換算するときは、実機のCPUクロックを`--cpu-hz`で指定します。`analyze`ではこの引数は省略可能です。

### 3. タイムラインを作る

```sh
uv run --project host_app python host_app/ai_model_monitor/ai_model_monitor.py \
  visualize /tmp/ai-trace.json -o /tmp/ai-trace.png \
  --cpu-hz 600000000 --max-inferences 12
```

既定では直近12件の推論を描きます。`--max-inferences -1`で全件、`--show`で図のウィンドウ表示も行います。リモート環境ではPNGを開いて確認できます。`-o`の既定値は作業ディレクトリの`ai_model_monitor.png`です。

### まとめて実行する

```sh
uv run --project host_app python host_app/ai_model_monitor/ai_model_monitor.py \
  all host_app/ai_model_monitor/sample/ai_model_monitor.bin \
  --json /tmp/ai-trace-all.json --png /tmp/ai-trace-all.png \
  --cpu-hz 600000000 --pretty
```

`--json`と`--png`を省略すると入力ファイルの隣に同名のJSONとPNGを作ります。既存のサンプルや入力JSONを上書きしないよう、出力先を明示することを推奨します。

### 実機のデータを取得する

実行中のai-appとST-LINK接続がある場合は、次のターゲットで取得から可視化まで行えます。

```sh
make -C userspace/ai-app thread-monitor
```

Hot Plug接続でCPUを一時停止してリングを読み出し、CPUを再開してからJSONとPNGを生成します。dumpのみが必要なら`thread-monitor-dump`を使います。リングは揮発性なので、リセット前に取得してください。

出力先は`THREAD_MONITOR_DUMP`、`THREAD_MONITOR_JSON`、`THREAD_MONITOR_PNG`で指定できます。

```sh
make -C userspace/ai-app thread-monitor \
  THREAD_MONITOR_DUMP=/tmp/ai-live.bin \
  THREAD_MONITOR_JSON=/tmp/ai-live.json \
  THREAD_MONITOR_PNG=/tmp/ai-live.png
```

## 結果の読み方

タイムラインはモデルごとにCPUとNPUの2行で描き、各区間に推論IDを付けます。異なるモデルや推論の区間が重なるかを見て、パイプラインの並行実行を確認します。

CPU区間は`Evaluate()`の開始から終了までの時間です。スケジューリング待ちを含むため、CPUの実占有時間やCPU使用率とは一致しません。タスク別のCPU使用率は[cpu_task_monitor](cpu_task_monitor.md)で確認します。

## 主なオプションと注意点

| オプション | 対象 | 内容 |
| --- | --- | --- |
| `-o` / `--output` | `decode`、`visualize` | JSONまたはPNGの出力先 |
| `--json`、`--png` | `all` | JSONとPNGの出力先 |
| `--cpu-hz` | `analyze`、`visualize`、`all` | サイクル換算に使うクロック。`visualize`と`all`の既定は600000000 Hz |
| `--top` | `analyze`、`all` | 表示する遅いepoch群の数。既定12 |
| `--max-inferences` | `visualize`、`all` | 描画する直近の推論数。既定12、`-1`で全件 |
| `--dpi` | `visualize`、`all` | 図の解像度。既定140 |
| `--pretty` | `decode`、`all` | JSONを整形して出力 |

- クロックが異なるボードでは`--cpu-hz`を変更してください。
- `trace ring dropped ... records`は、リングに保持されなかった記録があることを示します。取得結果を実行開始からの全履歴として扱わないでください。
- `matplotlib`の設定ディレクトリに書けない環境では`MPLCONFIGDIR=/tmp/uai-matplotlib`を付けて実行します。
- ホストのlibstdc++と`matplotlib`が衝突する環境では、ホストに合ったlibstdc++の事前読込が必要になることがあります。x86_64 Linuxでの例は[ツールのREADME](https://github.com/kons-9/uai-studio/blob/main/host_app/ai_model_monitor/README.md)を参照してください。

詳細なヘルプは`python3 host_app/ai_model_monitor/ai_model_monitor.py <サブコマンド> --help`で確認できます。