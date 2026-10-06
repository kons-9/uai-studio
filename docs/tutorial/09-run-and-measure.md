# 9. 実行と計測

## 手順のまとめ

初回は次の順です。2回目以降は`ram-run`だけで済みます（モデルを変えたときだけ`ai-load`）。

```sh
make -C userspace/mini-ai-app setup      # ai-deps → ai-models → CubeMX生成 → CMake構成
make -C userspace/mini-ai-app monitor    # 別端末。UARTを開いたままにする
make -C userspace/mini-ai-app ai-load    # ビルドして重みとblobをNORへ
make -C userspace/mini-ai-app ram-run    # ビルドしてRAMへ書き、実行
```

`setup`は`ENABLE_AI=1`なので、`ai-deps`（STEdgeAIランタイムと後処理ソースの確認）と`ai-models`（`AI_MODEL_NAMES`のモデル生成）を先に行います。CubeMXの生成は`CUBEMX_EXECUTABLE`（`local.mk`）で動かします。

## UARTの読み方

起動から定常状態までに出る主な行です。

| 行 | 出所 | 意味 |
| --- | --- | --- |
| `mini: driver init begin` / `done nor=1` | initialize | ドライバ初期化の開始と完了。`nor=0`ならNORが読めず推論は無効 |
| `camera: pipe1=started pipe2=started` | camera driver | Pipe1/Pipe2のDMAが始まった |
| `mini: model ready input=691200 bytes outputs=3` | inference | モデルの初期化と検証に成功 |
| `mini: first inference seq=N boxes=M` | inference | 最初の推論が完了。`seq`はPipe2のフレーム番号 |
| `mini: inference/s=A pipe2=B drops=C csi_errors=D` | inference、1秒ごと | 推論回数/秒、Pipe2の累計フレーム数、推論に回らず捨てた累計、CSIエラー累計 |
| `cpu: ...` | cpu_task_monitor、1秒ごと | タスク別CPU使用率（`ENABLE_CPU_TASK_MONITOR=1`のとき） |
| `camera: no frame for N ms; starting recovery #k` | camera driver | フレームが止まり再初期化した |
| `error: component=<name> operation=... code=...` | 各所 | `common::Error::LogStatus()`。`code`は[エラーコード](../kernel/common.md) |

`drops`が増えるのは正常です。カメラは約20 fps、personモデルの推論は10 fps台なので、推論中に届いたPipe2フレームは捨てられます。`csi_errors`が増え続ける場合はケーブルやボードの問題を疑ってください（ただし、増えてもフレームが来ている間は復旧しません）。

## CPU使用率を見る

```sh
make -C userspace/mini-ai-app cpu-task-monitor
```

ST-LINKで`Key::kCpuTaskMonitor`の領域（PSRAM上の512 KiB）を読み出し、`build-mini-ai-app/cpu_task_monitor.png`（タスク別使用率、ガント図、ループ時間）、CSV、JSONを作ります。読み出しのあいだCPUを一時停止しますが、停止中もDMAとLTDCは動くので表示は乱れません。

見方の目安:

- `camera`タスクのループ時間が1フレーム（50 ms）を超えていなければ表示は間に合っています。`ComposeAndPresent()`のコピー（768 KiB、PSRAM→PSRAM）が大半です。
- `inference`タスクのループは「NPU待ち」を含みません（`wait`はチャネル待ち、`process`は`RunOne()`全体なので、NPUのイベント待ちが`process`に入ります）。NPU実行中もCPUを使っているわけではないので、使用率はガント図の幅ほど高くなりません。
- `usermain`と`initialize`はほぼ0%です。

uvの環境は`UV_CACHE_DIR=/tmp/uai-uv-cache uv sync --project host_app --locked`で用意します（[host_app](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md)）。

## よくある失敗

| 症状 | 原因と対処 |
| --- | --- |
| `make configure`が`mini-ai-app dependency is missing: .../models/person/network.c` | モデル未生成。`make ai-models` |
| `STEdgeAI runtime version mismatch` | モデルを生成したSTEdgeAIと`STEDGEAI_LIB_DIR`のバージョンが違う。同じ版で生成し直す |
| `mini: driver initialization failed`と`component=driver code=kHardware` | PSRAM/NOR/LCD/カメラのHAL失敗。直前の`error:`行の`component`で特定する |
| `nor=0`、`model unavailable` | NORが初期化できない。`ai-load`で一度でも書いていればマップされるので、未書き込みが多い |
| `component=npu.init` | 重みかblobが無い・古い。`ai-load` |
| 画面が黒いまま、`pipe1=started`が出ない | カメラモジュールの接続、またはCSI。`camera: diag ...`行のレジスタ値を見る |
| 枠が出ない | `inference/s`が0なら推論が回っていない。0でなければしきい値（0.6）が高いだけの可能性。`first inference`の`boxes`を見る |
