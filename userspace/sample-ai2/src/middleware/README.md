# sample-ai2 の層境界

| 置き場所 | 責務 | 外部依存 |
| --- | --- | --- |
| `middleware/common/error.hpp` | HAL 非依存のエラー契約 | C++ 標準ライブラリのみ |
| `middleware/buffer_layout/` | バッファ容量・アラインメント計算 | C++ 標準ライブラリのみ |
| `middleware/image_resizer/` | CPU RGB 変換とリサイズ | C++ 標準ライブラリのみ |
| `middleware/buffer_layout/region_guard.hpp` | 領域の重複・包含と境界ガード | C++ 標準ライブラリのみ |
| `middleware/pipeline/` | タスク間の継続チケットと内部 stage の順序検証 | C++ 標準ライブラリのみ |
| `driver/camera_driver/dcmipp_resize.*` | STM32N6 Pipe1/2 の縮小倍率・間引きポリシー | middleware の Error のみ |
| `driver/` のその他 | HAL, BSP, センサ, PSRAM, NOR, NPU のレジスタと IRQ | STM32 固有 |
| `static_memory_layout/` | リンカの物理アドレスを型付きで公開 | ボード固有のリンカスクリプト |
| `memory_allocator/` | ボード寸法・バッファ所有権と物理領域への割当 | middleware とボード配置 |
| `npu_runtime/`, `models/` | STAI プロトコル、モデルデコード、NPU スケジューリング | STAI と現在のドライバ API |
| `task/` | µT-Kernel タスクと Pipe1/2 → NPU → LCD の組み立て | ドライバとランタイム |

middleware は `driver/`, `task/`, HAL/STAI, リンカシンボルを include しません。`npu_runtime` と `models` は現状ドライバ型や STAI 型に依存するため、汎用化したと主張せず移動していません。今後は model の公開契約から STAI / `CacheDriver` / `InferenceFrame` を分離し、メモリ所有権と NPU 実行を port interface にしてから移します。

## 並行推論の所有権と境界

`LeasePool<Slots>` は物理アドレスを知らず、Free → Ready（カメラ完了）→ Owned（CPU 前処理・NPU・CPU 後処理）→ Free の遷移を保持します。64-bit のトークンで遅延したキューメッセージを拒否します。ISR/タスク間の短い状態遷移はボードアロケータ側で割り込みをマスクし、キャッシュ同期や PSRAM アクセス中はマスクしません。カメラのDMA書込み中のスロットは Free でも物理的にアクティブなため、カメラドライバが別途 `completed` を除外して選択します。

５個の推論スロット（うち２個は旧 raw-dump 領域）と５個の各スナップショットを別の物理領域に置きました。これにより NPU があるスロットを読み取り中に、CPU は**別スロット**の前処理と Pipe2 の撮影を進められます。共有 scratch は推論タスクによる前処理だけで利用し、NPU の入力・出力には渡しません。現在の先読みは NPU ステータス待ちの間に同じ CPU タスクで処理され、専用 CPU スレッドの同時実行を意味しません。

起動時に全リンカ領域の重複と各スロット容量を確認します。PSRAM 初期化後に推論スロットの有効末尾とスナップショット末尾へ32-byteのガードを設置し、カメラ完了・所有権取得・解放時に cache invalidate 後の破壊を検査します。出力ポインタのサイズと配置も Claim 時に検証します。破壊が見つかったスロットは Free に戻しません。これは境界侵害の**検出**であり MPU による全アクセスの防止ではありません。キャッシュライン内の侵害や使用中の瞬間的な変更は検出できない場合があります。

raw-dump 診断を同時に有効にするとコンパイル時に停止します。さらに大きなバッファを必要とするモデルの場合はリンカ配置とメモリプールの変更が必要です。

