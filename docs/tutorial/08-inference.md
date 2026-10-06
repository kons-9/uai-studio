# 8. 推論

## 目的

推論タスクで、届いたPipe2フレームをNPUに流し、出力テンソルをYOLOXの後処理で検出枠（`inference::BoxSet`）にしてカメラタスクへ送ります。この章の終わりでLCDに枠が出ます。

## 追加するコード

```text
src/model/person_decoder.hpp / .cpp   出力テンソル → BoxSet（CPU、ST vision_models_pp）
src/task/inference_task.cpp           モデルの初期化と1フレームの推論
```

### 起動時: モデルの初期化と検証

```cpp
void InferenceTask::Run()
{
    AppContext &app = App();
    tk_wai_flg(app.external_memory_ready, kExternalMemoryReady, TWF_ANDW, &pattern, TMO_FEVR);
    if (!app.external_nor_ready) { /* 警告を出して眠る。カメラ表示は続く */ }

    static PersonModel model;
    static PersonDecoder decoder;
    static stai_network_info info{};
    npu::NpuManagement &npu = npu::NpuManagement::Instance();

    npu.Initialize(model);             // STAIランタイムとネットワークの初期化
    npu.GetInfo(&info);                // 入出力テンソルの形と量子化
    ValidateModelInfo(info, npu);      // 入力 480x480x3、出力3つ、出力が予約領域に収まる、
                                       // GetOutputs() が nullptr（--no-outputs-allocation）
    decoder.Configure(info);           // 出力の scale / zero_point を後処理へ

    static npu::NpuManagement::Accessor npu_accessor;
    npu.Acquire(&npu_accessor);        // このタスクがNPUを占有する
    ...
}
```

### 1フレームの推論

`RunForever`の`wait`でフレームチャネルを待ち、`process`で1回ぶん処理します。

```cpp
common::Task::RunForever(app.cpu_task_monitor, "inference",
    [&] { app.frames.Receive(&frame); },        // 永久待ち
    [&] {
        status = app.memory.ClaimInferenceBuffer(frame);   // 所有権を推論側へ
        if (!status.Ok()) { status.LogStatus("memory"); return; }   // 古いフレーム
        status = inference.RunOne(frame);
        if (!status.Ok()) status.LogStatus("inference");
        app.memory.ReleaseInferenceBuffer(frame).LogStatus("memory");   // 必ず返す
        inference.Report();
    });
```

`RunOne()`の中身です。

```cpp
common::Error Inference::RunOne(const pipeline::InferenceFrame &frame)
{
    // (1) 入力: Pipe2のDMAが書いた範囲のキャッシュを無効化する
    const buffer::Buffer input{frame.buffer.address, info.inputs[0].size_bytes,
                               frame.buffer.index, buffer::Region::kInference};
    app.cache.PrepareForCpuRead(input);

    // (2) NPUに入力と出力の場所を教える
    npu::NpuDriver &driver = *npu.Get();
    const auto &writer = npu.Ownership();
    driver.SetInput(reinterpret_cast<stai_ptr>(input.address), input.size, writer);
    stai_ptr outputs[kMaxOutputs]{};
    for (i = 0; i < info.n_outputs; ++i) outputs[i] = frame.outputs[i].address;
    driver.SetOutputs(outputs, info.n_outputs, writer);

    // (3) 実行。完了割り込みまでこのタスクは待つ
    driver.Run(writer);
    driver.NewInference(writer);

    // (4) 出力: NPUが書いた範囲のキャッシュを無効化してからCPUで読む
    for (i = 0; i < info.n_outputs; ++i) {
        app.cache.PrepareForCpuRead({frame.outputs[i].address, info.outputs[i].size_bytes, ...});
        views[i] = frame.outputs[i].address;
    }

    // (5) 後処理 → BoxSet → カメラタスクへ
    inference::BoxSet boxes{};
    decoder.Decode(views, info.n_outputs, &boxes);
    boxes.model_sequence = ++sequence;
    boxes.capture_sequence = frame.capture_sequence;
    return app.results.Publish(boxes);
}
```

