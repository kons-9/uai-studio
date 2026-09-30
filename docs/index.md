# uai-studio 開発ガイド

uai-studioのカーネル、ドライバー、ミドルウェアを使ってアプリを書くための資料です。セットアップとai-appの実行手順はリポジトリの[README](https://github.com/kons-9/uai-studio#readme)を参照してください。全体像は[紹介スライド](https://kons-9.github.io/uai-studio/introduction.html)にまとめています。

## 構成

```text
userspace/<app>        アプリ（usermain、タスク、モデル）
kernel/middleware      ai_runtime、memory_manager、モニター、image_resizer
kernel/driver          カメラ、LCD、NPU、PSRAM、NOR、RIF、キャッシュ
kernel/common          エラー型、ログ
kernel/utkernel        µT-Kernel 3.0 BSP2
kernel/pre_kernel      CubeMX生成コードとRAM起動
```

上の層は下の層だけを使います。`kernel/driver`と`kernel/middleware`は現在ai-appのビルドでだけ有効です。

## 文書

| 文書 | 内容 |
| --- | --- |
| [kernel.md](kernel.md) | ビルド構成、起動の流れ、µT-Kernel、エラー型とログ、アプリの追加方法 |
| [driver.md](driver.md) | ドライバーの初期化順序、所有権、各ドライバーの使い方 |
| [middleware.md](middleware.md) | ai_runtime、メモリ管理、モニター、image_resizer |

## 名前空間

| 名前空間 | 場所 |
| --- | --- |
| `uai::ai::common` | `kernel/common` |
| `uai::ai::driver`、`uai::ai::config`、`uai::ai::cache`、`uai::ai::camera`、`uai::ai::lcd`、`uai::ai::npu`、`uai::ai::nor`、`uai::ai::psram`、`uai::ai::rif` | `kernel/driver` |
| `uai::ai::ai_runtime`、`uai::ai::inference`、`uai::ai::memory_manager`、`uai::ai::memory_allocator`、`uai::ai::static_memory_layout`、`uai::ai::pipeline`、`uai::ai::image_resizer`、`uai::ai::middleware::*` | `kernel/middleware` |

インクルードは`kernel/`からの相対パスで書きます（例: `#include "driver/npu_driver/npu_driver.hpp"`、`#include "middleware/ai_runtime/pipeline_dispatcher.hpp"`）。
