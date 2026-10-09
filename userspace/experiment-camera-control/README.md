# experiment-camera-control

## 状態と目的

実験内のカメラ制御・shell・実機エントリ・ビルド定義と、独立ホストテストを実装済み。ARMビルドと最終ELFのHAL/IRQリンク監査は通過した。書き込み・実機確認は未完了であり、動作確認済みとは扱わない。

対象は[01: カメラ制御](../../tmp/plans/01-camera-control.md)、[02: AI露出](../../tmp/plans/02-ai-driven-exposure.md)、[07: shell](../../tmp/plans/07-shell.md)、[08: 統一ホストI/F](../../tmp/plans/08-host-app-interface.md)。このexperimentはカメラ、表示、ボード用driverを`src/driver/`に、BSP設定とCubeMX IOCを`config/`に持つ。初版は左右400x480のRGB565、SRAMバッファ、stockセンサ設定。NPUはまだ接続しない。

`kernel/driver`・`kernel/middleware`には依存しない。基盤依存はμT-Kernel、pre-kernel、STM32Cube BSP/HAL/ISP。エラー型も実験内の`console::Status`を使用する。

## 実装済みと実行方法

リポジトリルートから:

```sh
make -C userspace/experiment-camera-control test
```

- [shell.hpp](src/shell.hpp): 128バイトの行バッファ、最大8引数、CRLF、BS/DEL、Ctrl-C、長すぎる行の全破棄、静的コマンド表。
- [camera_control.hpp](src/camera_control.hpp): 入力検証、カメラコマンド、letterboxを考慮した座標変換。
- [isp_camera.cpp](src/isp_camera.cpp): AE、EV補正、手動露出/ゲイン、統計領域、AWB。手動設定はセンサの許容範囲も検査する。
- [exposure.hpp](src/exposure.hpp): 顔→人物→前景の選択、微小変化抑制、消失猶予、sequence重複/逆行拒否。マスクの外接矩形生成と、適用成功時だけAppliedを呼ぶ方式。NPUの生成モデルは未接続。
- [runtime.hpp](src/runtime.hpp) / [bsp_device.cpp](src/bsp_device.cpp): 停止・開始・復旧、設定の再適用、IMX335のFPS/反転とPipe2 cropを実験内で制御する。実機での画質と安全性は未検証。
- [main.cpp](src/main.cpp): 同じカメラ所有タスクでProcessとコマンドを適用。USART1受信IRQとフレーム通知で起床し、フレーム停滞時に復旧を試みる。
- [scenario.hpp](src/scenario.hpp): 起動時に一巡する32段階の自動試験。両Pipeの進行、読戻し、実測FPS、停止再開・復旧・不正入力を検証し、最後に開始前の設定へ戻す。
- [check_link.py](check_link.py): 最終ELFとmapのHAL/IRQ採用元を検査し、ファームウェアのリンク後に自動実行する。
- [console.py](console.py): UARTシェル操作入口。UART transportもこのexperiment内に置き、1ポートから順にコマンドを送る。

UART起動ログも受け取る場合は、書き込み前に別端末で開いて待つ:

```sh
python3 userspace/experiment-camera-control/console.py --uart /dev/ttyACM0 --timeout 120 --wait-ready --command 'cam stat' --command 'frames'
```

別端末で対象実験の`make ram-run`を実行する。`--command`を省略すると標準入力から対話的にコマンドを読む。`--wait-ready`を省略する場合は起動済みのシェルへ直接コマンドを送る。monitor、HW試験runner、モデルuploadとUARTを同時に開かない。

実機接続後のコマンド:

```text
help
uptime
frames
cam stat
cam ae off
cam manual 12000 3000
cam ae on
cam ev -2
cam area 0 0 800 480
cam wb-list
cam wb auto
scenario stat
scenario stop
scenario start
capture stop
capture start
capture fps 20
capture flip 1 0
capture crop 100 0 1600 1920
capture recover
subject 3 900 120 90 80 80 5000
```

evは0.5EV単位の-4..4。手動設定の初版コマンド範囲は100..30000 us / 0..24000 mdB。wbはautoまたはwb-listで得た色温度を指定する。statのreported_us / reported_mdBはBSP保持値であり、センサレジスタの実測値ではない。手動設定失敗時は元の保持値への復元を試みるが、通信障害時の復元成功は保証せずhardwareエラーを返す。

