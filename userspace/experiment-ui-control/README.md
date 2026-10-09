# experiment-ui-control

## 状態と目的

主目的は、機能の組み合わせ制約を使い、UI操作をトランザクションとして適用する実装の実験。制約ツール本体とGUIは実験内の[tool/](tool/README.md)に置く。既存ai-appで確認しているUI部品や画面を再実装・再検証することは目的にしない。独自画面は状態と適用先を観察するための実験用表示に限定する。

機能制約チェッカ、操作列シミュレーション、C++ガード/値表生成、一括適用/失敗時復元/復旧、結果保持/タッチ操作/周期処理、カメラとUIを単一LCDフレームへ合成するGT911実験用エントリを実装している。UI表示のためのLTDC第2レイヤーやPSRAM上のパネル領域は使わない。画面/タッチの実機確認と生成NPU結果の供給は未完了。kernel/driver・middlewareには依存しない。

## 実装済みと実行方法

リポジトリルートから:

```sh
cmake -S userspace/experiment-ui-control -B build/experiment-ui-control-host
cmake --build build/experiment-ui-control-host
ctest --test-dir build/experiment-ui-control-host --output-on-failure
make -C userspace/experiment-ui-control ui-tool
```

[制約CLI](tool/feature_constraints/cli.py)は到達状態を全探索し、宣言を検証してC++を生成する。[app_state.hpp](src/app_state.hpp)がshellとタッチの操作を共通化する。CMakeも同じCLIを実験ディレクトリから呼ぶ。

## 機能制約のプロトツール

GUIの入力は機能名・状態・初期値と、2つの表だけ。「Cannot use together」は対称の共存不可、「Needs」は行の状態に列の状態が必要という有向の条件。変更はCLIが判定し、拒否されたら元の試行設定と競合理由を表示する。操作名・ケース・ガード・JSONの直接編集は不要。[簡易サンプル](tool/feature_constraints/example.json)は`schema_version=2`で、実機を操作しない。

実機用の[config/features.json](config/features.json)は既存の操作名を維持する旧形式として残す。検証用の仮の仕様「Pipe2表示中は検出枠ONを禁止」であり、HW上の必然的な制約ではない。旧宣言はCLIから検証できるが、簡易GUIへ自動変換しない。次は実験ディレクトリで実行する。

```sh
python3 -m tool.feature_constraints check config/features.json
python3 -m tool.feature_constraints simulate config/features.json --actions ToggleBoxes ShowPipe2
```

`simulate`はHWを操作しない。初期状態から操作列を一つのトランザクションとして検証し、名前付き状態、許可操作、拒否操作の条件、各操作の候補状態をJSONで返す。上の一括操作は`display=pipe2, boxes=off`へ確定する。操作なしの場合は初期状態と操作条件だけを表示する。

`--actions ToggleBoxes ShowPipe2 ToggleBoxes`は最後の操作が拒否され、候補状態は記録されるが、最終状態は初期状態のままになる。拒否時の終了コードは2、宣言不正・未知の操作は1、成功は0。GUIはHTTPサーバー経由で同じCLIを別プロセスとして呼ぶ。JSONは保存と内部連携の形式として使う。GUIの保存はブラウザによるダウンロードで、実機用入力ファイルを自動上書きしない。簡易生成コードには`SetAction`があるが、実機側の切り替えは別作業とし、本変更では既存トランザクションを維持する。

## UI操作のトランザクション

`Transact(actions, count)`は次の順で同期的に実行する。単一操作の`Dispatch`とタッチもこの経路を使う。

1. 確定状態のコピー上で操作列を順に検証する。一つでも拒否されたらHWを操作しない。
2. 最終候補状態を`Backend::Apply`へ一度だけ渡す。途中の候補状態はHWへ渡さない。
3. 適用に成功したら状態を確定し、`updates`を一回増やす。
4. 適用に失敗したら、直前の確定状態をHWへ再適用する。成功なら`rolled-back`となり、状態と`updates`は変わらない。
5. 復元にも失敗したら`faulted`を保持し、追加の操作と検出枠表示を停止する。`Recover`による確定状態の再適用が成功した場合だけ操作を再開する。

`Backend::Apply`は差分操作ではなく、指定された状態を全体として再適用できる契約とする。成功応答は適用の完了を表し、失敗時には部分的にHWが変更されていてもよい。`faulted`中の`Features`と`ui stat`は前回の確定状態であり、実HWと一致しているとは扱わない。

### LCDとUARTの診断表示

