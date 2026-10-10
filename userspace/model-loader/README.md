# model-loader

STM32N6570-DK向けの独立したuserspaceアプリ。登録済みのモデル実行コードを使い、起動後にHeader・重み・command blob・入力テンソルをPSRAMへロードする。未知のONNX/TFLiteや実行コードを再ビルドなしで動かすrelocatable loaderではない。

## 対応範囲

- 既定はNPU有効、person/face/segのカタログ構成。単一入力、固定形状、最大8出力。
- モデル名と件数は`MODEL_LOADER_MODELS`で指定する。異なるモデルが同じ`kind`を持っていてもよい。実行APIは登録Header全体の一致で選択する。
- 複数登録できるが、同時に有効なのは1モデル。weights/blob/input/outputは共有スロット。転送失敗時に旧モデルを維持する二重スロット方式ではない。
- 未検証データを実行せず、NPU使用中の書き換えを拒否する。停止確認に失敗した場合は領域のロックを保持する。
- camera/ISP/display/consoleはアプリ内のローカル実装。別experimentや`kernel/driver`・`kernel/middleware`へ依存しない。共有pre-kernel/μT-Kernel、ベンダーHAL/BSP/ISP、ビルド基盤は使用する。

モデルの生成物とdescriptorは`models/<name>/`、意味記述は同じディレクトリの`semantics.json`に置く。既存3モデルの意味記述は用途のみを宣言し、未確認の出力意味・入力正規化・クラス名は`unknown`または空のままにしている。既定Decoderはrawで、検出・クラス判定を推測しない。

## Headerと契約

パッケージには次のファイルを含める。

| ファイル | 内容 |
| --- | --- |
| `manifest.bin` | UAIM v3実行Header、CRC32、意味契約のSHA-256 |
| `contract.json` | 版付きのモデルID、実行・入力・出力意味・Decoder契約 |
| `package.json` | 契約とHeader/weights/blobのSHA-256 |
| `weights.bin` / `blob.bin` | PSRAMへロードするモデルデータ |

v3はlittle-endian。v2と同じ先頭84バイトと40バイトtensor descriptor列の後に32バイトの契約SHA-256を追加し、最後にHeader CRC32を置く。Header長は`120 + 40 * (1 + output_count)`バイト、最大480バイト。C++構造体のメモリ表現は送信しない。

契約のschema versionは1。JSONはキーをsortし、空白なし、ASCII escape、非有限数禁止で正規化する。tensorの数値はwire上のfloat32へ丸めてから契約に含める。デバイスは受信Headerの契約digestをリンク済みカタログと照合し、ホストは契約本文、実行記述、各payloadを検証してからUARTを開く。未知の契約schemaは拒否する。

SHA-256/CRCは対応関係と破損の検査であり、署名・認証ではない。許可領域や登録モデルとの照合を省略しない。外部アンカー等の付属データを参照する契約は未実装で、必要なDecoderとデータ検証を追加してから使用する。

```sh
python3 -m host_app model-loader contract inspect /path/to/package
python3 -m host_app model-loader contract verify /path/to/package
python3 -m host_app model-loader contract diff /path/to/before /path/to/after
```

diffはモデルID、型/形状/量子化/前処理、出力意味、Decoder、成果物の差分を分けて表示する。実行契約が同じでも意味契約が同じとは限らない。Policyはモデルパッケージに含めず、結果に設定とdigestを記録する。

## 結果の解釈と判断

`生テンソル → Decoder → 共通結果 → Policy → 判断`を分離する。現在のDecoderは`raw/v1`と`segmentation.argmax/v1`。segmentationにはbatch-one、NHWC/NCHW、class軸、各軸の意味、class logits/probabilitiesの宣言、全クラスのラベルを要求する。単一チャネルのbinary mask、person/faceのアンカー復元やNMSは未対応。

`decoded.json`のstatusは`raw`、`unsupported`、`decoded`、legacy Headerでは`unavailable`。未知のDecoderではrawテンソルを保存するが、解釈・判定を捏造しない。rawからのPolicy評価も`unavailable`となり、`matched=false`で代用しない。

segmentation結果は`segmentation/v1`で、クラスIDマスク、クラスごとの画素数と面積率を返す。座標は`model_output_grid`。元画像へのletterbox解除や座標復元は未実装で、入力ごとの変換情報は別に保存する。

Policy例は次のとおり。class IDの意味はモデルの実際のラベル定義から決める。

