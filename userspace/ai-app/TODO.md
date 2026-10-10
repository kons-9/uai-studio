# ai-app 実機確認TODO

05・07・08・12の実装後に必要な確認。03のHW追加は対象外。以下はすべて実機では未確認で、起動成功やホスト試験の成功だけではチェックを付けない。

各確認ではFWのcommit・ビルド設定・モデル・FPS・操作内容・結果・UARTログの保存先を記録する。失敗した項目はチェックせず、再現手順を残す。

## 1. 準備・起動・継続動作

- [ ] 実機環境でARMビルドと最終リンクが成功する。変更したタスク・driverが最終ELFに入っていることを確認する。カメラIRQ/BSP callbackの上書きは、リンクマップの配置元と`arm-none-eabi-nm`のシンボル種別も確認する。ホスト用の偽カーネル試験で代替しない。
- [ ] 同じUARTを開いている既存モニタがないことを確認し、UARTを開いてログ保存を開始してから書き込む。モデルを変更した場合だけ先に`ai-load`する。

リポジトリルートから実行する。monitorはminicomで双方向通信でき、コマンドはその画面に入力してEnterで送る。ログ保存はCtrl+A、L。

```sh
make -C userspace/ai-app monitor
```

別端末で実行する。

```sh
# モデル変更時だけ
make -C userspace/ai-app ai-load
make -C userspace/ai-app ram-run
```

- [ ] UARTに`camera: pipe1=started pipe2=started`と`shell ready; type help`が出る。映像と推論結果が更新される。
- [ ] 同じ設定で5〜10分動かし、両Pipeのframe数とモデル完了数が増え続ける。lease不整合・二重返却・HALエラー・通常時のtimeout/復旧が繰り返されず、画面や応答が停止しない。

## 2. シェル・UIの同期

- [ ] `help`、`uptime`、`tasks`、`memory`、`camera status`、`ui status`が応答する。`memory`は静的予約容量の表示であり、実使用ヒープ量の計測ではない。
- [ ] 未知コマンド、不正引数（例: `camera fps 11`）、長すぎる行の後も、次の正しいコマンドに応答する。CR/LF・Backspaceとログ混在で応答が破損せず、1行が二重適用されない。
- [ ] タッチとシェルの両方からモデル・枠・AI露出を変更し、`ui status`と設定画面のチェック状態が一致する。成功時は`APPLIED`となる。confidence・更新周期も変更し、画面遷移と定期更新が続く。
- [ ] 設定画面で連続タップ・スライダー操作を行い、同時にシェル要求を送っても、表示・両Pipe・推論が進む。押下状態が残ったり、要求が通知待ちで処理されなくなったりしない。
- [ ] `camera fps 10`と`camera fps 30`を順に適用し、読み戻したFPSが一致する。反転・有効範囲内のcropも変更し、変更前の古い結果が再表示されず、新しい結果と両Pipeが継続する。

シェル入力例:

```text
models person
models face
models seg
models all
ui boxes off
ui boxes on
ui exposure off
ui exposure on
camera status
ui status
```

応答timeout時も要求が後から適用され得るため、再送前に`camera status` / `ui status`で確認する。

## 3. 制約・読み戻し・失敗通知

- [ ] AI露出ON中の`camera ae off`、`camera manual 9000 1200`、`camera stats 10 20 100 80`が拒否され、実値とUI状態が変わらない。成功した操作として表示されない。
- [ ] `ui exposure off`、`camera ae off`後に手動露出を設定でき、`camera status`で適用値を読み戻せる。`ui exposure off`を再度送っても、手動モード・独自統計の設定を失わない。
- [ ] 手動露出からAI露出へ戻すとAEがONになり、読み戻し成功後だけUIもONになる。対象が消えた後、露出の1秒保持と250ms処理周期に従って統計領域が全画面へ戻る。表示の3秒保持と混同しない。
- [ ] AI露出を利用不可にした試験用FWでは、ボタンが暗くなり入力を受け付けず、シェルの有効化も拒否される。押下中にボタンを無効化する試験では、離した際にtapが発生しない。
- [ ] 試験用の失敗注入でHW適用失敗・読み戻し失敗を起こし、UI/モードを成功扱いにせず、前の制御値の復元とエラー通知を確認する。復元失敗も注入し、状態不明が明示される。最後の`APPLY ERROR <code>`は定期統計更新で消えない。

失敗注入・動的なボタン無効化は通常シェルにはないため、試験用FWまたはデバッガの制御が必要。状態不明となった場合は通常運転を続けず、実値を確認して再設定・再初期化する。

## 4. 結果失効・停止・復旧

- [ ] 表示とカメラを動かしたまま特定モデルの結果発行だけを止め、最後の推論完了から約3秒でそのモデルの枠・maskが消える。他モデルの更新や同じ集約結果の再受信で延命・復活しない。
- [ ] 正常な検出0件の新しい結果で、以前の検出が直ちに消える。モデル再開後は新しい結果を表示する。
- [ ] 試験用の制御でフレーム供給を停止しても、シェル、設定画面のタッチ・描画、統計、露出期限処理が続く。通知だけを待って停止しない。
- [ ] 片方/両方のPipe停止を試し、フレームtimeoutと自動復旧が動く。復旧失敗時も再試行が継続し、再開後に両Pipeと推論が進む。復旧前のqueued frame・未完了Future・表示/露出結果が新しい世代へ混ざらない。
- [ ] `kInferenceFrameStride`を1以外（例: 3）にした試験用FWで、実sequenceの飛びやFPS変更後も投入位相が正しく、過去の投入を追いつき実行しない。選ばなかった実バッファも返却され、長時間運転でpoolが枯渇しない。

