# experiment-ai APP 縮小・容量予算計画

## 目的

[実装設計](implementation/README.md) の新トレース基盤を載せる前に、[experiment-ai](../userspace/experiment-ai/README.md) の実行イメージと内部 RAM の空きを把握・確保する。**機能を壊してまでサイズだけを下げない**。Pipe1 表示、Pipe2 入力、3 モデル推論、既存の障害解析手段を維持する構成を標準とし、単一モデル版は容量制約が強い場合の別プロファイルとして設計する。

ここでいう「APP 縮小」は一つの数字ではない。① ELF の APP 領域の配置と残量、②ロード／署名する APP イメージのサイズ、③内部 RAM の常駐状態・stack・追跡バッファ、④モデル用 NOR／NPU RAM、⑤PSRAM を**別々に**測り、制約に直接効く変更だけを選ぶ。

## 現時点でコードから確定できる制約（サイズ実測ではない）

| 項目 | 現行の配置・予約 | 何に効くか |
|---|---|---|
| APP | [リンカスクリプト](../userspace/experiment-ai/stm32n6570-dk-npu-ram.ld) の `0x34000400` から `1023K`、末端 `0x34100000` | 実行コード、データ、NOLOAD、常駐 stack、計測領域の合計。**`.text` と `.rodata` の実使用量は未測定** |
| 固定 RAM 起動エントリ | `.text.uai_ram_entry` を `0x34052000` に置く | `.text`、`.rodata`、初期化配列が入る起点 `0x34000400` からの区間は最大 **327 KiB**（配置・整列を含む）。ここを越すなら APP 全体に空きがあってもリンクできない。直前の余白も map で測る |
| ThreadMonitor 記録 | APP 内の `.sample_ai_thread_monitor (NOLOAD)` に `0x20000` = **128 KiB** | APP のアドレス空間を占有するが、記録データ自体は `experiment-ai.bin` に書き込まれない。縮小しても APP バイナリが同量小さくなるとは限らない |
| アプリのタスク stack | [TaskContext](../userspace/experiment-ai/src/task/task_context.hpp) の初期化 32 + カメラ 32 + 推論 16 + 後処理 16 = **96 KiB** | APP の `.bss`。安全な削減量は high-water 未計測のため**不明**。初期化タスクは初期化後も sleep している |
| モニタ側 | [ThreadMonitor 実装](../userspace/experiment-ai/src/npu_runtime/thread_monitor/thread_monitor.cpp) の stack **4 KiB** と、[pending events](../userspace/experiment-ai/src/npu_runtime/thread_monitor/thread_monitor.hpp) **512 件** | APP の `.bss`。pending レコードの実サイズと上書き件数は ELF／実機で確認する |
| heap／stack 予備 | リンカの `.heap` `0x1000`、`.stack_dummy` `0x1000` と `_Min_Stack_Size = 0x800` | 前二者は APP の NOLOAD 予約。`_Min_Stack_Size` は `_sstack` 計算にも使われるため、単純加算や削除はしない |
| モデル重み／command blob | [モデル配置](../userspace/experiment-ai/models/README.md) と [ビルド定義](../userspace/experiment-ai/CMakeLists.txt) により外部 NOR へ別配置・別イメージ出力 | 重みや NOR 上の blob を削っても直ちに APP イメージ分は減らない。ランタイム command buffer／activation は生成物と map で**実際の配置先**を確認する |
| capture／display／inference／raw dump | リンカが外部 PSRAM に NOLOAD で予約 | PSRAM を削っても APP の空きにはならない。外部メモリ容量や帯域の課題として別管理 |

**未測定:** 現在の作業ツリーに有効な experiment-ai ELF／map／bin が見つからず、`.text`・`.rodata`・`.bss`、APP の実際の残量、NOR のコスト、最大 object、署名イメージ長は数値化していない。古い [experiment-ai/build の CMake キャッシュ](../userspace/experiment-ai/build/CMakeCache.txt) は別の作業パスを示しており、現在のビルド結果として使わない。過去資料の推定 KiB や「削減見込み」を実測値として採用しない。

## フェーズ A：容量表と動作基準を作る（最優先）

1. 現行の再現可能な条件で `APP_TARGET=experiment-ai` を configure/build し、生成された ELF／map／`experiment-ai.bin`／モデル別 blob の実ファイル、コンパイラ設定、コミット、モデル生成版を保存する。生成に必要な CubeMX／STEdgeAI がない場合は先に環境を整え、古いキャッシュの数字を使わない。
2. `arm-none-eabi-size -A`、`arm-none-eabi-readelf -S -l`、`arm-none-eabi-nm -S --size-sort`、map の input section／`--print-memory-usage` を突き合わせる。表には `.text`＋`.rodata` が固定エントリ直前までに収まる余裕、APP 全体の使用末尾・未使用量、`.data`／`.bss`／NOLOAD、最大シンボルと由来 object、**ELF 中の NOR セクションと APP bin の差**を記録する。単純な `size` 合計だけで領域超過を判定しない。
3. リンカの APP 終端と `_sstack`／`_estack`、スタックの高水位、カメラと推論の並行動作、ThreadMonitor の書込み速度・上書き件数を実機で測る。stack の高水位は各タスクの未使用領域を poison 等で初期化して走査する方法を**先に検証**し、DMA／キャッシュと競合させない。
4. 予算を `固定エントリ手前の余白`、`APP 全体の余白`、`新しいトレースに割ける常駐 RAM` の 3 つで設定する。予算が不足すれば [トレーシング基盤設計](implementation/tracing.md) の新リングを追加せず、既存記録との共有やイベント削減を先に検討する。

