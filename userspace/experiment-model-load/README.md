# experiment-model-load

## 状態と目的

マニフェスト生成/C++読取り、期待値/CRC検証、隔離PSRAMスロットへの転送、非同期NPU実行、画像入力、出力テンソル取得を実装している。既定は`models/generated/`にあるsegmentationモデルを使うNPU有効の単一モデル構成。`EXPERIMENT_MODEL_MULTI=ON`では、このexperiment内で生成したperson/face/segの実行コードを一度ビルドし、起動後にHeader・重み・blobを切り替える。単一入力と最大8出力に対応する。kernel/driver・middlewareや別userspaceの実装には依存しない。

単一モデル構成ではSTM32N6570-DKでRAM起動、Pipe1/2開始、PSRAM直接書き込み、manifest/CRC、ゼロ入力の出力CRC `956690df`を確認済み。3モデル切り替え・画像前処理・結果転送はホストテスト済み。relocatable API対応は未確認で、未知のモデルコードを再ビルドなしで実行する方式ではない。

## 実装済みと実行方法

```sh
make -C userspace/experiment-model-load model-fixture
cmake -S userspace/experiment-model-load -B build/experiment-model-load-host
cmake --build build/experiment-model-load-host
ctest --test-dir build/experiment-model-load-host --output-on-failure
python3 userspace/experiment-model-load/tool/manifest.py pack --help
python3 userspace/experiment-model-load/tool/manifest.py verify --help
```

`EXPERIMENT_MODEL_NPU=OFF`でモデルマニフェストを指定しないビルドでは、[tool/fixture.py](tool/fixture.py)が小さなweights/blobを作る。転送だけを試すfixtureモードとして使う。3モデル版はweights 8 MiB、blob 2 MiB、入力2 MiB、出力全体4 MiB以内。単一モデル版は入出力各512 KiB以内。

[tool/manifest.py](tool/manifest.py)のpackには重み/blobのファイル、配置アドレス、予約領域、ランタイム版、モデル種別、入出力バイト数を明示する。予約領域外・セグメント重複・アドレスオーバーフロー・入力ファイルへの上書きを拒否する。packはローカルファイル生成だけで、デバイスへ書き込まない。verifyもローカルファイルとのサイズ/CRC照合のみ。

[manifest.hpp](src/manifest.hpp)はバイト列から明示的にlittle-endian読取りを行い、C++構造体のpaddingに依存しない。Decode、Within、Matches、Verifyは別段階。実機でメモリを読む前にWithinで許可領域を検査し、リンク済みモデルの期待値とMatchesで照合する必要がある。

初版形式は1モデルにつき52バイト: UAIM、u16版=1、u16サイズ=52、u32のruntime/kind/input_bytes/output_bytes、重みとblobそれぞれのaddress/bytes/CRC32、最後に先頭48バイトのCRC32。型・テンソル形状全体・署名はまだ含まない。同じ入出力バイト数でも互換とは限らず、この形式だけで任意モデルを実行してはならない。計画10の最終マニフェスト仕様として固定したものではない。

対象は[10: モデル動的ロード](../../tmp/plans/10-model-dynamic-load.md)。マニフェスト、PSRAMステージング、対応が確認できた場合のrelocatableモデルの順に検証する。共有NPUドライバ、ai_runtime、メモリ契約、ai-load、pre-kernelは変更しない。

## このディレクトリに必要なもの

| 構成案 | 責務 |
| --- | --- |
| CMakeLists.txt / Makefile / config/ | 専用build、モデル配置とscratch/staging領域、比較条件 |
| src/ | マニフェスト照合、モデル単位の状態、ロード/検証要求、NPU完了を待った切替 |
| モデル生成設定 | 使用STEdgeAI版、ランタイム版、生成オプション、固定入力と期待出力 |
| tests/ | 形式のencode/decode、境界・オーバーフロー、サイズ・バージョン・CRC・テンソル整合 |
| 実験記録 | 書き込み計画、モデル識別子、ELF/map、UART、入力/出力CRC、所要時間 |