従来の無限デモは、起動時に一回だけ実行する自動シナリオへ置き換えた。通常の確認でshell操作は不要。実行中のcam設定変更、capture操作、subject投入は拒否し、試験条件を固定する。help、uptime、frames、cam stat、cam wb-listとscenario診断は利用できるが、停止区間のcam読取りはinvalid-stateになる。

## 一回の起動で確認するシナリオ

ビルド後、端末AでUARTを先に開いて結果を収集する:

```sh
make -C userspace/experiment-camera-control monitor
```

端末Bで起動する:

```sh
make -C userspace/experiment-camera-control ram-run
```

カメラ開始後、約1分で次の32段階を一巡する。実行中のaction名を画面左上にも表示する。各段階の状態確認を約1秒、FPS測定を2秒、最後の連続動作確認を10秒に短縮した。BEGINログと設定読戻し、両Pipeの進行、FPS許容差の確認は維持する。visual=requiredのため、表示の明るさ・色・向き・画角は引き続き別途目視確認する。

| 段階 | 内容 | 自動で確認すること |
| --- | --- | --- |
| baseline | 通常の映像 | 両Pipeの継続進行と初期状態 |
| manual、exposure-*、wb-* | AE停止、露出1000/4000/12000/28000us、WB auto/2810/4015/6650K | 設定成功、読戻し、両Pipe進行 |
| ae-on、ev-minus/plus、statistics-center | AE再開、EV -2/+2、センサ中央の測光領域 | AE中の手動変更拒否、各設定の読戻し |
| manual-fixed、fps-* | 露出12000usへ戻し、10/15/20/25/30fpsを順に適用 | 両Pipeの実測FPSが要求値の±30%以内 |
| flip-*、crop | 左右・上下・両方・無反転、crop 100,0,1600,1920 | 設定成功、保持値と両Pipe進行。向き・画角は目視 |
| invalid-input | FPS 12、反転値2、幅0のcrop、露出99us | 拒否され、直前の設定が変わらない |
| stop、restart | 停止後に再開 | 停止中の両カウンタ停止、設定保持、両Pipe再開 |
| recovery-1/2 | 復旧を2回 | 設定保持、両Pipe再開 |
| stability | 10秒連続動作 | 途中の片側停止、処理失敗、意図しない復旧がない |
| restore | 開始前の設定へ復元 | AE・EV・露出/ゲイン・測光領域・WBとFPS/反転/cropの復元、両Pipe進行 |

PASSはAPI成功だけではない。要求値を保持した状態と読戻しを照合し、手動露出は100us、ゲインは300mdBの量子化許容差を使う。AE中の露出/ゲインとAWB中の色温度は変動するため固定値では比較しない。各Pipeが1.5秒進まない場合、カメラ/表示処理のエラー、予期しない復旧、監視間隔の1.5秒超過はFAILになる。設定APIの操作に5秒を超えた場合は、返却後にFAILとする。HAL内部の無期限待ちやCPUフォルトを強制中断する仕組みではない。

失敗した段階で中止し、開始前の設定への復元を試みる。残りの段階はSKIPとして集計し、復元にも失敗した場合はcleanup FAILを追加する。成功時は通常表示に戻り、再起動するまで自動では繰り返さない。

```text
CAMTEST baseline BEGIN
CAMTEST STATE ae=1 ...
CAMTEST baseline PASS pipe1=60 pipe2=60 elapsed_ms=2000
...
CAMTEST SUMMARY pass=32 fail=0 skip=0 visual=required
```

pass=32、fail=0、skip=0が自動検証の合格条件。SUMMARYが出ない起動や中断は合格ではない。visual=requiredは、明るさ・色・反転・画角の正しさを自動判定したという意味ではなく、別途目視確認が必要という表示。読戻しはドライバの保持値であり、独立したセンサレジスタ検査でもない。UART受信・行編集や実際のNPU検出結果との接続は、この自動シナリオの対象外。

