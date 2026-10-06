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

- 受け取った`InferenceFrame`は、使っても使わなくても必ず`ReleaseInferenceBuffer()`で返します。返さないとPipe2のDMAが書き込み先を失い、フレームが落ちます。
- `ClaimInferenceBuffer()`と`ReleaseInferenceBuffer()`はフレームの`capture_sequence`と`lease_token`を照合します。キューに残った古いメッセージで再利用後のスロットを操作すると`kOwnership`を返します。
- 推論バッファには入力画像の後ろにモデル出力領域があり、`frame.outputs[]`で参照できます。`frame.source`は`SnapshotInferenceSource()`のコピー先、`frame.scratch`は共有の作業領域です。

## フレームの型（pipeline）

`middleware/pipeline/`はフレームと画像形式の型だけを定義します。

| 型 | 内容 |
| --- | --- |
| `pipeline::CaptureFrame` | Pipe1のフレーム。`buffer`と`sequence` |
| `pipeline::DisplayBuffer` | LCDの表示バッファ |
| `pipeline::InferenceFrame` | Pipe2のフレームと推論用の付随バッファ（`source`、`scratch`、`outputs[]`）、`capture_sequence`、`lease_token` |
| `pipeline::kCaptureFormat` | 800x480、2 byte/pixel |
| `pipeline::kInferenceFormat` | 480x480、3 byte/pixel |
| `pipeline::kInferenceContentFormat` | 480x288、3 byte/pixel（letterboxの有効領域） |

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