ツールは`tool/`内に置き、host_app統一入口へは接続しない。重みはPSRAM `0x91200000`から8 MiB、blobは`0x91a00000`から2 MiB。以前のblobアドレスから変更したため、モデルとファームウェアは再生成・再ビルドする。転送はPSRAM初期化とPipe開始後に行う。`model adopt`はD-cacheをinvalidateし、Headerと全CRCを検証してから公開する。`verified`は推論成功を意味しない。

既定の単一モデルを実機で動かすには、UARTを先に開く端末Aで次を起動する。アップローダがUARTを排他的に保持し、起動ログとPipe1/2開始行を確認してから、ST-LinkでPSRAMへ重み/blobを書き、CRC検証とゼロ入力推論を実行する:

```sh
UART_DEVICE=/dev/ttyACM0 make -C userspace/experiment-model-load model-upload MODEL_WAIT_READY=ON
```

端末AがUARTを待っている状態で、端末Bから起動する:

```sh
make -C userspace/experiment-model-load ram-run
```

端末Aに`MODEL state=verified`と`MODEL RESULT npu=done output_crc=...`が出ればロードと推論の完了。UART監視を終えるにはCtrl-Cを押す。`make monitor`や別のUARTツールを同時に起動しない。ボードのシリアル名が異なる場合は`UART_DEVICE`を実際のVCPへ合わせる。

## 三モデルのHeaderと転送

v2はUAIM/little-endian、最大448バイト。C++構造体をそのまま送らない。

| オフセット | 内容 |
| --- | --- |
| 0–7 | magic、u16版=2、u16 Header長 |
| 8–47 | runtime版、kind、入出力バイト数、weights/blobのアドレス・サイズ・CRC32 |
| 48–59 | 生成network.cのCRC32識別子、入力数=1、出力数1–8、前処理、色順、padding画素値 |
| 60–83 | float32 mean[3]、divisor[3] |
| 84以降 | 入力と各出力の40バイトdescriptor |
| 末尾4バイト | Header全体のCRC32（末尾自身を除く） |

descriptorはu8型/layout/rank/role、u32 shape[4]/bytes、float32 scale、i32 zero-point、u32 offset/reserved=0。出力offsetは32バイト境界で、重複を拒否する。型は1:uint8、2:int8、3:float32、4:int16、5:uint16、6:float16、7:int32。layoutは0:raw、1:NHWC、2:NCHW。kindは1:person、2:face、3:seg。roleは0:raw、1:boxes、2:scores、3:landmarks、4:多クラスlogits。形状の積とbytes、範囲、floatの有限性、CRCをホスト/デバイス両方で検査する。CRCは破損検出であり署名・認証ではない。

前処理は0:テンソルを直接入力、1:stretch、2:letterbox。色順は1:RGB、2:BGR、3:gray。画像から `(pixel-mean)/divisor` を作り、整数型なら `round(value/scale+zero)` を型の範囲へ飽和させる。量子化はテンソル単位で、per-axis量子化や動的形状は未対応。値は使用するモデルと学習時の前処理から決め、推測で設定しない。

モデルごとにJSONを用意する。以下は形式の例であり、STのpersonモデルの実際の出力仕様ではない。実モデルの形状・型・scale/zero-point・前処理へ置き換える。生成ヘッダーとの入力bytes・出力数/各bytesはコンパイル時にも検査するが、意味や量子化の正しさは記述者が確認する。

```json
{
	"runtime_version": 1201,
	"kind": 1,
	"input": {"type": "uint8", "layout": "nhwc", "shape": [1, 320, 320, 3], "scale": 1.0, "zero": 0},
	"outputs": [
		{"type": "int8", "layout": "raw", "shape": [1, 20, 4], "scale": 0.25, "zero": 0, "role": 1},
		{"type": "int8", "layout": "raw", "shape": [1, 20, 1], "scale": 0.01, "zero": -128, "role": 2}
	],
	"preprocessing": 2,
	"color": 1,
	"padding": 114,
	"mean": [0, 0, 0],
	"divisor": [1, 1, 1]
}
```

