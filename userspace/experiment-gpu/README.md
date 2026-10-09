# experiment-gpu

## 状態と目的

CPU参照Copy/Convert/Fill/Blend/最近傍Resize、DMA2D実転送とHALモック、起動時の一括シナリオを実装済み。GPU用の1ファームを1回起動して検証する。NPUとモデル書き込みは対象外。STM32N657にはNeoChrom GPU2Dが搭載されるが、この実験はGPU2Dを呼び出さない。`gpu2d=UNAVAILABLE`はハードウェア未搭載ではなく、この実験でGPU2Dを未接続・未検証という意味。NemaGFXの統合とGPU2D用ボード設定は未実施。STM32N6570-DKでARMビルドと実機DMA2Dシナリオを確認済み。kernel/driver・middlewareには依存しない。

## 実装済みと実行方法

```sh
make -C userspace/experiment-gpu test
make -C userspace/experiment-gpu generate
```

先にUARTを開き、そのまま別端末でRAMへ書き込む。専用の`build-experiment-gpu`と既存のcamera-pipe2ボード設定を使い、カメラ制御の自動シナリオはリンクしない。ホスト固有のCubeMX/CubeN6、ツールチェーン、ST-LINK設定は[設定例](../../build-system/host-config/local.mk.example)を参照する。

```sh
make -C userspace/experiment-gpu monitor
```

別端末:

```sh
make -C userspace/experiment-gpu ram-run
```

UARTの`camera: pipe1=started pipe2=started`を確認すると、追加コマンドなしで全シナリオが進む。各ケースを約600ms表示してから次へ進み、60秒の負荷試験も行うため、完了まで約75秒。途中のFAILも集計に残し、残りの条件を同じ起動で検証する。起動時にDMA2DでRGB565の色帯とシナリオ表示を作り、カメラ映像と同じLTDC Layer 0のフレームバッファへ描画する。画面右上の大きな色帯と判定表示、左上のケース番号で進行を確認できる。UARTはフレームバッファへのDMA2D書き込み後に画素を読み返して確認する。`gpu visual off|on`で表示を切り替えられる。再実行は`gpu scenario start`、状態確認は`gpu scenario stat`。実行中はカメラ設定・capture操作と単発GPU比較をロックする。

## 一括シナリオ

| 条件 | 判定 |
| --- | --- |
| RGB888/RGB565コピー、双方向変換、RGB565/RGB888の赤・緑・青Fill | CPU参照と画素・バイト一致 |
| Alpha 0/128/255のBlend、奇数寸法と行末padding | Alpha端点は完全一致。中間値はRGB565で最大8、RGB888で最大1のチャンネル差 |
| 入力/output重複、background/output重複、短いバッファ、不正stride、非整列、DMA2D Resize | 処理を拒否し、バッファを変更しない |
| 拒否後の正常転送 | 同じDMA2Dインスタンスを再使用して画素一致 |
| カメラ・LCDと60秒併用 | 18条件を繰り返し、毎回比較。1.5秒以内に両Pipeが進行し、camera/display処理エラー・復旧の増加がない |

合計26項目。各転送で入力パターンを更新し、行末余白・未使用領域・前後32バイトのガード・入力の不変性を検査する。ガードを含むバッファ全体をDMA前後にcache同期する。画像は専用SRAMの64x32、padding条件は63x31。PSRAM全画面転送の帯域試験ではない。

出力例:

```text
GPU VISUAL PASS dma2d=pixel-checked output=layer0 status=pending
GPU VISUAL PASS framebuffer=layer0 readback=ok
GPU SCENARIO START cases=26 case_interval_ms=600 stress_ms=60000 npu=excluded
GPU SCENARIO 01/26 copy-rgb888 PASS cycles=... max_channel_error=0 corrupted_bytes=0
GPU SCENARIO SUMMARY dma2d=PASS pass=26 fail=0 transfers=2022 gpu2d=UNAVAILABLE npu=excluded visual=required
```

これはDMA2Dの自動判定であり、GPU2Dの合格ではない。色帯はDMA2DからLTDCの表示フレームバッファへの出力を目視するための目印であり、カメラ画像全体の色・行ずれ・欠けは別途目視が必要。既存のcamera/display処理エラーカウンタを監視するが、CSI個別エラーやLTDCアンダーラン専用カウンタは未接続。

## 構成と残る確認

[scenario.hpp](src/scenario.hpp)が条件一覧・時間管理・集計、[verification.hpp](src/verification.hpp)が画像生成と破損検査、[firmware.cpp](src/firmware.cpp)が起動・UART・カメラ監視を担う。[blit.hpp](src/blit.hpp)と[operations.hpp](src/operations.hpp)がCPU参照、[dma2d.hpp](src/dma2d.hpp)がHALの有限待ちと失敗後のabort/resetを実装する。`gpu cpu|dma2d copy|convert|fill|blend|resize`の単発比較も残す。

ホスト試験は画素比較の破損検出、HAL/cache操作順序、timeout時のabort/reset、集計、Pipe停止・処理エラー、時刻wrap、自動開始・操作ロック・再実行を検証する。timeout/resetはHALモックでの確認であり、実機の強制エラー試験ではない。DMA2Dはpolling方式で、完了IRQの試験は行っていない。

GPU2Dをこの一括シナリオへ追加するには、対応版NemaGFXとライセンス、GPU2Dの実験用IOC、RIF/RISAF設定、コマンド領域・割り込み待機の接続が必要。共有IOC、生成済みHAL、既存LCDドライバ、image_resizer、ai_runtime、pre-kernel本体は変更しない。既存設定で満たせない変更は実施前に確認する。GPU2Dの未接続やホスト成功を実機検証済みとして扱わない。
