---
marp: true
theme: default
paginate: true
title: μAI-Studio
---

# μAI-Studio

μT-Kernel 3.0向けエッジAI開発環境

TRONプログラミングコンテスト2026 開発環境・開発ツール部門
後藤俊樹

<!-- 対象ボード: STM32N6570-DK / ライセンス: MIT（自作部分） -->

---

## μAI-Studioとは

μT-Kernel 3.0でNPUを使うAIアプリを開発するための開発環境

- 対象：STM32N6570-DK（Cortex-M55＋Neural-ART NPU）
- モデル変換、メモリ配置、ビルド、書き込み、実行時の解析を、コマンドラインで一貫して扱う
- TRON×AIを2つの面で実現する
  - **μT-Kernel上で動くAI**：NPU推論アプリを作るためのツールとミドルウェア
  - **AIが開発に参加できる開発環境**：AIエージェントがビルドから実機確認まで進められる

---

## 構成

| 分類 | 構成要素 | 役割 |
|---|---|---|
| 開発基盤 | CMakeとMakeによるCLI | μT-Kernelからアプリまでの一括ビルド、書き込み、UART監視 |
| ホストツール | auto_static_memory_layout | メモリ配置の生成と検査 |
| ホストツール | AI model monitor / CPU task monitor | 実行記録の解析と可視化 |
| ミドルウェア | ai_runtime、memory_manager、image_resizer | μT-Kernel上のAI実行基盤 |
| μT-Kernel拡張 | フックAPIの実装 | タスク切替と割込みの記録 |
| 評価用サンプル | ai-app | カメラ表示＋3モデル推論 |

---

## 従来の開発との違い

| | ベンダーIDE中心の開発 | μAI-Studio |
|---|---|---|
| 操作 | IDEのGUI | Makeのコマンド |
| エディタ | IDEに固定 | 任意（LSP対応） |
| AIエージェント | GUI操作は任せにくい | ビルドから実機確認まで任せられる |
| メモリ配置 | 手で設定 | 生成して、ビルド時に検査 |
| NPUとCPUの並列化 | アプリごとに作り込む | ミドルウェアがμT-Kernelのタスクで並列に実行 |
| 性能の解析 | 個別に計測コードを書く | タスク別、モデル別の記録を1コマンドで可視化 |

---

## 必要なもの

- ネイティブのLinux、STM32N6570-DK（カメラモジュールとLCDは付属）
- OSのパッケージ：CMake、GNU Armツールチェーン、Python、minicom
- STの無償ツール（別途入手）：STM32CubeMX 6.x、STM32CubeN6、STEdgeAI 4.0、STM32CubeProgrammer
- ホストごとのパスは`build-system/host-config/local.mk`に書くだけ
- 利用している既存ソフトウェアの一覧は`THIRD_PARTY_NOTICES.md`

---

## 開発の流れ

```sh
make -C userspace/ai-app setup             # モデル取得、NPUコード生成、CubeMX生成、CMake構成
make -C userspace/ai-app monitor           # UART監視（別端末で開いておく）
make -C userspace/ai-app ai-load           # モデルの重みとNPUコマンド（command blob）を外部NOR Flashへ書き込み
make -C userspace/ai-app ram-run           # ビルドしてRAMへ転送し、実行
make -C userspace/ai-app thread-monitor    # AI model monitor：モデルごとの実行記録を取得して可視化
make -C userspace/ai-app cpu-task-monitor  # CPU task monitor：タスク別CPU使用率を取得して可視化
```

作る → 動かす → 測る → 直す、をすべてコマンドで回せる

---

## 課題1：開発環境がIDEとGUIに縛られる

- マイコン開発は、ベンダーIDEとGUI操作を前提にしたものが多い
- ビルド、書き込み、ログ確認がIDEの中に閉じていて、自動化しにくい
- AIコーディングエージェントやCIから、同じ手順を再現できない
- 使い慣れたエディタやツールを選べない

---

## 解決1：CMakeとMakeで開発フローをコマンド化