Headerにはファームウェアのモデルカタログと一致する記述が必要。未知のHeader、別モデルの重み/CRC、異なるランタイムを拒否する。モデルが変わったら再生成・再ビルドするが、一度ビルド済みの3モデル間はファームウェアを書き直さずに切り替えられる。

### 生成と初回起動

ホストの画像前処理はPillow/NumPyを使用する。専用venvに[tool/requirements.txt](tool/requirements.txt)を入れ、`MODEL_PYTHON`にそのPythonを指定する。

```sh
make -C userspace/experiment-model-load model-generate \
	MODEL_NAME=person MODEL_SOURCE=/path/person.tflite MODEL_DESCRIPTOR=/path/person.json
make -C userspace/experiment-model-load model-generate \
	MODEL_NAME=face MODEL_SOURCE=/path/face.tflite MODEL_DESCRIPTOR=/path/face.json
make -C userspace/experiment-model-load model-generate \
	MODEL_NAME=seg MODEL_SOURCE=/path/seg.onnx MODEL_DESCRIPTOR=/path/seg.json
make -C userspace/experiment-model-load model-package EXPERIMENT_MODEL_NPU=ON EXPERIMENT_MODEL_MULTI=ON
```

STEdgeAIは`MODEL_STEDGEAI`、STランタイムは`STEDGEAI_LIB_DIR`で指定する。3モデルは同じSTランタイム版を使う。生成物は`models/person/`、`models/face/`、`models/seg/`に置き、他userspaceの生成物を参照しない。3モデルのblobはoverlayで同じ実行アドレスへリンクし、ファームウェアbinから除外する。OBJECTのblobにリンク再配置がある場合はパッケージ化を拒否する。

最初の起動は、端末AでUARTを先に開く:

```sh
make -C userspace/experiment-model-load model-upload EXPERIMENT_MODEL_NPU=ON EXPERIMENT_MODEL_MULTI=ON \
	MODEL_NAME=person MODEL_IMAGE=/path/image.jpg MODEL_RESULT_DIR=/path/results/person \
	UART_DEVICE=/dev/ttyACM0 MODEL_WAIT_READY=ON \
	MODEL_PYTHON=/path/venv/bin/python STM32_PROGRAMMER_CLI=/path/STM32_Programmer_CLI
```

UART待機後、端末Bで `make -C userspace/experiment-model-load ram-run EXPERIMENT_MODEL_NPU=ON EXPERIMENT_MODEL_MULTI=ON` を実行する。STランタイムの設定は両端末で同じにする。

### 起動済みボードへの再ロード

監視をCtrl-Cで閉じ、他のUARTモニターを止めてから実行する。`MODEL_WAIT_READY`の既定はOFFで、起動ログを再度要求しない。

```sh
make -C userspace/experiment-model-load model-upload EXPERIMENT_MODEL_NPU=ON EXPERIMENT_MODEL_MULTI=ON \
	MODEL_NAME=face MODEL_IMAGE=/path/face-image.jpg MODEL_RESULT_DIR=/path/results/face \
	UART_DEVICE=/dev/ttyACM0 MODEL_PYTHON=/path/venv/bin/python \
	STM32_PROGRAMMER_CLI=/path/STM32_Programmer_CLI
```

segは`MODEL_NAME=seg`へ変更する。画像の代わりに`MODEL_INPUT=/path/input.bin`で準備済みテンソルを渡せる。ゼロ入力試験は`MODEL_INPUT_ZEROS=ON`を明示する。`MODEL_RUN=OFF`ならモデルのロード・CRC確認だけを行う。

転送順序は停止確認→ST-Linkで重み/blob/入力をPSRAMへ書込み→UART同期→Headerを64バイトずつACK付き転送→adopt/CRC検証→入力CRC検証→推論。UART単独の[tool/upload.py](tool/upload.py)ではモデル本体・入力も順序付きhexチャンクで送る。未検証/途中の入力を実行しない。Header・入力受信は10秒無通信で破棄する。NPU使用中の再転送を拒否し、停止を確認できない場合はロックを保持する。

