# トレーシング基盤

**優先度:** 最優先。全ての分析に渡すデータ契約。容量判定は [APP 縮小計画](../app-size-reduction.md) に従う。

## 作るもの

- sample-ai と共存する軽量な静的トレースポイント、容量上限のあるバイナリ記録、ST-LINK/SWD からのダンプ手段、ホスト側デコーダ。取得・解析はホスト主体にする。
- 記録を正規化した「イベント列＋取得条件／品質情報」を [通常タスクと NPU のモニタ](thread-monitor.md)、[ボトルネック検出](bottlenecks.md) 等に渡す。既存 [ThreadMonitor](../../userspace/sample-ai/src/npu_runtime/thread_monitor/README.md) の生形式は比較用に扱い、新形式と互換とは仮定しない。

## データ契約と収集

- セッションヘッダ: magic／format version／record size／build ID／boot ID／CPU 周波数／記録開始・終了／write index／上書き・producer 側欠落件数。イベント: sequence、時刻（取得元と単位を明示）、type、task ID または ISR、frame `capture_sequence`、model ID、必要な payload。イベントごとに使わないフィールドは明示的に無効値とする。
- v1 イベントは `PIPE1_FRAME`、`PIPE2_FRAME`、`FRAME_DROP(reason)`、`FRAME_ENQUEUE/DEQUEUE`、`NPU_SUBMIT/DONE`、`CSI_ERROR`、`BUFFER_STATE`、`TASK_STATE` に絞る。後続のイベントを追加しても古いデコーダが未知 type を飛ばせるようにする。同一フレームの実際の ID は [TaskContext とカメラ経路](../../userspace/sample-ai/src/task/task_context.hpp) で確認する。
- まず userspace の queue／ドライバ境界から記録。カーネルのコンテキストスイッチ記録は ISR・再入・割込みマスク・スケジューラとの相互作用を検証してから追加する。ホットパスでは `tk_get_tid()` や printf を毎回呼ばず、分かっている task ID を渡す。
- µT-Kernel/DS の `td_hok_dsp` と `td_hok_int` を利用できるようにし、ディスパッチ境界と `TA_HLNG` 割込みハンドラ境界をトレースの入力にする。`USE_DBGSPT_TRACE` はコンパイル時定数で既定値を 0 とし、無効ビルドではホットパスのフック呼出し自体を生成しない。`UAI_KERNEL_TRACE_HOOKS=ON` は同じ設定をCMakeから有効化する。
- STM32N6570-DK の BSP は `TK_TRAP_SVC=FALSE` の直接C関数呼出し方式であるため、`td_hok_svc` の設定APIは提供するが、SVCトラップ経路のイベントは生成しない。SVCフックを使うポートでは、そのポートのSVC入口／出口から同じ保存済み定義を呼び出す。
- DWT cycle は短い区間に使い、回り込みを跨ぐイベントやクロックの変更は補正可能な情報がある場合だけ復元する。長時間の並びは RTOS の単調時刻と同期点で表現し、どちらもリセットを跨いで連続した時刻とは見なさない。
- 複数 producer の slot 予約／publish 手順、cache 可視性、commit marker を規定する。ホストは途中書込み・不正ヘッダ・欠落を検出し、その区間の統計を作らない。アプリ停止後の SWD 取得を標準とし、UART の高頻度送信は後述の動的計測で実測後に判断する。

## APP 容量による構成選択

| 予算 | ファームウェア側 | ホスト側 |
|---|---|---|
| 余裕なし | 既存モニタの収集経路を調査して共有／形式移行を優先し、追加リングを予約しない。フレーム境界とエラーカウンタに限定 | 既存ダンプと低頻度ログから扱える情報だけ表示 |
| 小さい余裕 | 既存領域との二重予約を避けた小リング、軽量固定イベント、対象 task／イベントをビルド設定で限定 | 上書き前提で直近窓のみ分析し、欠落を可視化 |
| 余裕あり | 詳細 stage／NPU epoch とタスク切替イベントを追加。事前に record 生成速度から保持可能な時間を計算 | 高密度表示や相関分析を有効化 |

空きがない場合にも大きな新規 RAM を「必要経費」として確保しない。コード量の上限（固定起動エントリ手前）と NOLOAD の上限（APP 全体）は別に判定する。

## 完了条件

wrap／上書き／破損／時刻回り込み／warm reset／異なるビルド ID を合成ダンプで検証し、実機で Pipe2 受付→NPU 完了の同一 ID を追える。記録有無の同一条件比較で表示・推論・CSI エラーへの影響を報告し、[共通の実機ゲート](README.md#共通の検証ゲート)を通す。
