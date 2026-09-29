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
バッファを LCD に連続表示する場合は、タスク起動前に
`TaskContext::diagnostics.inference_input_display = true` を設定します。
入力内容のUART確認も行う場合は `inference_input = true` を追加します。

```sh
cmake -S . -B build-sample-ai -DAPP_TARGET=sample-ai
cmake --build build-sample-ai --target sample-ai.elf
```

UARTには30フレームごとに `ai: input inspect` としてアドレス、サイズ、CRC、RGBサンプル、
輝度統計が出ます。LCD中央の480x480画像が動くカメラ映像になっていれば、Pipe2のDMA、
キャッシュ無効化、RGB888入力の受け渡しが継続して成立しています。診断中もNPU推論は
継続しますが、LCDはPipe1表示ではなく推論入力の確認表示になります。通常の枠表示に
戻すときは `inference_input_display = false`（デフォルト）に戻してください。

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

推論実行は、生成モデルのC API、NPUスケジューリング、推論実行を分離した構成です。

- `src/models/`：共通の `Model` インターフェースと、`person/`・`segmentation/`・
  `face/`ごとのモデル実装・Decoderを配置します。STEdgeAIが生成した `*_model_*`
  C関数名と、anchor/dequant/NMS・mask変換・座標変換などのモデル固有処理は各モデル
  namespaceに閉じ込めます。具体的な型は `uai::ai::models::<model>::Model` です。
- `src/npu_runtime/`：アプリケーション向けの`NpuRuntime` Facadeです。Taskにはモデル登録、
  初期化、推論実行だけを公開し、SchedulerとInferenceDispatcherを内部に隠します。NPU
  ドライバの初期化、preload、選択済みモデルのruntime切り替えもここで行います。
- `src/npu_runtime/scheduler/`：登録済みモデルの管理と、次に実行するモデルの選択を
  担当します。選択中Modelへの入力準備、Decoder、結果変換の委譲もこの層で行います。
  モデル選択ポリシーはこの層に閉じ込め、NPUドライバは持ちません。
- `src/npu_runtime/inference_dispatcher/`：選択済みモデルの入力準備、cache処理、動的出力、
  NPU実行、Decoder呼び出しを担当します。モデル固有の入力準備と`BoxSet`変換は、
  `Model`インターフェースを通して具体的なモデルへ委譲します。

Taskは起動時に`NpuRuntime::RegisterModel()`でperson、segmentation、faceを登録し、
`NpuRuntime::Run()`を呼ぶだけです。フレームごとのモデル順序はTaskが決定せず、Schedulerが
現在ラウンドロビンで選択します。`ModelKind`と入力形状だけを持つ`ModelDescriptor`の共通型は
`models/model.hpp`に置き、具体的なdescriptor値は各モデルの`model.cpp`が所有します。
出力数・出力サイズ・量子化値は生成STAIの`stai_network_info`から実行時に取得し、公開
モデルI/Fには持ち込みません。現在のdescriptorはSchedulerからDispatcherへ提供します。

そのため、モデル選択の条件分岐はSchedulerのbinding表へ集約し、NPUの実行手順は
NpuRuntimeとDispatcherへ集約しています。モデル選択用のCMakeオプションや`AI_MODEL_*`のビルド分岐はなく、
生成C API・後処理・3モデルのcommand blobを常に同じ構成でリンクします。生成C
ラッパーとモデル固有後処理も各`src/models/<model>/`にまとめています。

## 先に必要なもの

モデルの元ファイルは公式リポジトリから取得します。生成物はモデルごとに
大きく、STEdgeAIのバージョンにも依存するため、現在はperson、segmentation、
faceの生成済みファイルをリポジトリに保持しています。再生成する場合は次を
準備してください。

1. STM32CubeN6 パッケージ。`STM32CUBE_N6_DIR` に設定します。
2. 生成モデルと同じ版の STEdgeAI の `Middlewares/ST/AI` を用意し、
   `STEDGEAI_LIB_DIR` に設定します。現在の生成物は `atonn-v1.1.3-275` と
   `NetworkRuntime1201_CM55_GCC.a` に対応しています。
3. `stedgeai` CLI と `arm-none-eabi-objcopy` が PATH にあること。
4. [`models/README.md`](models/README.md) に記載した公式モデルを取得すること。

STEdgeAIランタイムはSTの配布物であり、サイズ・ライセンス・生成物との版依存が
あるため、リポジトリの `third_party/` にはコピーしていません。一方、ビルドに
必要なvision-models post-processingの小さなソース一式は同ディレクトリに保持して
います。クリーンなcheckout直後は、次のコマンドで依存関係とll_atonの版を確認
できます。

```sh
sh userspace/sample-ai/scripts/setup_third_party.sh \
  /opt/ST/STEdgeAI/4.0/Middlewares/ST/AI
```

引数を省略すると、`STEDGEAI_LIB_DIR`または `/opt/ST/STEdgeAI/*` から自動検出します。
依存関係が不足している場合は、CMake configureの時点で具体的な不足パスを表示して
停止します。これにより、無視された開発者固有の `third_party` コピーに依存した
ままビルドが通ることはありません。

生成された4ファイル (`network.c`, `network_ecblobs.h`, `stai_network.c`,
`stai_network.h`) と `network_data.xSPI2.bin`/`.hex` は選択したモデルの
ディレクトリに置かれます。これらはSTEdgeAIのバージョンと入力モデルに依存する
自動生成物のためGit管理対象外です。クリーンcheckoutでは、ビルド前に3モデル分の
生成コマンドを実行してください。

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
  -DSTEDGEAI_LIB_DIR=/path/to/STEdgeAI/4.0/Middlewares/ST/AI \
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