### 結果の扱い

推論後の全出力をUARTで64バイトずつ取得し、報告された全体CRCと照合する。出力4 MiBの場合は115200 baudで数分以上かかるため、実行時間と結果転送時間は別に扱う。結果ディレクトリには`header.bin`、`result.bin`、`result.json`、`tensors.npz`を保存する。JSONにはkind・型・shape・offset・量子化と、元画像/縮小画像サイズ・余白を残す。整数の出力は `(q-zero)*scale` で実数へ戻す。

person/faceのNMS、YOLOX/BlazeFaceのアンカー復元、ランドマーク変換はまだ接続していない。生テンソルを保存してモデル固有の後処理をホストで追加できる構成にする。segでrole=4、batch-one NHWC/NCHWの多クラスlogitsを指定した場合はargmaxのクラスIDマスク`classes_<index>.png`も保存する。マスクは元画像サイズへ戻す前のモデル出力座標で、letterboxの除去・復元は別処理。

| PSRAM領域 | 用途 |
| --- | --- |
| `0x90400000` / 8 MiB | activation |
| `0x90c00000` / 4 MiB | 全出力（テンソル間padding込み） |
| `0x91000000` / 2 MiB | 入力 |
| `0x91200000` / 8 MiB | 重み |
| `0x91a00000` / 2 MiB | blob |

カメラはSRAMバッファのまま使う。固定PSRAMカメラプロファイルは入力と衝突するため3モデル版では拒否する。画像入力はホストからの固定画像/テンソルであり、カメラの連続DMAから直接推論する取得・解放処理はまだ接続していない。

## 単一モデル版（既定）

既定は`EXPERIMENT_MODEL_MULTI=OFF`で、ここに記載する単一モデル経路を使う。3モデル版の入力・結果転送とは別で、ゼロ入力の疎通確認用。

STEdgeAIのST AI C APIで生成できる単一入力・単一出力モデルを用意する。weights 8 MiB、blob 2 MiB以内、入力と出力は各512 KiB以内。[tool/model.py](tool/model.py)はローカルの[model.mpool](config/model.mpool)を使い、PSRAMとSRAM5/6へNPU作業領域を予約する。カメラのSRAM3/4やアプリのSRAM1/2を生成ツールに割り当てない。

以下のモデルパスと入出力バイト数は例で、使用するモデルに合わせる。入力・出力の個数とバイト数は生成ヘッダーに対するコンパイル時検査でも照合する。runtime-versionは使用する`NetworkRuntime<version>_CM55_GCC.a`の版と合わせる。

```sh
make -C userspace/experiment-model-load model-generate \
	MODEL_SOURCE=/path/to/small-model.onnx \
	MODEL_INPUT_BYTES=192 MODEL_OUTPUT_BYTES=16 MODEL_RUNTIME_VERSION=1201 \
	MODEL_STEDGEAI=/opt/ST/STEdgeAI/4.0/Utilities/linux/stedgeai

make -C userspace/experiment-model-load model-package \
	EXPERIMENT_MODEL_NPU=ON EXPERIMENT_MODEL_MULTI=OFF \
	STEDGEAI_LIB_DIR=/opt/ST/STEdgeAI/4.0/Middlewares/ST/AI
```

生成物は`models/generated/`へ置く。blobの実行アドレスは`0x91a00000`。モデルデータはNOLOADで、起動後にアップローダから転送する。

UARTを開く端末A。NPU有効時は起動を確認後、同じプロセスがST-Link HOTPLUGでバイナリをPSRAMへ直接書いてCRC検証と推論を行う:

```sh
UART_DEVICE=/dev/ttyACM0 make -C userspace/experiment-model-load model-upload \
	EXPERIMENT_MODEL_NPU=ON EXPERIMENT_MODEL_MULTI=OFF MODEL_WAIT_READY=ON \
	STM32_PROGRAMMER_CLI=/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.500.202603051304/tools/bin/STM32_Programmer_CLI \
	STM32_PROGRAM_SERIAL=004300223234511233353533
```

`MODEL_RUN=OFF`を指定するとPSRAMへのロードとmanifest/CRC検証までで終了し、`model stat`を表示する。推論まで行う場合は既定の`MODEL_RUN=ON`を使う。UART待機後に端末B:

```sh
make -C userspace/experiment-model-load ram-run EXPERIMENT_MODEL_NPU=ON EXPERIMENT_MODEL_MULTI=OFF
```

STランタイムのパスは両端末で同じ設定を使う。NPU有効時の`model-upload`はパッケージ生成後、UARTで起動確認と実行停止を行い、ST-LinkのSWD HOTPLUGでweights/blobをマップ済みPSRAMへ直接書き込み、manifest/CRCを確認してからゼロ埋め入力で推論する。UART経由でモデル本体をhex転送しない。`MODEL RESULT npu=done output_crc=xxxxxxxx elapsed_ms=... input=zeros`を待つ。既知の同条件の出力CRCがある場合は`MODEL_EXPECTED_OUTPUT_CRC=0x12345678`を指定して照合できる。実際のNOR版との比較結果は未取得。

モデルを変えたら`model-generate`とアプリ再ビルドが必要。既存モデルのNOR領域には書き込まない。NPUビルドのマニフェストはビルドから自動生成するため、`EXPERIMENT_MODEL_MANIFEST`との併用は拒否する。

NPU無効ビルドで任意の転送データを試す場合は、`EXPERIMENT_MODEL_MANIFEST`、`MODEL_WEIGHTS`、`MODEL_BLOB`をすべて明示する。ホスト側のサイズ/CRC/スロット検査はUARTを開く前に行う。

## モデルコマンドと状態

| コマンド | 動作 |
| --- | --- |
| `model stat` | 転送状態、受信バイト数、NPU状態、最後の出力CRCと所要時間 |
| `model header <offset> <hex>` | v2 Headerを順序付きで分割受信 |
| `model begin` / `model adopt` | 分割受信したHeaderで開始/直接PSRAM採用 |
| `model adopt <manifest-hex>` | 直接配置されたPSRAMスロットをmanifestとCRCで検証して公開 |
| `model begin <manifest-hex>` | リンク済みモデルの期待値と照合し、転送開始。NPU使用中は拒否 |
| `model chunk weights\|blob <offset> <hex>` | 各セグメントを順序付きで受信。UARTでは最大512バイト/要求 |
| `model commit` | サイズ・全CRC・キャッシュcleanを確認して公開 |
| `model verify` | 公開済みモデルの全CRCを再検証。不一致なら無効化 |
| `model input begin <bytes> <crc32-hex>` | モデル入力サイズとCRCを宣言 |
| `model input chunk <offset> <hex>` / `model input commit` | 入力を受信・CRC検証 |
| `model input adopt <bytes> <crc32-hex>` | ST-Linkで置いた入力のキャッシュ同期・CRC検証 |
| `model run` | 再CRC検証後、検証済み入力による非同期推論を開始 |
| `model result <offset> <bytes>` | 完了済み出力を最大64バイトずつ取得 |
| `model abort` | NPUの停止・登録解除を確認してから転送/検証状態を破棄 |

推論中もカメラ/ISP/コンソールのループを継続する。NPUの進行は1回のTickにつき最大1エポック、タイムアウトは5秒。正常終了または停止確認後だけスロットを解放する。停止APIが失敗した場合は`npu=faulted`のままロックを保持し、`model abort`で停止を再試行するまで再転送を許可しない。未ロード・転送途中・CRC不一致のデータを実行しない。電源断後は未ロード状態から始める。