右側UI下部に最後に読んだタッチ座標、ヒットしたボタン、診断メッセージ、成功した更新回数を表示する。ボタンを押している間は黄色に変わる。`WAIT TOUCH`のままならタッチイベントを受け取れておらず、`TOUCH OUTSIDE`なら座標がボタン領域外、`HIT NONE`なら現在の座標からボタンを特定できていない。`TOUCH INIT FAIL`と`TOUCH READ FAIL`はGT911初期化または読み出しの失敗を示す。制約で拒否されたときは、たとえば`PIPE2 NEED BOX OFF`を表示する。HW適用や復旧に失敗した場合も理由を表示する。

UARTの`ui stat`は`touch/読み出し成功`, `seen`, `xy`, `hit`, `failure`, `denied`を返す。座標はLCD全体の値で、画面上のボタン領域はCameraがx=420..579/y=50..109、Pipe2がx=610..769/y=50..109、BOXがx=420..579/y=140..199。`hit`はBOX=0、PIPE2=1、Camera=2、未検出=-1。画面反応がない場合、`touch=1/1`なのに`seen=0`ならまだイベント未検出、`seen=1`でも`hit=-1`なら座標/領域のずれ、`hit>=0`で`failure=constraint`なら`denied`の操作が機能制約で拒否されたと切り分けられる。

同じ所有タスク上で直列実行する前提であり、別タスク・IRQから同じHWを変更する場合の排他や、LCDに一瞬も途中状態が見えない保証はない。HW全体の原子的な切り替えには、別途HW側の仕組みが必要。

### UARTでの実験

起動直後の`display=camera, boxes=on`を基準に、次の順で試す。

```text
ui pipe2
ui stat
ui apply boxes pipe2
ui stat
ui apply camera boxes
ui fail apply
ui apply boxes pipe2
ui stat
ui fail rollback
ui apply boxes pipe2
ui stat
ui boxes
ui recover
ui stat
```

最初の`ui pipe2`は`rejected`、一括操作は`committed`、`ui fail apply`後は`rolled-back`、`ui fail rollback`後は`faulted=1`となることを確認する。`faulted`中の`ui boxes`は適用されず、復旧後は元の`camera/on`に戻り`faulted=0`になる。成功した一括操作二回だけが`updates`に加算される。

失敗注入は次の許可されたトランザクション一回だけに作用する。`apply`は実LCD層の適用後に失敗を返す。`rollback`はさらに復元の適用を実行せず失敗を返す。ガード拒否では消費しない。`ui fail off`で未使用の注入を解除できる。これは失敗応答と復元方針の実験であり、BSPの実故障やHW読み戻し検証を代替しない。復旧自体の失敗はホストテストで検証する。

既存の`ui camera|pipe2|boxes|result <x> <y> <w> <h> <ttl-ms>`も使用できる。

## 付帯実験

[periodic.hpp](src/periodic.hpp)は時刻またはフレームsequenceを入力にする周期処理。skipは過去の期限を捨て、ちょうど現在の期限だけ実行する。latest-onlyは遅れても1回、catch-upは設定した上限まで実行し、超過は捨てて統計に加える。期限位相を維持する。入力間隔・遅延は2^31未満を前提とし、時刻とフレーム番号を同じインスタンスへ混在させない。runsは返した実行枠数であって、タスクの完了回数ではない。

主対象は[06: 機能制約](../../tmp/plans/06-ui-feature-constraints.md)と、[05: UI結合](../../tmp/plans/05-ui-integration.md)の操作経路・適用失敗の扱い。[12: 周期処理](../../tmp/plans/12-frame-rate-scheduling.md)、[13: フレームと状態](../../tmp/plans/13-frame-state-structs.md)の結果保持・状態分類は付帯実験として残す。状態・UI・周期の変更を同時投入せず、一段ずつ比較する。

既存middleware、ai-app、pre-kernel、共通ビルド設定は変更しない。既存UIには作業中の変更があるため、計画の「現状」をそのまま前提にせず、実装済み機能を確認して再利用する。既存のレイアウトや生成物を上書きしない。

## このディレクトリに必要なもの

| 構成案 | 責務 |
| --- | --- |
| CMakeLists.txt / Makefile / config/ | 独立build、実験専用UIレイアウト、機能宣言、生成先、メモリ配置 |
| src/ui/ | 生成ハンドラと実験の操作実装の接続。既存UIで不足する部分は実験用に分離 |
| src/state/ | アプリ固有の結果保持・診断状態、RenderTaskState、FrameContext |
| src/task/ | UI操作・shell要求を所有タスクで適用し、実験用周期処理を評価する |
| tests/ | トランザクション、結果保持期限、周期・遅延方針。制約CLIと生成C++の比較はtool側のテストを呼ぶ |
| 実験記録 | 使用レイアウト・機能宣言、画面写真、UART、CPU/AIモニタ、合否 |

