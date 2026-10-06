# image_resizer

NPUの入力サイズに合わせて画像を縮小するときに、DCMIPPとCPUのどちらを使うかを決める方針層です。ハードウェアは操作しません。

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

CPUで縮小する場合は[image_processing](image_processing.md)を使います。

## テスト

ホストPCでDCMIPPのdecimation選択とCPUフォールバックの判定を確認できます。

```sh
make -C kernel/middleware/image_resizer/tests test
```