### 後処理: PersonDecoder

STの`vision_models_pp`（`od_st_yolox_pp_reset`/`od_st_yolox_pp_process_int8`）を呼びます。`Configure()`で3つの出力をサイズ順（S < M < L）に並べ、グリッド（15/30/60）、アンカー、しきい値（conf 0.6、IoU 0.5）、量子化パラメータを渡します。`Decode()`は正規化座標の検出を受け取り、Pipe1座標へ投影します。

```cpp
/* Pipe2は800x480を480x480にletterboxする: 有効域は480x288、上下に96pxの余白。 */
inference::Box ProjectToCapture(const YoloxDetection &d)
{
    constexpr float kPadTop = (480.0F - 288.0F) / 2.0F;
    constexpr float kVerticalScale = 480.0F / 288.0F;
    const float left = (d.x_center - d.width * 0.5F) * 800.0F;
    const float top = ((d.y_center - d.height * 0.5F) * 480.0F - kPadTop) * kVerticalScale;
    ...
}
```

`YoloxParams`/`YoloxDetection`はライブラリの`od_st_yolox_pp_static_param_t`/`od_pp_outBuffer_t`と同じレイアウトの再定義です。ライブラリのヘッダは`arm_math.h`を引き込むため、C++側には含めていません（ai-appと同じ扱い）。

## なぜそうするか

- 所有権: `TakeCompletedInference()`で受けたフレームは「カメラが書き終えた」状態、`ClaimInferenceBuffer()`で「推論が使用中」、`ReleaseInferenceBuffer()`で「DMAが再利用できる」状態になります。`Claim`が`kOwnership`で失敗するのは、キューに残っている間にスロットが再利用された古いフレームです。この場合は何も返す必要がありません（[memory_manager](../middleware/memory_manager.md)）。
- キャッシュ: 入力はDMAが書き、出力はNPUが書きます。どちらもCPUのキャッシュには入っていないので、読む前に`PrepareForCpuRead()`（invalidate）します。入力をCPUで加工していないので、clean（`PrepareForPeripheralRead()`）は不要です。CPUで縮小した画像を入力にする場合（ai-appのface/segmentation）は、書いた後にcleanが要ります（[キャッシュ](../driver.md)）。
- 同期実行: `Run()`は`StartRun()`＋`WaitRun()`で、NPU割り込みがイベントフラグを立てるまでこのタスクだけが待ちます。カメラタスクは優先度5で別に回っているので表示は止まりません。前処理・NPU・後処理を別タスクで重ねて流すのがai-appの`ai_runtime`です（[次のステップ](10-next-steps.md)）。
- `Accessor`を起動時に1回だけ取り、以後はそのトークンで`driver.Run(writer)`のように呼びます。`NpuManagement`の簡易メソッドは呼び出しごとに所有権を取るため、`Accessor`を持ったまま呼ぶと自分自身を待ってしまいます。

## 確認

```sh
make -C userspace/mini-ai-app ram-run
```

UARTに次の順で出れば完了です。

```text
camera: pipe1=started pipe2=started
mini: model ready input=691200 bytes outputs=3
mini: first inference seq=... boxes=...
mini: inference/s=... pipe2=... drops=... csi_errors=...
```

カメラの前に人が立つとLCDに枠が出ます。`inference/s`はNPUの実行時間で決まります（personモデルは10 fps台）。`drops`はPipe2のフレームが推論に間に合わず捨てられた数で、推論より速いカメラでは増え続けて正常です。

`mini: model does not match this application`で止まる場合は、生成したモデルの入力サイズか出力数が違います（別のモデルファイルを指定した、`--inputs-ch-position`を変えた等）。`error: component=npu.init`は重みかblobがNORに無い・古いことが多いので、`ai-load`をやり直してください。
