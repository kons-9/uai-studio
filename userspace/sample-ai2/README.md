# sample-ai2: sample-ai から再コピーした Neural-ART 推論

## face 表示用アプリ（新しい3レーンランタイム）

sample-ai2は現在faceモデルを`src/middleware/ai_runtime/`経由で実行します。起動後は
Pipe1のLCD表示とPipe2のフレーム取得を従来どおり維持し、前処理CPUタスクが
フレームを取り込み、NPUタスクがSTAI非同期実行（IRQ待ち・epoch継続）、
後処理CPUタスクがface検出結果をLCDの青枠キューに渡します。faceのモデル重みと
command blobが必要です。ビルド先は`build-sample-ai2-person`です。

まずperson/faceのモデル生成物とSTEdgeAI、CubeMXソース、FSBLを準備してから
`make APP_TARGET=sample-ai2 build`を使います。`setup`と`ai-load`はperson/faceを
初期化失敗時はカメラ表示を継続し、推論のみ無効にします。UARTには
`ai: face pipeline enabled (pre/npu/post)`が出ます。各ステップのID/時刻は
`diagnostics.inference_trace`を有効にしたときだけ記録します。
詳細とホストテストは[ランタイムの説明](src/middleware/ai_runtime/README.md)を参照。
実機では起動、Pipe1/2開始、face推論、face枠生成まで確認済みです。

このアプリは sample-ai をコピーした再構築版です。以下のコピー元の実機計測値は
sample-ai2 の検証結果ではありません。sample-ai2 のモデル生成物は同梱していません。
リポジトリ直下で、初回は `make APP_TARGET=sample-ai2 setup`（モデル生成と
CubeMX 生成を含む）、以降は `make APP_TARGET=sample-ai2 build` を使います。
face表示構成の既定のビルド先は `build-sample-ai2-person` で、sample-ai の
`build` と共用しません。以下の3モデル構成・旧ビルド先の記述は旧経路の説明です。
ボード書き込みの前に UART を用意してください。

`sample-ai` は、STM32N6570-DK の Neural-ART NPU で量子化 AI モデルを推論する
最小構成です。一般的な意味での GPU (NeoChrom) ではなく、AI 推論専用の NPU
を使います。face推論はカメラのPipe2 RGB888を入力にし、`sample-camera-lcd` の
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

推論の投入周期は20 msです。faceモデルは128x128入力のため、Pipe2の480x480画像を
前処理CPUで縮小してからNPUへ渡します。NPU=1 GHzで
初回ウォームアップ後のNPU実行は実機でおおむね30 msです（Thread Monitor平均約30.2 ms、
STEdgeAI生成時のCPU込み推定約28.7 ms）。以前の10〜12 msという記載は現モデル／入力形状と
一致しないため更新しています。詳細UART・入力テンソル走査を一時的に有効にする場合は、
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
バッファのサイズ・alignment・region metadataは`MemoryAllocator`が`Buffer` descriptor
として返し、camera driver側でraw addressから再構築しません。Pipe2のISRとタスク間では
capture sequenceに加えてslot lease tokenも照合するため、古いqueue messageが再利用済み
slotを誤ってclaim/releaseすることを防ぎます。
`--no-outputs-allocation` で生成したモデルはこの出力領域へ切り替わり、旧生成物は
固定出力へフォールバックします。

### 推論入力画像の確認

faceモデルの推論元は Pipe2 の 480x480 RGB888 です。実際に NPU へ渡す
バッファを LCD に連続表示する場合は、タスク起動前に
`TaskContext::diagnostics.inference_input_display = true` を設定します。
入力内容のUART確認も行う場合は `inference_input = true` を追加します。

