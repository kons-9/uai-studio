# image_resizer (HAL 非依存 middleware)

RGB565 → RGB888 の nearest-neighbor 変換、および RGB888 のリサイズと letterbox 余白塗りを提供します。バッファ確保、cache clean/invalidate、カメラ、HAL に依存しません。入力・出力バッファの所有権とキャッシュ同期は呼び出し側で管理します。

DCMIPP Pipe1/2 の縮小倍率・間引き選択は `driver/camera_driver/dcmipp_resize.*` に分離しました。HAL の設定呼び出しは `driver/camera_driver/camera_driver.cpp` にあります。DMA2D/GPU2D は未実装です。将来 HW backend を追加する際は、その実装とポリシーを driver 側へ追加し、汎用 CPU 処理へ HAL ヘッダを持ち込まないでください。
