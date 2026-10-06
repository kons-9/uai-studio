# image_processing

CPUによる画像の縮小と形式変換です。`uai::ai::image_processing`名前空間で、OS/HALに依存せず、メモリを確保しません。入出力のバッファとキャッシュ操作は呼び出し側が持ちます。

入力形式は`Rgb565Source`と`Rgb888Source`の型で区別します。strideはRGB565ではピクセル、RGB888ではバイト単位です。いずれもnearest-neighborで、入力と出力の重なりはサポートしません。

| 関数 | 内容 |
| --- | --- |
| `Resize(source, crop_x, crop_y, crop_w, crop_h, destination)` | RGB565の一部を切り出してRGB888へ縮小 |
| `Resize(source, destination)` | RGB888を`destination`の大きさへ縮小 |
| `ResizeLetterbox(source, destination, content_w, content_h, pad)` | RGB888を縦横比を保って縮小し、余白を埋める |
| `FillLetterboxPadding(destination, content_w, content_h, pad)` | letterboxの余白だけを埋める |

`Resize()`はcontent部分だけ、`FillLetterboxPadding()`はpadding部分だけを書き、両者を続けて呼ぶと`ResizeLetterbox()`と同じ結果になります。不正な引数では`kInvalidArgument`を返し、出力には何も書きません。

ai-appではfaceとsegmentationの前処理（`AiFuture`の前処理ステップ）で、letterbox済みの480x480の入力（`frame.source`）を`Resize()`で縮小しています。縮小先はNPUが読むため、書き終えたら`CacheManagement::PrepareForPeripheralRead()`を呼びます。DCMIPPを使うかCPUを使うかの判断は[image_resizer](image_resizer.md)が行います。

## テスト

```sh
make -C kernel/middleware/image_processing/tests test
```

小さな既知画像で全バイトを比較し、クロップ端、奇数サイズ、拡大/縮小、行末と前後の番兵、非ゼロpadding、不正入力時の無書き込みを確認します。
