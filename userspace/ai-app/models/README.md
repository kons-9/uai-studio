# ai-appのモデル生成

[tool/generate_model.sh](../tool/generate_model.sh)はSTEdgeAIで3モデルのNeural-ARTコードと重みイメージを生成します。通常は`make -C userspace/ai-app setup`（または`ai-models`）から呼ばれます。

| モデル | 取得元 | 元モデル |
| --- | --- | --- |
| `person` | [STM32N6-GettingStarted-ObjectDetection](https://github.com/STMicroelectronics/STM32N6-GettingStarted-ObjectDetection) | `st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite` |
| `segmentation` | [STM32N6-GettingStarted-SemanticSegmentation](https://github.com/STMicroelectronics/STM32N6-GettingStarted-SemanticSegmentation) | `deeplab_v3_mobilenetv2_05_16_320_fft_qdq_int8.onnx` |
| `face` | [STM32N6-GettingStarted-FaceDetection](https://github.com/STMicroelectronics/STM32N6-GettingStarted-FaceDetection) | `blazeface_front_128_quant_pc_ff_od_wider_face.tflite` |

元モデルはサイズが大きく、個別のライセンス条件があるためGit管理外です。利用条件は各取得元を確認してください。

## 実行方法

```sh
sh userspace/ai-app/tool/generate_model.sh person
sh userspace/ai-app/tool/generate_model.sh segmentation
sh userspace/ai-app/tool/generate_model.sh face
```

第2引数を省略すると`models/source/<model>/`の元モデルを使い、なければ取得元からダウンロードします。任意のファイルを使う場合は第2引数にパスを渡します。`stedgeai`と`arm-none-eabi-objcopy`がPATHに必要です。

## 出力

`models/<model>/`に次を出力します。いずれもGit管理外です。

| ファイル | 内容 |
| --- | --- |
| `network.c`、`network_ecblobs.h` | Neural-ARTのcommand blob |
| `stai_network.c`、`stai_network.h` | ST.AI C API |
| `network_data.xSPI2.bin`、`network_data.hex` | 外部NORへ書く重み |

生成時の重みアドレスと書き込みアドレスは一致している必要があります。既定のアドレスは[ai-appのREADME](../README.md#モデル)の表の通りです。

## 生成オプション

環境変数で上書きできます。

| 変数 | 既定値 | 内容 |
| --- | --- | --- |
| `AI_MODEL_OPTIMIZATION` | `balanced` | `time`、`ram`、`balanced` |
| `AI_MODEL_INPUT_DATA_TYPE` | `uint8` | 入力の型 |
| `AI_MODEL_OUTPUT_DATA_TYPE` | `int8` | 出力の型 |
| `AI_MODEL_INPUTS_CH_POSITION` | `chlast` | 入力のチャネル位置 |
| `AI_MODEL_OUTPUTS_CH_POSITION` | モデルごと | 出力のチャネル位置（faceは`chfirst`） |
| `AI_MODEL_C_API` | `st-ai` | 生成するC API |
| `AI_MODEL_CUT_OUTPUT_TENSORS` | モデルごと | 出力を切り出すテンソル名。segmentationは最終Resizeの手前で切ります |
| `AI_MODEL_NETWORK_ADDRESS` | モデルごと | 重みを置くxSPI2アドレス |
| `AI_MODEL_DOWNLOAD_URL` | 取得元 | ダウンロードURL |

入出力バッファはアプリが所有するため、`--no-inputs-allocation --no-outputs-allocation`で生成します。NPUのメモリプールは`my_mpools/stm32n6-app2_STM32N6570-DK.mpool`、Neural-ARTの設定は`user_neuralart_STM32N6570-DK.json`です。

生成に使うSTEdgeAIと、ビルドでリンクするランタイムは同じ版にしてください。CMakeは`ll_aton`のバージョン不一致を検出するとconfigureを止めます。
