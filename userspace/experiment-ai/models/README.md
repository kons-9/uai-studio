# experiment-aiのモデル生成

`generate_model.sh`はSTEdgeAIでperson、segmentation、faceのNeural-ARTコードと重みイメージを生成します。モデル生成スクリプトとモデル固有の設定は、このexperiment内にあります。

| モデル | 元モデル | 重みアドレス | command blobアドレス |
| --- | --- | --- | --- |
| person | `st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite` | `0x70380000` | `0x70500000` |
| segmentation | `deeplab_v3_mobilenetv2_05_16_320_fft_qdq_int8.onnx` | `0x70600000` | `0x70560000` |
| face | `blazeface_front_128_quant_pc_ff_od_wider_face.tflite` | `0x70800000` | `0x70580000` |

元モデルはサイズが大きく、個別のライセンス条件があるためGit管理外です。各モデルの取得元と利用条件を確認してください。生成スクリプトには取得URLが設定されています。

## 実行方法

```sh
sh userspace/experiment-ai/models/generate_model.sh person
sh userspace/experiment-ai/models/generate_model.sh segmentation
sh userspace/experiment-ai/models/generate_model.sh face
```

第2引数を省略すると`models/source/<model>/`の元モデルを使い、なければ取得元からダウンロードします。別のファイルを使う場合は第2引数にパスを渡すか、`AI_MODEL_SOURCE`を設定します。`stedgeai`と`arm-none-eabi-objcopy`がPATHに必要です。

生成物は`models/<model>/`に置かれます。主なファイルは`network.c`、`network_ecblobs.h`、`stai_network.c`、`stai_network.h`、`network_data.xSPI2.bin`、`network_data.hex`です。生成ファイルはGit管理外です。重みの生成アドレスと書き込みアドレスは上表に合わせてください。

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
| `AI_MODEL_CUT_OUTPUT_TENSORS` | モデルごと | 出力を切り出すテンソル名。segmentationは最終Resizeの手前で切る |
| `AI_MODEL_NETWORK_ADDRESS` | モデルごと | 重みを置くxSPI2アドレス |
| `AI_MODEL_DOWNLOAD_URL` | 取得元 | ダウンロードURL |

入出力バッファはアプリが所有するため、`--no-inputs-allocation --no-outputs-allocation`で生成します。Neural-ARTのmemory poolは`my_mpools/stm32n6-app2_STM32N6570-DK.mpool`、追加設定は`user_neuralart_STM32N6570-DK.json`です。

生成に使うSTEdgeAIとビルドでリンクするランタイムは同じ版にしてください。CMakeは`ll_aton`のバージョン不一致を検出するとconfigureを止めます。
