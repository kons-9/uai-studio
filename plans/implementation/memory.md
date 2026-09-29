# メモリ分析・破壊検出

**優先度:** 高（領域使用量・所有権）、その後に中（破壊検出）。依存: [APP 容量表](../app-size-reduction.md) と [トレース](tracing.md)。

## 作るもの

- ビルド時: ELF／map から APP の固定起動エントリ手前の余裕、APP 全体、stack／trace／NOLOAD、NOR blob、PSRAM、NPU 用 activation を混ぜずに一覧化。baseline／計測有効版の差分をレポート。
- 実行時: [MemoryAllocator](../../userspace/sample-ai/src/memory_allocator/memory_allocator.cpp) が管理する capture／display／inference slot について、slot ID、フレーム ID、取得元、CPU／DMA／NPU／表示側への所有権遷移、現在数・高水位・枯渇理由を記録。queue の深さと破棄／再利用も突き合わせる。
- stack は high-water と canary を対応するタスクの安全な検査点で記録する。使用量と破壊検出を別の指標とし、未使用領域の初期化方法・チェック時点・余裕を文書化する。

## デバッグ計測の拡張

- ガードバイト、double-free、解放済み領域の poison は、対象を **CPU が所有するソフトウェア allocator に限定**してオプション化。DMA／LTDC／NPU が使うバッファ境界に無条件で canary や poison を書かない。キャッシュとアライメントを維持できる設計・所有権移動後の同期が先。
- 発見できるのはチェック時点までに残った破壊であり、使い捨てのバッファや DMA 書込みによる全ての破壊を検出できるわけではない。リーク報告は実際の割当／解放履歴が取得できる範囲のみ。

## APP 依存

空きが厳しければリンカ map のホスト解析＋低頻度の allocator 統計を先行し、全 slot の per-event 記録は絞る。破壊検出コード・ガード・走査用 RAM は通常の計測プロファイルに常駐させず debug ビルドとして分離する。トレースリングの予約量や stack 縮小の採否も容量表で判定する。

## 完了条件

領域ごとのメモリ使用量を二重計上せず、slot の二重取得／解放、枯渇、stack 警告を意図的に検出できる。通常経路で Pipe1／Pipe2／推論のバッファを傷付けないことを [共通の実機ゲート](README.md#共通の検証ゲート)で確認する。