必要な場合だけ、scenario statで進捗、scenario stopで中止と復元、scenario startで開始前の状態を新たに保存して再実行できる。自動結果の収集にはmonitorを使い、consoleと同じUARTを同時に開かない。対話consoleは標準入力待ちの間に自動ログを読み続けないため、今回の連続ログ収集には使わない。

既存tm_getcharはwaitを無視し、割り込み禁止で受信待ちするため使用しない。UART受信エラー時は行末まで破棄する。送信はT-Monitorの同期出力なので、応答中のCPU時間・フレームへの影響は実機で測る。

外部依存: NPU生成結果の供給、AI方式Bが必要かの判断、ARMビルドとクロック/IRQを含む実機評価。既存monitor middlewareはリンクしない。uptimeのループ数/DWT busy_cyclesとframesは実験内の比較用であり、busy_cyclesは厳密なCPU利用率ではない。

pre-kernelとビルド設定はcamera-controlの接続に必要な範囲だけ変更した。共有ドライバ、既存middleware、ai-app、tmp/plansは変更しない。実験に成功しただけで計画全体を完了扱いにしない。他の新規実験のpre-kernel接続は引き続き保留する。

## 実験の境界

カメラ所有タスク内でshell、ISP設定と表示を直列に処理する。外部入力はUSART1のIRQキュー、フレーム進行は通知フラグから受け取る。実験専用のCMake/Makefile、ホストテストとリンク監査は本ディレクトリ内に置く。実機のビルド条件・ELF/map・UARTログ・観測結果は実験記録として残し、生成物はソース管理しない。

新しいmiddlewareの追加や既存アプリへのリンクは行わない。ログ出力先や実行時ログレベルも変更しない。

ホストツールも実験ディレクトリ内で実装・検証する。host_app統一入口への組み込み、既存CLI・Makefile・GUIサーバ・トレース形式の置き換えは保留する。UARTはmonitorとホストランナで同時に開かず、一方が所有して収集・操作する。

## pre-kernelへの接続と実機準備

2026-10-08に[ボード選択](../../build-system/cmake/camera_board.cmake)を追加した。APP_TARGETとボード設定はexperiment-camera-control自身を選び、pre-kernelとアプリは同じローカル設定を使う。

| 接続点 | 接続内容と残る確認 |
| --- | --- |
| アプリ選択 | UAI_KERNEL_APPSには追加しない。共有middleware / driverターゲットはリンクしない |
| クロックとメモリ権限 | UAI_CAMERA_LCD_CLOCKSを有効化し、既存のPLL設定、AXISRAM3/4、DCMIPP/LTDCのRIF設定を適用する。実機での動作は未確認 |
| HAL・ボード設定 | utkernelにはpipe2のboard/includeとCMSIS、secure-state定義を適用。pre-kernelは既存カメラ実験と同じHALヘッダ選択とボード設定分岐を使う |
| CubeMX生成物 | ローカルの`config/stm32n6570-dk-fullsecure.ioc`を使用し、出力はbuild-experiment-camera-control/cubemxへ分離する。生成されたMSP、割り込み、スタートアップはARMビルド後に確認する |
| メモリ配置とRAM実行 | pipe2のSRAMリンカスクリプトとframe_bufferを再利用。ロード先0x34000400、実行先0x34000800、MSP 0x34200000を選択する |
| HALソース | pre-kernelの既存ソースと重複しないよう不足モジュールのみ追加する。実際のSDKのヘッダ・実装の版と最終リンク結果を確認する |
| IRQ・コールバック | USART1受信IRQとDCMIPP/CSIの処理は実行ファイルへ直接リンクする。リンク後にcheck_link.pyで採用元を監査する |

camera-controlではEXPERIMENT_PREKERNEL_READYの既定値をONにした。他の新規実験はOFFのままで、ONを指定しても対応するボード設定がなければ構成を拒否する。ONは実機確認済みという意味ではない。

[ホスト設定テンプレート](../../build-system/host-config/local.mk.example)を参考に、build-system/host-config/local.mkで次を設定する:

