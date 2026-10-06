# memory_manager

`memory_manager::MemoryManager`は、キャプチャ、表示、推論の各バッファをどのコンポーネントが使っているかを管理します。カメラのDMA、LCD、推論タスクが同じPSRAM上のバッファを扱うため、所有権を1か所で追跡して、書き込み中のバッファを読んだり、使用中のバッファにDMAが上書きしたりしないようにします。

アプリは1つのインスタンスを作り、ドライバーより先に`Initialize()`して、LCDとカメラの`Initialize()`に渡します。バッファ数とサイズは[memory](memory.md)で生成した`memory_config.hpp`から取ります。

## バッファの種類

| 種類 | 数（ai-app） | 書く側 | 読む側 | 扱う場所 |
| --- | --- | --- | --- | --- |
| キャプチャ（Pipe1、RGB565 800x480） | 2 | カメラDMA | LCD | カメラとLCDのドライバー内部 |
| 表示（RGB565 800x480） | 2 | LCDドライバー（合成） | LTDC | LCDドライバー内部 |
| 推論（RGB888 480x480＋モデル出力） | 3 | カメラDMA（Pipe2） | 推論タスク | アプリ |

キャプチャと表示のバッファはカメラとLCDのドライバーが内部で扱います。アプリが直接扱うのは推論バッファです。

## 推論バッファの流れ

```text
カメラのDMAが書き込む
  -> TakeCompletedInference()   kReadyForAi（カメラドライバーが返す）
  -> ClaimInferenceBuffer()     kInUseByAi（推論側が使用中）
  -> ReleaseInferenceBuffer()   kFree（DMAが再利用できる）
```

```cpp
// カメラタスク
pipeline::InferenceFrame frame{};
if (camera.TakeCompletedInference(&frame).Ok()) {
    camera.SnapshotInferenceSource(&frame);   // frame.source へ不変なコピーを作る
    // frame をキューで推論タスクへ渡す
}

// 推論タスク
if (memory.ClaimInferenceBuffer(frame).Ok()) {
    // 推論を投入する
}

// 推論完了コールバック
memory.ReleaseInferenceBuffer(frame);
```

- 受け取った`InferenceFrame`は、使っても使わなくても必ず`ReleaseInferenceBuffer()`で返します。返さないとPipe2のDMAが書き込み先を失い、フレームが落ちます。ai-appでは`InferenceFrameChannel`が送信失敗時に古いフレームを取り出して返却し、取り出せなければ送信するはずだったフレームを返却します。
- `ClaimInferenceBuffer()`と`ReleaseInferenceBuffer()`はフレームの`capture_sequence`と`lease_token`を照合します。キューに残った古いメッセージで再利用後のスロットを操作すると`kOwnership`を返します。
- 推論バッファには入力画像の後ろにモデル出力領域があり、`frame.outputs[]`で参照できます。`frame.source`は`SnapshotInferenceSource()`のコピー先、`frame.scratch`は共有の作業領域です。

## フレームの型と画像診断（pipeline）

`middleware/pipeline/`はフレームと画像形式の型と、画像を検査するヘッダだけの関数を定義します。ハードウェアやμT-Kernelには依存しません。

| 型 | 内容 |
| --- | --- |
| `pipeline::CaptureFrame` | Pipe1のフレーム。`buffer`と`sequence` |
| `pipeline::DisplayBuffer` | LCDの表示バッファ |
| `pipeline::InferenceFrame` | Pipe2のフレームと推論用の付随バッファ（`source`、`scratch`、`outputs[]`）、`capture_sequence`、`lease_token` |
| `pipeline::kCaptureFormat` | 800x480、2 byte/pixel |
| `pipeline::kInferenceFormat` | 480x480、3 byte/pixel |
| `pipeline::kInferenceContentFormat` | 480x288、3 byte/pixel（letterboxの有効領域） |

`image_diagnostics.hpp`はカメラや前処理の出力を調べるための関数です。ai-appの`DiagnosticsConfig`でフレーム単位の診断を有効にしたときに使われます。

| 関数 | 内容 |
| --- | --- |
| `Crc32Bytes(bytes, size)` | バッファのCRC32。フレームが更新されているかの確認に使います |
| `SampleRgb565Luminance(pixels, count, step)` | RGB565を間引いて輝度の平均と最大を求めます（露出の確認） |
| `InspectRgb888(bytes, size, pixel_count, step)` | RGB888のCRC、最小・最大値、平均輝度（NPU入力の確認） |
| `InspectCaptureRows(pixels)` | Pipe1フレームの行ごとにデータの有無とCRCを調べ、空行や重複行を数えます（DMA転送の確認） |

```sh
make -C kernel/middleware/pipeline/tests test
```

## キャッシュとの関係

PSRAM上のバッファはDキャッシュの対象です。所有権が移るときは`CacheManagement`で対応するキャッシュ操作を行います（[ドライバー](../driver.md)）。

| 所有権の移動 | キャッシュ操作 |
| --- | --- |
| DMA/NPUが書いた → CPUが読む | `PrepareForCpuRead()` |
| CPUが書いた → DMA/NPUが読む | `PrepareForPeripheralRead()` |
| CPUが使った → DMA/NPUに書かせる | `PrepareForDmaWrite()` |

## テスト

ホストPCで表示バッファの切り替えと、推論バッファの予約・lease照合を確認できます。テスト用の生成レイアウトfixtureを使い、実際のメモリ管理実装をリンクします。ISRとタスクの同時実行はこのテストの対象外です。

```sh
make -C kernel/middleware/memory_manager/tests test
```
