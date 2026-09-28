# image_resizer

`image_resizer` は、入力元に応じて画像縮小を担当するHWを選ぶための方針層です。
HALのレジスタ設定そのものは各ドライバが担当し、ここでは「どのHWを使うか」と
「DCMIPPに渡す縮小条件」を決めます。バッファの確保・所有権・キャッシュ操作も
呼び出し側の責任です。

## 現在の選択

| 入力 | 選択されるHW | 状態 |
| --- | --- | --- |
| カメラPipe入力 | DCMIPP | 対応済み。crop後のdownsizeとdecimationを使う |
| メモリ上のRGB565 | CPU | 対応済み。nearest-neighborでRGB888へ変換 |
| メモリ上の画像をDMA転送・変換 | DMA2D / Chrom-ART | 未対応。現在は選択しない |
| 2D描画・拡大縮小・回転・合成 | GPU2D / NeoChrom | 未対応。現在は選択しない |

### DCMIPP

カメラのCSI入力と同じデータ経路にあるため、カメラ画像をNPU入力へ縮小する場合の
第一候補です。現在の実装では、DCMIPPのdecimationを `1/2/4/8` から選び、入力の
縦横に同じ係数を適用します。その後のDCMIPP downsizeが扱える縮小比を最大8:1に
収めます。アップスケールはDCMIPPの経路では行いません。

たとえばfaceの経路では、センサー画像から中央の `1555x1555` をcropし、Pipe2で
`128x128 RGB888` へ縮小します。現在の実機構成ではこの処理はCPUで画像を作り直さず、
DCMIPP Pipe2が実行します。

### CPU

すでにメモリ上にあるRGB565画像をRGB888へ変換する安全なfallbackです。動的な確保は
行わず、呼び出し側から渡された入力・出力バッファへnearest-neighborで書き込みます。
カメラPipeを使える場合は、CPU fallbackよりDCMIPPを優先します。

### DMA2D / Chrom-ART

通常のDMAと違い、ピクセル形式変換、fill、blendなどを扱う2Dアクセラレータです。
ただし、現状の`image_resizer`では未実装です。DMA2Dを一般DMAの代わりとして暗黙に
選択することもありません。将来対応する場合は、RGB565/RGB888変換、stride、crop、
cache整合性、完了待ちを含む専用backendを追加します。

### GPU2D / NeoChrom

2Dグラフィックス向けの拡大縮小、回転、合成を行うアクセラレータです。現状は
`image_resizer`から選択しません。LCD合成や複雑な変換を高速化する候補ですが、
カメラPipeの代替としては、入力バッファの形式とcache/同期方法を確認してから追加
する必要があります。

### GPDMA / HPDMA

これらは汎用のデータ転送DMAであり、画像の画素補間や形式変換を行うresizerでは
ありません。バッファ搬送には使えますが、画像縮小HWとしては`image_resizer`の
候補にしていません。

## 推論経路との関係

faceモデルの現在の経路は次の通りです。

```text
CSI camera
  -> DCMIPP Pipe2 crop/decimation/downsize
  -> 128x128 RGB888 inference buffer
  -> NPU
  -> CPU postprocess
  -> Pipe1 LCD overlay
```

Pipe1の表示用フレームをCPUで縮小して推論入力にする経路とは異なり、Pipe2の画像が
そのまま推論入力になります。そのため、枠の位置を変更する場合は、Pipe2のcropと
`model_manager`のPipe2からPipe1への座標変換を同時に確認してください。

性能を測るときは、`AI_DCMIPP_PIPE2_FRAME_RATE` と
`AI_INFERENCE_DIAGNOSTICS` を記録してください。前者が
`DCMIPP_FRAME_RATE_1_OVER_2` なら、20 fpsのカメラに対してPipe2の入力上限は約10 fps
です。また、後者を有効にすると推論経路でUARTログが増えます。T-Monitorの
`tm_printf()`は1文字ずつ送信するため、診断ログを有効にしたままではカメラタスクや
推論タスクの実測値が悪化する可能性があります。

## 今後のbackend追加方針

`Select()`が返すHWと実際の実装を一致させ、未対応のDMA2D/GPU2Dへ暗黙のfallbackを
しない方針です。backendを追加するときは、少なくとも次を実測・確認します。

1. 入出力formatとstride
2. crop範囲と縮小比
3. cache clean/invalidate
4. DMA/アクセラレータ完了待ちとバッファ所有権
5. Pipe2 frame drop数とNPU入力sequence