- ARM_NONE_EABI_TOOLCHAIN_PATH: arm-none-eabi-*がPATHにない場合のツールチェーンルート。
- STM32CUBE_N6_DIR: BSP、HAL、ISP/evisionライブラリを含むSTM32CubeN6パッケージ。
- CUBEMX_EXECUTABLE: STM32CubeMX 6.xの実行ファイル。
- STM32_PROGRAMMER_ROOT: STM32CubeProgrammerのルート。
- UART_DEVICEと、必要ならSTM32_PROGRAM_SERIAL: 接続したST-LINK/VCP。

準備できたら、リポジトリルートから生成とビルドを実行する。NPUは使用しないためモデル生成やai-loadは不要:

```sh
make -C userspace/experiment-camera-control generate
make -C userspace/experiment-camera-control build
```

ビルド成功後、端末AでUARTを先に開き、起動後は対話的に操作する。デバイス名は実際のVCPへ置き換える:

```sh
python3 userspace/experiment-camera-control/console.py --uart /dev/ttyACM0 --timeout 120 --wait-ready
```

端末Bで書き込んで開始する:

```sh
make -C userspace/experiment-camera-control ram-run
```

UARTのcamera: pipe1=started pipe2=startedを確認後、help、cam stat、framesを実行し、framesを再取得して両カウンタが増えることを確認する。起動ログの収集だけなら端末Aはmake monitorでもよいが、consoleと同じポートを同時に開かない。

この変更ではボード選択、IOC/生成先/RAM実行設定、ホスト設定テンプレートの適用を実CMake/Makeで検証した。検証環境ではARMツールチェーンとCubeMX実行ファイルが利用できず、ST-LINK VCPも見つからなかったため、ARMビルド・書き込み・Pipe1/2開始確認は未実施。

## HALのリンク取り違え対策

ライブラリ化した際に意図した関数ではなくHAL側の関数が採用された、という利用者からの注意事項がある。過去の事象の原因は未確定だが、次の条件を必ず検査する。

- HALのweak定義で参照が解決すると、static archive内の上書き用strong定義を含むオブジェクトが抽出されないことがある。「strongなら必ず勝つ」と考えない。
- hal_time.cとdcmipp_callbacks.cはアプリ内のソースとして最終ELFへ直接含める。最終リンクコマンドとmapで採用元も確認する。
- HAL本体、BSP、アプリの各定義を調べ、割り込みハンドラ・コールバック・時刻関数それぞれの採用元を一つに決める。C++実装ではCリンケージとシグネチャも確認する。
- カメラBSPのVsync/Frameコールバックはcamera_pipe2_bsp_付きの名前へ変更している。実験でも名前変更と転送先の整合を確認し、単に重複定義を隠さない。
- ライブラリ順の変更だけ、全ライブラリへの--whole-archive、--allow-multiple-definitionを解決策にしない。既存のstrong定義が二つある場合は、リンクするソースの選択を直す。
- map、最終ELFのシンボル、逆アセンブルを保存する。必要なコールバックがGLOBAL/strongであることだけでなく、採用されたオブジェクトと、ベクタ→IRQ→HAL→コールバックの呼び出し経路まで確認する。

ビルド可能になった後、ELFを実験の最終ELFのパスに設定して調べる例:

```sh
arm-none-eabi-nm -A -C --defined-only "$ELF" | grep -E 'HAL_(InitTick|GetTick|Delay)|DCMIPP.*(Callback|IRQHandler)'
arm-none-eabi-readelf -Ws "$ELF" | grep -E 'HAL_(InitTick|GetTick|Delay)|DCMIPP.*(Callback|IRQHandler)'
arm-none-eabi-objdump -d "$ELF" | less
```

nmのWはweak、Tは通常のグローバルコードシンボル。ただしTだけでは実験版である証明にならないため、mapの入力オブジェクトと照合する。リンク時に--trace-symbolで対象シンボルを追跡する方法もある。確認対象は実際に上書きする関数と実際のIRQ名に合わせて追加する。

## 実装と自動確認の順序