- μT-Kernel 3.0本体、ドライバ、ミドルウェア、アプリをCMakeで一括ビルド
- `compile_commands.json`を出力し、clangdなどLSPに対応したエディタでコード補完が効く
- ホストごとの設定は`build-system/host-config/local.mk`の1ファイルに集約
- **AIエージェントが開発に参加できる**
  - すべての操作がコマンドなので、AGENTS.mdに対応したエージェント（GitHub Copilot、Codexなど）がビルド、書き込み、UARTでの起動確認まで自分で進められる
  - AGENTS.mdには、使うMakeターゲットと、成功を示すUARTログ（`camera: pipe1=started pipe2=started`）を書き、「実機に書き込み、起動を確認するまで」を作業の完了条件にしている

---

## 自分のアプリを追加する

カーネルを書き換えずにアプリを追加でき、`setup`、`ram-run`、`monitor`はそのまま使える

1. `userspace/<app>/src/main.cpp`に`usermain()`を書く
2. `CMakeLists.txt`でμT-Kernelとボード設定のターゲットをリンクする
3. `Makefile`でアプリ名を指定し、共通の`common.mk`を読み込む
4. CubeMXの設定ファイル（IOC）を`config/`に置く
5. `make -C userspace/<app> setup`、`ram-run`で動かす

- 最小の例は`userspace/experiment-hello-world`。手順の詳細は`docs/kernel.md`の「アプリの追加」
- ドライバ、ミドルウェア、AI用のMakeターゲットは現在ai-app向け。ほかのアプリで使う場合は、ルートの`CMakeLists.txt`とメモリ配置の設定を追加する

---

## 課題2：NPUアプリのメモリ配置は手作業

AIモデルを動かすには、置き場所を決める対象が多い

| 対象 | 置き場所 |
|---|---|
| モデルの重み | 外部NOR Flash |
| NPUコマンド | 外部NOR Flash（リンカで配置） |
| NPU作業領域 | NPU RAM |
| カメラ、表示、推論用のフレーム | PSRAM |
| アプリ本体、実行記録 | 内部RAM / PSRAM |

- モデルを増やすたびに、各領域のアドレスとサイズを手で調整し直す必要がある
- 設定がずれてもビルドは通り、**実機で原因の分かりにくい不具合**になる

---

## 解決2：メモリ配置の生成と検査（auto_static_memory_layout）

```text
board_memory.json ──────┐
application_memory.json ┼─▶ resolve ─▶ memory_layout.json ─┬─▶ C++ヘッダー
model_layout.json ──────┤              （配置の正本）       ├─▶ リンカスクリプト
STEdgeAIの生成物 ───────┘                                   └─▶ YAML（確認用）
```

- ボード定義、アプリのバッファ要求、モデルの生成物（入出力テンソル、NPUコマンドのサイズ）を読み込む
- フレームバッファやNPUコマンド領域を配置し、C++ヘッダーを生成して、ベースのリンカスクリプトに配置を書き込む
- 生成物はCMakeのビルドに直接つながり、手で書き写す必要がない
- 領域の重なり、容量不足、NPUコマンドの枠あふれを**ビルド時にエラーにする**

---

## 課題3：NPU推論とRTOSをどう組み合わせるか

- NPUの完了を待つ間、CPUを遊ばせたくない
- 推論が遅れても、カメラ映像の表示は止めたくない
- 複数のモデルを同時に扱いたい

---

## 解決3：μT-Kernel向けAIミドルウェア

```text
カメラ ─┬─ Pipe1 ─▶ LCD表示（推論を待たない）
        └─ Pipe2（DCMIPPで縮小）─▶ [前処理タスク] ─▶ [NPUタスク] ─▶ [後処理タスク]
                                   person / face / segmentation を流れ作業で処理
```

- **パイプラインランタイム**：NPUの実行中も、CPUは別モデルの前処理や後処理を進める（コアはホストPCでテスト可能）
- **バッファの所有権管理**：カメラDMA、LCD、推論のどれが使っているかを固定プールで管理する
- **画像縮小**：NPU入力の縮小と余白付きの配置（letterbox）をDCMIPPで行い、CPUでの縮小を減らす
- **ドライバの排他制御**：ハードウェアごとに書き込み権を1つに限り、タスク間の競合を防ぐ

---

## 課題4：どこで時間を使っているか見えない

- CPU、NPU、複数のタスクが並行して動くと、ボトルネックが分からない
- μT-Kernel 3.0には、タスク切替と割込みを記録するフックAPI（`td_hok_dsp` / `td_hok_int`）が宣言されている
- しかし**実装がなく**、BSP2ではアプリからAPIを使うこともできなかった

