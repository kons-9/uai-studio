# hw-test

## 状態と目的

STM32N6570-DKのHWを検査するアプリ。[experiment-hw-test](../experiment-hw-test/README.md)と同じ17試験、カメラ制御、LCD画面、UARTコマンドを持ち、共有middlewareとDMA2D driverを使う。AI推論・モデル生成・STEdgeAIランタイムは不要。

`uai::middleware`のUIと画像演算・転送検証器、`uai::drivers`のDMA2D driverを使用する。ローカルmiddlewareの実装コピーは持たない。通常の周辺機器・カメラ・ISP・GT911の試験fixture、UART処理、IOC、ボード設定は本アプリ内に保持し、他userspaceの実装を参照しない。元experimentは変更せず独立した構成を維持する。

LCD表示の初期化確認に続いて、RNG、HASH、CRC、GPDMA、HPDMA、RTC、TIM、SRAM、PSRAM、NOR、DMA2Dと、カメラ両Pipe・ISP制御・DMA2D詳細・GT911読取りを実行する。未実装項目をSKIPで登録することはしない。外部機器の試験は選択時にstatic_assertでコンパイルを止める。実装済みと実機検証済みは区別する。

起動時は短縮版の`all`を自動実行する。UART shellからの入力を待つ必要はない。ビルド時に`HWTEST_AUTORUN_TEST=all-stress`を指定すると、長時間試験も含めて自動実行できる。個別の試験名も指定できる。

## 配置と依存

| ファイル | 責務 |
| --- | --- |
| [src/main.cpp](src/main.cpp) | T-Monitor UART、試験タスク、shellの実機入口 |
| [src/commands.hpp](src/commands.hpp) | `hwtest list / run / all / all-stress`。listに目的・実行時間上限・負荷試験区分を表示 |
| [src/hwtest.hpp](src/hwtest.hpp) | 共通I/F、登録検査、実行、PASS/FAIL、SUMMARY出力 |
| [src/ui/display.cpp](src/ui/display.cpp) | LCDの試験一覧・ログ・プレビューとタッチ操作 |
| [src/ui/log_buffer.hpp](src/ui/log_buffer.hpp) | 今回のログ・結果集計・スクロール |
| [src/integration.cpp](src/integration.cpp) | カメラ所有処理・ISPシナリオ・GT911試験 |
| [src/tests/dma2d_driver/suite.cpp](src/tests/dma2d_driver/suite.cpp) | DMA2Dの画素・guard/cache・併用負荷検査 |
| [src/graphics/verification.hpp](src/graphics/verification.hpp) | 共有image_processing検証器への型alias |
| [src/graphics/dma2d.hpp](src/graphics/dma2d.hpp) | 共有Dma2dManagementへのアダプタ |
| [generate_memory.py](generate_memory.py)、[config/memory_layout.json](config/memory_layout.json) | AIモデルなしの共有メモリ契約生成 |
| [src/tests/suite.cpp](src/tests/suite.cpp) | 実装済み試験だけの登録表、目的、タイムアウト |
| [src/tests/suite.hpp](src/tests/suite.hpp) | 各`<hardware>_driver::Run`の宣言 |
| [src/tests/board.hpp](src/tests/board.hpp) | HALと試験対象のRIF設定 |
| [src/tests/dma_copy.hpp](src/tests/dma_copy.hpp) | GPDMA/HPDMA共通の実転送・ガード・cache検査 |
| [config/stm32n6xx_hal_conf.h](config/stm32n6xx_hal_conf.h) | ローカルHAL設定へ試験対象モジュールを追加 |
| [camera-runtime-ram.ld](camera-runtime-ram.ld) | コード・表示ページ・撮像バッファの独立したSRAM配置 |
| [scratch.ld](scratch.ld) | PSRAMの`0x91000000`から4KiBを試験専用に予約 |
| [runner.py](runner.py)、[uart.py](uart.py) | 実機UART結果の収集とJUnit変換。ホスト上でHW試験を実行するものではない |

## 試験セット

`hwtest all`は短時間の起動確認用で、通常の周辺機器・メモリ試験と`touch-read`を実行する。60秒のカメラ連続動作、32段階のカメラ制御、60秒のDMA2D同時負荷試験は含めない。長時間の負荷を含めて確認する場合は`hwtest all-stress`を実行する。両セットとも手動操作が必要な`touch`試験は含めず、個別に実行する。

LCD左側の試験一覧は上下スワイプでスクロールでき、行をタップして選択できる。右側の結果ログも上下スワイプでスクロールできる。見出しに登録済みの総試験数、開始前の`P/F/T`に選択した試験数を表示する。複数シナリオを持つ試験は一覧の試験名の横に`n/n`を表示する。`RUN`で選択した試験または試験セットを開始する。新しい実行を始めると、前回のログを消して今回の結果を表示する。

