# ボトルネック検出

**優先度:** 高。依存: [トレース](tracing.md)、[スレッドモニタ](thread-monitor.md)、可能なら [スケジューリング分析](scheduling.md)。

## 作るもの

- Pipe2 の capture sequence を相関キーとして、受付→queue→モデル選択・入力準備→NPU submit／完了→後処理→結果の表示への反映を一つの因果グラフにする。Pipe1 表示は独立経路として扱い、枠は必ずしも同世代の映像とは限らない（[設計](../../userspace/experiment-ai/DESIGN.md)）。
- frame ごとに queue 待ち、CPU 準備、NPU 関連 wall time、後処理、表示反映待ちを表示。モデル別の p50／p95／max、スループット、入力フレーム鮮度、drop reason、CSI エラー・復旧回数を比較する。
- 出力は「観測された長い区間」「ボトルネック候補」「データ不足／判定不能」の三区分＋該当イベントへのリンク。負荷の重なりは critical path 上でのみ加算し、prefetch と NPU が重複した時間を合計しない。

## 診断ルールの順序

1. 完了しなかったフレームの最終観測点と drop reason（Pipe2 用バッファ不足、queue latest-wins、NPU エラー等）を特定。送られていない frame は推論遅延に含めない。
2. 完了したフレームで「queue で待った」「入力準備にかかった」「NPU submit→done が長い」「postprocess が滞留」「結果は出たが表示が遅い」を測定可能なイベントで区別する。
3. CSI エラーと NPU 実行の時刻を並べ、Pipe2 のみ／NPU 併用の対照実験で差を示す。相関は原因確定でない。優先度変更やバッファ増加は試す仮説として扱う。

## APP 依存

最低限の `FRAME_ENQUEUE/DEQUEUE`・`NPU_SUBMIT/DONE`・drop/error カウンタがあればホストだけで粗い経路分析は可能。詳細 stage・IRQ・kernel dispatch が取れたら細分化する。必要な時だけ計測を増やし、問題の再現性を損なう場合は詳細記録を外して情報不足を明示する。

## 完了条件

queue 滞留、NPU 待機増加、postprocess 遅延、buffer 枯渇の別々のテストで候補を区別する。欠落・clock 不整合があるときは一意な犯人を表示しない。[共通の実機ゲート](README.md#共通の検証ゲート)で同一設定の通常版と比較する。