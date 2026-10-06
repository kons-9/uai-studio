# image_resizer

NPUの入力サイズに合わせて画像を縮小するための、ハードウェア選択の方針層とCPUによる縮小です。バッファの確保とキャッシュ操作は呼び出し側が行います。

## 方針

縮小はできるだけカメラのDCMIPPに任せ、CPUの処理を減らします。ai-appではPipe2のDCMIPPが800x480を480x480のletterboxへ縮小し、CPUはそこからさらに小さい入力（face 128x128、segmentation 320x320）を作るだけです。

| 入力 | 選ばれるハードウェア |
| --- | --- |
| `kCameraPipe`（DCMIPPの出力） | DCMIPP。decimation（1/2/4/8）の後、downsizeで最大8:1まで縮小します。拡大はしません |
| `kRgb565Memory`（メモリ上の画像） | CPU |

DMA2DとGPU2D（NeoChrom）は未実装で、暗黙に選ばれることもありません。GPDMA、HPDMAは画素の補間ができないため候補にしていません。

## ハードウェアの選択

```cpp
image_resizer::Request request{};
request.input = image_resizer::InputKind::kCameraPipe;
request.input_width = 800U;
request.input_height = 480U;
request.output_width = 128U;
request.output_height = 77U;

image_resizer::Selection selection{};
common::Error status = image_resizer::Select(request, &selection);
// selection.hardware == Hardware::kDcmipp、selection.dcmipp_decimationに1/2/4/8
```

`Select()`は設定値を返すだけで、ハードウェアは操作しません。DCMIPPの設定はカメラドライバーが行います。

## CPUによる縮小

CPU処理は`uai::image_processing`が提供します。`Resize()`の入力形式は`Rgb565Source`と`Rgb888Source`の型で区別します。いずれもnearest-neighborで、メモリを確保しません。

| 関数 | 内容 |
| --- | --- |
| `Resize(source, crop_x, crop_y, crop_w, crop_h, destination)` | RGB565の一部を切り出してRGB888へ縮小 |
| `Resize(source, destination)` | RGB888を`destination`の大きさへ縮小 |
| `ResizeLetterbox(source, destination, content_w, content_h, pad)` | RGB888を縦横比を保って縮小し、余白を埋める |
| `FillLetterboxPadding(destination, content_w, content_h, pad)` | letterboxの余白だけを埋める |

ai-appではfaceとsegmentationの前処理（`AiFuture`の前処理ステップ）で、letterbox済みの480x480の入力（`frame.source`）を`Resize()`で縮小しています。縮小先はNPUが読むため、書き終えたら`CacheManagement::PrepareForPeripheralRead()`を呼びます。

## テスト

ホストPCでハードウェア選択、画素変換、letterboxの書き込み範囲を確認できます。

```sh
make -C kernel/middleware/image_resizer/tests test
```