```sh
cmake -S . -B build-sample-ai2 -DAPP_TARGET=sample-ai2
cmake --build build-sample-ai2 --target sample-ai2.elf
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

- `src/models/`：モデルごとの生成C API、入力前処理、Decoder、座標変換を配置します。
- `src/middleware/ai_runtime/`：Preprocess CPU、NPU、Postprocess CPUの3レーンを管理する
  `AiFuture`、Scheduler、Dispatcherを配置します。
- `src/task/pipeline_task.cpp`：フレーム受信、前処理、NPU実行、後処理のタスクを
  起動し、モデル出力をLCDの枠キューへ渡します。

フレームは `FrameEntry` が受信してSchedulerへ投入します。Preprocessワーカーが入力を
準備し、NPUワーカーがSTAI非同期実行とIRQ待ちを行い、Postprocessワーカーがface検出
結果を変換します。

### パイプライン設計

`AiFuture`は推論単位の非同期状態を保持し、`is_ready()`が真のときだけ
`Evaluate()`で一つのステップを実行します。返り値の`AiRuntimeResult`は既存の
`common::Error`と、成功時のみ有効な次の実行先・待ち条件を持ちます。
`completed`が真なら次のステップはありません。待ち条件ゼロは即時実行、
複数条件の場合は`WaitMode`で全件待ちかいずれか一件待ちかを指定します。
CPU処理とNPU処理の投入先はランタイムが決定し、モデルは実行先キューやRTOSの
待ち方を知りません。`src/middleware/ai_runtime/`に3キューとScheduler、各キューのDispatcher、
Futureごとの通知、ステップ前後のトレースフックを独立実装しています。後処理完了まで
Futureと入出力バッファを保持する責務は利用側に残ります。RTOSタスク、モデル、
既存の推論ディスパッチャと実機ログにはまだ接続していません。

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
sh userspace/sample-ai2/scripts/setup_third_party.sh \
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
sh userspace/sample-ai2/models/generate_model.sh person \
  /path/to/st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite

sh userspace/sample-ai2/models/generate_model.sh segmentation \
  /path/to/deeplab_v3_mobilenetv2_05_16_320_fft_qdq_int8.onnx

sh userspace/sample-ai2/models/generate_model.sh face \
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
cmake -S . -B build-sample-ai2 \
  -DAPP_TARGET=sample-ai2
cmake --build build-sample-ai2 --target sample-ai2
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

先に `make APP_TARGET=sample-ai2 setup` でモデルと専用 CubeMX 出力を
生成してください。手動で再設定する場合は次のように専用のビルドディレクトリを
使えます。

```sh
cmake -S . -B build-sample-ai2 \
  -DAPP_TARGET=sample-ai2 \
  -DSTM32CUBE_N6_DIR=/path/to/STM32Cube_FW_N6_V1.3.0 \
  -DSTEDGEAI_LIB_DIR=/path/to/STEdgeAI/4.0/Middlewares/ST/AI \
  -DCUBEMX_OUTPUT_DIR="$PWD/build-sample-ai2/cubemx" \
  -DCUBEMX_IOC="$PWD/userspace/sample-ai2/config/stm32n6570-dk-sample-ai.ioc"

cmake --build build-sample-ai2 --target sample-ai2
```

`CUBEMX_OUTPUT_DIR` は、sample-ai2 の IOC から生成した出力を指定してください。

`ram-run` の既定スタック上限は sample-ai2 のリンカ配置に合わせた
`0x34100000` です。

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
`build-sample-ai2/userspace/sample-ai2/network_blobs_*.hex` をモデルごとの
固定アドレス（person: `0x70500000`、segmentation: `0x70560000`、
face: `0x70580000`）へ書き込んでください。主なビルド成果物は次の通りです。

- `sample-ai2.bin`: AXISRAM1へロードするアプリケーション本体
- `network_blobs_person.hex` / `network_blobs_segmentation.hex` /
  `network_blobs_face.hex`: それぞれ絶対アドレス付きcommand blob

command blobの書き込み後に`ram-run`を実行します。モデル初期化はNORの
memory-mapped化後に行われるため、外部Flash上のblobを参照できます。

## 外部Flashからの起動

FSBL、LRUN形式のアプリケーション、モデル重み、command blobをまとめて
書き込む場合は、プロジェクトルートで次を実行します。

```sh
make APP_TARGET=sample-ai2 flash
```

FSBLは、動作確認済みのSTM32N6570-DK公式サンプル由来のイメージを
[`fsbl/stm32n6570-dk-ai_fsbl.hex`](fsbl/stm32n6570-dk-ai_fsbl.hex)として管理しています。
元の同梱ファイルは`ref/STM32N6_Survivor_Detection/Binaries/ai_fsbl.hex`です。
`make flash`はこのIntel HEXを外部FlashのFSBL領域へ書き込みます。

このターゲットはFSBLを`0x70000000`、アプリケーションを`0x70100000`へ書き込み、
person/segmentation/faceの重みとcommand blobもそれぞれの絶対アドレスへ書き込みます。
書き込み後にリセットして外部Flashから起動するには、STM32N6570-DKのブート設定を
外部Flash起動（BOOT0: 1-2、BOOT1: 1-2）にしてください。RAM起動へ戻す場合は
ブート設定を元に戻して`make APP_TARGET=sample-ai2 ram-run`を実行します。

## コピー元から継承している範囲

アプリケーション、NPU初期化、NPU cache/RAM有効化、NORメモリのmemory-mapped
設定、STEdgeAIランタイムのリンク指定はコピー元の設計を継承しています。
sample-ai2 用のモデル生成物と CubeMX 出力はそれぞれ別に準備します。
