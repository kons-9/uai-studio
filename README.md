# μAI-Studio

μT-Kernel 3.0上のRTOSアプリケーション開発をAIで支援する分析・可視化ツール群。2つの動作モードを持つ。

1. **トレースモード** — 本番Appと共存するeBPF着想トレーシング基盤を組み込み、タスクモニタ・スケジューリング分析・ボトルネック検出・メモリ分析をPC側ダッシュボードで可視化・AI分析する。
2. **プロファイルモード** — マイコン上のMCPサーバにPC側AIエージェントがJSON-RPC経由で接続し、OTAローダーで関数バイナリを動的デプロイして関数単位の性能計測を対話的に行う。

## アーキテクチャ

```
┌─────────────────────────────┐     ┌──────────────────────────────────┐
│  MCU (μT-Kernel 3.0)        │     │  PC (Python)                     │
│                              │     │                                  │
│  ┌─────────────────────┐    │     │  ┌───────────────────────────┐   │
│  │ Tracing Infrastructure│   │UART │  │ Task Monitor              │   │
│  │ ├ Tracepoints        │◄──┼─────┼──│ Scheduling Dashboard      │   │
│  │ ├ Ring Buffer        │   │ TCP  │  │ Bottleneck Detector (AI)  │   │
│  │ ├ Dynamic Hooks      │   │     │  │ Memory Monitor            │   │
│  │ └ Mini VM (filter)   │   │     │  └───────────────────────────┘   │
│  └─────────────────────┘    │     │                                  │
│                              │     │  ┌───────────────────────────┐   │
│  ┌─────────────────────┐    │     │  │ MCP Client                │   │
│  │ MCP Server (JSON-RPC)│◄──┼─────┼──│ Probe Compiler            │   │
│  │ ├ Resources (read)   │   │     │  │ Dashboard (unified)       │   │
│  │ └ Tools (execute)    │   │     │  └───────────────────────────┘   │
│  └─────────────────────┘    │     │                                  │
│                              │     │                                  │
│  ┌─────────────────────┐    │     │                                  │
│  │ OTA Loader           │   │     │                                  │
│  │ ├ Probe Arena (RAM)  │   │     │                                  │
│  │ └ Binary Deploy/Call │   │     │                                  │
│  └─────────────────────┘    │     │                                  │
└─────────────────────────────┘     └──────────────────────────────────┘
```

## ディレクトリ構成

```
uai-studio/
├── tracing/                    # トレースモード
│   ├── firmware/               # MCU側 (C++17)
│   │   ├── tracepoint.h        # トレースポイント定義・マクロ
│   │   ├── ring_buffer.h       # ロックフリーSPSCリングバッファ
│   │   ├── trace_hook.h/cpp    # 動的フック基盤・TraceEngine
│   │   ├── trace_sender.h      # トレースイベント送信
│   │   ├── stack_monitor.h     # スタック使用量モニタ
│   │   └── mini_vm/            # eBPF着想フィルタVM
│   │       ├── bytecode.h      # バイトコード命令セット定義
│   │       └── vm.h            # ミニVM実行エンジン
│   └── host/                   # PC側 (Python)
│       └── task_monitor.py     # タスクモニタ
│
├── profiling/                  # プロファイルモード
│   ├── firmware/               # MCU側 (C++17)
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── mcp/
│   │   │   ├── json_parser.h   # 軽量JSONパーサ/ライター
│   │   │   └── mcp_server.h    # MCPサーバ (JSON-RPC)
│   │   └── ota/
│   │       ├── probe_arena.h   # プローブメモリアリーナ
│   │       └── ota_loader.h    # OTAファンクションローダー
│   └── host/                   # PC側 (Python)
│       ├── mcp_client.py       # MCPクライアント
│       └── probe_compiler.py   # プローブコンパイラ
│
├── analyzer/                   # 分析ツール
│   ├── web/                    # Rust/WASMダッシュボード
│   │   ├── Cargo.toml
│   │   ├── index.html
│   │   └── src/                # Rustモジュール群
│   └── cli/                    # CLIダッシュボード
│       └── dashboard.py        # 統合ダッシュボード
│
└── tests/
    └── test_tools.py           # テストスイート
```

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

## クイックスタート

### Webダッシュボード（推奨）

Rust/WASMベースのオフライン解析ダッシュボード。MCUリングバッファのダンプをブラウザでインポートし、タスク・メモリ・ボトルネックを可視化。

```bash
cd analyzer/web
wasm-pack build --target web
# index.html をブラウザで開く
```

### 統合ダッシュボード（CLI）

```bash
# モード一覧表示
python analyzer/cli/dashboard.py

# トレースモード（デモ）
python analyzer/cli/dashboard.py trace --demo

# プロファイルモード（デモ）
python analyzer/cli/dashboard.py profile --demo
```

### タスクモニタ

```bash
python tracing/host/task_monitor.py --demo          # デモモード
python tracing/host/task_monitor.py --port 5001      # 実機接続モード
```

### MCPクライアント（プロファイルモード）

```bash
python profiling/host/mcp_client.py --demo                  # デモモード
python profiling/host/mcp_client.py --tcp 192.168.1.100:5005  # TCP接続
python profiling/host/mcp_client.py --port COM3             # シリアル接続
```

### プローブコンパイラ

```bash
python profiling/host/probe_compiler.py list-templates      # テンプレート一覧
python profiling/host/probe_compiler.py compile my_probe.c -o my_probe.bin
python profiling/host/probe_compiler.py deploy my_probe.c --tcp 192.168.1.100:5005
```

## テスト

```bash
cd uai-studio
python -m pytest tests/ -v
```

## MCU側ファームウェアビルド（シミュレーション）

```bash
cd profiling/firmware
cmake -B build -DUAI_PLATFORM_SIM=ON -DUAI_TRACE_MODE=DYNAMIC
cmake --build build
```

## 関連

- [μAI-Bridge](../uai-bridge/) — ミドルウェア部門作品（マイコン側通信基盤）