**実行コンテキストの遷移:** NPU 所有タスク（モデル選択）→ CPU 入力タスク（Pipe2 キューからフレームを claim、モデル固有の `PrepareInput()` を完了）→ NPU 所有タスク（入力のキャッシュ同期、非同期 submit、必要なら IRQ 待ち・epoch 継続）→ CPU 後処理タスク（出力のキャッシュ同期、decode/convert）。NPU 所有タスクが NPU の実行中に「次モデルの入力」を要求するため、CPU 入力タスクは別のフレームの前処理を進められます。IRQ ハンドラはフラグを通知するだけで、NPU の STAI 操作は NPU 所有タスクだけが行います。CPU も NPU も Cortex-M55 上で別タスクとしてスケジュールされますが、**CPU 入力タスクと NPU 所有タスクの CPU コードが同時に二つのコアで走る意味ではありません**。NPU ハードウェアとのオーバーラップです。

キューをまたぐ際には `Handoff` に「元・次の実行コンテキスト、要求 ID、lease token」を載せます。要求段階ではまだフレームがないため token は 0、CPU 入力タスクの結果と後処理への引き継ぎには claim 済みフレームの token を入れます。受け手が方向と token を検証し、失敗または queue full 時は claim 済みのフレームを解放します。後処理が完了するまでは NPU タスクもフレームを解放しません。`kNpuProtocol` は NPU 所有タスクの共通 STAI プロトコル、`kResized` の copy/resize/letterbox は CPU 側のモデル前処理の**内部**順序です。各 `Plan` は単一タスクの手順に限定します。外部タスクへは stage ID を渡さず、モデル選択と `Handoff` を渡します。STAI 完了後の `epoch_continue` は必要な場合だけ実行します。`Overlap::kMayOverlap` は「前フレームの NPU ハードウェア実行中に次フレームを CPU で準備可能」の意味で、常に重なる保証ではありません。

### pipeline 周辺の型の責務

| 型 | 何を持つか | 何を持たないか |
| --- | --- | --- |
| `pipeline::Handoff` | 元と次の task・要求番号・lease token の受け渡し契約 | 内部の stage ID やモデルの処理順序 |
| `pipeline::Stage` | **内部**処理の種類を表す ID（例: `kResize`、`kSubmit`） | 実行関数やバッファ。タスク間の継続先は示さない |
| `pipeline::Descriptor` | 1 stage の ID・名前・実行場所・先読み可否・説明 | 実行中の状態。`name` と `processing` は説明であり実行されない |
| `pipeline::Plan` | `Descriptor` の不変配列への参照と要素数。NPU 側の共通手順（`kNpuProtocol`）とモデル CPU 側の順序（`kResized`）を別々に表す | 配列の所有権やタスク間キュー。異なるタスクの stage を同じ Plan に混ぜない |
| `models::ModelDescriptor` | モデルの種類・名前・入力画像サイズ。stage の `Descriptor` とは別物 | stage の順序 |
| `InputPreparationRequest` / `InputPreparationResult` | 選択済みモデルと今回の要求 ID、処理後のフレームと lease | NPU ドライバの状態。CPU タスクは STAI を操作しない |
| `InferenceDispatcher::PipelineState` | 今回の NPU 処理の現在位置・分岐・計測値 | 全モデル共通の実行順序（これは `Plan` が持つ） |
| `InferenceCompletion` | NPU 完了後、別タスクへ渡すモデル・出力**参照**・座標条件の固定情報 | 出力バッファ本体のコピー。後処理までフレームを保持する必要がある |

CPU タスクが前処理を完了したら `Handoff` を添えて NPU 所有タスクへ送信します。受信側タスクが要求番号・lease・予定モデルを確認し、ディスパッチャは選択したモデルとの一致を検証して `PipelineState` を更新します。NPU 完了時は `InferenceCompletion` を後処理タスクへ送ります。`Plan` 自体はタスクや NPU を起動せず、`kIrqWait` から `kEpochContinue` への遷移は未完了の場合だけです。

各アプリは別々のソースを持ち、sample-ai の動作・アドレスは変更しません。
