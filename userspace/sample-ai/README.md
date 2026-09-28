# sample-ai: STM32N6570-DK Neural-ART 推論

`sample-ai` は、STM32N6570-DK の Neural-ART NPU で量子化 AI モデルを推論する
最小構成です。一般的な意味での GPU (NeoChrom) ではなく、AI 推論専用の NPU
を使います。person推論はカメラのPipe2 RGB888を入力にし、`sample-camera-lcd` の
表示と同じカメラ経路で動作します。推論結果のobjectnessと枠情報はUARTにも表示
できます。

## カメラ推論経路

カメラ表示は `DCMIPP_PIPE1` の RGB565 800x480、推論入力は `DCMIPP_PIPE2` の
RGB888（person: 480x480、segmentation: 320x320、face: 128x128）を使用します。
Pipe2は800x480全体をアスペクト比維持でモデル入力内へletterboxし、実画像領域は
それぞれ480x288、320x192、128x77です。上下の余白を含む正方形テンソルをPSRAM上の
推論バッファへDMAします。

checked-inのperson、segmentation、face生成物は、`--no-inputs-allocation
--no-outputs-allocation`で生成してPipe2入力とallocator出力を受け取ります。
実行時に`MemoryAllocator`が返すPipe2入力バッファと出力バッファをSTAIへ接続し、
固定入力アドレスへのコピーは使用しません。モデル内部のactivationは、Neural-ARTが
生成時にmemory poolへ割り当てるNPU内部領域です。

UARTには `pipe2 frame queued`、`pipe2` イベント数、ドロップ数を出力します。CSI
エラーが発生してもPipe1のフレームsequenceが継続するか、`recovery` が増えないかを
合わせて確認してください。

推論の投入周期は20 msです。実機でのpersonモデルは初回ウォームアップ後、NPU実行が
おおむね10〜12 msでした。詳細UART・入力テンソル走査を一時的に有効にする場合は、
`TaskContext::diagnostics` の `inference_trace` と `inference_input` を設定します。
FPSだけを測る場合は `inference_fps` のみを設定してください。通常はUARTが推論を
妨げないよう、すべてfalse（ゼロ初期値）にします。

### CSIエラー／カメラ再起動の切り分け結果（2026-09-25）

実機UARTで、Pipe2のみ動作させてNPUを止めた場合はCSIエラーが再現せず、Pipe2と
NPUを同時動作させた場合に `ESOTSYNCDL*`、`SYNCERR`、`CCFIFO` が再現した。
したがって、カメラ配線だけでなく、sample-ai固有のNPU実行経路とCSI/AXI帯域の
同時使用が再起動の誘因である。FPSを20に下げてもエラーは残ったため、FPSだけでは
根本対策にならない。

ref/と同じく、sample-aiもPipe2のPSRAMバッファをNPUのユーザー入力へ直接渡す
生成設定に更新済みである。CSIエラー割込みの再アームは行わず、フレーム停止を
監視するフェイルセーフ復旧を残す。

### 推論バッファの所有権

allocatorはPipe2の入力とモデル出力を1つの推論スロットとして管理します。
各スロットは32 byte境界で、入力領域の後ろにモデル出力領域を確保します。
推論中はスロットをNPUが所有し、NPU完了とキャッシュ無効化後に再利用します。
`--no-outputs-allocation` で生成したモデルはこの出力領域へ切り替わり、旧生成物は
固定出力へフォールバックします。

### 推論入力画像の確認

personモデルの推論入力は Pipe2 の 480x480 RGB888 です。実際に NPU へ渡す
バッファを LCD に連続表示するには、configure 時に次を指定します。

```sh
cmake -S . -B build-sample-ai \
  -DAPP_TARGET=sample-ai \
  -DAI_INFERENCE_INPUT_DISPLAY_DIAGNOSTIC=ON
cmake --build build-sample-ai --target sample-ai.elf
```

UARTには30フレームごとに `ai: input inspect` としてアドレス、サイズ、CRC、RGBサンプル、
輝度統計が出ます。LCD中央の480x480画像が動くカメラ映像になっていれば、Pipe2のDMA、
キャッシュ無効化、RGB888入力の受け渡しが継続して成立しています。診断中もNPU推論は
継続しますが、LCDはPipe1表示ではなく推論入力の確認表示になります。通常の枠表示に
戻すときは `OFF`（デフォルト）に戻してください。

## モデル

sample-aiはperson、segmentation、faceの3モデルを常に同時にリンクし、起動時に
各モデルのEC command blobをランタイムRAMへ一度だけ展開します。切り替え時はNPUの
モデルコンテキストを選択するだけなので、モデルごとのNOR再読み出し・再初期化を
行いません。単一モデルだけをリンクするビルド構成は廃止しています。

