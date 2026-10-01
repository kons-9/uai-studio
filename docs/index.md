# μAI-Studio 開発ガイド

μAI-Studioは、μT-Kernel 3.0でNeural-ART NPUを使うAIアプリを開発するための環境です。対象ボードはSTM32N6570-DKで、モデル変換からメモリ配置、ビルド、書き込み、実行時の解析までをコマンドラインで扱います。

この開発ガイドは、μAI-Studioのカーネル、ドライバー、ミドルウェアを使ってアプリを書くための資料です。全体像は[紹介スライド](https://kons-9.github.io/uai-studio/introduction.html)、ソースは[GitHub](https://github.com/kons-9/uai-studio)にあります。

## 読む順番

| 目的 | ページ |
| --- | --- |
| ボードとツールを用意して、ai-appを動かす | [はじめに](getting-started.md) |
| ビルドの仕組みと起動の流れを知る、アプリを追加する | [カーネル](kernel/index.md) |
| カメラ、LCD、NPUなどの周辺機能を使う | [ドライバー](driver.md) |
| 推論パイプライン、バッファ管理、モニターを使う | [ミドルウェア](middleware/index.md) |

## 構成

```text
userspace/<app>        アプリ（usermain、タスク、モデル）
kernel/middleware      ai_runtime、memory_manager、モニター、image_resizer
kernel/driver          カメラ、LCD、NPU、PSRAM、NOR、RIF、キャッシュ
kernel/common          エラー型、ログ
kernel/utkernel        μT-Kernel 3.0 BSP2
kernel/pre_kernel      CubeMX生成コードとRAM起動
build-system           CMake・Makeの共通定義、CubeMX生成とUARTのスクリプト、ホスト設定
host_app               PCで動かすメモリ配置の生成とモニターの解析ツール
```

上の層は下の層だけを使います。`kernel/driver`と`kernel/middleware`は現在ai-appのビルドでだけ有効です。

## 開発の流れ

```sh
make -C userspace/ai-app setup             # ツールの確認、モデル生成、CubeMX生成、CMake構成
make -C userspace/ai-app monitor           # 別端末でUARTを開く
make -C userspace/ai-app ai-load           # 初回とモデル変更時。モデルを外部NORへ書く
make -C userspace/ai-app ram-run           # ビルドしてRAMで実行
make -C userspace/ai-app thread-monitor    # AI model monitor
make -C userspace/ai-app cpu-task-monitor  # CPU task monitor
```

UARTに`camera: pipe1=started pipe2=started`が出れば、カメラの2系統が動き出しています。

## 名前空間

| 名前空間 | 場所 |
| --- | --- |
| `uai::ai::common` | `kernel/common` |
| `uai::ai::driver`、`uai::ai::config`、`uai::ai::cache`、`uai::ai::camera`、`uai::ai::lcd`、`uai::ai::npu`、`uai::ai::nor`、`uai::ai::psram`、`uai::ai::rif` | `kernel/driver` |
| `uai::ai::ai_runtime`、`uai::ai::inference`、`uai::ai::memory_manager`、`uai::ai::memory_allocator`、`uai::ai::static_memory_layout`、`uai::ai::pipeline`、`uai::ai::image_resizer`、`uai::ai::middleware::*` | `kernel/middleware` |

インクルードは`kernel/`からの相対パスで書きます（例: `#include "driver/npu_driver/npu_driver.hpp"`、`#include "middleware/ai_runtime/pipeline_dispatcher.hpp"`）。

## 関連資料

| 資料 | 内容 |
| --- | --- |
| [README](https://github.com/kons-9/uai-studio#readme) | リポジトリの概要とai-appの実行手順 |
| [userspace/ai-app/README.md](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/README.md) | 評価用サンプルの内部構成、モデル、設定 |
| [host_app/README.md](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md) | メモリ配置の生成ツールとモニターの解析ツール |
| [THIRD_PARTY_NOTICES.md](https://github.com/kons-9/uai-studio/blob/main/THIRD_PARTY_NOTICES.md) | 利用している既存ソフトウェアとμT-Kernelへの変更 |
