# ミドルウェア

`kernel/middleware`はドライバーの上で動く共通処理です。NPU推論をμT-Kernelのタスクとして流すランタイム、バッファの所有権管理、実行の記録、画像縮小を提供します。CMakeターゲットは`uai::middleware`です。

```text
                 +-----------------------------------------------+
  アプリ         |  AiFuture（モデルごとの前処理、NPU、後処理）   |
                 +-----------------------------------------------+
                      |                     |
  ミドルウェア   ai_runtime            memory_manager        image_resizer
                 （3レーンの実行）     （バッファの所有権）   （縮小の方針とCPU縮小）
                      |                     |
                 ai_model_monitor      memory / pipeline      cpu_task_monitor
                 （ステップの記録）    （配置と型）           （タスク別CPU使用率）
                      |                     |
  ドライバー     NPU                   カメラ、LCD、キャッシュ
```

## モジュール

| モジュール | 内容 |
| --- | --- |
| [ai_runtime](ai_runtime.md) | 推論を前処理CPU、NPU、後処理CPUの3レーンで実行するパイプライン |
| [memory](memory.md) | 生成したメモリ配置へのアクセスと、バッファの型 |
| [memory_manager](memory_manager.md) | キャプチャ、表示、推論バッファの所有権管理と、フレームの型（pipeline） |
| [ai_model_monitor](ai_model_monitor.md) | AIパイプラインの実行トレース |
| [cpu_task_monitor](cpu_task_monitor.md) | タスク別CPU使用率とループ時間 |
| [image_resizer](image_resizer.md) | 画像縮小のハードウェア選択とCPU縮小 |

## 共通の考え方

- 戻り値は`common::Error`で、例外は使いません（[エラー型とログ](../kernel/common.md)）。
- 動的なメモリ確保はしません。バッファはビルド時に解決した配置（[memory](memory.md)）から取り、所有権は`memory_manager`で管理します。
- ハードウェアに触れる処理はドライバーに任せ、ミドルウェアは方針とスケジューリングを担当します。このため`ai_runtime`のコアはホストPCでもビルドしてテストできます。
- ai-appでの使い方は[userspace/ai-app/README.md](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/README.md)を参照してください。
