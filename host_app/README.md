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

サブコマンドは`resolve`、`generate_yml`、`generate_cpp`、`all`です。詳細は`--help`で確認できます。生成ヘッダの使い方は[docs/middleware/memory.md](../docs/middleware/memory.md)を参照してください。
