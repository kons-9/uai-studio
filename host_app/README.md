# host_app

PCで動かすai-app向けツールです。Python 3.10以上と`uv`を使います。依存パッケージはリポジトリルートで次のように用意します。

```sh
UV_CACHE_DIR=/tmp/uai-uv-cache uv sync --project host_app --locked
```

| ディレクトリ | 内容 |
| --- | --- |
| [ai_model_monitor/](ai_model_monitor/README.md) | AIパイプラインの実行トレース（ThreadMonitorリング）のデコード、集計、タイムライン図 |
| [cpu_task_monitor/](cpu_task_monitor/README.md) | タスク別CPU使用率とループ時間の集計、図 |
| `auto_static_memory_layout/` | ai-appのメモリ配置を解決し、リンカスクリプトとC++ヘッダを生成 |
| [ui_designer/](ui_designer/README.md) | 画面のボタン配置をブラウザで編集し、プレビューPNGと実機用C++ヘッダを生成。追加パッケージ不要 |
| [feature_constraints](../userspace/experiment-ui-control/tool/README.md) | UI実験の機能制約、遷移検証、シミュレーション、C++生成（統合GUIからも呼び出せます） |

## 統合GUI

リポジトリルートから一つのアプリを起動します。引数なしではGUIが開きます。

```sh
uv run --project host_app python -m host_app
# 自動ブラウザ起動を止める場合
uv run --project host_app python -m host_app gui --no-browser
```

既定URLは `http://127.0.0.1:8768/` です。使用中の場合は`gui --port 8769`、空きポートを選ぶ場合は`gui --port 0`を指定します。VS Codeのリモート環境では表示されたポートを転送します。標準ライブラリのみの`python3 -m host_app`でもエディタとCLIは起動できますが、モニタの図生成には上記の依存が必要です。

- UI Designer / Memory Layout: 既存の編集画面を同じアプリ内で操作できます。タブを切り替えても編集中の状態を保持します。制約ツールは`--features PATH`で明示指定した場合だけタブに加わります。
- AI Monitor: raw dump / JSONからdecode、analyze、visualize、allを実行し、タイムラインと出力を表示します。
- CPU Monitor: raw dump / UARTログからPNG/SVG、JSON、CSVを生成します。
- モニタの入力はサーバー上のパスまたはブラウザからのファイル選択（10 MiB以下）。サンプル入力も利用できます。
- 全タブのCLI欄から個別ツールの任意の非対話コマンドを実行できます。実行コマンド、標準出力、標準エラー、終了コードを表示します。引数はシェルを介さず渡します。

GUIの`--layout`、`--board`、`--application`、`--models-dir`、`--model-config`、`--linker-base`で編集対象を指定できます。既定値はai-appの入力ファイルですが、任意のホスト側ファイルに置き換えられます。標準のai-appレイアウトには`userspace/ai-app/config/ui_feature_catalog.json`の機能IDを自動適用します。独自レイアウトには必要に応じて`--ui-feature-catalog PATH`を指定します。制約エディタを使用する場合だけ`--features userspace/experiment-ui-control/config/features.json`を渡してください。このオプションを省略した統一GUIはuserspaceのツール実装を読み込まず、userspaceのCLIもGUIから実行できません。UIのSaveは指定したレイアウトへ書き戻します。メモリ配置と制約エディタはブラウザから成果物をダウンロードし、入力ファイルを上書きしません。

モニタフォームの成果物はセッション専用の一時領域へ生成し、ダウンロードできます。GUI終了時に削除されるため、必要な成果物は終了前にダウンロードします。CLI欄から明示的な出力先を指定した場合は、そのパスへ書き込みます。CLI欄の`{output}`はセッションの出力領域、`{input}`は選択済み入力ファイルに置換されます。CLI実行は最大120秒、同時に2件までです。常駐サーバーの起動は個別CLIで行います。

### CLIを必須とする構造

各ホストツールは必ずGUIから独立したCLIを提供します。GUIは編集・実行・結果表示のラッパーであり、ドメイン処理を別実装しません。既存エディタはCLIと同じ解析・生成APIを利用し、モニタとCLI欄は`sys.executable -m host_app <ツール名> ...`を独立プロセスで呼び出します。GUIの起動や追加依存なしで各CLIのヘルプを利用できることを回帰テストで確認します。

サーバーはlocalhostにのみ公開し、Hostを検証します。API操作にはセッショントークンが必要で、エディタの埋め込みは同一オリジンに限ります。ネットワークへ公開しないでください。

```sh
python3 -m unittest host_app.test_gui -v
```

## 個別CLI

ツール固有の引数はそのまま渡せます。従来の個別起動方法も残しています。

