# ビルド構成

## 全体の流れ

```text
make -C userspace/<app> <target>
  -> userspace/<app>/Makefile         APP_TARGET、BUILD_DIR、ENABLE_* を決める
  -> project-tools/make/common.mk     includeの入口。自身の位置からパスを決める
     -> defaults.mk                  local.mk、既定値、パス正規化、環境変数
     -> commands.mk                  成果物とコマンドの依存関係
  -> cmake -S . -B <build> -DAPP_TARGET=<app> ...
  -> CMakeLists.txt（ルート）         kernel/* と userspace/<app> を追加する
```

ルートの`CMakeLists.txt`は`APP_TARGET`で選んだ`userspace/<APP_TARGET>/CMakeLists.txt`を読み込みます。各アプリの`Makefile`は`APP_TARGET`とビルド先を決めて`project-tools/make/common.mk`を読み込むだけなので、通常は`make -C userspace/<app> <target>`で操作します。ホスト固有の値は`project-tools/host-config/local.mk`に置きます（[はじめに](../getting-started.md)）。

GNU Make 4.4以上が必要です。複数成果物を一度に生成するグループターゲットと、並列実行時の順序を指定する`.WAIT`を使います。

include先は`common.mk`自身の場所から絶対パスで決めます。`BUILD_DIR`、`CONFIG_FILE`、`CUBEMX_IOC`、`CUBEMX_OUTPUT_DIR`などに指定する相対パスは、コマンドラインで上書きした場合もリポジトリルート基準です。別ディレクトリから`make -f /absolute/path/to/userspace/<app>/Makefile <target>`と呼んでも同じ場所を使います。パスを含まない`cmake`、`python3`などのコマンド名はPATHで検索します。

ツールチェーンは`project-tools/cmake/toolchain.cmake`で`arm-none-eabi-gcc`に固定しています。`project()`より前に読み込むため、CMakeを直接呼ぶ場合も`-DCMAKE_TOOLCHAIN_FILE`は不要です。`compile_commands.json`は既定で出力され、clangdなどのLSPから参照できます。

## CMakeターゲット

| ターゲット | 種類 | 内容 |
| --- | --- | --- |
| `uai::utkernel` | STATIC | μT-Kernel 3.0本体とT-Monitor（`kernel/utkernel`） |
| `uai::stm32n6570_dk` | OBJECT | CubeMX生成コード、HAL、RAM起動コード（`kernel/pre_kernel/stm32n6570-dk`） |
| `uai::middleware` | STATIC | `kernel/middleware`。`uai::utkernel`に依存 |
| `uai::drivers` | STATIC | `kernel/driver`。`uai::utkernel`と`uai::middleware`に依存 |
| `uai::driver_overrides` | OBJECT | HALの弱シンボル（`HAL_GetTick`、`HAL_Delay`、DCMIPPコールバック）を上書きするコード。実行ファイルへ直接入れます |

`uai::middleware`と`uai::drivers`は`APP_TARGET`がルート`CMakeLists.txt`の`UAI_KERNEL_APPS`（ai-app、mini-ai-app）にあるときだけ追加されます。生成したメモリ配置ヘッダを置く`UAI_GENERATED_INCLUDE_DIR`（`<build>/generated`）も同じ条件で定義されます。

## CMakeオプション

| オプション | Make変数 | 内容 |
| --- | --- | --- |
| `UAI_CPU_TASK_MONITOR` | `ENABLE_CPU_TASK_MONITOR=1` | CPU task monitorを有効にします。μT-Kernelのディスパッチフックも有効になります |
| `UAI_KERNEL_TRACE_HOOKS` | なし | μT-Kernelの実行フックだけを有効にします（[μT-Kernel](utkernel.md)） |
| `STM32CUBE_N6_DIR` | `STM32CUBE_N6_DIR` | STM32CubeN6パッケージのルート |
| `STEDGEAI_LIB_DIR` | `STEDGEAI_LIB_DIR` | STEdgeAIランタイム（`Middlewares/ST/AI`） |

## Makeターゲット

### 共通

| ターゲット | 内容 |
| --- | --- |
| `setup` | AI依存の確認とモデル生成（AIアプリのみ）、CubeMX生成、CMake構成 |
| `generate`、`cubemx-generate` | 必須CubeMX成果物を用意します。CMake構成は実行しません |
| `configure` | CubeMX成果物を用意してからCMakeを構成し、`CMakeCache.txt`を生成・更新します |
| `build` | `configure`の後にアプリをビルドします。ソースの更新判定はCMakeに委ねます |
| `clean` | CMakeのビルド成果物を削除します。CubeMX生成は実行しません |
| `monitor` | UARTモニタ（minicom）を開きます。`UART_DEVICE=auto`のときはST-LINKの仮想COMポートを探します |
| `ram-run` | ビルドしてRAMへロードし、実行します |
| `sign` | `build`の後に`STM32_SigningTool_CLI`でFlash起動用の署名イメージを作ります |
| `program`（`flash`） | `build`の後にFSBL、署名済みアプリ、モデルを外部NORへ書き込み、検証します |
| `help` | 利用できるターゲットと、読み込んだ`local.mk`のパスを表示します |

