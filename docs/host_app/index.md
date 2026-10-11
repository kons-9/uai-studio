# host_app

`host_app`は、μAI-Studioの開発・計測をPC側から支援するPythonツール群です。画面の配置設計、静的メモリ配置の生成、実機で記録したAI・CPUの実行データの解析を行います。組み込みアプリやドライバーとは別に動作し、統合GUIと個別CLIから利用できます。

各ツールの概要と使い方は下表の個別ページを参照してください。共通の環境準備と統合アプリの操作は[セットアップと統合I/F](usage.md)にまとめています。

## ツール一覧

| ツール（概要・使い方） | 統合CLI名 | 役割と主な入出力 |
| --- | --- | --- |
| [ai_model_monitor](ai_model_monitor.md) | `ai-model-monitor` | ThreadMonitorのraw dumpやJSONから、AIステップ時間の集計とタイムラインPNGを生成 |
| [cpu_task_monitor](cpu_task_monitor.md) | `cpu-task-monitor` | raw dumpやUARTログから、CPU使用率・ループ時間・スケジュール統計の図とJSON/CSVを生成 |
| [auto_static_memory_layout](auto_static_memory_layout.md) | `memory-layout` | メモリ設定と生成済みモデルから、配置JSON/YAML、C++ヘッダ、リンカスクリプトを生成 |
| [ui_designer](ui_designer.md) | `ui-designer` | 複数画面・8種類のウィジェットを編集し、レイアウトJSON、プレビューPNG、実機用C++ヘッダを生成 |

個別ページでは、各ツールを単独で実行するCLI/GUI、入力形式、生成物、結果の読み方、制約を説明しています。統合GUIを起動せずに利用できます。

### userspaceの関連ツール

統合CLIは次のツールへの入口も提供します。これらの実装は`host_app`本体ではなく、それぞれのuserspaceにあります。

| 統合CLI名 | 役割 | 概要・使い方 |
| --- | --- | --- |
| `model-loader` | モデル書き込み用データを扱うCLIを呼び出す | [model-loader](https://github.com/kons-9/uai-studio/blob/main/userspace/model-loader/README.md) |
| `feature-constraints` | UIの機能制約、状態遷移の検証・シミュレーション・C++生成 | [機能制約ツール](https://github.com/kons-9/uai-studio/blob/main/userspace/experiment-ui-control/tool/README.md) |

## GUIとCLI

引数なしの`python -m host_app`は統合GUIを起動します。UI Designer、Memory Layout、AI Monitor、CPU Monitorを同じブラウザ画面から操作でき、タブを切り替えても編集中の状態を保持します。機能制約エディタは`--features`で設定ファイルを指定した場合に追加されます。

各ツールはGUIから独立したCLIを持ちます。GUIは編集・実行・結果表示を担当し、解析や生成にはCLIと同じ処理を利用します。繰り返し実行や自動化にはCLI、配置の編集や結果の確認にはGUIを使えます。

## 実機側との関係

| 作業 | host_appの処理 | 実機側との接点 |
| --- | --- | --- |
| UI設計 | レイアウトからC++ヘッダを生成 | [ui](../middleware/ui.md)が生成した配置を使って描画・タッチ操作を処理 |
| メモリ配置 | 配置を解決し、ヘッダとリンカスクリプトを生成 | [memory](../middleware/memory.md)が生成した配置を参照。ai-appのビルドから自動実行 |
| AI計測 | 実行トレースを解析し、タイムラインを生成 | [ai_model_monitor](../middleware/ai_model_monitor.md)が記録したデータを使用 |
| CPU計測 | CPU使用率とループ時間を集計 | [cpu_task_monitor](../middleware/cpu_task_monitor.md)が記録したデータを使用 |

UIの編集や保存済みログの解析には実機は不要です。メモリ配置の解決には生成済みモデルが必要で、実機から新しい計測データを取得するには、別途ボードとデバッグ・UART接続が必要です。

## 開発での利用順序

1. UI Designerで画面の配置を編集し、実機用ヘッダを生成します。
2. モデルを準備し、Memory Layoutでバッファや予約領域を検証します。ai-appの通常ビルドでは配置の生成が自動実行されます。
3. 実機でアプリを実行し、AI・CPUの計測データを取得します。
4. AI MonitorとCPU Monitorで処理時間、CPU使用率、パイプラインの重なりを確認します。

## 関連資料

- [セットアップと統合I/F](usage.md): 共通の環境準備と統合GUI/CLI。
- [host_appのREADME](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md): 個別ツールのREADMEと開発者向け情報。
- [model-loaderのREADME](https://github.com/kons-9/uai-studio/blob/main/userspace/model-loader/README.md): モデルローダーの構成と操作。
- [機能制約ツールのREADME](https://github.com/kons-9/uai-studio/blob/main/userspace/experiment-ui-control/tool/README.md): 制約の定義、検証、生成。