```sh
uv run --project host_app python -m host_app --help
uv run --project host_app python -m host_app ui-designer serve \
  --layout userspace/ai-app/config/ui_layout.json
uv run --project host_app python -m host_app memory-layout gui
uv run --project host_app python -m host_app ai-model-monitor all \
  host_app/ai_model_monitor/sample/ai_model_monitor.bin
uv run --project host_app python -m host_app cpu-task-monitor \
  host_app/cpu_task_monitor/sample/uart.log -o /tmp/cpu-task-monitor.png
python3 -m host_app feature-constraints check \
  userspace/experiment-ui-control/tool/feature_constraints/example.json
```

利用できるツールは`ui-designer`、`memory-layout`、`ai-model-monitor`、`cpu-task-monitor`、`feature-constraints`です。詳細なオプションは`python3 -m host_app <ツール名> --help`で確認できます。

実機から取得して図を作るまでは、Makeターゲットでまとめて実行できます。

```sh
make -C userspace/ai-app thread-monitor
make -C userspace/ai-app cpu-task-monitor
```

出力は既定で`build-ai-app-person/`に置かれます。

## auto_static_memory_layout

ai-appのビルド時にCMakeから自動で実行されます。手動で実行する必要は通常ありません。

入力は3つのJSONと生成済みモデルです。

| 入力 | 内容 |
| --- | --- |
| `config/board_memory.json` | 物理メモリ領域、モデル重みのアドレス、command blobのセクション |
| `config/application_memory.json` | キャプチャ、表示、推論などのバッファ数とサイズ、固定予約領域 |
| `config/model_layout.json`と`models/` | モデルの順序、`stai_network.h`のテンソル情報、command blob |

解決結果のJSONを中間表現とし、そこからYAML、`key.hpp`、`raw.hpp`、`memory_config.hpp`、リンカスクリプトを生成します。出力先は常に引数で指定し、ソースツリーは変更しません。

```sh
python3 host_app/auto_static_memory_layout all \
  --board userspace/ai-app/config/board_memory.json \
  --application userspace/ai-app/config/application_memory.json \
  --models-dir userspace/ai-app/models \
  --model-config userspace/ai-app/config/model_layout.json \
  --output-dir /tmp/ai-app-memory-layout \
  --linker-base userspace/ai-app/stm32n6570-dk-npu-ram.ld
```

サブコマンドは`resolve`、`generate_yml`、`generate_cpp`、`all`、`gui`です。詳細は`--help`で確認できます。生成ヘッダの使い方は[docs/middleware/memory.md](../docs/middleware/memory.md)を参照してください。

### GUI

リポジトリルートから起動します。Python標準ライブラリだけで動作します。

```sh
python3 host_app/auto_static_memory_layout gui --open-browser
```

既定URLは `http://127.0.0.1:8766/` です。使用中の場合は `--port 8767`、空きポートを自動選択する場合は `--port 0` を指定します。VS Codeのリモート環境では、表示されたポートを転送して開きます。localhost以外には公開しません。

- Runtime: バッファ寸法、ピクセル当たりのバイト数、個数、アラインメントを編集。
- Reservations / Regions: 予約と物理メモリ領域を編集。領域マップから予約を選ぶと詳細行を選択。
- JSON: board / application / model_configの全項目を編集、JSONファイルをインポート・エクスポート。予約の追加・削除もJSONで行えます。
- Validate: CLIと同じ処理で解決し、領域重複・容量超過などを検証。編集後も自動検証します。
- Export ZIP: 編集済みの3つの入力JSON、解決済みJSON/YAML、C++ヘッダ3つ、リンカスクリプトをダウンロード。

入力は既定でai-appの設定を読みます。`--board`、`--application`、`--models-dir`、`--model-config`、`--linker-base`で変更できます。モデル生成物はリポジトリ内に配置してください。編集はブラウザ内のドラフトとして保持され、ソースファイルやビルド出力は書き換えません。Reloadで入力ファイルを再読込してドラフトを破棄できます。別のポートやブラウザにはドラフトは引き継がれません。

解決とZIP出力には、CLIと同じ生成済みモデル（`stai_network.h`、`network.c`、`network_ecblobs.h`、重み）が必要です。不足時はエラーを表示し、使用量を未計算として表示します。設定の編集とJSON単体の出力は可能です。モデルの準備は[モデルのREADME](../userspace/ai-app/models/README.md)を参照してください。

マップは静的予約の容量を表示します。コード、モデル重み、command blobは使用量に含みません。予約のオフセットは予約領域内の相対値であり、最終アドレスはリンカが決定します。セクション名を変更する場合は、対応するセクションを持つリンカベースも必要です。

回帰テスト（モデル読込境界は固定データ、配置計算・出力・HTTPは実処理）:

```sh
python3 -m unittest host_app.auto_static_memory_layout.test_gui -v
```
