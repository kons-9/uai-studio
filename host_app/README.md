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