---

## 解決4：フックを実装し、CPUとNPUを可視化

- `td_hok_dsp` / `td_hok_int`を実装し、BSP2からアプリがフックを登録できるようにした
- Cortex-M55（ARMv8-M）のディスパッチ処理と割込み処理から、フックを呼ぶようにした
- μT-Kernel/DSの標準APIなので、ほかのアプリもCMakeオプション`UAI_KERNEL_TRACE_HOOKS`でフックを使える
- フックの呼び出しは、オプションを指定したときだけ組み込まれる（ai-appでは既定で有効）
- この上に2つの可視化ツール（次の2枚）を作った。記録はPSRAM上のリングバッファにため、ホストがSWDでボードを一時停止して読み出す（UARTの帯域を使わない）

---

## 可視化の例：CPU task monitor

![h:500](host_app/cpu_task_monitor/sample/cpu_task_monitor.png)

フックの記録から作成。上段：タスク別CPU使用率　中段：各タスクのループのガント図　下段：ループ時間の平均と最大

---

## 可視化の例：AI model monitor

![h:500](host_app/ai_model_monitor/sample/ai_model_monitor.png)

AIランタイムに埋め込んだ計測処理から作成。上段：モデルごとのCPU処理（青）とNPU処理（橙）のタイムライン　下段：各段階の平均時間

---

## 活用例1：CPUとNPUは並列に動いているか

評価用サンプル（3モデル同時推論）を実機で記録した結果

- personやsegmentationをNPUが推論している間に、faceの前処理や後処理がCPUで進んでいることを、AI model monitorのタイムラインで確認できた
- CPU task monitorからは、カメラ表示タスクがCPU時間の約77%を使っていることも分かる

---

## 活用例2：どこが遅いか

| モデル | CPU処理（平均） | NPU処理（平均） |
|---|---:|---:|
| person | 17.3 ms | 396.4 ms |
| segmentation | 42.0 ms | 331.4 ms |
| face | 19.7 ms | 62.8 ms（中央値20 ms、ときどき200 ms超） |

※記録の分解能は10 ms

- personとsegmentationのNPU処理が突出して長く、ここがボトルネックだと一目で分かる

[TODO（任意）: 実施した対策と、改善前後の数値]

---

## 評価用サンプル：ai-app

- カメラ映像をLCDに表示しながら、3つのモデルをNPUで推論する
  - 人物検出（YOLOX nano）
  - 顔検出（BlazeFace）
  - セグメンテーション（DeepLab v3）
- 開発環境の各機能（メモリ配置の生成、ミドルウェア、モニター）をすべて使っている

[TODO: デモ動画、写真]

---

## μT-Kernel 3.0との関わり

- μT-Kernel 3.0本体からアプリまでを、1つのCMakeプロジェクトでビルドする
- 宣言のみだったフックAPIを実装し、解析ツールの土台にした。μT-Kernel/DSの標準APIなので、ほかのアプリからも使える
- 推論の各段階を、μT-Kernelのタスクとして実行する
- カメラ、LCD、NPU、NOR Flash、PSRAMのドライバを、μT-Kernel上で動くように整えた

---

## 今後の展望

- **AIモデルの作成から支援する**
  - ボードのカメラで撮った画像を集め、学習、量子化、NPU向け変換、配置、書き込みまでをつなげる
- **AIエージェントによる性能チューニング**
  - モニターの記録はJSONで出力している。AIエージェントがこれを読んでボトルネックを見つけ、改善案を実機で試す流れを作る
- **対応ボードを増やす**
  - CMakeベースの構成を活かし、ほかのNPU搭載マイコンにも広げる
- **μT-Kernel本体への還元**
  - フックAPIの実装を、TRONフォーラムの上流リポジトリへ提案する

---

## まとめ

| 課題 | 解決 |
|---|---|
| IDEとGUIに縛られる | CMakeとMakeによるCLIで、どのエディタやAIエージェントからも開発できる |
| メモリ配置が手作業 | 配置を生成し、不整合をビルド時に検出する |
| NPUとRTOSを組み合わせる基盤がない | μT-Kernel向けAIミドルウェア |
| 実行状況が見えない | フックAPIを実装し、CPUとNPUを可視化する |

MITライセンスで公開（自作部分）：https://github.com/kons-9/uai-studio