通常のHW試験はHAL／BSPと試験専用領域を使い、experimentと同じ合格条件を保つ。`dma2d-suite`は共有検証器と共有DMA2D driverを経由する。通常の`dma2d`試験は元のARGB8888検査を保持し、共有driverの所有権を取得してから実行する。検査後はDMA2Dクロックを戻し、後続の共有driver転送を可能にする。互換性のため試験コードの`experiment::*`名前空間は維持しているが、元experimentをimport・リンクしない。

## 共通I/F

各試験は次の関数を実装し、[登録表](src/tests/suite.cpp)へ明示的に追加する。

```cpp
namespace experiment::hwtest::tests::rng_driver {
Result Run(const Context &context);
}
```

`Context`は`clock()`によるミリ秒時刻、`wait(milliseconds)`、wrapを考慮した`Expired(begin, timeout)`、シナリオ進捗の`Progress(current, total)`を提供する。`Result`は`Outcome::kPass`または`Outcome::kFail`とUART出力用の詳細文字列を返す。試験側がクロック・RIF・HAL初期化、有限時間の検査、停止・deinitを担当する。対象資源はこの専用アプリが占有し、通常アプリと同時に実行しない。

ランナは実行関数・目的・時間上限の欠落や重複登録を拒否する。破壊的試験の許可不足は実行前のFAILであり、SKIPにはしない。同期処理のため、時間上限の事後確認だけでハングを中断することはできない。

## 試験対象と合格条件

| 対象 | 何を確認するか | 現状 |
| --- | --- | --- |
| rng | 64ワード取得、出力が固定されていないこと、seed/clockエラーなし | 本体実装済み |
| hash | SHA-256の3バイト・56バイト入力を既知digestと照合 | 本体実装済み |
| crc | `123456789`のCRC-32/MPEG-2=`0x0376e6e7`、再計算時の初期化 | 本体実装済み |
| gpdma / hpdma | Channel0で1/3/31/32/33/255/256バイトをSRAM転送。未転送部分・guard・入力保持・cache整合 | 本体実装済み・ポーリング完了 |
| rtc | LSIで試験日時23:59:59を設定し、翌日のdate/weekdayへ進むこと | 本体実装済み・試験日時へ上書き |
| tim | TIM2の25ms/100msのカウンタ進行をOS時間と比較、停止後に進まないこと | 本体実装済み |
| sram | 専用4KiB領域のアドレス由来・4パターンの読み書き一致 | 本体実装済み |
| psram | 各パターンをXSPI1の専用領域へ書き、cache clean/invalidate後も全バイト一致 | 本体実装済み |
| nor-read | XSPI2の先頭256バイトを2回読み、一致。消去・書込みは行わない | 本体実装済み |
| dma2d | 8×8 ARGB8888実転送、全guardと入力保持、D-cache整合 | 本体実装済み |
| camera-pipes | Pipe1/2のフレーム進行を60秒監視 | 負荷試験（`all-stress`） |
| camera-control | 32段階のISP・geometry・停止／再開／復旧検査 | 負荷試験（`all-stress`） |
| dma2d-suite | 25種の画素・guard・cache検査と60秒のカメラ/LCD同時負荷 | 負荷試験（`all-stress`） |
| touch-read | GT911のID・初期化と繰り返し入力poll | 短縮セット（`all`） |
| touch | 四隅と中央の5ターゲットを押して離す | 個別の対話試験 |

GPDMA/HPDMAの現在の対象は内部SRAMであり、PSRAM転送、リンクリスト、実割り込み経路の検査はまだ含まない。RNG検査はエラーと固定出力の検出であり、統計的な乱数品質の証明ではない。RTCは通常アプリの時刻を保存・復元する試験ではなく、専用アプリのカレンダーを書き換える。

ADC、LPTIM、暗号、watchdog、JPEG/VENC等の実行本体は含まない。

## 外部機器のコンパイル拒否

外部機器の試験は通常ビルドの登録表へ入れない。以下のCMakeオプションをONにすると、対応する`src/tests/<hardware>_driver/test.cpp`がコンパイル対象となり、必要なfixtureを示す`static_assert(false, ...)`で止まる。SKIPやダミーFAIL関数で置き換えない。

| CMakeオプション | ディレクトリ | 必要な準備 |
| --- | --- | --- |
| HWTEST_ENABLE_SDMMC | sdmmc_driver | 試験用microSD、書込みを許可したscratch範囲 |
| HWTEST_ENABLE_ETHERNET | ethernet_driver | ケーブル、対向機、固定パケットの照合プロトコル |
| HWTEST_ENABLE_USB | usb_driver | USBホスト、ケーブル、device classと転送プロトコル |
| HWTEST_ENABLE_AUDIO | audio_driver | codec/microphoneと音声fixture |
| HWTEST_ENABLE_EXTERNAL_INPUT | external_input_driver | 外部信号源とピン構成 |

