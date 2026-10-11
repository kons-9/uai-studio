# セットアップと統合I/F

このページは共通の環境準備と統合GUI/CLIを説明します。各ツールを単独で使う手順は[ai_model_monitor](ai_model_monitor.md)、[cpu_task_monitor](cpu_task_monitor.md)、[auto_static_memory_layout](auto_static_memory_layout.md)、[ui_designer](ui_designer.md)を参照してください。

コマンドはリポジトリルートで実行します。

## 準備

Python 3.10以上と`uv`を用意し、依存パッケージをインストールします。`uv`が未導入の場合は[公式の導入手順](https://docs.astral.sh/uv/getting-started/installation/)を参照してください。

```sh
UV_CACHE_DIR=/tmp/uai-uv-cache uv sync --project host_app --locked
```

モニタの図生成には`matplotlib`を使用します。Python標準ライブラリだけでもエディタやCLIのヘルプを利用できます。

```sh
python3 -m host_app --help
python3 -m host_app ui-designer --help
```

## 統合GUIを起動する

```sh
uv run --project host_app python -m host_app
```

既定ではブラウザが開き、`http://127.0.0.1:8768/`で操作できます。ブラウザを自動起動しない場合は次のように指定します。

```sh
uv run --project host_app python -m host_app gui --no-browser
```

ポートが使用中なら`gui --port 8769`、空きポートを自動選択するなら`gui --port 0`を指定します。VS Codeのリモート環境では、ターミナルに表示されたポートを転送してブラウザで開きます。終了するには、起動したターミナルで`Ctrl+C`を押します。

サーバーはlocalhost専用です。ファイルの読み書きやCLIの実行を行うため、ネットワークへ公開しないでください。

### 編集対象を変更する

既定ではai-appの設定を読み込みます。別の設定を扱うときはGUIの起動引数で変更できます。

| オプション | 指定するもの |
| --- | --- |
| `--layout` | UIレイアウトJSON |
| `--ui-feature-catalog` | UIで扱う機能ID・操作のカタログJSON |
| `--board` | 物理メモリ領域・予約設定のJSON |
| `--application` | バッファ寸法・個数などのアプリ設定JSON |
| `--models-dir` | 生成済みモデルのディレクトリ |
| `--model-config` | モデルの配置・順序を指定するJSON |
| `--linker-base` | 生成のベースにするリンカスクリプト |
| `--features` | 機能制約エディタを追加するための設定JSON |

標準のai-appレイアウトには`userspace/ai-app/config/ui_feature_catalog.json`の機能IDが自動適用されます。独自レイアウトには必要に応じて`--ui-feature-catalog`を指定します。

機能制約エディタを使う場合の起動例です。

```sh
uv run --project host_app python -m host_app gui \
  --features userspace/experiment-ui-control/config/features.json
```

### タブと保存先

| タブ | 個別の操作手順 | 保存時の動作 |
| --- | --- | --- |
| UI Designer | [ui_designer](ui_designer.md) | Saveは起動時に指定したレイアウトJSONへ書き戻す |
| Memory Layout | [auto_static_memory_layout](auto_static_memory_layout.md) | ブラウザ内のドラフトを検証し、成果物をダウンロード。入力ファイルは上書きしない |
| AI Monitor | [ai_model_monitor](ai_model_monitor.md) | 入力を処理し、タイムラインと集計結果を表示・ダウンロード |
| CPU Monitor | [cpu_task_monitor](cpu_task_monitor.md) | 入力を処理し、図とJSON/CSVを表示・ダウンロード |

モニタの入力はサーバー上のパスか、ブラウザで選択したファイルです。ファイル選択は10 MiBまでで、サンプル入力も利用できます。

モニタフォームの成果物はセッション専用の一時領域に保存され、GUI終了時に削除されます。必要なものは終了前にダウンロードしてください。タブを切り替えても編集中の状態を保持します。

### GUIからCLIを実行する

各タブのCLI欄では、個別ツールの非対話コマンドを実行できます。実行コマンド、標準出力・標準エラー、終了コードを確認できます。

- `{input}`は選択済みの入力ファイル、`{output}`はセッションの出力領域に置き換わります。
- 明示的な出力パスを指定した場合、そのパスへ書き込みます。
- 引数はシェルを介さず渡されるため、パイプやリダイレクトなどのシェル構文は使いません。
- 実行時間は最大120秒、同時実行は2件までです。常駐サーバーはターミナルから起動します。

## 統合CLIの呼び出し方

統合CLIはツール名以降の引数を個別ツールへそのまま渡します。たとえば`python3 -m host_app ui-designer validate ...`は、単独CLIの`python3 host_app/ui_designer validate ...`と同じ検証を行います。

詳しいサブコマンドや出力オプションは個別ページと各ツールの`--help`で確認できます。

```sh
uv run --project host_app python -m host_app ui-designer --help
uv run --project host_app python -m host_app memory-layout --help
uv run --project host_app python -m host_app ai-model-monitor --help
uv run --project host_app python -m host_app cpu-task-monitor --help
python3 -m host_app model-loader --help
python3 -m host_app feature-constraints --help
```

`model-loader`と`feature-constraints`はuserspace側のCLIです。詳しい操作は[概要の関連ツール一覧](index.md#userspaceの関連ツール)から参照してください。

## 困ったとき

| 症状 | 確認すること |
| --- | --- |
| モニタの図を生成できない | `uv sync --project host_app --locked`で依存を用意し、`uv run --project host_app`経由で実行しているか |
| GUIを開けない | 表示されたURL・ポートを確認。リモート環境ではポート転送を設定し、競合時は`--port 0`で起動 |
| Memory Layoutが解決できない | 生成済みモデルの有無、設定パス、容量超過・領域重複のエラーを確認 |
| GUI終了後に成果物が見つからない | フォームの成果物は一時保存。終了前にダウンロードするか、CLIで明示的な出力先を指定 |
| 機能制約エディタが表示されない | GUI起動時に`--features`を指定しているか |