# ai-app 残る実機確認

**結果の撮像sequenceを修正したFWで、実Face検出から露出領域の縮小・全画面復帰を確認すること**が未確認。加えて、輝度ヒストグラムと白・黒ゼブラもホスト確認までで、実機向けビルド・起動・表示確認は未実施。下記を実機環境で行い、結果を記録する。

## 修正内容

実Person・Face・Segmentationの後処理結果は`capture_sequence=0`のまま発行されていた。露出Controllerはモデルごとに同じ／古いsequenceを拒否するため、最初の空検出以降の検出を採用できなかった。

[共通発行処理](src/models/result_publisher.hpp)で撮像フレームのsequenceを設定するように修正済み。検出閾値・画像前処理・座標投影は変えていない。発行処理から表示・露出チャネルへ通すホスト試験で、空検出後の有効Face採用、移動追従、1秒保持後の全画面復帰、sequence/generation保持を確認した。合成結果の成功は実Faceの確認成功には読み替えない。

## 実行手順

### 1. 修正後FWを起動する

1. 修正を含むソースを実機環境へ反映する。rootとkernelのcommit、未commit差分、ビルド設定・モデル・FPSを記録する。古い起動中FWで検証しない。
2. 既存のUARTモニタがないことを確認し、UARTを開いてログ保存を開始する。`/dev/ttyACM0`は実際のデバイスに置き換える。minicomのログ保存はCtrl+A、L。

```sh
make -C userspace/ai-app monitor UART_DEVICE=/dev/ttyACM0
```

3. UARTを開いたまま別端末でビルド・RAMロードする。今回はモデルを変更していないので`ai-load`は不要。

```sh
make -C userspace/ai-app ram-run
```

4. ARMビルド・最終リンクの成功と、UARTの`camera: pipe1=started pipe2=started`、`shell ready; type help`を確認する。エラーがあればここで止める。
5. UARTシェルで合成Face出力を止め、通常の露出状態を確認する。

```text
test-face off
log info
ui exposure on
camera status
ui status
```

`camera status`でAEがON、`ui status`でAI露出がONであることを確認する。合成出力を停止してから数秒待ち、前の合成結果が保持されていない状態で次へ進む。

### 2. 実Faceで領域の縮小と復帰を観測する

1. minicomを終了する（Ctrl+A、X）。確認ツールとモニタを同じUARTへ同時に接続しない。
2. [確認ツール](tool/verify.py)を実行する。新しいログディレクトリを使う。同名のUARTログは上書きできない。

```sh
make -C userspace/ai-app verify-exposure UART_DEVICE=/dev/ttyACM0
```

3. 最初の案内で、検出可能な顔をカメラ入力に置いてEnterを押す。ツールは全画面の基準値を取得した後、AI露出ON・faceモデルで領域縮小を待つ。
4. 領域が縮まったことをツールが確認したら、次の案内で対象を外してEnterを押す。全画面への復帰と再読取り、両Pipeの進行、エラー増加、開始設定への復元を確認する。

標準の観測期限は10秒。必要なら直接CLIで30秒にする。

```sh
python3 userspace/ai-app/tool/verify.py exposure \
	--device /dev/ttyACM0 --observe-timeout 30 \
	--log /tmp/ai-face-recheck/exposure.log \
	--output /tmp/ai-face-recheck/exposure.json
```

## 合格条件

- [ ] 修正後FWで実Faceによる領域縮小を観測し、対象を外した後の全画面復帰・AE ON・両Pipe進行・新規エラーなし・開始設定への復元を、UARTログと結果JSONで確認した。

`exposure.json`の`full_frame`、`target`、`returned`を比較する。最初から全画面だった場合や、領域縮小を観測できなかった場合は未確認で、合格にしない。一般的な顔検出率の評価や厳密な1秒保持・250ms周期の測定はこの確認の対象ではない。

終了コードは`0`が確認成功、`1`が失敗、`2`が接続等のエラー、`3`が未確認。手動で顔を外すため、`return_seconds_after_enter`は厳密な対象消失時刻からの時間ではない。

失敗・中断時にもログを保存する。ツールは開始時の設定へ復元を試みるが、復元失敗時や応答timeout時は要求が後から適用され得る。ツール終了後にモニタを開き直し、`test-face off`、`camera status`、`ui status`で実値を確認する。

領域が縮まらない場合は、Face後処理の検出数、発行結果の`capture_sequence`、露出側の採用・要求領域と読み戻しを順に切り分ける。検出数だけを根拠に閾値を変更しない。

## 輝度ヒストグラム・ゼブラの実機確認

