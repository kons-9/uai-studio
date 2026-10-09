# experiment-model-load

## 状態と目的

マニフェスト生成/C++読取り、期待値/CRC検証、隔離PSRAMスロットへの順序付き転送、非同期NPU実行、UARTアップローダを実装している。既定は従来の転送fixtureで、NPUは無効。`EXPERIMENT_MODEL_NPU=ON`では、このexperiment内で生成した単一入力・単一出力の固定モデルをロード後に実行できる。kernel/driver・middlewareや別userspaceの実装には依存しない。

ホストテストは確認済み。STEdgeAI、ARMツールチェーン、実機がない環境での実装のため、NPU有効ビルド、STランタイムの停止処理、実機推論、NORとの結果一致、Pipe1/2継続は未検証。relocatableモデルはAPI対応を確認できていないため未実装。任意モデルを再ビルドなしで交換できる段階ではない。

## 実装済みと実行方法

```sh
make -C userspace/experiment-model-load model-fixture
cmake -S userspace/experiment-model-load -B build/experiment-model-load-host
cmake --build build/experiment-model-load-host
ctest --test-dir build/experiment-model-load-host --output-on-failure
python3 userspace/experiment-model-load/tool/manifest.py pack --help
python3 userspace/experiment-model-load/tool/manifest.py verify --help
```

モデルマニフェストを指定しない実機ビルドでは、[tool/fixture.py](tool/fixture.py)が小さなweightsとblobをbuildディレクトリへ作り、そこから期待マニフェストを生成する。実際の生成モデルを使う場合は`EXPERIMENT_MODEL_MANIFEST`を指定し、weights/blobとも各64KiB以内であること。

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

pack / verifyと[tool/upload.py](tool/upload.py)は実験内の`tool/`に置き、host_app統一入口へは接続しない。weightsは0x91010000、blobは0x91020000の別々の64KiB専用スロットへ転送する。`model stat`の`verified`はCRC検証済みを意味し、推論成功を意味しない。fixtureでは`npu=unavailable`、NPU有効ビルドでは`npu=ready`となり、実際の初期化・登録は`model run`時に行う。

既定のfixtureを実機で確認するには、UARTを先に開く端末Aで次を起動する。アップローダがUARTを排他的に保持し、起動ログ、Pipe1/2開始行、アップロード後の`model stat`を表示する:

```sh
UART_DEVICE=/dev/ttyACM0 make -C userspace/experiment-model-load model-upload
```

端末AがUARTを待っている状態で、端末Bから起動する:

```sh
make -C userspace/experiment-model-load ram-run
```

アップロード完了後、端末Aに`MODEL state=verified`と`npu=unavailable`が出る。UART出力の監視を終えるにはCtrl-Cを押す。`make monitor`や別のUARTツールを同時に起動しない。ボードのシリアル名が異なる場合は`UART_DEVICE`を実際のVCPへ合わせる。

## 固定モデルのPSRAM実行

STEdgeAIのST AI C APIで生成できる、小さな単一入力・単一出力モデルを用意する。weights、blob、入力、出力はそれぞれ64KiB以内。[tool/model.py](tool/model.py)はローカルの[model.mpool](config/model.mpool)を使用し、重みをPSRAMの0x91010000、作業領域をSRAM5/6の0x342e0000–0x343bffffへ固定する。カメラのSRAM3/4やアプリのSRAM1/2を生成ツールに割り当てない。

以下のモデルパスと入出力バイト数は例で、使用するモデルに合わせる。入力・出力の個数とバイト数は生成ヘッダーに対するコンパイル時検査でも照合する。runtime-versionは使用する`NetworkRuntime<version>_CM55_GCC.a`の版と合わせる。

```sh
make -C userspace/experiment-model-load model-generate \
	MODEL_SOURCE=/path/to/small-model.onnx \
	MODEL_INPUT_BYTES=192 MODEL_OUTPUT_BYTES=16 MODEL_RUNTIME_VERSION=1201 \
	MODEL_STEDGEAI=/opt/ST/STEdgeAI/4.0/Utilities/linux/stedgeai

make -C userspace/experiment-model-load model-package \
	EXPERIMENT_MODEL_NPU=ON \
	STEDGEAI_LIB_DIR=/opt/ST/STEdgeAI/4.0/Middlewares/ST/AI
```