fixtureと実行本体を整備した段階でstatic_assertを外し、同じRun I/Fへ接続する。

## 実機での実行

ARMツールチェーン、STM32CubeN6、CubeMX、CubeProgrammerとST-LINK/VCPの接続を用意する。ホスト固有設定は[local.mk.example](../../build-system/host-config/local.mk.example)を参照。[本アプリのIOC](config/stm32n6570-dk-fullsecure.ioc)とローカルのボード設定を使用する。ビルド先は`build-hw-test`。共有コンポーネント用ヘッダーと予約領域はビルド時に生成する。NPU driverはこのアプリのビルドから除外する。

```sh
make -C userspace/hw-test generate
make -C userspace/hw-test build
make -C userspace/hw-test monitor
```

UARTを開いたまま、別端末で書き込む。

```sh
make -C userspace/hw-test ram-run
```

`HWTEST READY`を確認して、UARTから次を実行する。

```text
hwtest list
hwtest run rng
hwtest run hash
hwtest run gpdma
hwtest run rtc
hwtest run sram
hwtest run psram
hwtest run nor-read
hwtest run dma2d
hwtest all
hwtest all-stress
```

結果は`HWTEST <name> PASS|FAIL <詳細>`。末尾の`HWTEST SUMMARY pass=<n> fail=<n> total=<n> skip=0`には実行数を表示する。このファームウェアはSKIPを出さない。`all`は短縮セット、`all-stress`は長時間試験を含むセット。将来破壊的試験を追加した場合、許可のないセット実行や`run`は実行前に拒否し、個別に`hwtest run <name> allow-destructive`で許可する。

自動収集を使う場合はmonitorを閉じ、代わりにUARTランナを先に起動する。

```sh
python3 userspace/hw-test/runner.py --uart /dev/ttyACM0 \
	--junit result.xml --log-dir logs --wait-ready --timeout 600
```

`--wait-ready`を指定すると起動時に選んだ自動実行セットの結果をそのまま収集し、shellへコマンドを送らない。起動行（`camera: pipe1=started pipe2=started`を含む）は`logs/startup.log`へ保存する。通常モードでは`hwtest all`または`--test all-stress`、`--test <name>`をshellへ送る。ランナは実機のHWTEST結果だけを集計し、結果欠落・重複・SUMMARY不一致・通信断は不成功とする。終了値は0=PASSあり/FAILなし、1=FAILあり、2=入力不完全等。`total`も結果行数と照合する。

## ホスト検証

```sh
cmake -S kernel/middleware/tests -B build/middleware-tests
cmake --build build/middleware-tests --target hw_test image_processing_test dma2d_driver_test ui_test
ctest --test-dir build/middleware-tests -R '^(hw_test|image_processing_test|dma2d_driver_test|ui_test)$' --output-on-failure
python3 -m unittest discover -s userspace/hw-test/tests -p 'test_*.py' -v
```

ホストでは試験登録、共有UI描画、共有画像検証器、25ケースと60秒負荷シナリオの進行、メモリ契約、ビルド登録、リンク監査を検査する。ハードウェア試験の関数は実行せず、実機のPASSやキャッシュ整合を証明するものではない。

最終ELFのビルド後は[check_link.py](check_link.py)がHAL／IRQのstrong実装に加え、UIとDMA2Dが意図した共有オブジェクトから配置されたことをnmとリンクマップで確認する。

## 安全条件と確認範囲

- 試験コードは内部SRAMで実行する。PSRAMパターン試験が書くのは`0x91000000`の4KiBだけ。共有メモリ契約の予約は`0x91010000`以降へ分離し、試験領域と重複させない。カメラ・画面は元experimentと同じSRAM配置を使う。
- NORは読出し専用。二重読出しの一致だけでは既知データの正しさや全容量の正常性を証明しない。
- cache整合はDMA2Dの実転送で検査する。CPU memcpyやPSRAMパターン検査をDMA整合の代用にしない。
- 同期実行の時間上限は戻り時にも確認するが、ハングを中断するものではない。各HAL処理・待機に個別の有限タイムアウトを持たせる。
- SD/USBの書込みには試験専用媒体を用意する。watchdogのreset試験には再起動を跨ぐマーカーとホスト収集の設計が必要。
- OTP/fuseへの書込みや、復帰処理のないRIF/ECCフォルト注入は実行しない。
- camera-pipes / camera-control / dma2d-suiteは`all-stress`でのみ実行する。起動ログだけを試験結果として流用しない。
- 試験後はUARTを先に開いて通常ai-appへ戻し、`camera: pipe1=started pipe2=started`と推論を確認する。この全体確認を省略しない。

実機確認はUARTを先に開き、RAM実行で各試験の結果とLCD表示を確認する。その後`ai-app`をRAM実行し、UARTの`camera: pipe1=started pipe2=started`でPipe1/2の復帰を確認する。
