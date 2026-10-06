# ミドルウェア

`kernel/middleware`はドライバーの上で動く共通処理です。NPU推論をμT-Kernelのタスクとして流すランタイム、バッファの所有権管理、実行の記録、画像縮小と、それらが共有する基盤（foundation）を提供します。CMakeターゲットは`uai::middleware`です。

```text
                 +-----------------------------------------------+
  アプリ         |  AiFuture（モデルごとの前処理、NPU、後処理）   |
                 +-----------------------------------------------+
                      |                     |
  ミドルウェア   ai_runtime            memory_manager        image_resizer
                 （3レーンの実行）     （バッファの所有権）   （縮小の方針とCPU縮小）
                      |                     |
                 ai_model_monitor      memory / pipeline      cpu_task_monitor
                 （ステップの記録）    （配置と型、画像診断） （タスク別CPU使用率）
                 -----------------------------------------------------------
                 foundation（Error、ログ、Task、MessageChannel、OwnedBuffer）
                      |                     |
  ドライバー     NPU                   カメラ、LCD、キャッシュ
```

## モジュール

| モジュール | 内容 |
| --- | --- |
| [foundation](../kernel/common.md) | `common::Error`、`UAI_LOG_*`、タスクの起動とループ、型付きメッセージチャネル、固定長バッファ |
| [ai_runtime](ai_runtime.md) | 推論を前処理CPU、NPU、後処理CPUの3レーンで実行するパイプラインと、推論結果の型 |
| [memory](memory.md) | 生成したメモリ配置へのアクセスと、バッファの型 |
| [memory_manager](memory_manager.md) | キャプチャ、表示、推論バッファの所有権管理と、フレームの型・画像診断（pipeline） |
| [ai_model_monitor](ai_model_monitor.md) | AIパイプラインの実行トレース |
| [cpu_task_monitor](cpu_task_monitor.md) | タスク別CPU使用率とループ時間 |
| [image_resizer](image_resizer.md) | 画像縮小のハードウェア選択とCPU縮小 |

## 共通の考え方

- 戻り値は`common::Error`で、例外は使いません。失敗は`LogStatus()`で記録し、再試行で済むもの（`IsRoutine()`）はエラーとして扱いません（[共通基盤](../kernel/common.md)）。
- 動的なメモリ確保はしません。バッファはビルド時に解決した配置（[memory](memory.md)）から取り、所有権は`memory_manager`で管理します。タスクのスタックとメッセージバッファも`StableAlignedBytes`と`MessageChannel`で静的に持ちます。
- ハードウェアに触れる処理はドライバーに任せ、ミドルウェアは方針とスケジューリングを担当します。このためミドルウェアはホストPCでビルドしてテストできます。
- ai-appでの使い方は[userspace/ai-app/README.md](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/README.md)を参照してください。

## ホストテスト

ホストテストは`kernel/middleware/tests`の単独のCMakeプロジェクトにまとめてあり、実機用のビルドやARMツールチェーンは不要です。CMake、GoogleTest（Ubuntuでは`libgtest-dev`）、C++17コンパイラが必要です。μT-KernelのAPIは`kernel/utkernel/linux`のモックで置き換えます。

各モジュールの`tests/`にあるMakefileから実行します。ビルド先は`build/middleware-tests`です。

```sh
make -C kernel/middleware/ai_runtime/tests test        # foundation、memory_manager、image_resizer、ai_model_monitor、pipelineも同様
make -C kernel/middleware/ai_runtime/tests tsan        # ThreadSanitizer付き（build/middleware-tests-tsan）
cmake -S kernel/middleware/tests -B build/middleware-tests && cmake --build build/middleware-tests && ctest --test-dir build/middleware-tests
```

最後のコマンドは全モジュールと、ai-appのフレーム・結果チャネルのテスト（`userspace/ai-app/tests/frame_channels_test.cpp`）をまとめて実行します。
