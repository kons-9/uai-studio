# 利用している既存ソフトウェアとμT-Kernelへの変更

μAI-Studioのうち、応募者が作成した部分は[MITライセンス](LICENSE)で公開している。
この文書では、それ以外の既存ソフトウェアと、μT-Kernel 3.0 / BSP2に加えた変更を示す。
既存ソフトウェアはいずれも無償で入手でき、それぞれのライセンスに従う。

## 既存ソフトウェア

### リポジトリに含むもの

| 名称 | 権利者 | 入手方法 | 機能・用途 | ライセンス |
|---|---|---|---|---|
| μT-Kernel 3.0 | 坂村健（配布：TRONフォーラム） | [tron-forum/mtkernel_3](https://github.com/tron-forum/mtkernel_3)（改変版：[kons-9/mtkernel_3](https://github.com/kons-9/mtkernel_3)）。`kernel/utkernel/mtk3_bsp2/mtkernel`にサブモジュールとして取得 | リアルタイムOS | T-License 2.2 |
| μT-Kernel 3.0 BSP2 | 坂村健（配布：TRONフォーラム） | [tron-forum/mtk3_bsp2](https://github.com/tron-forum/mtk3_bsp2)（改変版：[kons-9/mtk3_bsp2](https://github.com/kons-9/mtk3_bsp2)）。`kernel/utkernel/mtk3_bsp2`にサブモジュールとして取得 | STM32向けのμT-Kernel実装 | T-License 2.1 / 2.2（各ファイルの記載に従う） |
| STM32N6570-DK BSP（XSPI、カメラ） | STMicroelectronics | STM32CubeN6から`kernel/driver/c_bsp/`、`kernel/driver/camera_driver/board_driver/`に取り込み | 外部メモリとカメラの制御 | 配布元のLICENSEに従う |
| IMX335センサードライバ | STMicroelectronics | STM32CubeN6から`kernel/driver/camera_driver/sensor_driver/`に取り込み | カメラセンサーの制御 | 配布元のLICENSEに従う |
| vision_models_pp | STMicroelectronics | STM32N6 GettingStartedの後処理ライブラリから`userspace/ai-app/third_party/`に取り込み。`od_pp_st_yolox.c`に出力数の上限チェックを追加 | 物体検出と顔検出の後処理 | 配布元のLICENSEに従う |
| STM32 ISPライブラリの一部（`isp_core.c`） | STMicroelectronics | STM32N6向けISPライブラリから`userspace/experiment-camera-pipe2/`に取り込み | 実験用アプリのカメラ画質調整 | 配布元のLICENSEに従う |
| STM32N6570-DK用FSBL（`stm32n6570-dk-ai_fsbl.hex`） | STMicroelectronics | STのSTM32N6570-DK向けサンプル（STM32N6_Survivor_Detection）のバイナリを`userspace/ai-app/fsbl/`に取り込み。[TODO: 公開URL] | 外部Flash起動の第1段ブートローダ | 配布元のLICENSEに従う |
| Neural-ARTのメモリプール定義と変換プロファイル | STMicroelectronics | [STM32N6-GettingStarted-ObjectDetection](https://github.com/STMicroelectronics/STM32N6-GettingStarted-ObjectDetection)の設定をもとに`userspace/ai-app/models/`に配置 | STEdgeAIによるモデル変換の設定 | 配布元のLICENSEに従う |
| Lucide 0.468.0（3アイコン） | Lucide Contributors | [lucide-static](https://www.npmjs.com/package/lucide-static/v/0.468.0)から`host_app/auto_static_memory_layout/static/icons/`に配置 | メモリ配置GUIの操作アイコン | [ISC](host_app/auto_static_memory_layout/static/icons/LICENSE) |

実験用の`userspace/experiment-*`にも、上記のST製ファイル（BSP、IMX335ドライバ、vision_models_pp、FSBL、メモリプール定義）のコピーを含む。各ファイルの冒頭にある著作権表示はそのまま残している。

### ビルドや実行時に取得・使用するもの（リポジトリには含まない）

| 名称 | 権利者 | 入手方法 | 機能・用途 | ライセンス |
|---|---|---|---|---|
| STM32CubeN6（HAL、CMSIS、BSP） | STMicroelectronics、Arm | STのWebサイトまたはGitHubから取得 | マイコン周辺機能のドライバ | 配布元のLICENSEに従う（CMSISはApache-2.0） |
| STM32CubeMX | STMicroelectronics | STのWebサイトから取得 | 初期化コードの生成 | ST独自ライセンス |
| STEdgeAI 4.0 | STMicroelectronics | STのWebサイトから取得 | モデルのNPU向け変換、Neural-ARTランタイム | ST独自ライセンス |
| STM32CubeProgrammer | STMicroelectronics | STのWebサイトから取得 | 書き込み | ST独自ライセンス |
| 人物検出モデル（YOLOX nano） | STMicroelectronics（元の手法：Megvii） | `setup`時に[STM32N6-GettingStarted-ObjectDetection](https://github.com/STMicroelectronics/STM32N6-GettingStarted-ObjectDetection)から取得 | 評価用サンプルの推論 | 配布元のLICENSEに従う |
| セグメンテーションモデル（DeepLab v3） | STMicroelectronics（元の手法：Google） | `setup`時に[STM32N6-GettingStarted-SemanticSegmentation](https://github.com/STMicroelectronics/STM32N6-GettingStarted-SemanticSegmentation)から取得 | 評価用サンプルの推論 | 配布元のLICENSEに従う |
| 顔検出モデル（BlazeFace） | STMicroelectronics（元の手法：Google、学習データ：WIDER FACE） | `setup`時に[STM32N6-GettingStarted-FaceDetection](https://github.com/STMicroelectronics/STM32N6-GettingStarted-FaceDetection)から取得 | 評価用サンプルの推論 | 配布元のLICENSEに従う |
| GNU Arm Embedded Toolchain、CMake、Python、uv、minicom | 各開発元 | OSのパッケージ管理などから取得 | ビルド、ホストツールの実行、UART監視 | 各ライセンス |
| matplotlib | Matplotlib Development Team | `uv`で`host_app/pyproject.toml`から取得 | モニターの可視化 | Matplotlib License（PSFベース） |

## μT-Kernel 3.0 / BSP2への変更

上流のBSP2 v1.00.04（コミット1ab52cc。μT-Kernel 3.0 v3.00.07を参照）に対して、次の変更を加えている。
フックの呼び出しは、CMakeオプション`UAI_KERNEL_TRACE_HOOKS`または`UAI_CPU_TASK_MONITOR`を有効にしたときだけ組み込まれる（ai-appでは`UAI_CPU_TASK_MONITOR`が既定で有効）。
SysTickの計数とフォルト時のレジスタ表示は常に組み込まれる。計数用の変数`uai_systick_count`はアプリ側で定義する。

| ファイル | 変更内容 |
|---|---|
| `config/config.h` | ミューテックスの最大数を4から8に変更（ドライバの排他制御で使用）。`USE_DBGSPT_TRACE`を追加し、有効時にデバッガサポート機能（`USE_DBGSPT`）も有効にする |
| `include/tk/dbgspt.h` | 空だったラッパーから、μT-Kernel本体の`tk/dbgspt.h`を読み込むように変更し、アプリからフックAPIを使えるようにした |
| `sysdepend/stm32_cube/cpu/core/armv8m/dispatch.S` | タスク切替時に`knl_dispatch_hook`を呼ぶ処理を追加 |
| `sysdepend/stm32_cube/cpu/core/armv8m/interrupt.c` | 割込みハンドラの前後で`knl_trace_int_enter` / `knl_trace_int_leave`を呼ぶ処理と、SysTickの計数を追加 |
| `sysdepend/stm32_cube/cpu/core/armv8m/exc_hdr.c` | フォルト発生時に、原因を調べるためのレジスタ値を表示するようにした |
| `.gitmodules` | μT-Kernel本体の参照先を改変版リポジトリに変更 |
| μT-Kernel本体（`mtkernel`、改変版のコミットb6cc90b） | `td_hok_dsp` / `td_hok_int`と、上記から呼ばれるフック処理を実装 |

## 権利処理について

応募者は、上記の既存ソフトウェアの利用にあたり、各ライセンスの条件を確認し、著作権などの権利処理を行ったことを、TRONプログラミングコンテストの主催者及び協力団体に対して保証する。
