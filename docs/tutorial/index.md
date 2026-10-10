# チュートリアル: mini-ai-appを作る

カメラ映像をLCDに出しながら、Neural-ART NPUで人物を検出して枠を描く最小のAIアプリ[mini-ai-app](https://github.com/kons-9/uai-studio/tree/main/userspace/mini-ai-app)を、空のディレクトリから組み立てます。各章の終わりに、実機のUARTで確認できる状態があります。

完成形はリポジトリの`userspace/mini-ai-app`にあり、常にビルド対象です。章のコード片はそこから抜粋しているので、迷ったら完成形を開いてください。全部入りの[ai-app](https://github.com/kons-9/uai-studio/tree/main/userspace/ai-app)との違いは[最終章](10-next-steps.md)にまとめています。

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
- ai-appを一度動かしていると、モデル生成物と後処理ソースをそのまま使えます（mini-ai-appは`userspace/ai-app/models`と`userspace/ai-app/third_party`を共有します）。

## 章立て

| 章 | 内容 | 到達状態 |
| --- | --- | --- |
| [1. 最小アプリ](01-minimal-app.md) | `usermain()`だけのアプリ。Makefile・CMake・IOC | UARTに`hello` |
| [2. カーネルアプリとして登録する](02-build-system.md) | ミドルウェアとドライバをリンクするための`UAI_KERNEL_APPS`、エントリアドレス | CMake構成が通る |
| [3. メモリ配置](03-memory-layout.md) | `config/*.json`とリンカスクリプト雛形、生成器 | 生成ヘッダとldが出る |
| [4. タスクと共有資源](04-tasks-and-context.md) | `AppContext`、`Task::Start`/`RunForever`、チャネル、`usermain()` | 3タスクが起動する |
| [5. ドライバ初期化](05-driver-init.md) | 初期化タスクと順序、クロック維持 | `mini: driver init done` |
| [6. カメラとLCD](06-camera-lcd.md) | カメラタスク。Pipe1表示、Pipe2転送、結果の保持 | 映像が出る、`pipe1=started pipe2=started` |
| [7. モデル](07-model.md) | モデル生成、Cラッパ、`NpuNetwork`、NORへの書き込み | `ai-load`が通る |
| [8. 推論](08-inference.md) | 推論タスク。所有権、キャッシュ、NPU実行、後処理 | `mini: first inference`、枠が出る |
| [9. 実行と計測](09-run-and-measure.md) | 手順のまとめ、UARTの読み方、cpu-task-monitor | 図が出る |
| [10. 次のステップ](10-next-steps.md) | ai-appとの差分、各機能の読み先 | — |

## 進め方

各章は「目的」「追加するファイル」「なぜそうするか」「確認」の順です。コードは完成形から抜粋しているので、章の途中では未定義のものを参照していることがあります。章の終わりの状態でビルドが通るように、ファイルは章単位でまとめて置いてください。

ビルドと実行のコマンドはどの章でも同じです。

```sh
make -C userspace/mini-ai-app build      # ビルドのみ
make -C userspace/mini-ai-app monitor    # 別端末でUARTを開いたままにする
make -C userspace/mini-ai-app ram-run    # ビルドしてRAMへ書き込み、実行
```
