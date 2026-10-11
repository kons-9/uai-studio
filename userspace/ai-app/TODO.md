# ai-app 現行FWの確認TODO

元の確認項目1〜3は現在動いているai-appを使い、別構成・再ビルド・書込み・故障注入を追加しない。今回別途行った合成face出力の露出経路確認は、専用shellコマンドを追加してRAM実行したもので、実Faceモデルの検出率確認には含めない。画面目視とタッチ操作は対象外。

## 準備

確認ツールは[tool/verify.py](tool/verify.py)。Python標準ライブラリだけで動き、GTestは不要。UART実行はLinux/POSIX用で、既存シェルへコマンドを送り、ログと結果JSONを保存する。各応答の後に`uptime`を送って完了を区切る。

1. ai-appが起動済みで、Pipe1/2とシェルが動いていることを確認する。手順のために`ram-run`や`ai-load`をやり直さない。
2. `make monitor`で`log info`を送り、5秒ごとの`cpu: schedule`が出ることを確認する。露出確認は`ui exposure on`かつ`camera status`のAEがONの状態で開始する。
3. minicomを終了する（Ctrl+A、X）。確認ツールと`make monitor`を同時に同じUARTへ接続しない。ツールは既存の接続を検査し、排他で開く。
4. UARTが自動検出できなければ`UART_DEVICE=/dev/ttyACM0`等で実際のデバイスを指定する。

コマンドはリポジトリルートから実行する。既定の出力先は`build-ai-app-person/verify/<実行日時>/`。`AI_VERIFY_DIR`で変更できる。同名のUARTログは上書きしないので、再実行には新しいディレクトリを使う。

## 1. 露出の全画面復帰

- [ ] 対象領域へいったん絞られ、その対象を外した後に全画面へ戻ることを、UARTの統計領域で確認する。
- [x] 合成Face出力を2件流し、統計領域の縮小とOFF後の全画面復帰を実機確認（2026-10-11）。実Faceモデルからの検出率確認は未完了。

```sh
make -C userspace/ai-app verify-exposure UART_DEVICE=/dev/ttyACM0
```

ツールはモデル停止・AI露出OFFで全画面の基準値を読み、AI露出ON・faceモデルへ切り替える。最初の案内で検出可能な顔をカメラ入力に置いてEnterを押す。領域が基準値より絞られたことを確認できたら、次の案内で対象を外してEnterを押す。

**合格条件:** 領域の絞り込みを実際に観測した後、全画面へ戻り、再読取りでも全画面・AE ONを維持する。両Pipeのカウンタが進み、新しいカメラエラーがない。終了時は元のモデル・枠・FPS・AI露出状態へ戻る。

検出できない場合は未確認（終了コード3）で、最初から全画面だったことを合格にしない。手動で対象を外すため、1秒保持・250ms周期の厳密な時間測定はしない。標準の観測期限は10秒で、必要なら直接CLIの`--observe-timeout`で調整する。

保存する証拠: `exposure.log`、`exposure.json`の`full_frame`、`target`、`returned`、`return_seconds_after_enter`。

## 2. 操作を含む30分耐久

- [x] 現行FWでモデル・枠・FPSを切り替えながら30分以上動かし、応答・両Pipe・推論結果の更新が継続する。

```sh
make -C userspace/ai-app verify-endurance UART_DEVICE=/dev/ttyACM0
```

`models person/face/seg/none/all`、`ui boxes off/on`、`camera fps 10/30/開始時のFPS`を繰り返す。既定は操作間隔10秒、全体1800秒。各操作後の状態・FPSを読み戻し、両Pipeの進行とエラー増加を確認する。終了・中断・失敗時も開始時の設定へ復元を試み、その読み戻しを確認する。

**合格条件:** 30分以上完走し、読み戻しが要求と一致する。各観測区間で両Pipeが進み、新しいカメラエラー・応答timeout・leaseエラー・処理失敗がない。scheduleの`results.completed`が増え、開始時の設定へ復元できる。

接続の短い確認は可能だが、30分試験の合格にはしない。

```sh
make -C userspace/ai-app verify-endurance UART_DEVICE=/dev/ttyACM0 AI_VERIFY_SECONDS=30 AI_VERIFY_STEP_SECONDS=2
```

保存する証拠: `endurance.log`、`endurance.json`の実行秒数・操作回数・初期状態・解析結果・エラー。失敗時はログを残し、要求が後から適用され得るので、ツール終了後に`camera status`と`ui status`で実値を確認する。

## 3. 現行FWの統計確認

- [x] 保存UARTログの処理別増分・失敗・drop理由・最大遅延を整理し、操作中と負荷を戻した後の安定性を確認する。

```sh
make -C userspace/ai-app verify-stats AI_VERIFY_LOG=/tmp/ai-app-uart-20261011.log
```

または直接CLIを使う。

```sh
python3 userspace/ai-app/tool/verify.py analyze /tmp/ai-app-uart-20261011.log --output /tmp/ai-app-statistics.json
```