2026-10-11に追加した表示機能。[README](README.md#輝度ヒストグラムゼブラ)に操作としきい値の意味を記載した。ホストの`ui_layout_test`と`display_driver_test`、レイアウト検証・生成物一致は確認した。`shell_test`は既存の`RegisterTestFaceOutput`未リンクで実行できていない。ホスト試験は実機確認の代用にしない。

- [ ] UARTモニターが重複していないことを確認し、UARTを開いたまま最新FWをRAMロードして`camera: pipe1=started pipe2=started`を確認する。モデルは変更していないため`ai-load`は不要。
- [ ] 設定→`LUMA TOOLS`で3機能の初期OFF、各機能の独立ON/OFF、設定画面とカメラへの遷移、しきい値スライダーを確認する。AI露出・モデル選択・推論枠の状態が変わらないことも確認する。
- [ ] 暗い画面と明るい画面でヒストグラムの左右への偏り、`B`/`W`の割合と色を目視する。ゼブラON/OFFで集計値が斜線色に汚染されないことを確認する。
- [ ] 暗部のシアン斜線・明部の赤斜線がしきい値に対応することを確認する。両方同時ON、ヒストグラムOFFでのゼブラ表示、ゼブラOFFでのヒストグラム表示、推論枠・ステータスの可読性を確認する。
- [ ] `thread-monitor`と`cpu-task-monitor`で全OFF・各機能ON・すべてONを比較し、両Pipe進行、表示失敗、FPS、表示遅延、CPU負荷を記録する。追加処理は有効時に全画素をCPUで解析するため、実測前に性能を保証しない。

| 確認日 | FW・設定 | 表示・CPU/FPS・残課題 | UART・トレース・画面の記録 |
| --- | --- | --- | --- |
| 未実施 | 最新FWで実行する | 上記項目が未確認 | - |

## 確認記録

| 確認日 | 修正後FW・kernel・差分・設定 | 結果と残課題 | 起動ログ・exposure.log・exposure.json |
| --- | --- | --- | --- |
| 未実施 | 修正を含むFWで実行する | 上記1件が未確認 | - |

## 過去の確認済み事項

2026-10-11の記録。以下は修正前FWまたは合成データの証拠であり、上記チェックの代用にはしない。通常シェル・トレース照合・耐久を最初から繰り返す必要はなく、今回の修正後FWの起動と露出経路を確認する。

| 内容 | 確認済みの結果・証拠 |
| --- | --- |
| 起動・通常シェル | root `7c017a160d84`、kernel working tree `8461ec416015`（root gitlink `e1cbd48b157b`）、MinSizeRel、CPU monitor ON、最終FPS 20。ARM build/link、Pipe1/2とshell ready、入力回復、露出制約・手動値、FPS/flip/crop。起動CSI anomaly `0x8000`が1回、以後のエラー増加なし。`/tmp/ai-app-uart-20261011.log`。 |
| AI/CPUトレース | 通常転送と独立pause/resume、実機UART/SWDバイト一致、AI JSON・CPU JSON/CSV一致。明示pauseのCRC32はAI `4f540a98`、CPU `8ac512e3`。`/tmp/ai-uart.log`、`/tmp/ai-swd.bin`、`/tmp/cpu-uart.log`、`/tmp/cpu-swd.bin`と解析出力。 |
| 30分耐久・統計 | 1800.6秒・170操作、両Pipe +36091 frame、camera error増分0/0/0、results.completed +8079・failed 0、開始設定へ復元。`build-ai-app-person/verify/20261011-1030-current-fw/`のログ・JSON。display.failed +6575、最大表示遅延190msは警告として記録され、画面の正常性は未確認。 |
| 合成Face露出 | `test-face on 2`で全画面`0,0,2592,1944`から`712,258,390,390`へ縮小し、OFF後約1.5秒で復帰。AE ON・両Pipe進行・エラー増加なし。これは実Face検出による追従の証拠ではない。 |
| 実Faceの以前の試行 | retry-5で124回の後処理中、0件26回・1件58回・2件40回。統計領域の縮小・復帰は未観測。`build-ai-app-person/verify/20261011-exposure-retry-5/`のログ・JSON。この後sequence欠落を修正した。 |

## 対象外・未検証

今回の範囲外であり、完了扱いにはしない。

- 画面目視・タッチ・無効ボタン・画面遷移、表示失敗警告の実画面評価。
- 別構成（AI露出利用不可、stride=3）と旧FWとのCPU/FPS改善比較。
- 故障注入、復元失敗、片方/両Pipe停止・復旧・旧世代拒否の実機試験。
- モデル別3秒失効・正常な検出0件の消去、厳密なpool使用量／枯渇余裕の実機測定。
- 03で扱っていたHW追加。計画ディレクトリの削除はHW項目の実装完了を意味しない。

通常操作は[README](README.md)、実機の準備と書込み順序は[AGENTS.md](../../AGENTS.md)を参照する。未解決項目はここで追跡する。
