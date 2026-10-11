# ai-app 現行FWの確認TODO

現在動いているai-appをそのまま使う。別構成・再ビルド・書込み・故障注入は追加しない。今回実施するのは露出復帰、操作を含む30分耐久、保存UART統計の確認だけ。画面目視とタッチ操作は対象外。

## 準備

確認ツールは[tool/verify.py](tool/verify.py)。Python標準ライブラリだけで動き、GTestは不要。UART実行はLinux/POSIX用で、既存シェルへコマンドを送り、ログと結果JSONを保存する。各応答の後に`uptime`を送って完了を区切る。

1. ai-appが起動済みで、Pipe1/2とシェルが動いていることを確認する。手順のために`ram-run`や`ai-load`をやり直さない。
2. `make monitor`で`log info`を送り、5秒ごとの`cpu: schedule`が出ることを確認する。露出確認は`ui exposure on`かつ`camera status`のAEがONの状態で開始する。
3. minicomを終了する（Ctrl+A、X）。確認ツールと`make monitor`を同時に同じUARTへ接続しない。ツールは既存の接続を検査し、排他で開く。
4. UARTが自動検出できなければ`UART_DEVICE=/dev/ttyACM0`等で実際のデバイスを指定する。

コマンドはリポジトリルートから実行する。既定の出力先は`build-ai-app-person/verify/<実行日時>/`。`AI_VERIFY_DIR`で変更できる。同名のUARTログは上書きしないので、再実行には新しいディレクトリを使う。

## 1. 露出の全画面復帰

- [ ] 対象領域へいったん絞られ、その対象を外した後に全画面へ戻ることを、UARTの統計領域で確認する。

```sh
make -C userspace/ai-app verify-exposure UART_DEVICE=/dev/ttyACM0
```

ツールはモデル停止・AI露出OFFで全画面の基準値を読み、AI露出ON・faceモデルへ切り替える。最初の案内で検出可能な顔をカメラ入力に置いてEnterを押す。領域が基準値より絞られたことを確認できたら、次の案内で対象を外してEnterを押す。

**合格条件:** 領域の絞り込みを実際に観測した後、全画面へ戻り、再読取りでも全画面・AE ONを維持する。両Pipeのカウンタが進み、新しいカメラエラーがない。終了時は元のモデル・枠・FPS・AI露出状態へ戻る。

検出できない場合は未確認（終了コード3）で、最初から全画面だったことを合格にしない。手動で対象を外すため、1秒保持・250ms周期の厳密な時間測定はしない。標準の観測期限は10秒で、必要なら直接CLIの`--observe-timeout`で調整する。

保存する証拠: `exposure.log`、`exposure.json`の`full_frame`、`target`、`returned`、`return_seconds_after_enter`。

## 2. 操作を含む30分耐久

- [ ] 現行FWでモデル・枠・FPSを切り替えながら30分以上動かし、応答・両Pipe・推論結果の更新が継続する。

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

- [ ] 保存UARTログの処理別増分・失敗・drop理由・最大遅延を整理し、操作中と負荷を戻した後の安定性を確認する。

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
| 未実施 | 露出復帰・30分耐久・統計確認 | ツール実装とホスト試験のみ。実機で実行してからチェックする。 | - |

ツール自体の試験は次で行う。擬似端末・偽の実行中アプリを使い、FWコンパイルや実機接続はしない。

```sh
python3 -m unittest discover -s userspace/ai-app/tests -p test_verify.py
```

通常操作と既存の同一トレース取得手順は[README](README.md)を参照する。今回はFW・モデルの変更をしない。