**確認する値:** `due/started/completed/failed/skipped`、drop理由別増分、`max_lateness_ms`、`max_frame_delay_ms`、`idle_wakeups`。32bitラップは増分として扱い、リセットや順序不整合は警告ではなく失敗にする。起動時に既にあったエラーカウンタは、その後増えなければ新しいエラーと数えない。

**判定:** 解析は失敗増加・入力準備drop・カメラエラー増加を検出する。キューdropや間引きが非ゼロなだけでは不合格とせず、モデル停止・FPS・負荷との対応をログで確認する。累積最大遅延は下がらないため、この値だけで負荷解除後の回復を証明しない。待ち時間の増大や応答停止がないことも確認する。

過去の不正入力・制約拒否の試験を含むログでは、意図的な`error:`応答も`issues`に列挙される。操作内容と照合して扱いを記録し、耐久試験用には正常操作だけの新しいログを採取する。解析からエラーを隠して合格扱いにはしない。

`submit.completed`はキュー送信成功で、推論完了そのものではない。`results.completed`は表示向けの新しい集約結果の採用数。追加統計はUART拡張で、旧binary ringにはない。必要な統計がない／1回分しかないログを合格にせず、infoログを有効にして再取得する。解析詳細は[CPUモニタのREADME](../../host_app/cpu_task_monitor/README.md)を参照する。

終了コード: `0`は確認成功または統計観測済み、`1`は確認失敗、`2`は入力・接続等のエラー、`3`は未確認。統計解析の`observed`は値を観測できたという意味で、性能改善や全項目合格を意味しない。

## 確認済みの記録

以下は以前の実機確認。今回の3項目を合格に読み替えず、再ビルドや同一トレース照合を最初から繰り返す必要もない。

| 確認日・FW | 内容 | 証拠 |
| --- | --- | --- |
| 2026-10-11 / root `7c017a160d84` / kernel working tree `8461ec416015`（root gitlink `e1cbd48b157b`） / MinSizeRel / CPU monitor ON / person+face+seg / 最終FPS 20 | ARM build/link・UART先行・起動・約5.4分の両Pipe/推論カウンタ進行。起動CSI anomaly `0x8000`が1回、以後の再発・timeout・lease/HAL errorなし。 | 当時のELF/map、`/tmp/ai-app-uart-20261011.log` |
| 同上 | 基本シェル、未知/不正/長行・Backspace/CRLF後の復帰。露出制約・手動値・stats保持・AI露出へ復帰。FPS 10/30・flip/crop適用。 | `/tmp/ai-app-uart-20261011.log` |
| 同上 | 通常AI転送32768 bytes CRC32 `c98281d2`、通常CPU転送524288 bytes CRC32 `a9849527`、終了後running。CPU転送前後のカウンタ`68572`→`91444`。明示pauseでAI `4f540a98` / CPU `8ac512e3`、UART/SWDバイト一致、AI JSON・CPU JSON/CSV一致、独立pause/resumeを確認。 | `/tmp/ai-trace-normal-uart.log`、`/tmp/ai-trace-normal.bin`、`/tmp/cpu-trace-normal-only-uart.log`、`/tmp/cpu-trace-normal.bin`、`/tmp/ai-uart.log`、`/tmp/ai-swd.bin`、`/tmp/ai-uart.json`、`/tmp/ai-swd.json`、`/tmp/cpu-uart.log`、`/tmp/cpu-swd.bin`、対応JSON/CSV |

当時30分超の起動継続も記録されているが、操作・FPS切替・統計観測を含む専用耐久試験は未完了。今回の実行には、起動中FWのcommit・モデル・開始FPSと、新しいログ/JSONの保存先を記録する。

## 今回行わない確認

以下は完了ではなく未検証のまま。現行ai-app単体で行う3確認の範囲へ戻さない。

- 別構成: AI露出利用不可、stride=3、変更前FWとのCPU/FPS改善比較。
- 故障注入: HW適用・読み戻し・復元失敗、片方/両Pipeの停止・自動復旧・旧世代拒否。
- 結果の内部状態: モデル別3秒失効、正常な検出0件の消去。通常シェルだけでは十分に観測できない。`models none`やCPU全体のhaltは代用にしない。
- 画面目視・タッチ・無効ボタン・画面遷移の実機確認。
- 厳密なpool使用量／枯渇余裕の測定。`memory`は静的予約容量であり、耐久完走は全返却経路の証明ではない。

## 新しい確認結果

