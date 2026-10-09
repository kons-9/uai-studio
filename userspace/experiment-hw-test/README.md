# experiment-hw-test

## 状態と目的

STM32N6570-DKのHWを直接検査する実機専用アプリ。既存experimentで動かしているHWだけでなく、未実験の内蔵周辺機器も試験対象として登録する。ドライバの単体試験ではなく、HAL／BSPを直接呼び、クロック・ピン・転送・キャッシュ・IRQ等の実HWの動作を確認する。

`kernel/driver`、`uai::drivers`、他experimentの実装には依存しない。μT-Kernelとpre-kernelをリンクし、試験用のBSP設定、UART処理、driver補助コードはこのexperiment内に置く。ホスト実行ファイル、HALモック、CTest、ホスト向け`make test`は置かない。

主対象は未実験HW。LCD表示の初期化確認に続いて、RNG、HASH、CRC、GPDMA、HPDMA、RTC、TIM、SRAM、PSRAM、NOR、DMA2Dを実行する。未実装項目をSKIPで登録することはしない。外部機器の試験は選択時にstatic_assertでコンパイルを止める。実装済みと実機検証済みは区別する。

起動時は登録済みの全試験を自動実行する。UART shellからの入力を待つ必要はない。ビルド時に`HWTEST_AUTORUN_TEST`を指定すると、自動実行する試験を個別に選択できる。

## 配置と依存

| ファイル | 責務 |
| --- | --- |
| [src/main.cpp](src/main.cpp) | T-Monitor UART、試験タスク、shellの実機入口 |
| [src/commands.hpp](src/commands.hpp) | `hwtest list / run / all`。listに目的と実行時間上限を表示 |
| [src/hwtest.hpp](src/hwtest.hpp) | 共通I/F、登録検査、実行、PASS/FAIL、SUMMARY出力 |
| [src/display_log.cpp](src/display_log.cpp) | LCDへ試験の開始・終了行を表示し、画面下端で自動スクロール |
| [src/tests/suite.cpp](src/tests/suite.cpp) | 実装済み試験だけの登録表、目的、タイムアウト |
| [src/tests/suite.hpp](src/tests/suite.hpp) | 各`<hardware>_driver::Run`の宣言 |
| [src/tests/board.hpp](src/tests/board.hpp) | HALと試験対象のRIF設定 |
| [src/tests/dma_copy.hpp](src/tests/dma_copy.hpp) | GPDMA/HPDMA共通の実転送・ガード・cache検査 |
| [config/stm32n6xx_hal_conf.h](config/stm32n6xx_hal_conf.h) | 生成HAL設定へ試験対象モジュールを追加 |
| [scratch.ld](scratch.ld) | PSRAMの`0x91000000`から4KiBを試験専用に予約 |
| [runner.py](runner.py)、[uart.py](uart.py) | 実機UART結果の収集とJUnit変換。ホスト上でHW試験を実行するものではない |

試験本体は`src/tests/<hardware>_driver/test.cpp`へ置く。ディレクトリ名のdriverは対象を表すもので、共有driverへの依存や製品用driverの実装ではない。HAL／BSPと試験専用領域を使い、アプリ側のdriverを経由しない。driver、middleware、共有pre-kernelを変更しない。

## 共通I/F

各試験は次の関数を実装し、[登録表](src/tests/suite.cpp)へ明示的に追加する。

```cpp
namespace experiment::hwtest::tests::rng_driver {
Result Run(const Context &context);
}
```

`Context`は`clock()`によるミリ秒時刻、`wait(milliseconds)`、wrapを考慮した`Expired(begin, timeout)`を提供する。`Result`は`Outcome::kPass`または`Outcome::kFail`とUART出力用の詳細文字列を返す。試験側がクロック・RIF・HAL初期化、有限時間の検査、停止・deinitを担当する。対象資源はこの専用アプリが占有し、通常アプリと同時に実行しない。

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

