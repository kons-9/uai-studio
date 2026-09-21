# μAI-Studio
とりあえずターゲットはSTM32N6570-DK

## ビルド、RAM実行、外部Flash書き込み

STM32N6570-DK向けの最初のサンプルは、CMakeでビルドし、開発モードの
Secure AXI SRAMへpyOCD経由でロードして実行します。

```sh
make build

make ram-run \
  PYOCD_PACK=/tmp/Keil.STM32N6xx_DFP.1.2.0.pack
```

`ram-run`は開発用のRAMロードです。CubeMX生成、署名、電源断後も保持される
外部Flash書き込みは次のCLIターゲットを使います。

```sh
make generate CUBEMX_SCRIPT=/absolute/path/to/generate-project.txt

make sign STM32_SIGN_INPUT=/absolute/path/to/input.bin \
  STM32_SIGN_OUTPUT=/absolute/path/to/signed.bin \
  'STM32_SIGNING_ARGS=-nk -of 0x80000000 -t fsbl -hv 2.3 -align'

make program STM32_EXTERNAL_LOADER=/absolute/path/to/MX66UW1G45G_STM32N6570-DK.stldr \
  STM32_PROGRAM_IMAGE=/absolute/path/to/signed.bin \
  STM32_PROGRAM_ADDRESS=0x70000000
```

`STM32CubeMX`、`STM32_SigningTool_CLI`、`STM32_Programmer_CLI`をPATHに追加
していない場合は、それぞれ`*_EXECUTABLE`または`*_CLI`に実在する絶対パスを
指定してください。`/path/to/...`は説明用のプレースホルダーで、そのままでは
実行できません。

個別に実行する場合は`make configure`、`make build`、`make attach`、`make clean`も
使用できます。`make flash`は外部Flash書き込みの別名です。

pyOCDはプロジェクトの`.venv`にインストールできます。CMSIS-DAPパックの
取得方法と、STM32N6のFSBL・外部Flash/XIPイメージについては
[`userspace/sample0/README.md`](userspace/sample0/README.md)を参照してください。

μT-Kernel 3.0上のRTOSアプリケーション開発をAIで支援する分析・可視化ツール群。2つの動作モードを持つ。

1. **トレースモード** — 本番Appと共存するeBPF着想トレーシング基盤を組み込み、タスクモニタ・スケジューリング分析・ボトルネック検出・メモリ分析をPC側ダッシュボードで可視化・AI分析する。
2. **プロファイルモード** — マイコン上のMCPサーバにPC側AIエージェントがJSON-RPC経由で接続し、OTAローダーで関数バイナリを動的デプロイして関数単位の性能計測を対話的に行う。

## 主な機能

### トレースモード（本番App共存）

- **タスクモニタ** — RTOSタスクのリアルタイム状態監視、CPU使用率表示
- **スケジューリングダッシュボード** — 推論パイプラインのタイミング可視化、レイテンシ分解
- **ボトルネック検出 (AI)** — CPU過負荷タスク検出、優先度逆転検出、デッドラインミス予測、リソース競合分析、スケジューリングジッター分析
- **メモリモニタ** — バッファオーバーフロー/アンダーフロー、use-after-free、double-free、スタックオーバーフローのリアルタイム検出
- **Webダッシュボード** — ブラウザベースのトレースダンプ解析GUI。MCUリングバッファのダンプ(UART/デバッガ)をインポートし、タスク・メモリ・ボトルネックをChart.jsで可視化。MCUへの通信負荷ゼロ

### プロファイルモード（MCP対話型）

- **MCPサーバ** — μT-Kernel上の軽量JSON-RPCサーバ。リソース（タスク一覧、メモリ状態、計測結果）とツール（プローブデプロイ/実行/削除）を提供
- **OTAローダー** — PC側から関数バイナリを受信、RAMの専用アリーナに配置、callして実行時間を計測
- **プローブコンパイラ** — C関数をPIC (position-independent code) としてクロスコンパイルし、MCP経由でマイコンに転送
- **MCPクライアント** — PC側のインタラクティブCLI。AIエージェントがRTOS内部を探索・計測・分析

### MCU側トレーシング基盤

- **eBPF着想設計** — 静的トレース（コンパイル時フック）と動的トレース（ランタイムattach/detach）をコンフィグで切替
- **ミニVM** — ループ禁止・命令数上限・読み取り専用アクセスの安全なバイトコードフィルタエンジン
- **ロックフリーリングバッファ** — ISR→タスク間のSPSCバッファ（1.3KB〜20KB選択可能）