| 確認日・起動中FW/設定 | 項目 | 結果・残課題 | UARTログ・JSON |
| --- | --- | --- | --- |
| 2026-10-11 / 前回記録の起動中FW `7c017a160d84`（今回書込みなし） / kernel working tree `8461ec416015` / CPU monitor ON / all models / FPS 20 / AE・AI露出 ON / boxes ON | 30分耐久・統計 | 合格: 1800.6秒・170操作。Pipe1/2とも+36091 frame、camera error増分0/0/0、results.completed +8079・failed 0。初期状態へ復元。submit.completed +11585 / skipped 24139 / drop_stride 4 / drop_source・drop_queue 0 / max_lateness 1566ms。Pipe2.completed +11589 / failed 0 / dropped 24310（source・queue drop 0）。統計status `observed`、issuesなし。display.failed +6575、max_frame_delay 190msは警告として記録（画面確認は対象外）。UARTコマンド応答を待ってから完了区切りを送り、送信を2ms/byteに調整。ホスト試験20件成功。 | `build-ai-app-person/verify/20261011-1030-current-fw/endurance.log`、`endurance.json`、`statistics.json` |
| 同上 | 露出全画面復帰 | 未確認: 10秒観測を2回、30秒観測を1回実施したが、ツールの観測中に対象領域を取得できず、全画面復帰も確認できなかった。1回目は設定復元中に領域変化 `0,194,2200,1555` を記録したが、対象領域として観測できた後の復帰ではないため合格にしない。各回とも終了後に初期設定へ復元。 | `build-ai-app-person/verify/20261011-1030-current-fw/exposure.log`、`exposure.json`、`build-ai-app-person/verify/20261011-1050-exposure/exposure.log`、`exposure.json`、`build-ai-app-person/verify/20261011-1110-exposure-retry/exposure.log`、`exposure.json` |
| 同上 / UART monitorでschedule出力とPipe2進行を確認後、同一FWで実行 | 露出全画面復帰（再試行） | 未確認: ツールを起動して全画面基準値 `0,0,2592,1944` を取得し、AE ON・FPS 20を確認。顔入力待ちで対象を提示できなかったため中断し、対象領域と復帰は未観測。終了時にmodels=7、boxes=1、AI露出ON、AE ON、FPS 20、全画面統計を読み戻し、カメラエラー `0/1/0` は開始時から増加なし。 | `build-ai-app-person/verify/20261011-exposure-retry-4/exposure.log`、`exposure.json` |
| 同上 / 現行FW（書き込みなし） / 実験後UART monitorでPipe2とフレーム進行を確認 | 露出全画面復帰（顔入力あり） | 未確認: 顔モデルで `face postprocess` の検出数1〜2を複数回観測したが、各 `camera status` の統計領域は全画面のまま。領域縮小も、その後の全画面復帰も観測できなかった。終了時に初期設定と全画面統計へ復元し、カメラエラー `0/1/0` に増加なし。 | `build-ai-app-person/verify/20261011-exposure-retry-5/exposure.log`、`exposure.json` |

ツール自体の試験は次で行う。擬似端末・偽の実行中アプリを使い、FWコンパイルや実機接続はしない。

```sh
python3 -m unittest discover -s userspace/ai-app/tests -p test_verify.py
```

通常操作と既存の同一トレース取得手順は[README](README.md)を参照する。今回はFW・モデルの変更をしない。

## 合成face出力による露出経路の確認（2026-10-11）

実Faceモデルと露出制御の間を切り分けるため、shellから固定BoxSetを投入する`test-face`を追加した。`test-face on [1..16]`は指定数のface boxを250ms周期で通常のModelResultChannelとExposureResultChannelへ流し、`test-face off`は空結果を1回送る。OFF後も通常制御どおり最大1000msは最後のface領域を保持する。

実機ではUART monitorを開いて`log info`を設定後、`ram-run`で今回のFWをRAMへロードした。まずモデルを`none`にして実AI出力を止め、AI露出ON・AE ONのまま`test-face on 2`を実行した。UARTのLCD結果ログで`face=2`を確認したが、`models none`ではUIのモデルマスクがface boxを隠す。表示経路も通す追加試行では`models face`に切り替えて同コマンドを実行した。`VisibleBoxes`がface boxを通し、通常の`ComposeAndPresent`へ渡す条件にした。パネル画像の直接取得・目視確認はしていない。

- UARTに合成出力`boxes=2`を確認。`diag display on`で`lcd: boxes person=0 face=2`、OFF後に`face=0`を確認した。
- `camera status`の統計領域が`0,0,2592,1944`から`712,258,390,390`へ縮小した。
- `test-face off`後、約1.5秒で`0,0,2592,1944`へ復帰。AEはONのまま。
- その間Pipe1/2のframeカウンタは進行し、カメラエラーは`0/1/0`から増えなかった。最後に`models all`、`diag display off`、`log info`へ戻した。

これはshellから両結果チャネルを通した合成データの確認で、実カメラ画像に対するFaceモデルの検出率を測ったものではない。既存のretry-5ログでは顔入力を提示した試行中に124件のFace後処理ログがあり、box数は0件26回、1件58回、2件40回だった。Face後処理の`conf_threshold=0.35`、`iou_threshold=0.3`に変更履歴はなく、直近のFaceFuture変更は型・名前空間整理と整形だった。旧`ResizeRgb888`と現行`image_processing::Resize`もどちらもnearest-neighbor縮小で、実装差は見つからなかった。照明・顔の大きさ・距離を固定した比較ログがないため、検出率が以前より低下したとは判断できない。

このコマンドは実AIとは独立した露出経路確認用。実Faceモデルの認識率や実カメラ画像からのROI追従確認は引き続き別項目として扱う。