ここでいうランタイムRAMは、生成コードの`ECBLOB_RUNTIME_SECTION`にある実行用
command bufferです。NPU activation用のAXISRAM3-6を3モデル分重複確保する方式では
なく、生成済みmpoolのactivation領域を各モデルで共有します。したがって、現在の
構成は「command blobを全モデル分RAM常駐」「activationは実行中モデル分を共有」です。

| モデル | 重みの配置アドレス | サイズ |
| --- | ---: | ---: |
| `person` | `0x70380000` | 約1.15 MiB |
| `segmentation` | `0x70600000` | 約0.87 MiB |
| `face` | `0x70800000` | 約0.10 MiB |

- `person`: ST YOLOX Nano、480x480、人物検出
- `segmentation`: DeepLabV3 MobileNetV2、320x320、人物セグメンテーション
- `face`: BlazeFace Front、128x128、顔検出

モデル生成の対応表と公式取得元は
[`models/README.md`](models/README.md) にまとめています。

ST公式のモデル取得元、モデルファイル名、ライセンスと利用条件は
[`models/README.md`](models/README.md) に記載しています。

### 推論ソフトウェアの構成

推論実行は、生成モデルのC APIとアプリケーションの責務を分離した3層構成です。

- `src/models/`：共通の `Model` インターフェースと、`person/`・`segmentation/`・
  `face/`ごとのモデル実装を配置します。STEdgeAIが生成した `*_model_*` C関数名は
  各モデルnamespaceに閉じ込めます。具体的な型は
  `uai::ai::models::<model>::Model` です。Faceのanchor/dequant/NMSと座標変換も
  `face/face_decoder.cpp`に閉じています。
- `src/npu_scheduler/`：`NpuDriver`の初期化、command blobのpreload、モデル選択、
  入出力バインド、NPU実行と完了待ちを管理します。動的切り替え時もここで
  `SelectModel()`を呼ぶだけで、`ModelManager::Initialize()`をやり直しません。
- `src/model_manager/`：モデルの入力変換、出力所有権、person/segmentationの出力処理と
  共通`BoxSet`への変換を担当します。Face固有のC後処理APIは直接参照せず、decoder結果
  だけを共通結果へ詰め替えます。

`ModelManager`に残す責務は、アプリケーションから見た推論のライフサイクル、現在の
モデル状態、allocator/cacheとの入出力バインド、モデル固有出力の共通`BoxSet`への
変換です。`ModelKind`と入力形状だけを持つ`ModelDescriptor`の共通型は
`models/model.hpp`に置き、具体的なdescriptor値は各モデルの`model.cpp`が所有します。
出力数・出力サイズ・量子化値は生成STAIの`stai_network_info`から実行時に取得し、公開
モデルI/Fには持ち込みません。descriptorは生成モデルの実体を所有する
`ModelManager`から現在のモデルに対して取得します。

そのため、モデル切り替えやNPUの実行順序に関する条件分岐はschedulerのbinding表へ
集約しています。モデル選択用のCMakeオプションや`AI_MODEL_*`のビルド分岐はなく、
生成C API・後処理・3モデルのcommand blobを常に同じ構成でリンクします。生成C
ラッパーとモデル固有後処理も各`src/models/<model>/`にまとめています。

## 先に必要なもの

モデルの元ファイルは公式リポジトリから取得します。生成物はモデルごとに
大きく、STEdgeAIのバージョンにも依存するため、現在はperson、segmentation、
faceの生成済みファイルをリポジトリに保持しています。再生成する場合は次を
準備してください。

1. STM32CubeN6 パッケージ。`STM32CUBE_N6_DIR` に設定します。
2. `stedgeai-lib`。STEdgeAI の配布物、または上記公式リポジトリの
   `Middlewares/stedgeai-lib` を `STEDGEAI_LIB_DIR` に設定します。
3. `stedgeai` CLI と `arm-none-eabi-objcopy` が PATH にあること。
4. [`models/README.md`](models/README.md) に記載した公式モデルを取得すること。

生成された4ファイル (`network.c`, `network_ecblobs.h`, `stai_network.c`,
`stai_network.h`) と `network_data.xSPI2.bin`/`.hex` は選択したモデルの
ディレクトリに置かれます。

## モデル生成

```sh
sh userspace/sample-ai/models/generate_model.sh person \
  /path/to/st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite

sh userspace/sample-ai/models/generate_model.sh segmentation \
  /path/to/deeplab_v3_mobilenetv2_05_16_320_fft_qdq_int8.onnx

sh userspace/sample-ai/models/generate_model.sh face \
  /path/to/blazeface_front_128_quant_pc_ff_od_wider_face.tflite
```

