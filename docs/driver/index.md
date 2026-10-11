# ドライバー

`kernel/driver`はSTM32N6570-DKの周辺機能を扱うドライバー層です。アプリから使う公開APIと、HAL/BSPやレジスタを操作する実装を分離しています。CMakeターゲットは`uai::drivers`と`uai::driver_overrides`です。

## HWとドライバーの対応

| HW・機能 | ドライバー | 主な用途 |
| --- | --- | --- |
| [カメラ](camera.md): IMX335、CSI、DCMIPP、ISP | `camera::CameraManagement` | 表示用と推論用の画像取得、露出制御 |
| [LCD](lcd.md): LTDC、800x480 LCD | `lcd::LcdManagement` | カメラ画像、検出結果、UIの表示 |
| [DMA2D](dma2d.md) | `dma2d::Dma2dManagement` | コピー、画素形式変換、塗りつぶし、ブレンド |
| [タッチ](touch.md): GT911、I2C2 | `touch::TouchManagement` | タッチ座標のポーリング |
| [NPU](npu.md): Neural-ART | `npu::NpuManagement` | 生成モデルのロード、切り替え、推論 |
| [キャッシュ](cache.md): CPU D-cache、CACHEAXI | `cache::CacheManagement` | CPUとDMA/NPU間のキャッシュ整合 |
| [PSRAM](psram.md): APS256XX、XSPI1 | `psram::PsramManagement` | 外部PSRAMのメモリマップ |
| [NOR Flash](nor.md): MX66UW1G45G、XSPI2 | `nor::NorManagement` | 外部NOR Flashのメモリマップ |
| [RIF](rif.md) | `rif::RifManagement` | 外部メモリなどへのアクセス権設定 |
| [コンソールUART](console.md): USART1 | `console::ConsoleManagement` | ログ送信とシェル入力 |
| [汎用周辺機能](peripheral.md): RNG、HASH、CRC、GPDMA/HPDMA、TIM2、RTC | `peripheral::PeripheralManagement` | 乱数、ハッシュ、転送、時刻と計測 |
| [ボード共通処理](board.md) | `driver::board` | HAL時刻、割り込み優先度、レジスタ診断 |

## 読み方

初めて使う場合は[共通ルールと初期化順序](common.md)から読みます。各HWのページでは、接続・データ経路、初期化条件、公開API、ユースケース、制約と診断方法を説明します。

ここで扱うのは共有ドライバーです。`userspace/experiment-*`のローカル実装は独立しており、共有ドライバーの変更やAPI説明がそのまま適用されるとは限りません。汎用周辺機能は`UAI_DRIVER_PERIPHERAL_TESTS`構成で追加されるため、利用時は対象アプリのビルド設定も確認してください。

## ワークフローから選ぶ

| 目的 | 使用するHW・API |
| --- | --- |
| カメラプレビュー | カメラの`TakeCompletedCapture()`からLCDの`ComposeAndPresent()`へ渡す |
| カメラ画像を推論 | Pipe2取得、`SnapshotInferenceSource()`、NPU実行、出力取得、フレーム返却 |
| タッチで画面操作 | タッチの`Read()`をUIに渡し、LCDのoverlayとして描画 |
| 自前の画像を転送・合成 | DMA2Dの`Transfer()`。resizeはmiddlewareなど別の経路 |
| 起動や停止原因を調べる | コンソールUART、カメラ診断、ボードのレジスタダンプ |
| 転送・乱数・チェック値・時刻を検証 | 汎用周辺Driverの各操作をAccessorから呼ぶ |

## 関連ドキュメント

- [ビルド構成](../kernel/build.md): ドライバーのリンクとHAL/BSP依存。
- [μT-Kernel](../kernel/utkernel.md): タスク、HALのtick、割り込み登録。
- [resource_ownership](../middleware/resource_ownership.md): ドライバーの所有権と排他。
- [memory_manager](../middleware/memory_manager.md): 画像バッファの確保と返却。
- [ドライバ初期化チュートリアル](../tutorial/05-driver-init.md): 最小アプリの初期化例。