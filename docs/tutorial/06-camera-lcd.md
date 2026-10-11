# 6. カメラとLCD

## 目的

カメラタスクで、Pipe1（RGB565 800x480）をLCDに表示し続け、Pipe2（RGB888 480x480 letterbox）のフレームを推論タスクへ渡し、届いた検出枠を映像に重ねます。この章の終わりで映像が出ます。枠は章8で出るようになります。

## 追加するコード

`src/task/camera_task.cpp`です。起動時に初期画面を出してカメラを開始し、1 msごとのループで4つのことをします。

```cpp
void CameraTask::Run()
{
    AppContext &app = App();
    common::Error status = app.lcd.ShowInitialFrame(inference::BoxSet{});
    if (!status.Ok()) {
        status.LogStatus("lcd");
        common::Task::Halt("mini: initial frame failed\n");
    }
    status = app.camera.Start();
    if (!status.Ok()) {
        status.LogStatus("camera");
        common::Task::Halt("mini: camera start failed\n");
    }

    inference::BoxSet shown_boxes{};
    std::uint32_t last_result_tick = common::Task::Now();

    common::Task::RunForever(app.cpu_task_monitor, "camera",
                             [] { tk_dly_tsk(1); },
                             [&] {
        app.camera.Process();                          // (1) ISP更新と自動復旧
        ForwardPipe2Frame(app);                        // (2) Pipe2 → 推論タスク

        const std::uint32_t now = common::Task::Now(); // (3) 最新の結果を取り出す
        const message_channel::DrainResult drained = app.results.DrainLatest(&shown_boxes);
        if (drained.updated) last_result_tick = now;
        if (shown_boxes.person_valid && now - last_result_tick >= kResultHoldMs) {
            shown_boxes = inference::BoxSet{};         // 3秒更新がなければ枠を消す
        }

        pipeline::CaptureFrame capture{};              // (4) Pipe1 を合成して表示
        if (!app.camera.TakeCompletedCapture(&capture).Ok()) return;   // kNoFrame は通常
        status = app.lcd.ComposeAndPresent(capture, shown_boxes);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            if (!status.IsRoutine()) common::Task::Halt("mini: lcd compose failed\n");
        }
    });
}
```

`ShowInitialFrame()`で黒画面を出してLTDCの走査を始め、`Start()`でPipe1/Pipe2を開始します。どちらかが失敗すれば、エラーを記録して停止します。

Pipe2の転送は、NOR初期化に失敗した場合はその場でバッファを返します。モデル内容の検証とは別の条件です。

```cpp
void ForwardPipe2Frame(AppContext &app)
{
    pipeline::InferenceFrame frame{};
    const common::Error status = app.camera.TakeCompletedInference(&frame);
    if (status.Ok()) {
        if (app.external_nor_ready) app.frames.Send(frame);
        else app.memory.ReleaseInferenceBuffer(frame).LogStatus("memory");
        return;
    }
    if (status.Code() != common::ErrorCode::kNoFrame &&
        status.Code() != common::ErrorCode::kNoBuffer) status.LogStatus("camera");
}
```

## なぜそうするか

- `Process()`は毎ループ呼びます。ISP（自動露出・ホワイトバランス）の更新はvsyncに同期して行われ、フレームが一定時間止まるとセンサとDCMIPPを再初期化します（[カメラ](../driver/camera.md)）。復旧回数は`GetDiagnostics().recovery_count`で見え、完成形ではその変化をWarnログにしています。
- `TakeCompletedCapture()`と`TakeCompletedInference()`は、フレームがなければ`kNoFrame`を返す非待機の呼び出しです。表示タスクは`tk_dly_tsk(1)`で回るので、どちらも「あれば処理する」だけで書けます。
- Pipe2のフレームは所有権（lease）を持って届きます。チャネルへ送れなかったときも、NOR初期化に失敗したときも、必ず`ReleaseInferenceBuffer()`で返します。推論バッファは3枚なので、3枚とも誰かが持ったままだとPipe2のDMAは`PSRAM_PIPE2_DROP`へ捨て書きし、`pipe2_drop_count`が増えます。
- `ComposeAndPresent()`はPipe1のバッファを表示用バッファへコピーし、`BoxSet`の枠を描いてLTDCに切り替えを予約します。LTDCの切り替えがまだ終わっていなければ`kNoBuffer`を返すので、`IsRoutine()`なエラーは無視して次のループへ進みます。
- 結果の保持（`kResultHoldMs`）は「推論が止まっても古い枠を残さない」ためです。更新がある間は最新の枠を、途切れたら3秒で消します。

## 確認

```sh
make -C userspace/mini-ai-app monitor    # 別端末。すでに開いていれば追加しない
make -C userspace/mini-ai-app ram-run
```

UARTに`camera: pipe1=started pipe2=started`が出て、LCDにカメラ映像が出れば完了です。`nor=0`なら`mini: model unavailable; camera remains live`が出て、推論を止めたまま映像だけが動きます。モデル未書き込みでも`nor=1`になり得るため、このログが必ず出るとは限りません。完成形を動かす場合は、第7章の`ai-load`も先に済ませてください。

映像が出ない・止まるときは、`camera: no frame for ... ms; starting recovery`（自動復旧）と、その後の`camera: recovery attempts=...`を見てください。CSIのエラーは`csi_error_count`に数えられますが、それだけでは復旧しません（フレームが止まったときだけ復旧します）。
