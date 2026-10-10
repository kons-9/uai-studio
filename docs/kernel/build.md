# ビルド構成

## 全体の流れ

```text
make -C userspace/<app> <target>
  -> userspace/<app>/Makefile         APP_TARGET、BUILD_DIR、ENABLE_* を決める
  -> project-tools/make/common.mk      共通ターゲット。local.mk を読み込む
  -> cmake -S . -B <build> -DAPP_TARGET=<app> ...
  -> CMakeLists.txt（ルート）         kernel/* と userspace/<app> を追加する
```

ルートの`CMakeLists.txt`は`APP_TARGET`で選んだ`userspace/<APP_TARGET>/CMakeLists.txt`を読み込みます。各アプリの`Makefile`は`APP_TARGET`とビルド先を決めて`project-tools/make/common.mk`を読み込むだけなので、通常は`make -C userspace/<app> <target>`で操作します。ホスト固有の値は`project-tools/host-config/local.mk`に置きます（[はじめに](../getting-started.md)）。

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
| `generate` | CubeMX生成とCMake構成 |
| `configure`、`build`、`clean` | CMakeの構成、ビルド、クリーン |
| `monitor` | UARTモニタ（minicom）を開きます。`UART_DEVICE=auto`のときはST-LINKの仮想COMポートを探します |
| `ram-run` | ビルドしてRAMへロードし、実行します |
| `sign` | `STM32_SigningTool_CLI`でFlash起動用の署名イメージを作ります |
| `program`（`flash`） | FSBL、署名済みアプリ、モデルを外部NORへ書き込み、検証します |
| `help` | 利用できるターゲットと、読み込んだ`local.mk`のパスを表示します |

### AIアプリ（`ENABLE_AI=1`）

| ターゲット | 内容 |
| --- | --- |
| `ai-deps` | STEdgeAIランタイムと後処理ライブラリの場所を確認します |
| `ai-models` | モデルを取得し、`stedgeai`でNPU向けコードを生成します。`ai-model-<model>`で個別に実行できます |
| `ai-load` | モデルの重みとcommand blobを外部NORへ書き込みます（`ai-load-weights`、`ai-load-blobs`） |
| `ai-run` | `ai-load`の後に`ram-run`を実行します |

モデル変換のオプション（`AI_MODEL_OPTIMIZATION`、`AI_MODEL_INPUT_DATA_TYPE`など）は`common.mk`に既定値があり、コマンドラインで上書きできます。

### モニター（`ENABLE_THREAD_MONITOR=1`、`ENABLE_CPU_TASK_MONITOR=1`）

| ターゲット | 内容 |
| --- | --- |
| `thread-monitor` | AI model monitorの記録をST-LINKで読み出し、JSONとPNGを生成します |
| `cpu-task-monitor` | CPU task monitorの記録を読み出し、JSON、CSV、PNGを生成します |

`*-dump`は読み出しだけを行います。出力先は`THREAD_MONITOR_PNG`、`CPU_TASK_MONITOR_PNG`などで変更できます。解析ツールは[host_app/README.md](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md)を参照してください。

## CubeMXの扱い

HALの初期化コードはIOCから生成し、`<build>/cubemx`に置きます。`project-tools/scripts/cubemx-generate.sh`がIOCをビルドツリーへコピーしてから`STM32CubeMX -q`で生成するため、ソースツリーに生成物は入りません。IOCはアプリの`config/`に置き、`Makefile`の`SAMPLE_DEFAULT_IOC`または`local.mk`の`CUBEMX_IOC`で指定します。

## メモリ配置の生成（AIアプリ）

ai-appでは、`build`の中で`host_app/auto_static_memory_layout`が`config/board_memory.json`、`config/application_memory.json`、`config/model_layout.json`と生成済みモデルを読み、`<build>/generated/`へ次を出力します。

| 出力 | 用途 |
| --- | --- |
| `memory_layout.json`、`memory_layout.yml` | 配置の正本と確認用の一覧 |
| `middleware/memory/generated/**/*.hpp` | `static_memory_layout`のキーと領域の表、バッファ数（[memory](../middleware/memory.md)） |
| `stm32n6570-dk-npu-ram.ld` | ベースのリンカスクリプトに領域を追加したもの |

領域の重なり、容量不足、command blobの枠あふれはこの段階でエラーになります。入力ファイルが変わると自動で再生成されます。