### AIアプリ（`ENABLE_AI=1`）

| ターゲット | 内容 |
| --- | --- |
| `ai-models` | モデルを取得し、`stedgeai`でNPU向けコードを生成します。`ai-model-<model>`で個別に実行できます |
| `ai-load` | モデルの重みとcommand blobを外部NORへ書き込みます（`ai-load-weights`、`ai-load-blobs`） |
| `ai-run` | `ai-load`の後に`ram-run`を実行します |

モデル変換のオプション（`AI_MODEL_OPTIMIZATION`、`AI_MODEL_INPUT_DATA_TYPE`など）は`defaults.mk`に既定値があり、コマンドラインで上書きできます。依存関係はCMake構成時に確認します。`ai-build`はモデル生成の後にビルドし、`ai-load`はビルド、重み、command blobの順に実行します。`ai-run`はその後にRAM実行します。段階ごとに依存ターゲットと再帰makeで順序を保つため、GNU Make 4.3でも`make -j`でも処理順が変わりません。

### モニター（`ENABLE_THREAD_MONITOR=1`、`ENABLE_CPU_TASK_MONITOR=1`）

| ターゲット | 内容 |
| --- | --- |
| `thread-monitor` | AI model monitorの記録をST-LINKで読み出し、JSONとPNGを生成します |
| `cpu-task-monitor` | CPU task monitorの記録を読み出し、JSON、CSV、PNGを生成します |

`*-dump`は読み出しだけを行います。出力先は`THREAD_MONITOR_PNG`、`CPU_TASK_MONITOR_PNG`などで変更できます。解析ツールは[host_app/README.md](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md)を参照してください。

## CubeMXの扱い

HALの初期化コードはIOCから生成し、`<build>/cubemx`に置きます。`project-tools/scripts/cubemx-generate.sh`がIOCをビルドツリーへコピーしてから`STM32CubeMX -q`で生成するため、ソースツリーに生成物は入りません。IOCはアプリの`config/`に置き、`Makefile`の`SAMPLE_DEFAULT_IOC`または`local.mk`の`CUBEMX_IOC`で指定します。

`defaults.mk`の`CUBEMX_OUTPUTS`は、CMakeが使用する次の必須成果物を明示します。CubeMXが生成する全ファイルの一覧ではなく、構成・ビルドを開始するための最低限の契約です。

| 出力（`CUBEMX_OUTPUT_DIR`からの相対パス） | 用途 |
| --- | --- |
| `FSBL/Core/Inc/main.h`、`stm32n6xx_hal_conf.h` | FSBL初期化とHAL設定 |
| `FSBL/Core/Src/main.c`、`extmem_manager.c`、`stm32n6xx_hal_msp.c`、`stm32n6xx_it.c`、`system_stm32n6xx_fsbl.c`、`syscalls.c`、`sysmem.c` | RAM起動・ボード初期化に組み込むソース |
| `FSBL/Core/Startup/startup_stm32n657x0hxq_fsbl.s` | FSBL起動コード |
| `Appli/Core/Inc/main.h` | アプリ側の生成ヘッダー |
| `Drivers/STM32N6xx_HAL_Driver/Inc/stm32n6xx_hal.h`、`Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c` | 生成したHALの存在確認 |
| `FSBL/STM32N657X0HXQ_AXISRAM2_fsbl.ld` | experiment-aiのFlash起動用FSBLのみ |
| `.uai-settings`、`.uai-generated` | 生成設定の変更検出と正常完了の記録 |

成果物と完了スタンプは一つのグループターゲットなので、並列Makeでも生成処理は一度です。IOC、生成スクリプト、Make定義、ホスト設定の更新、生成設定の差し替え、必須成果物の欠損で再生成します。CubeMXが成功を返しても必須ファイルが足りなければ失敗とし、CMake構成や書き込みへ進みません。完了スタンプは全成果物を確認した後だけ作るため、途中失敗の次回実行でも再生成します。

```text
IOC / 生成設定 / スクリプト
  -> CubeMX必須成果物 + 完了スタンプ
  -> configure
  -> build
  -> ram-run / sign / program
```

`configure`自体は毎回CMakeを実行し、コマンドラインの構成変更を反映します。CubeMX生成は更新が必要な場合だけです。

## メモリ配置の生成（AIアプリ）

ai-appでは、`build`の中で`host_app/auto_static_memory_layout`が`config/board_memory.json`、`config/application_memory.json`、`config/model_layout.json`と生成済みモデルを読み、`<build>/generated/`へ次を出力します。

| 出力 | 用途 |
| --- | --- |
| `memory_layout.json`、`memory_layout.yml` | 配置の正本と確認用の一覧 |
| `middleware/memory/generated/**/*.hpp` | `static_memory_layout`のキーと領域の表、バッファ数（[memory](../middleware/memory.md)） |
| `stm32n6570-dk-npu-ram.ld` | ベースのリンカスクリプトに領域を追加したもの |

領域の重なり、容量不足、command blobの枠あふれはこの段階でエラーになります。入力ファイルが変わると自動で再生成されます。