特定モデルの結果発行停止・フレーム供給停止の通常コマンドは未提供で、試験用FWが必要。モデルOFFは即座に非表示にするので3秒失効試験の代用にならない。CPU全体のhaltも全タスクを止めるため、フレーム停止中の応答試験には使えない。通電中のカメラ接続を抜く方法では試験しない。

capture停止中のライブ画面は返却済みbufferを使って再描画しないため、表示済みの映像・枠が画面に残ることはある。失効は内部の有効性と次の正当な描画時点で確認する。設定画面はcaptureなしでも再描画できる。

## 5. トレース転送・同一スナップショット照合

- [ ] 通常の`trace ai` / `trace cpu`が`@TRACE END`まで転送され、形式・版・長さ・CRC32を検証できる。転送後は記録が再開する。転送中も映像・推論が継続し、ログ混在でtrace行が破損しない。CPUリングのHEX転送は115200 bpsで数分かかる。
- [ ] 明示的pauseでは転送後も記録が停止したままとなり、`trace ai status` / `trace cpu status`で確認できる。AIとCPUは独立して保持・再開できる。
- [ ] 実機で固定した同一リングをUARTとST-LINKから取得し、AI・CPUそれぞれについてバイト一致と解析出力一致を保存する。別時刻のdumpやサンプル試験を同一取得の証拠にしない。

AIの手順。UARTログはこの1回の転送を含む別ファイルとして保存する。

1. シェルで`trace ai pause`、`trace ai`を送信し、`@TRACE END`まで保存する。
2. pauseを保持したまま、別端末で下記を実行する。起動中FWと一致するビルドのELFを使う。

```sh
make -C userspace/ai-app thread-monitor-dump THREAD_MONITOR_DUMP=/tmp/ai-swd.bin
python3 host_app/ai_model_monitor/decode_uart_trace.py /tmp/ai-uart.log /tmp/ai-uart.bin --compare /tmp/ai-swd.bin
python3 host_app/ai_model_monitor/ai_model_monitor.py decode /tmp/ai-uart.bin -o /tmp/ai-uart.json
python3 host_app/ai_model_monitor/ai_model_monitor.py decode /tmp/ai-swd.bin -o /tmp/ai-swd.json
diff -u /tmp/ai-uart.json /tmp/ai-swd.json
```

3. CPUも`trace cpu pause`、`trace cpu`で同様に保存し、下記を実行する。

```sh
make -C userspace/ai-app cpu-task-monitor-dump CPU_TASK_MONITOR_DUMP=/tmp/cpu-swd.bin
python3 host_app/ai_model_monitor/decode_uart_trace.py /tmp/cpu-uart.log /tmp/cpu-uart.bin --compare /tmp/cpu-swd.bin
uv run --project host_app python host_app/cpu_task_monitor/cpu_task_monitor.py /tmp/cpu-uart.bin -o /tmp/cpu-uart.png --json /tmp/cpu-uart.json --csv /tmp/cpu-uart.csv
uv run --project host_app python host_app/cpu_task_monitor/cpu_task_monitor.py /tmp/cpu-swd.bin -o /tmp/cpu-swd.png --json /tmp/cpu-swd.json --csv /tmp/cpu-swd.csv
diff -u /tmp/cpu-uart.json /tmp/cpu-swd.json
diff -u /tmp/cpu-uart.csv /tmp/cpu-swd.csv
```

4. 成功・失敗・中断にかかわらず、保持したリングに`trace ai resume` / `trace cpu resume`を送り、`status`でrunningを確認する。

比較中はリセット・書込み・記録再開をしない。SWD読取りのCPU停止区間は、UART転送中の継続動作試験や性能比較から除く。PSRAMリングは揮発性のため、再起動前に取得する。

## 6. 性能・長時間運転

- [ ] 変更前後でモデル・FPS・入力・診断設定・ログ負荷を揃え、`thread-monitor`と`cpu-task-monitor`を取得する。推論FPS・タッチ応答を維持し、通常運転の空転とCPU時間が減ることを確認する。比較対象FWがない場合は改善確認を未完了とする。
- [ ] 5秒ごとの`cpu: schedule`をUARTログへ保存し、開始/完了/失敗、間引き、drop理由、`late_ms`、`frame_ms`、`idle`の推移を確認する。負荷を上げても遅延やdropが継続的に悪化せず、leaseエラー・timeoutが新たに増えない。累積値の増加量で比較する。
- [ ] 通常運転・連続操作・FPS切替を含めて30分以上動作させ、キュー/バッファの枯渇、画面停止、復旧ループ、要求への無応答がないことを確認する。

`submit.completed`は入力キューへの送信成功で、推論完了はAIモニタで確認する。`skipped`や負荷時のdropが非ゼロなこと自体を失敗とせず、変更前・投入stride・入力負荷と照合する。追加スケジュール統計はUART拡張で、旧binary ringには含まれない。解析方法は[CPUモニタのREADME](../../host_app/cpu_task_monitor/README.md)を参照する。

## 記録

| 確認日・FW/設定 | 項目 | 結果・残課題 | UART/trace/比較結果の保存先 |
| --- | --- | --- | --- |
| 未実施 | 全項目 | 実機環境で確認する | - |

通常操作は[README](README.md)、実機書込み手順は[AGENTS.md](../../AGENTS.md)を参照する。チェックは項目単位で付け、試験用FWが必要な項目を通常動作確認だけで完了扱いにしない。