チェッカ・CLI・GUIとそのテストは本ディレクトリ内の`tool/feature_constraints/`に置き、アプリ固有の宣言とトランザクション・周期処理も本実験内に置く。新しいmiddlewareは追加しない。既存の統合GUIから呼ぶ場合は簡易サンプルを`--features`で明示する。組み込み側の正式な共通化は検証後の別作業とする。

## pre-kernel等への接続要件（今は変更しない）

- このexperiment内の`src/camera_runtime/`、`src/camera_runtime/driver/`、`config/`を使う。カメラ・LCD・タッチの初期化、クロック、IRQの採用元はこのexperimentのmapで確認する。
- 新規HWが不要なら既存設定を基準にする。実験名だけで既存と同じ設定が選ばれるとは仮定しない。
- 12のフレーム待ちはカメラIRQ→通知→タスク起床の接続が必要。既存の完了通知を別の待ち手が消費してしまわない設計にする。
- WaitForFrame相当の公開APIが足りない場合は、必要な契約をここへ記録する。共有CameraDriverを変更して実験を進めない。
- 13のInferenceFrameから途中状態を移す変更は共有契約に及ぶため保留する。アプリ内の状態分類から先に検証する。

## 実装と自動確認の順序

1. 現行UIと状態保持の挙動を確認し、変更なしの基準を取る。結果の保持期限が現行と計画で異なる場合、整理と仕様変更を別に評価する。
2. 06の宣言とチェッカを検証する。禁止状態の反例、到達性、上限超過、生成C++との遷移一致を確認する。
3. タッチとshellを同じ操作経路につなぐ。ガード拒否時は状態不変。HWへの適用失敗時も成功状態を表示しない。
4. 05の画面遷移と値更新を接続し、13のアプリ内状態を整理する。カメラ・推論の所有権契約は変えない。
5. 12の周期処理を試す。フレーム番号の飛び、時刻ラップ、skip / catch-up / latest-onlyをホストで確認する。バッファ返却は周期回数ではなく実際の取得可能データを基準にし、追いつき処理の上限も設ける。
6. フレーム待ちと時間周期の両方を実機確認する。フレームが止まってもタッチ・要求処理・復旧のために必要な起床が失われないことを確認する。

## 利用者が実施するテスト

UARTを先に開き、別端末で`make -C userspace/experiment-ui-control ram-run`を実行する。起動/Pipe1/2確認に加え、次の操作を行う。

| 操作 | 合格条件・記録 |
| --- | --- |
| `ui apply boxes pipe2`を実行する | 中間状態を確定せず、最終状態だけを適用する。`updates`が一回増える |
| `ui apply boxes pipe2 boxes`を起動直後に実行する | 最後の操作で拒否され、HW適用がなく、状態と`updates`が不変 |
| `ui fail apply`後に一括操作する | `rolled-back`となり、元の表示に戻る。状態と`updates`が不変 |
| `ui fail rollback`後に一括操作し、追加操作と`ui recover`を試す | `faulted`中は操作と検出枠表示が止まり、確定状態への再適用成功後だけ再開する |
| 全画面を往復し、タッチ押下・ドラッグ・画面外への移動を試す | 遷移が正しく、押下状態が残らない。無効・非表示の部品は反応しない。ラベルと数値が重ならない |
| 実装済みのボタン・スライダ等を上下限まで操作する | 値・表示・適用先が一致し、範囲外にならない |
| 禁止された機能の組み合わせをタッチとshellの両方から要求する | 両方で同じガードが働き、拒否後の状態が変わらない |
| 操作中に設定適用を意図的に失敗させる専用試験を実行する | UIの表示状態と実HW状態が食い違わず、失敗が通知される |
| 検出対象を画面から外し、再び入れる | 決めた保持期限で表示が消え、新しい結果で戻る。古い結果の混入がない |
| UI連続操作と推論を併用し、スケジューラ有効/無効を比較する | Pipe1/2が進み、操作が固まらない。ループ回数・タスクCPU時間・推論FPS・遅延統計を記録 |
| 制御されたフレーム停止・再開を行う | タッチやshellが応答し、復旧処理が実行され、再開後に両Pipeが進む |

機能状態の全探索は、IRQやタスクの全インターリーブまで証明するものではない。実機での所有権・待ち合わせ・負荷試験は別に必要。