GPDMA/HPDMAの現在の対象は内部SRAMであり、PSRAM転送、リンクリスト、実割り込み経路の検査はまだ含まない。RNG検査はエラーと固定出力の検出であり、統計的な乱数品質の証明ではない。RTCは通常アプリの時刻を保存・復元する試験ではなく、専用アプリのカレンダーを書き換える。

[04の棚卸し](../../tmp/plans/04-hardware-drivers.md)と[09のHW試験項目](../../tmp/plans/09-hw-unit-tests.md)を参考にするが、計画のdriver依存構成やホストテスト配置は採用しない。ADC、LPTIM、暗号、watchdog、JPEG/VENC等の実行本体は今回の11試験には含まない。

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

ARMツールチェーン、STM32CubeN6、CubeMX、CubeProgrammerとST-LINK/VCPの接続を用意する。ホスト固有設定は[local.mk.example](../../build-system/host-config/local.mk.example)を参照。既定のIOCはcamera-lcdのものを利用するが、camera-controlの実行コードはリンクしない。試験対象のクロックとRIF設定は各試験本体で行う。共有pre-kernelの変更は行わない。

```sh
make -C userspace/experiment-hw-test generate
make -C userspace/experiment-hw-test build
make -C userspace/experiment-hw-test monitor
```

UARTを開いたまま、別端末で書き込む。

```sh
make -C userspace/experiment-hw-test ram-run
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
```

結果は`HWTEST <name> PASS|FAIL <詳細>`。LCDには各試験の開始と終了だけを表示し、UARTには従来の結果形式だけを出力する。行が表示領域を越えると古い行を上へ送り、自動でスクロールする。既存UART収集形式との互換性のため、末尾は`HWTEST SUMMARY pass=<n> fail=<n> skip=0`を維持する。このファームウェアはSKIPを出さない。登録されている試験を`all`で実行する。将来破壊的試験を追加した場合、許可のない`all`/`run`は実行前に拒否し、個別に`hwtest run <name> allow-destructive`で許可する。

自動収集を使う場合はmonitorを閉じ、代わりにUARTランナを先に起動する。

```sh
python3 userspace/experiment-hw-test/runner.py --uart /dev/ttyACM0 \
	--junit result.xml --log-dir logs --wait-ready
```

`--wait-ready`を指定すると起動時の自動`all`の結果をそのまま収集し、shellへコマンドを送らない。通常モードでは`hwtest all`または`--test <name>`をshellへ送る。ランナは実機のHWTEST結果だけを集計し、結果欠落・重複・SUMMARY不一致・通信断は不成功とする。終了値は0=PASSあり/FAILなし、1=FAILあり、2=入力不完全等。旧ログのSKIP形式も読めるが、新ファームウェアからは出力しない。

## 安全条件と確認範囲

- 試験コードは内部SRAMで実行する。PSRAMへ書くのはリンカで予約した4KiBだけ。モデル、画像、スタックと領域を共有しない。
- NORは読出し専用。二重読出しの一致だけでは既知データの正しさや全容量の正常性を証明しない。
- cache整合はDMA2Dの実転送で検査する。CPU memcpyやPSRAMパターン検査をDMA整合の代用にしない。
- 同期実行の時間上限は戻り時にも確認するが、ハングを中断するものではない。各HAL処理・待機に個別の有限タイムアウトを持たせる。
- SD/USBの書込みには試験専用媒体を用意する。watchdogのreset試験には再起動を跨ぐマーカーとホスト収集の設計が必要。
- OTP/fuseへの書込みや、復帰処理のないRIF/ECCフォルト注入は実行しない。
- カメラ試験は登録していない。従来のcamera-controlの起動ログを、このアプリの検証結果として流用しない。
- 試験後はUARTを先に開いて通常ai-appへ戻し、`camera: pipe1=started pipe2=started`と推論を確認する。この全体確認を省略しない。

実機確認はUARTを先に開き、RAM実行で各試験の結果とLCD表示を確認する。その後`ai-app`をRAM実行し、UARTの`camera: pipe1=started pipe2=started`でPipe1/2の復帰を確認する。