ホスト試験にはマニフェスト/生成契約/パッケージ、実オブジェクトのblob抽出と再配置拒否、複数モデルのblob配置、非同期実行の正常/故障/停止失敗/時刻折り返し、アップローダのCRC比較とデバイスを開く前の拒否を含む。NPUのホスト試験は偽バックエンドで制御部分を確認するもので、ST API互換性やハードウェア動作の代用ではない。

## pre-kernel等への接続要件（今は変更しない）

- このexperiment内の`src/camera_runtime/`と`config/`を使う。実機ではXSPI初期化、PSRAM/NORのマッピング、キャッシュ、RIF、NPUクロック/IRQを確認する。
- PSRAMへの転送は初期化完了後に行う。ロード後のram-runや再初期化で内容が失われない起動順を検証する。単に転送アドレスをNORから変えるだけでは成立しない。
- ステージング領域は実験の生成メモリ配置で予約する。フレームバッファ・NPU作業領域・コードと重ならないことを生成時とロード時に検査する。
- 重み参照先が生成コード/blobに固定される場合、PSRAM用に再生成する。ロード先だけを書き換えて代用しない。
- pre-kernelに初期化順や待機経路の変更が必要なら、具体的な順序・対象・理由をここへ追記して保留する。
- relocatableは使用するSTEdgeAI/STM32N6で生成とロードAPIの対応を確認してから着手する。未対応ならPSRAM段階で止める。

## 実装と自動確認の順序

1. マニフェストの形式・範囲・期待値照合をホストで固める。計画中のアドレス例をそのまま使わず、実際の配置から決める。
2. 正常モデルと不一致モデルを識別する。不一致は当該モデルだけを使用不可にし、カメラと他モデルの継続を確認する。
3. ヘッダとサイズの一致だけでは、同サイズの古い重みや破損は検出できない。起動時に検査する範囲と、全CRC検証を必要とする範囲を明示する。
4. PSRAMロードはNPU利用中の領域への書込みを禁止し、転送・検証・キャッシュ処理完了後に公開する。途中失敗や電源断後に未検証の領域を実行しない。
5. 同じ固定入力でNOR版とPSRAM版の結果を比較し、Pipe2 DMAとNPUが同時にPSRAMへアクセスする際のエラーと性能を測定する。
6. 対応確認後にrelocatableを追加する。後処理の種別・テンソル形状・ランタイムの互換性を満たさないモデルは実行前に拒否する。

## 利用者が実施するテスト

実装者がホストテスト、書込み範囲、リンク結果、起動とPipe1/2を確認してから実施する。UARTを先に`model-upload`で開き、別端末で`make -C userspace/experiment-model-load ram-run`を実行する。

| 操作・準備 | 合格条件・記録 |
| --- | --- |
| 正常モデルで起動し、モデル一覧と検証結果を確認する | 意図したモデルが有効で、両Pipeと推論が進む |
| 専用テスト用マニフェストでサイズ/版等の不一致を試す | 対象モデルだけが拒否される。他モデルとカメラは継続する |
| 専用領域の同サイズ不正データに全CRC検証を行う | 不一致を検出する。ヘッダ検証だけの結果を完全一致と表示しない |
| 固定入力でNOR版とPSRAM版を実行する | 同一モデル・同一実行条件の出力CRCが一致し、CSI/NPUエラーが増えない |
| 実装済みの転送中断試験と再ロードを行う | 未検証モデルを実行せず、再ロード後に復帰できる |
| PSRAM版を電源断して再起動する | 消失したモデルを使用せず、未ロード状態または定義したフォールバックになる |
| relocatable対応後、互換モデルと非互換モデルをロードする | 互換モデルは再ビルドなしで推論可能。形状等の不一致は実行前に拒否される |
| 通常ai-appへ戻す | 必要なモデルをai-loadしてからram-runし、Pipe1/2と通常推論が戻る |

不正データ試験は、まずホストテストと実験用領域で行う。既存NORモデルを壊す試験は行わない。NOR書込みが必要になった場合は、対象アドレス・サイズ・復元元・復元手順を事前に確認する。
