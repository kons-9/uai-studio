# 9. 実行と計測

## 手順のまとめ

初回は次の順です。2回目以降は、UARTを開いた状態で`ram-run`を実行します。モデルの重みやcommand blobを変えた場合は、先に`ai-load`をやり直します。

UARTモニタは1つだけ起動します。`ps aux | grep '[m]ake'`で既存のモニタを確認し、すでに開いていればそれを使い、余分なモニタだけ終了してから書き込んでください。

```sh
make -C userspace/mini-ai-app setup      # ai-models → CubeMX生成 → CMake構成
make -C userspace/mini-ai-app monitor    # 別端末。UARTを開いたままにする
make -C userspace/mini-ai-app ai-load    # ビルドして重みとblobをNORへ
make -C userspace/mini-ai-app ram-run    # ビルドしてRAMへ書き、実行
```

`setup`は`ENABLE_AI=1`なので、`ai-models`（`AI_MODEL_NAMES := person`のモデル生成）を先に行います。CubeMXの生成は`CUBEMX_EXECUTABLE`（`local.mk`）で動かします。STEdgeAIランタイムと後処理ソースの依存関係はCMake構成時に確認しますが、後処理ソースは自動取得しません（[第7章](07-model.md)）。

`camera: pipe1=started pipe2=started`をUARTで確認してから、モデル初期化と最初の推論のログを確認します。`nor=1`だけでは、モデルが正しく書き込まれたことは確認できません。

## UARTの読み方

起動から定常状態までに出る主な行です。

| 行 | 出所 | 意味 |
| --- | --- | --- |
| `mini: driver init begin` / `done nor=1` | initialize | ドライバ初期化の開始と完了。`nor=0`ならNORが読めず推論は無効 |
| `camera: pipe1=started pipe2=started` | camera driver | Pipe1/Pipe2のDMAが始まった |
| `mini: model ready input=691200 bytes outputs=3` | inference | モデルの初期化と検証に成功 |
| `mini: first inference seq=N boxes=M` | inference | 最初の推論が完了。`seq`はPipe2のフレーム番号 |
| `mini: inference/s=A pipe2=B drops=C csi_errors=D` | inference、約1秒ごと | 集計期間内の推論完了件数、Pipe2の累計フレーム数、ドライバの捨て先へ流した累計、CSIエラー累計 |
| `cpu: ...` | cpu_task_monitor、1秒ごと | タスク別CPU使用率（`ENABLE_CPU_TASK_MONITOR=1`のとき） |
| `camera: no frame for N ms; starting recovery #k` | camera driver | フレームが止まり再初期化した |
| `error: component=<name> operation=... code=...` | 各所 | `common::Error::LogStatus()`。`code`は[エラーコード](../kernel/common.md) |

推論がカメラより遅い場合は、チャネルで古いフレームが置き換えられたり、バッファ不足で`drops`が増えたりします。`drops`はチャネルの置き換え件数ではなく、増加だけで異常とは判断しません。`pipe2`と推論完了件数が進んでいるかを併せて見ます。`csi_errors`が増え続ける場合はケーブルやボードの問題を疑ってください（ただし、増えてもフレームが来ている間は復旧しません）。

## CPU使用率を見る

```sh
make -C userspace/mini-ai-app cpu-task-monitor
```

ST-LINKで`Key::kCpuTaskMonitor`の領域（PSRAM上の512 KiB）を読み出し、`build-mini-ai-app/cpu_task_monitor.png`（タスク別使用率、ガント図、ループ時間）、CSV、JSONを作ります。読み出しのあいだCPUを一時停止し、読み出し後に再開します。UARTモニタはそのまま開いておきます。

見方の目安:

- `camera`タスクのループ時間は、実際のカメラのフレーム周期と比較します。表示時には`ComposeAndPresent()`で800×480×2 bytes（750 KiB）をPSRAM内でコピーし、枠を描きます。
- `inference`タスクのループ時間は、`process`内の`RunOne()`全体なのでNPU完了待ちを含みます。`wait`側のフレームチャネル待ちは含みません。ループ区間は経過時間、CPU使用率は実際のタスク実行時間なので、NPU待ちをCPU負荷と混同しないでください。
- `usermain`と`initialize`はほぼ0%です。

uvの環境は`UV_CACHE_DIR=/tmp/uai-uv-cache uv sync --project host_app --locked`で用意します（[host_app](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md)）。

## よくある失敗

| 症状 | 原因と対処 |
| --- | --- |
| `make configure`が`mini-ai-app dependency is missing: .../models/person/network.c` | モデル未生成。`make ai-models` |
| `STEdgeAI runtime version mismatch` | モデルを生成したSTEdgeAIと`STEDGEAI_LIB_DIR`のバージョンが違う。同じ版で生成し直す |
| `mini: driver initialization failed`と`component=driver` | PSRAM/LCD/カメラなどの初期化失敗。直前のログと`code`を確認する。NOR単独の初期化失敗ではカメラを止めない |
| `nor=0`、`model unavailable` | NORの初期化・読み出し・メモリマップに失敗。直前のNOR関連ログを確認する。モデル未書き込みと同じ意味ではない |
| `component=npu.init` | 重みかblobが無い・古い。`ai-load` |
| `mini-ai-app dependency is missing: .../vision_models_pp/...` | 後処理ソースがない。第7章の参照先を用意するか、CMakeの`AI_VISION_MODELS_PP_DIR`を指定する |
| 画面が黒いまま、`pipe1=started`が出ない | カメラモジュールの接続、またはCSI。`camera: diag ...`行のレジスタ値を見る |
| 枠が出ない | `inference/s`が0なら推論が回っていない。0でなければしきい値（0.6）が高いだけの可能性。`first inference`の`boxes`を見る |