1. 設定を変えない基準版でPipe1/2・表示・診断を確認する。NPUを使う評価は同じモデルとメモリ配置で比較する。
2. shellのホストテストを通し、help / uptime / cam statを追加する。行長・引数上限、未知コマンド、CR/LF、バックスペース、出力内容を確認する。
3. consoleは非ブロッキング入力と処理量上限を持たせる。初版は同じ所有タスクで設定を適用し、成功後だけOK appliedを返す。別タスク化が必要になった場合も共有middlewareへ依存せず、実験内の要求/応答経路を用意する。
4. AE切替、目標、手動露出・ゲイン、統計領域、AWBを追加する。AE有効時の手動設定拒否、範囲外入力、停止/再開・復旧時の再適用を確認する。
5. 反転、FPS、Pipe2 ROIを別段階で追加する。座標変換、letterbox余白のクリップ、露出上限の再計算をホストで検証する。
6. AI露出は方式Aから開始する。被写体選択、微小変化の抑制、消失猶予、古い推論結果の扱いを検証し、不足が観測された場合だけ方式Bへ進む。

## 利用者が実施するテスト

実装者がビルド、リンク検査、ホストテスト、実機起動とPipe1/2確認まで行う。次の目視・操作・照明条件の試験は利用者にも実施してもらう。未実装項目はPASSではなく未実施と記録する。

### 準備と共通手順

- [AGENTS.md](../../AGENTS.md)に従い、ST-LINK/VCPが見える接続・権限・sandbox設定を確認する。デバイスが見えない場合、書き込みや実機確認を済んだ扱いにしない。
- 書き込み前にUARTを開き、ログを保存する。camera-controlは上記のconsoleまたはmake monitorを先に開いてから、別端末でram-runする。既存ai-appでの基準確認は、必要時にai-loadを先に実行する。
- モデルを変更した場合はai-loadを先に行い、評価に使ったモデルを記録する。比較元へ戻す場合もNORの内容との一致を確認する。
- 起動ごとにcamera: pipe1=started pipe2=startedを確認する。開始ログだけでなく、フレーム数の継続増加とLCDの更新も確認する。
- 初版はuptimeとframesを複数回取得してフレーム進行を比較する。独立した計測手段を接続した後にタスクCPU時間も比較する。ai-app側の基準測定にthread-monitor / cpu-task-monitorを使う場合も、表示構成とモデルが異なる数値を直接比較しない。ST-LINKのhalt区間は除外する。

| 段階 | 利用者の操作 | 合格条件 |
| --- | --- | --- |
| 基準版 | 通常の被写体で60秒表示し、起動し直して再確認 | 表示が更新され続け、Pipe1/2カウンタが増える。エラー・復旧回数に増分があれば原因を記録 |
| shell | help、uptime、cam stat、未知コマンド、長い行、連続入力を試す | 応答・エラーが妥当でハングしない。入力中も表示・推論が進む。ログ混在で操作不能にならない |
| AE / 手動 | AE有効時に手動設定を要求。その後AE無効化し、露出・ゲインを変更 | 有効時は拒否される。無効化直後は現在値を保持し、その後の設定が読み戻しと明るさへ反映される。量子化・クリップ範囲を記録 |
| 統計領域 / AWB | 明暗差のある被写体で領域と目標を変更し、AWBモードを比較 | 意図した領域へ露出が寄る。色の変化とAWBへの影響も記録する |
| 再開 / 復旧 | 実装済みの停止→再開、制御された復旧試験を実行 | 設定が保持・再適用され、両Pipeが再び進む。手動レジスタ破壊や動作中の配線抜き差しはしない |
| 反転 / FPS / ROI | 各機能を一つずつ変更。ROIは有効範囲と範囲外を試す | 向き・実測FPS・画角が意図どおり。無効値は拒否。Pipe2が止まらずCSIエラーが増えない |
| AI露出 | 顔を中央・端・逆光に置き、出入りさせる。全画面AEと比較 | 被写体輝度の目標との差、収束フレーム数、領域更新回数を記録。明滅・色かぶり・スループット悪化がないか確認 |

性能の許容差は基準測定後に決め、数値未設定のまま「性能劣化なし」と判定しない。開始前後のカウンタ、UARTログ、実機で目視した現象をセットで残す。

## 他の実験

- [UI・周期処理](../experiment-ui-control/README.md)
- [DMA2D・GPU2D](../experiment-gpu/README.md)
- [HW単体試験](../experiment-hw-test/README.md)
- [モデルロード](../experiment-model-load/README.md)
