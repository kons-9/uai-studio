# experiment-model-load

## 状態と目的

マニフェスト生成/C++読取り、期待値/CRC検証、隔離PSRAMスロットへの順序付き転送、UARTアップローダとホストテストを実装済み。NPU登録・推論・relocatable対応には実際の生成モデルとランタイムAPIが必要で未接続。今回、合成マニフェストを使ったARMビルドとHAL/IRQリンク監査は通過した。実データでのビルドと実機確認は未完了。kernel/driver・middlewareには依存しない。

## 実装済みと実行方法

```sh
cmake -S userspace/experiment-model-load -B build/experiment-model-load-host
cmake --build build/experiment-model-load-host
ctest --test-dir build/experiment-model-load-host --output-on-failure
python3 userspace/experiment-model-load/manifest.py pack --help
python3 userspace/experiment-model-load/manifest.py verify --help
```

[manifest.py](manifest.py)のpackには重み/blobのファイル、配置アドレス、予約領域、ランタイム版、モデル種別、入出力バイト数を明示する。予約領域外・セグメント重複・アドレスオーバーフロー・入力ファイルへの上書きを拒否する。packはローカルファイル生成だけで、デバイスへ書き込まない。verifyもローカルファイルとのサイズ/CRC照合のみ。

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

pack / verifyと[upload.py](upload.py)は実験内に置き、host_app統一入口へは接続しない。実機ビルド時は`EXPERIMENT_MODEL_MANIFEST`に期待マニフェストを指定し、weightsは0x91010000、blobは0x91020000の別々の64KiB専用スロットに収める。UARTを先に開き、別端末で起動した後、アップローダに`--uart --manifest --weights --blob`を指定する。`model stat`の`verified`はCRC検証済みを意味し、NPU実行可能を意味しない（`npu=unavailable`）。

## pre-kernel等への接続要件（今は変更しない）

- このexperiment内の`src/camera_runtime/`と`config/`を使う。実機ではXSPI初期化、PSRAM/NORのマッピング、キャッシュ、RIF、NPUクロック/IRQを確認する。HAL/BSPの採用元はこのexperimentのmapと`check_link.py`で監査する。
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

実装者がホストテスト、書込み範囲、リンク結果、起動とPipe1/2を確認してから実施する。UARTを先に開き、別端末で`make -C userspace/experiment-model-load ram-run`を実行する。

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