```json
{"schema_version": 1, "id": "segmentation.area", "version": 1,
 "parameters": {"class_id": 1, "minimum_fraction": 0.25}}
```

保存済みのraw結果も再評価できる。ここでのCRCは読んだファイルから計算するもので、デバイスからの転送完全性を独立に証明するものではない。

```sh
python3 -m host_app model-loader result --package /path/to/package \
  --input /path/to/result.bin --output /path/to/reinterpreted --policy /path/to/policy.json
```

アップロード時は`MODEL_POLICY=/path/to/policy.json`で指定する。保存物はHeader、生バイナリ、tensor NPZ、入力変換情報、解釈と判断のJSON。解釈済みsegmentationの場合のみ`classes.png`を生成する。

## ホスト試験

```sh
python3 -m venv build/model-loader-host/venv
build/model-loader-host/venv/bin/python -m pip install -r userspace/model-loader/tool/requirements.txt
make -C userspace/model-loader test MODEL_PYTHON="$PWD/build/model-loader-host/venv/bin/python"
```

検証対象はv1/v2/v3のC++/Python読み取り、契約digest一致、改変拒否、構造/意味の差分、4モデル・同じkindの登録、実リンカoverlay、実MultiBackendの偽HAL上での選択、Decoderの期待マスク、Policy閾値、統一ホストCLI。偽HALの試験はST SDK互換性と実機動作の代用ではない。

## モデル生成とビルド

ホストのSDK/コンパイラ/STM32CubeProgrammer/UART設定は`project-tools/host-config/local.mk`に置く。ベンダーSDKはアプリに複製しない。

```sh
make -C userspace/model-loader model-generate \
  MODEL_NAME=detector2 MODEL_SOURCE=/path/to/model.tflite \
  MODEL_DESCRIPTOR=/path/to/tensor-descriptor.json MODEL_SEMANTICS=/path/to/semantics.json
make -C userspace/model-loader model-package \
  MODEL_LOADER_MODELS='person;face;seg;detector2'
```

`MODEL_DESCRIPTOR`は既存の型・shape・scale/zero-point・前処理の形式。`MODEL_SEMANTICS`は`semantics`と`decoder`を持つJSON。生成時に省略した意味記述は既存ファイルを保持し、初回に存在しない場合はunknown/rawとしてパッケージ化する。元モデルの仕様が変わった場合は意味記述も見直す。

モデル一覧からAPI登録表・overlayリンカ定義・期待Headerを生成する。追加の実行コードには再ビルドが必要で、登録済みモデル間のロードはファームウェア再書き込みなしで行える。メモリ領域の容量を超えるモデルは拒否する。

## 実機確認

UARTの多重起動を避け、`ps aux | grep make`で既存monitor/uploadを確認する。不要なものを終了してから開始する。RAMロード前にUARTを準備する。

まず起動だけを確認する場合、端末A:

```sh
make -C userspace/model-loader monitor UART_DEVICE=/dev/ttyACM0
```

端末B:

```sh
make -C userspace/model-loader ram-run
```

`camera: pipe1=started pipe2=started`と`MODEL READY`を確認する。モデル検証・推論は別の確認である。カタログはUARTの`model list`、状態は`model stat`で確認できる。

モデルロードとゼロ入力推論まで行う場合はmonitorを閉じ、端末Aで次を実行する。`model-upload`がUARTを排他的に保持するので、別monitorは起動しない。

```sh
make -C userspace/model-loader model-upload UART_DEVICE=/dev/ttyACM0 \
  MODEL_NAME=person MODEL_WAIT_READY=ON MODEL_INPUT_ZEROS=ON \
  MODEL_PYTHON="$PWD/build/model-loader-host/venv/bin/python"
```

UART待機後に端末Bから`ram-run`する。停止確認後にST-LinkでPSRAMへ転送し、Header/CRC/契約digest照合と入力CRC検査を経て推論する。`MODEL state=verified`と`MODEL RESULT npu=done`を確認する。起動済みボードへの再ロードでは`MODEL_WAIT_READY=OFF`を使用する。

モデル本体を転送する前にビルド・map・最終ELFのstrongシンボル検査を完了する。書き込み先は隔離PSRAM/RAMで、既存NORモデルは変更しない。全出力のUART取得はモデルサイズによって長時間かかる。

## 現在の検証状態

ホスト試験は実施済み。新アプリのST SDKクロスビルド、実機書き込み、UART起動、Pipe1/2、NPU推論は未確認。これらを完了するまでは実機動作済みとは扱わない。