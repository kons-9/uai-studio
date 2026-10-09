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

## バッファ検査と画像演算

hw-testのDMA2D試験で使用しているHAL非依存の処理を追加しています。既存の`Resize()`系APIは変更していません。experimentは独立したローカル実装を引き続き使用します。

[blit.hpp](../../kernel/middleware/image_processing/blit.hpp)の`Image`は、バッファ先頭、容量（bytes）、幅、高さ、行stride（常にバイト単位）、`Format::kRgb565`または`Format::kRgb888`を持ちます。RGB565はlittle-endian、RGB888はR/G/Bの順です。

| 関数 | 内容 |
| --- | --- |
| `Valid(image)` | 非null・形式・寸法・stride・最終画素までの容量・アドレス加算overflowを検査 |
| `Disjoint(first, second)` | 有効なバッファの全容量範囲が重複しないことを検査 |
| `BuildTransfer(source, destination, transfer)` | 同寸法・非重複の転送情報を生成。行末offsetはピクセル単位 |
| `ReferenceBlit(source, destination)` | CPUによるコピーとRGB565/RGB888相互変換 |

`BuildTransfer()`はDMA2Dに合わせ、幅と行末offsetを14bit、高さを16bitに制限します。不正入力では`transfer`を変更しません。`Valid()`はポインタのアラインメント、メモリの実在、DMAからのアクセス可否までは保証しないため、実機バックエンドが別途検査します。呼び出し側は`bytes`に実際の容量を指定してください。

[operations.hpp](../../kernel/middleware/image_processing/operations.hpp)の`Request`は演算、入力、背景、出力、RGB24色、8bit alphaを指定します。`Validate()`は演算に必要な入力だけを検査し、`Reference()`は次のCPU参照演算を実行します。不正な要求では`false`を返し、出力を変更しません。

| 演算 | 内容 |
| --- | --- |
| `Operation::kBlit` | 同寸法のコピー・形式変換 |
| `Operation::kFill` | RGB24色による塗りつぶし。入力・背景は不要 |
| `Operation::kBlend` | 各色成分を`(foreground * alpha + background * (255 - alpha)) / 255`で合成 |
| `Operation::kResize` | 画素中心でサンプリングするnearest-neighbor。形式変換、拡大・縮小をサポート |

いずれも行末paddingには書き込みません。`ReadPixel()`・`WritePixel()`は検査済み画像と範囲内座標に対する低レベル関数で、個別の境界検査は行いません。

## 転送バックエンドの検証

[verification.hpp](../../kernel/middleware/image_processing/verification.hpp)の`Verification`は、hw-test由来の25ケースを`kVerificationCases`で提供します。コピー、形式変換、塗りつぶし、alphaの端点・中間値、行末padding、不正要求の拒否と拒否後の再利用を検査します。

```cpp
#include "middleware/image_processing/verification.hpp"

namespace graphics = uai::ai::image_processing;

static graphics::Verification verification;
for (const auto &test : graphics::kVerificationCases) {
	const auto result = verification.Run(test, device, clock, cache);
	Report(test.name, result.passed, result.cycles,
		result.maximum_error, result.corrupted_bytes);
}
```

- `device.Run(const Request &, uint32_t timeout_ms)`は転送を同期実行し、成功時に`true`を返すバックエンドです。検証器はtimeoutに100msを指定します。所有権待ちや停止処理を含む関数全体の実時間上限はバックエンドの仕様に従います。失敗・拒否で戻る際にも、転送を停止してバッファへのアクセスを終了させてください。
- `clock()`は32bitサイクルカウンタ等を返します。`cycles`は転送呼出し前後の差で、32bit wrapを扱います。時間への換算は呼び出し側が行います。
- `VerificationCache`の`prepare(address, bytes)`と`inspect(address, bytes)`は任意のキャッシュ操作フックです。転送前後に入力・背景・出力の3バッファ全体へ適用します。実機ではバリアも含めたclean/invalidateを実装してください。
- 期待画素と全ガード・padding・未使用領域を照合し、入力と背景が保持されていることも確認します。中間alphaのブレンドだけはRGB888で各成分1、RGB565で8までの誤差を許容し、それ以外は完全一致を要求します。
- 既定ケースはDMA2D向けの契約です。unalignedな出力とresizeを拒否することも確認します。CPUの`Reference()`自体はこれらをサポートするため、そのままDMA2Dバックエンドの代用にはなりません。別のバックエンドでは`VerificationCase`を選択・定義してください。
- 検証器は32byte境界に整列した4バッファを内包し、約25KiBを使用します。小さいタスクスタック上へ置かず、静的領域等に配置してください。同じ検証器を複数タスクから同時に実行しないでください。

このmiddlewareはクロック・RIF・HAL初期化やIRQ・カメラの開始を行いません。ホストテストは演算と検証器を確認するもので、実DMA転送・キャッシュ整合・周辺機器の健全性の証明ではありません。それらは独立した[experiment-hw-test](../../userspace/experiment-hw-test/README.md)で確認します。

共有アプリでは[dma2d driver](../driver.md#dma2d)の`Dma2dManagement::Instance()`を`device`として渡せます。driverの初期化は検証前に行います。driver自身がキャッシュ操作と転送停止を担当するため、通常は検証器のcache hookを省略できます。

## テスト

```sh
make -C kernel/middleware/image_processing/tests test
```

小さな既知画像で全バイトを比較し、クロップ端、奇数サイズ、拡大/縮小、行末と前後の番兵、非ゼロpadding、不正入力時の無書き込みを確認します。追加した転送検証器には25ケースと、画素・入力・背景・ガード・padding破壊の故障注入、時計wrap、キャッシュフックの順序の回帰テストがあります。
