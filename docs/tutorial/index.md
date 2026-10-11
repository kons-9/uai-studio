# チュートリアル: mini-ai-appを作る

カメラ映像をLCDに出しながら、Neural-ART NPUで人物を検出して枠を描く最小のAIアプリ[mini-ai-app](https://github.com/kons-9/uai-studio/tree/main/userspace/mini-ai-app)を、空のディレクトリから組み立てます。設定とコードの役割を章ごとに確認し、最後に実機のUARTで起動と推論を確認します。

完成形はリポジトリの`userspace/mini-ai-app`にあり、このアプリのMakefileから`APP_TARGET=mini-ai-app`を選んでビルドします。章のコード片はそこから抜粋しているので、迷ったら完成形を開いてください。全部入りの[ai-app](https://github.com/kons-9/uai-studio/tree/main/userspace/ai-app)との違いは[最終章](10-next-steps.md)にまとめています。

## 作るもの

```text
IMX335 ──CSI──▶ DCMIPP ─ Pipe1 (RGB565 800x480) ──▶ カメラタスク ──▶ LCD（枠を重ねる）
                        └ Pipe2 (RGB888 480x480) ──▶ フレームチャネル ──▶ 推論タスク ─ NPU(person) ─ 後処理 ──▶ 結果チャネル ─┘
```

| タスク | 優先度 | 役割 |
| --- | --- | --- |
| initialize | 5 | ドライバを順に初期化し、ほかの2タスクを起動。以後は1秒ごとにCPUモニタの集計を出す |
| camera | 5 | `Process()`→Pipe2のフレームを推論へ→最新結果を取り出す→Pipe1を合成して表示 |
| inference | 6 | フレームを受け取り、NPUを同期実行し、YOLOXの後処理で枠にして送る |

モデルはST YOLOX nano（person、入力480x480 RGB888、出力3テンソル）1つだけです。

## 前提

- [はじめに](../getting-started.md)の環境（ボード、ARMツールチェーン、STM32CubeN6、STEdgeAI、STM32CubeProgrammer、CubeMX）が揃っていること。`project-tools/host-config/local.mk`を作ってあること。
- experiment-hello-worldが動くこと（[章1](01-minimal-app.md)で確認します）。
- mini-ai-appは`userspace/ai-app/models`のモデル生成物と`userspace/ai-app/third_party/vision_models_pp`の後処理ソースを共有します。ai-appの実行は必須ではありません。モデルは`make -C userspace/mini-ai-app ai-models`で生成でき、後処理ソースとSTEdgeAIランタイムはCMake構成時に存在を確認します（[章7](07-model.md)）。

## 章立て

| 章 | 内容 | 到達状態 |
| --- | --- | --- |
| [1. 最小アプリ](01-minimal-app.md) | `usermain()`だけのアプリ。Makefile・CMake・IOC | UARTに`hello` |
| [2. カーネルコンポーネントを有効にする](02-build-system.md) | `config/build.cmake`、共通ミドルウェアとドライバのリンク、エントリアドレス | アプリ側のビルド設定が揃う |
| [3. メモリ配置](03-memory-layout.md) | `config/*.json`とリンカスクリプト雛形、生成器 | 生成ヘッダとldが出る |
| [4. タスクと共有資源](04-tasks-and-context.md) | `AppContext`、`Task::Start`/`RunForever`、チャネル、`usermain()` | 3タスクが起動する |
| [5. ドライバ初期化](05-driver-init.md) | 初期化タスクと順序、クロック維持 | `mini: driver init done` |
| [6. カメラとLCD](06-camera-lcd.md) | カメラタスク。Pipe1表示、Pipe2転送、結果の保持 | 映像が出る、`pipe1=started pipe2=started` |
| [7. モデル](07-model.md) | モデル生成、Cラッパ、`NpuNetwork`、NORへの書き込み | `ai-load`が通る |
| [8. 推論](08-inference.md) | 推論タスク。所有権、キャッシュ、NPU実行、後処理 | `mini: first inference`、枠が出る |
| [9. 実行と計測](09-run-and-measure.md) | 手順のまとめ、UARTの読み方、cpu-task-monitor | 図が出る |
| [10. 次のステップ](10-next-steps.md) | ai-appとの差分、各機能の読み先 | — |

## 進め方

各章は「目的」「追加するファイル」「なぜそうするか」「確認」の順です。コードは完成形からの抜粋で、エラー処理や宣言の一部を省略しています。抜粋だけをそのまま貼り付けてもビルドできるとは限らないため、必要な定義は完成形と照合してください。

第1章の最小アプリは単独で実行できます。第2章以降の共通ドライバには、第3章のメモリ配置と第7章のモデル生成物が必要です。先に完成形の`setup`を済ませて各章の処理を追う場合と、空のディレクトリで全章の実装を揃えてから動かす場合を区別してください。

完成形のビルドと実行は次のコマンドです。第1章の最小構成ではAIを無効にするため、`ai-load`は不要です。

```sh
make -C userspace/mini-ai-app setup      # 初回。personモデル生成、CubeMX生成、CMake構成
make -C userspace/mini-ai-app build      # ビルドのみ
make -C userspace/mini-ai-app monitor    # 別端末でUARTを開いたままにする
make -C userspace/mini-ai-app ai-load    # 初回とモデル・command blobの変更時。NORへ書き込み
make -C userspace/mini-ai-app ram-run    # ビルドしてRAMへ書き込み、実行
```