生成コード・重み・契約は`models/generated/`へ置く。NPUビルドはコマンドblobを生成モデルのOBJECTから抽出し、そのblobと重みから期待マニフェストを生成する。blobにリンク時再配置が残る場合は拒否する。最終ELFでblobのアドレスを0x91020000に固定し、両PSRAMスロットをNOLOADにするため、ram-runによる転送やBSS初期化にモデルデータを混ぜない。[tool/check_link.py](tool/check_link.py)が最終ELF/mapのIRQのstrong定義・配置元と予約領域を監査する。

UARTを開く端末A:

```sh
UART_DEVICE=/dev/ttyACM0 make -C userspace/experiment-model-load model-upload \
	EXPERIMENT_MODEL_NPU=ON
```

UART待機後に端末B:

```sh
make -C userspace/experiment-model-load ram-run EXPERIMENT_MODEL_NPU=ON
```

STランタイムのパスは両端末で同じ設定を使う。NPU有効時の`model-upload`はパッケージ生成、転送、CRC確認、ゼロ埋め固定入力での推論を順に行い、`MODEL RESULT npu=done output_crc=xxxxxxxx elapsed_ms=... input=zeros`を待つ。既知の同条件の出力CRCがある場合は`MODEL_EXPECTED_OUTPUT_CRC=0x12345678`を指定して照合できる。実際のNOR版との比較結果は未取得。

モデルを変えたら`model-generate`とアプリ再ビルドが必要。既存モデルのNOR領域には書き込まない。NPUビルドのマニフェストはビルドから自動生成するため、`EXPERIMENT_MODEL_MANIFEST`との併用は拒否する。

NPU無効ビルドで任意の転送データを試す場合は、`EXPERIMENT_MODEL_MANIFEST`、`MODEL_WEIGHTS`、`MODEL_BLOB`をすべて明示する。ホスト側のサイズ/CRC/スロット検査はUARTを開く前に行う。

## モデルコマンドと状態

| コマンド | 動作 |
| --- | --- |
| `model stat` | 転送状態、受信バイト数、NPU状態、最後の出力CRCと所要時間 |
| `model begin <manifest-hex>` | リンク済みモデルの期待値と照合し、転送開始。NPU使用中は拒否 |
| `model chunk weights\|blob <offset> <hex>` | 各セグメントを順序付きで受信。UARTでは最大32バイト/要求 |
| `model commit` | サイズ・全CRC・キャッシュcleanを確認して公開 |
| `model verify` | 公開済みモデルの全CRCを再検証。不一致なら無効化 |
| `model run` | 再CRC検証後、ゼロ埋め入力による非同期推論を開始 |
| `model abort` | NPUの停止・登録解除を確認してから転送/検証状態を破棄 |

推論中もカメラ/ISP/コンソールのループを継続する。NPUの進行は1回のTickにつき最大1エポック、タイムアウトは5秒。正常終了または停止確認後だけスロットを解放する。停止APIが失敗した場合は`npu=faulted`のままロックを保持し、`model abort`で停止を再試行するまで再転送を許可しない。未ロード・転送途中・CRC不一致のデータを実行しない。電源断後は未ロード状態から始める。

ホスト試験にはマニフェスト/生成契約/パッケージ、実オブジェクトのblob抽出と再配置拒否、実リンカでのスロット境界、非同期実行の正常/故障/停止失敗/時刻折り返し、アップローダのCRC比較とデバイスを開く前の拒否を含む。NPUのホスト試験は偽バックエンドで制御部分を確認するもので、ST API互換性やハードウェア動作の代用ではない。

## pre-kernel等への接続要件（今は変更しない）

- このexperiment内の`src/camera_runtime/`と`config/`を使う。実機ではXSPI初期化、PSRAM/NORのマッピング、キャッシュ、RIF、NPUクロック/IRQを確認する。HAL/BSPの採用元はこのexperimentのmapと[tool/check_link.py](tool/check_link.py)で監査する。
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