生成設定は `models/user_neuralart_STM32N6570-DK.json` と、NPU RAM/XSPI2 の
アドレスを記述した `models/my_mpools/stm32n6-app2_STM32N6570-DK.mpool` です。
モデルごとのXSPI2重みアドレスは生成スクリプトが選択します。personは
`0x70380000`、segmentationは`0x70600000`、faceは`0x70800000`です。
生成時のアドレスとFlash書き込みアドレスは一致させる必要があります。
別の配置にする場合は`AI_MODEL_NETWORK_ADDRESS=0x...`を指定してください。
STEdgeAI と `stedgeai-lib` のバージョンは必ず一致させてください。生成 C
ファイル自身にもランタイムバージョンのチェックがあります。

STEdgeAIの主な調整項目は次の通りです。

- `--optimization time|ram|balanced`：推論速度とRAM使用量のトレードオフ。
- `--input-data-type` / `--output-data-type`：量子化I/Oの型。
- `--inputs-ch-position` / `--outputs-ch-position`：CHW/HWCの切り替え。
- `--memory-pool`：Activationを置くRAM領域と重みの外部メモリ領域。
- `--c-api st-ai|legacy`：`--allocate-activations` や `--allocate-states` を使う場合は
  `st-ai` が必要です。現在のsample-aiではActivationを静的配置するため、通常は変更しません。
- `--split-weights`、`--address`：重みの分割や外部Flash配置を調整します。

現在のsample-aiでは、入力・出力をアプリ側で所有するため、生成時の
`--no-inputs-allocation --no-outputs-allocation` を基本設定にします。

sample-ai専用リンカスクリプトは、生成されたcommand blobをXSPI2 NORの
`0x70500000`へ配置し、アプリケーション本体だけをAXISRAM1へ置きます。
AXISRAM2-6はNPU用に残します。モデルがXSPI1を使う場合にも対応できるよう、
実行時にDKのPSRAMとNORのmemory-mappedモードを初期化します。
これは `sample-hello-world` のリンカスクリプトを変更するものではありません。

### モデル切り替え

```sh
cmake -S . -B build \
  -DAPP_TARGET=sample-ai
cmake --build build --target sample-ai
```

起動時に`ai: model preloaded=segmentation`と
`ai: model preloaded=face`がUARTへ出ます（初期モデルpersonは通常の初期化ログに
含まれます）。切り替え時には`ai: model switched to ...`が出ますが、command blobの
再ロードログは出ません。Pipe2はperson用の480x480テンソル（有効画像480x288）で
常時動作し、segmentation/faceではallocatorの共有scratch領域を使って同じ有効画像を
320x320/128x128へCPU letterboxします。そのため、モデル切り替えのたびにカメラを
停止・再起動せず、推論結果はperson/faceの枠とsegmentationマスクを別々に保持して
LCD合成します。表示色はperson=赤、face=青、segmentation=緑です。

## 後で行う configure/build

既存の sample-hello-world 用 CubeMX 出力を使う場合は、次のように
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

`CUBEMX_OUTPUT_DIR` は、実際に動作確認済みの CubeMX 出力を指定してください。

後で `ram-run` を使う場合は、sample-aiのスタック上限に合わせて
`STM32_RAM_STACK=0x34100000` も指定してください。

## 重みの書き込み

モデル重みはアプリケーション ELF に入らず、モデルごとに次のXSPI2アドレスを
参照します。生成したモデルの`network_data.hex`を、対応するアドレスへ
STM32CubeProgrammerとDK用external loaderで書き込んでください。

- `person`: `0x70380000`
- `segmentation`: `0x70600000`
- `face`: `0x70800000`

生成時のアドレスとFlash書き込みアドレスは一致させる必要があります。

## command blobの書き込み

ビルド後、command blobはアプリケーションRAMイメージとは別に生成されます。
`build/userspace/sample-ai/network_blobs.bin`をDK用external loaderで
`0x70500000`へ書き込んでください。ビルド成果物は次の3つに分かれます。

- `sample-ai.bin`: AXISRAM1へロードするアプリケーション本体
- `network_blobs.bin`: XSPI2へ書き込むcommand blobのバイナリ
- `network_blobs.hex`: 同じblobの絶対アドレス付きIntel HEX

command blobの書き込み後に`ram-run`を実行します。モデル初期化はNORの
memory-mapped化後に行われるため、外部Flash上のblobを参照できます。

## sample-aiだけで完結している範囲

アプリケーション、NPU初期化、NPU cache/RAM有効化、NORメモリのmemory-mapped
設定、STEdgeAIランタイムのリンク指定はすべて `userspace/sample-ai/` に収めて
います。既存の `sample-hello-world` CubeMX生成物は読み込みますが、sample-ai用に生成し直す
必要はありません。

詳細な依存関係と、sample-ai外を変更していないことの記録は
[`OUTSIDE_CHANGES.md`](OUTSIDE_CHANGES.md) にあります。
