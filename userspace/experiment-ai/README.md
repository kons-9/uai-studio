# experiment-ai

NPUドライバーや推論ランタイムの実験用ディレクトリです。person、segmentation、faceの3モデルを1つのNPUタスクで順番に推論します。ドライバーやメモリ管理を`kernel/`ではなくこのディレクトリの`src/`内に持ち、推論は`src/npu_runtime`（`NpuRuntime`、`Scheduler`、`InferenceDispatcher`）で実行します。

## ビルドと実行

ツールとホスト設定はルートの[README](../../README.md)と共通です。

```sh
make -C userspace/experiment-ai setup
make -C userspace/experiment-ai monitor   # 別端末で先に起動
make -C userspace/experiment-ai ai-load
make -C userspace/experiment-ai ram-run
```

ビルド先は`build-experiment-ai/`です。モデルの生成方法と配置先は[このexperimentのmodels/README.md](models/README.md)に記載しています。

## ai-appとの違い

| 項目 | experiment-ai | ai-app |
| --- | --- | --- |
| ドライバー、メモリ管理 | `src/`内に独自実装 | `kernel/driver`、`kernel/middleware` |
| 推論の実行 | `NpuRuntime`が1タスクでモデルをラウンドロビン | `ai_runtime`の3レーン |
| メモリ配置 | リンカスクリプトを手で管理 | JSONから自動生成 |
| 実行トレース | APP RAMの128 KiBリング | PSRAMのリング |
| CPU task monitor | なし | あり |

## ソース構成

| パス | 内容 |
| --- | --- |
| `src/task/` | 初期化、カメラ表示、推論、後処理のタスク |
| `src/npu_runtime/` | `NpuRuntime`、`Scheduler`、`InferenceDispatcher`、ThreadMonitor |
| `src/models/` | モデルごとの生成コードのラッパーとデコーダー |
| `src/driver/` | カメラ、LCD、NPU、PSRAM、NOR、RIF、キャッシュのドライバー |
| `src/memory_allocator/` | 推論スロットとバッファの管理 |
| `src/image_resizer/` | 画像縮小（[README](src/image_resizer/README.md)） |
| `tools/` | ThreadMonitorのデコードと可視化 |

実行トレースは[src/npu_runtime/thread_monitor/README.md](src/npu_runtime/thread_monitor/README.md)を参照してください。