## フェーズ B：目的別の削減候補（一件ずつ測って採否を決める）

| 順 | 候補と変更箇所 | 効く領域／分かっている上限 | 採用ゲート・副作用 |
|---|---|---|---|
| B1 | [toolchain のフラグ](../cmake/toolchain.cmake) は既に `-ffunction-sections -fdata-sections`、`--gc-sections`。これを保持したまま APP 側と大きい依存単位で `-Os`／`-Oz`（ツールが対応すれば）を**別ビルドで比較**。[experiment-ai CMake](../userspace/experiment-ai/CMakeLists.txt) の `-O3` 上書きと kernel／BSP の設定も確認 | `.text`／`.rodata`、bin。削減量は map の差分のみで決める | 既存のカメラコピー、後処理、NPU submit の実行時間と p95、CSI／Pipe2 drop を測定。速度低下が厳しければ遅い translation unit のみ `-O3` に戻す |
| B2 | map の**実際の**上位 object／文字列／デバッグ経路を確認し、不要な register dump・raw dump・詳細 UART、未使用機能をコンパイル時選択にする。ログを外すときも最低限の起動／Pipe1/2／エラー記録は残す | 主に `.text`／`.rodata`、小さい `.bss`。候補別の節約量は未測定 | 使っていない条件分岐なら GC が既に除去している可能性がある。診断を削って原因追跡できなくならないか実機で確認 |
| B3 | [既存 ThreadMonitor](../userspace/experiment-ai/src/npu_runtime/thread_monitor/README.md) の記録範囲と上書き頻度を測り、128 KiB と新リングの**二重予約を避ける**。容量可変化・共有・イベント間引きを比較 | APP の NOLOAD：128 KiB の一部を再配分可能。`128→64 KiB` は **64 KiB のアドレス空間**削減という条件付き例で、bin の削減保証ではない | 30 秒以上の dense epoch 記録を取り、時間窓・上書き・障害直前データ保持・ホストデコードを再評価。根拠なしに先に半減しない |
| B4 | high-water と安全余裕の実測後、[TaskContext](../userspace/experiment-ai/src/task/task_context.hpp) の 96 KiB とモニタ stack 4 KiB を**タスクごとに**見直す。初期化後に初期化タスクを終了／stack 再利用する案は寿命管理を別途設計 | `.bss`／APP 空間。削減量は個別 stack 設定の差分；bin は基本不変 | 最悪経路・失敗時の詳細ログ・3 モデル切替え・長時間動作で canary と high-water を確認。全 stack 一括縮小はしない |
| B5 | [pending event キュー](../userspace/experiment-ai/src/npu_runtime/thread_monitor/thread_monitor.hpp) 512 件の最大滞留と欠落を実測し、容量／形式／集約周期を調整 | `.bss`／APP 空間。1 件の実サイズ × 削減件数が上限 | NPU epoch が密な瞬間も欠落を検知できること。間引いたイベントは UI で欠落と区別 |
| B6 | なお必要なら 3 モデル維持版と単一モデル版を分け、生成モデルラッパー／後処理／NOR イメージから除外できる範囲を試算する | `.text`／`.rodata`／NPU 用 RAM／NOR のどれが減るかを別記；実行中モデルだけのactivation共有との混同を避ける | 3 モデル同時リンクとランタイム切替えは標準構成で維持。容量に応じた別プロファイルなら単一モデル化も選択し、モデル切替え試験を失うことを明記 |

**禁止する近道:** `-g0`／ELF strip だけで実行時 RAM が空くと見なす、`--gc-sections` を新規節約として二重計上する、NOR blob の preload を止めれば APP が空くと根拠なしに言う、PSRAM の raw dump 4 MiB を消して APP 対策と呼ぶ、起動エントリ `0x34052000` をサイズだけのために無検証で移動する。ロード用 bin には固定配置のギャップも含み得るため、bin の長さだけから `.text` サイズを推定しない。

## 優先順位と停止条件

**着手順:** A（測定）→ B1／B2（コード）→ B3（トレース予算）→ B4／B5（RAM 常駐）→ B6（別プロファイル）。A の結果が「固定エントリ手前」の制約なら、NOLOAD／PSRAM だけを削っても解決しない。APP 総量の制約なら逆にコード削減だけで足りると決めつけない。画像／blob が NOR と PSRAM に分離されているため、容量表に「領域」と「差分」を必ず添える。

採用基準は**リンク成功かつ予算に必要な余裕ができること**、同じ条件で計測なし／変更前／変更後を比較して Pipe1 表示・Pipe2 入力・NPU 推論とエラー傾向を損なわないこと。目標空き KiB や「劣化許容率」は基準測定後に決め、数値を捏造しない。1 変更＝1 比較表とし、悪化したら撤回する。

## 共通実機ゲート

コード・リンカ・ビルド設定を変更した**各候補ごと**に、UART を先に準備し、ST-LINK／UART のデバイス権限・sandbox 設定を確認してから実機へロード／書き込む。[検証チェックリスト](../userspace/experiment-ai/VERIFICATION_CHECKLIST.md) に従い、UART で起動と Pipe1/2 の開始・sequence 進行、NPU 完了を確認し、LCD 実表示を目視確認する。30 秒以上継続させ、モデル切替え、CSI エラー、Pipe2 drop、復旧件数、推論 p95、トレース欠落と APP 使用量の前後差を記録する。実機確認できない変更は「採用済み」にしない。