# sample-ai: STM32N6570-DK Neural-ART 推論

`sample-ai` は、STM32N6570-DK の Neural-ART NPU で量子化 AI モデルを推論する
最小構成です。一般的な意味での GPU (NeoChrom) ではなく、AI 推論専用の NPU
を使います。入力はカメラではなく再現可能なテストパターンにしてあり、
`sample-camera-lcd` のカメラ実装と独立しています。推論結果の先頭バイトと、出力が
`float32` の場合は argmax を UART に表示します。

## 2つのモデル

モデルは同時にリンクするのではなく、`AI_MODEL` で1つを選択します。
重みは同じ XSPI2 の領域を使うため、実行時に選んだモデルの重みを1つだけ
書き込みます。

- `model1`: ST公式の STM32N6570-DK 例と同じ EfficientNet v2 B1 (240x240)
- `model2`: 小さい MobileNet v1 0.25 (96x96)。初期確認用

ST公式のモデルファイルは
[STM32N6-GettingStarted-ImageClassification](https://github.com/STMicroelectronics/STM32N6-GettingStarted-ImageClassification)
の `Model/` にあります。モデルのライセンスと利用条件は公式リポジトリを
確認してください。

## 先に必要なもの

このリポジトリには、STEdgeAI が生成するモデル固有の C ファイルと重みを
含めていません。生成物はモデルごとに大きく、STEdgeAI のバージョンにも
依存するためです。次を準備してください。

1. STM32CubeN6 パッケージ。`STM32CUBE_N6_DIR` に設定します。
2. `stedgeai-lib`。STEdgeAI の配布物、または上記公式リポジトリの
   `Middlewares/stedgeai-lib` を `STEDGEAI_LIB_DIR` に設定します。
3. `stedgeai` CLI と `arm-none-eabi-objcopy` が PATH にあること。
4. 上記公式リポジトリから、model1/model2 の元モデルを取得すること。

生成された4ファイル (`network.c`, `network_ecblobs.h`, `stai_network.c`,
`stai_network.h`) と `network_data.xSPI2.bin`/`.hex` は
`userspace/sample-ai/models/model1/` または `model2/` に置きます。

## モデル生成

```sh
sh userspace/sample-ai/models/generate_model.sh model1 \
  /path/to/efficientnet_v2B1_240_fft_qdq_int8.onnx

sh userspace/sample-ai/models/generate_model.sh model2 \
  /path/to/mobilenet_v1_0.25_96_tfs_int8.tflite
```

生成設定は `models/user_neuralart_STM32N6570-DK.json` と、NPU RAM/XSPI2 の
アドレスを記述した `models/my_mpools/stm32n6-app2_STM32N6570-DK.mpool` です。
STEdgeAI と `stedgeai-lib` のバージョンは必ず一致させてください。生成 C
ファイル自身にもランタイムバージョンのチェックがあります。

sample-ai専用リンカスクリプトは、生成された大きな command blob を収めるため
AXISRAM1全体をアプリケーションに割り当て、AXISRAM2-6をNPU用に残します。
モデルがXSPI1を使う場合にも対応できるよう、実行時にDKのPSRAMとNORの
memory-mappedモードを初期化します。
これは `sample-hello-world` のリンカスクリプトを変更するものではありません。

## 後で行う configure/build

現在の作業中に副作用が出ないよう、この手順は実行していません。`sample-camera-lcd`
の確認が終わってから、既存の sample-hello-world 用 CubeMX 出力を使う場合は次のように
別のビルドディレクトリを使えます。

```sh
cmake -S . -B build-sample-ai \
  -DAPP_TARGET=sample-ai \
  -DSTM32CUBE_N6_DIR=/path/to/STM32Cube_FW_N6_V1.3.0 \
  -DSTEDGEAI_LIB_DIR=/path/to/stedgeai-lib \
  -DCUBEMX_OUTPUT_DIR="$PWD/build/cubemx" \
  -DCUBEMX_IOC="$PWD/userspace/sample-hello-world/config/stm32n6570-dk-fullsecure.ioc"

cmake --build build-sample-ai --target sample-ai
```

model2 に切り替えるときは configure に
`-DAI_MODEL=model2` を追加します。`CUBEMX_OUTPUT_DIR` は、実際に動作
確認済みの CubeMX 出力を指定してください。CubeMX生成・ビルド・書き込みは
この実装作業では実行していません。

後で `ram-run` を使う場合は、sample-aiのスタック上限に合わせて
`STM32_RAM_STACK=0x34100000` も指定してください。

## 重みの書き込み

モデル重みはアプリケーション ELF に入らず、XSPI2 のメモリマップ領域
`0x70380000` を参照します。従って、初回とモデル切り替え時に、生成された
`network_data.hex` を STM32CubeProgrammer と DK 用 external loader で書き込む
必要があります。書き込みコマンドはボード接続を変更するため、上記 build と
同じく `sample-camera-lcd` の作業が終わってから実行してください。

## sample-aiだけで完結している範囲

アプリケーション、NPU初期化、NPU cache/RAM有効化、NORメモリのmemory-mapped
設定、STEdgeAIランタイムのリンク指定はすべて `userspace/sample-ai/` に収めて
います。既存の `sample-hello-world` CubeMX生成物は読み込みますが、sample-ai用に生成し直す
必要はありません。

詳細な依存関係と、sample-ai外を変更していないことの記録は
[`OUTSIDE_CHANGES.md`](OUTSIDE_CHANGES.md) にあります。
