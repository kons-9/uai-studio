# はじめに

μAI-Studioでアプリをビルドし、STM32N6570-DKで動かすまでの準備をまとめます。評価用サンプルのai-appを例にします。

## ハードウェアの前提

### 対象ボード

[STM32N6570-DK](https://www.st.com/en/evaluation-tools/stm32n6570-dk.html)（Discovery kit）だけを対象にしています。

| 構成要素 | 内容 | μAI-Studioでの使い方 |
| --- | --- | --- |
| STM32N657X0H3Q | Arm Cortex-M55、Neural-ART NPU、内蔵SRAM 4.2 MB | アプリ本体はSRAMにロードして実行します |
| Octo-SPI NOR Flash（MX66UW1G45G、1 Gbit） | `0x70000000`にメモリマップ | モデルの重みとNPUのcommand blob、Flash起動時のFSBLとアプリを置きます |
| Hexadeca-SPI PSRAM（APS256XX、256 Mbit） | `0x90000000`にメモリマップ | カメラ、表示、推論のフレームバッファと、モニターの記録先に使います |
| 5インチLCD（800x480、静電容量タッチ） | ボードに付属 | カメラ映像と推論結果の表示 |
| カメラモジュール（MB1854、IMX335） | ボードに付属 | DCMIPPの2系統で表示用と推論用のフレームを取得します |
| STLINK-V3EC | ボード上のデバッガ | SWDによる書き込みとメモリ読み出し、UARTの仮想COMポート |

STM32N6には内蔵Flashがありません。アプリはSWD経由でSRAMに直接ロードして実行するか、外部NORに署名済みイメージを書いてFSBLから起動します。開発中は前者（RAM実行）を使います。

### 接続

- ST-LINKのUSB Type-Cコネクタをホストにつなぎます。これ1本で電源、SWD、UARTをまかないます。電力が足りない場合はUSB Type-C同士のケーブルを使ってください。
- カメラモジュールとLCDはボードのコネクタに接続した状態にします。カメラが接続されていないとai-appは`camera: no frame`を繰り返します。
- UARTはST-LINKの仮想COMポート（Linuxでは`/dev/ttyACM*`）に出ます。115200 bps、8N1、フロー制御なしです。

### ブートモード

ボードのBOOTスイッチで起動方法を切り替えます。スイッチの位置はボードのユーザーマニュアルを参照してください。

| モード | 用途 |
| --- | --- |
| 開発モード（Development boot） | `ram-run`でSRAMにロードして実行するとき、外部NORへ書き込むとき |
| Flash起動（BOOT0、BOOT1ともにLOW） | `program`で書き込んだFSBLとアプリを電源投入時に起動するとき |

### ホストPC

ネイティブのLinuxを前提にしています。ST-LINKをUSBデバイスとして直接認識できる必要があるため、仮想マシンやWSLではUSBのパススルー設定が別途必要です。ST-LINKを一般ユーザーで使うには、STM32CubeProgrammerに付属するudevルールを導入するか、`/dev/ttyACM*`と対応するUSBデバイスへのアクセス権を与えてください。

## ツールの取得

### Linuxのパッケージ

Ubuntu系では次をインストールします。

```sh
sudo apt update
sudo apt install build-essential cmake git python3 minicom \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
```

| パッケージ | 用途 |
| --- | --- |
| `cmake`、`build-essential` | CMake 3.16以上とGNU Make 4.4以上 |
| `gcc-arm-none-eabi`など | Cortex-M55向けのクロスコンパイラ。PATHにない場合は`local.mk`の`ARM_NONE_EABI_TOOLCHAIN_PATH`で場所を指定します |
| `python3` | モデル生成スクリプトとメモリ配置の生成 |
| `minicom` | `make monitor`が使うUARTモニタ |
| `uv`（任意） | `thread-monitor`、`cpu-task-monitor`の解析ツールの依存を用意します。[uvの導入方法](https://docs.astral.sh/uv/getting-started/installation/)を参照してください |

`make --version`でGNU Make 4.4以上を確認してください。ディストリビューションの標準版が古い場合は、4.4以上を別途導入してPATHで優先します。CubeMXの複数成果物と、並列ビルド・書き込みの順序をMakeで管理するために必要です。

### STの無償ツール

STのWebサイトから取得します。ダウンロードにはmySTアカウント（無料）が必要です。いずれもLinux版を使います。

| ツール | 取得先 | 用途 | 設定する変数 |
| --- | --- | --- | --- |
| STM32CubeMX 6.x | [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html) | IOCファイルからHAL初期化コードを生成します。STM32C5向けのSTM32CubeMX2ではなく、6.x系を使います | `CUBEMX_EXECUTABLE` |
| STM32CubeN6 | [STM32CubeN6](https://www.st.com/en/embedded-software/stm32cuben6.html) | HAL、CMSIS、BSP。STM32CubeMXのパッケージ管理からも取得できます | `STM32CUBE_N6_DIR` |
| STEdgeAI Core 4.0 | [STEdgeAI-Core](https://www.st.com/en/development-tools/stedgeai-core.html) | `stedgeai` CLIによるモデルのNPU向け変換と、Neural-ARTランタイム（`ll_aton`） | `STEDGEAI_BIN`、`STEDGEAI_LIB_DIR` |
| STM32CubeProgrammer | [STM32CubeProg](https://www.st.com/en/development-tools/stm32cubeprog.html) | `STM32_Programmer_CLI`による書き込みとメモリ読み出し、External Loader、`STM32_SigningTool_CLI` | `STM32_PROGRAMMER_ROOT` |

- STEdgeAIのバージョンは、モデル生成に使う`stedgeai`とリンクするランタイムで一致させてください。不一致はCMakeの構成時にエラーになります。
- Neural-ARTランタイムはライセンスの都合でリポジトリに含めていません。`setup`時に`STEDGEAI_LIB_DIR`から参照します。
- STM32CubeMXはJavaで動くGUIアプリですが、μAI-Studioは`-q`オプションでスクリプト実行します。初回はGUIで起動してライセンスに同意し、STM32CubeN6パッケージを導入しておくと確実です。

## リポジトリの取得

サブモジュール（μT-Kernel 3.0 BSP2と本体）を含めて取得します。

```sh
git clone --recurse-submodules https://github.com/kons-9/uai-studio.git
cd uai-studio

# clone済みの場合
git submodule update --init --recursive
```

## ホスト設定（local.mk）

ホストごとに変わる値は`project-tools/host-config/local.mk`に集約しています。このファイルはGit管理外です。

```sh
test -f project-tools/host-config/local.mk || \
  cp project-tools/host-config/local.mk.example project-tools/host-config/local.mk
```

インストール先と接続するボードに合わせて編集します。

```make
STM32_PROGRAMMER_ROOT = /path/to/STM32CubeProgrammer/tools
STM32_PROGRAM_SERIAL = <ST-LINK serial number>
STM32CUBE_N6_DIR = /path/to/STM32Cube_FW_N6_V1.3.0
STEDGEAI_LIB_DIR = /path/to/STEdgeAI/4.0/Middlewares/ST/AI
STEDGEAI_BIN = /path/to/STEdgeAI/4.0/Utilities/linux
CUBEMX_EXECUTABLE = /path/to/STM32CubeMX
UART_DEVICE = auto
UART_BAUD = 115200
```

| 変数 | 内容 |
| --- | --- |
| `STM32_PROGRAMMER_ROOT` | STM32CubeProgrammerの`tools`ディレクトリ。`bin/STM32_Programmer_CLI`と`bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr`をここから探します |
| `STM32_PROGRAM_SERIAL` | ST-LINKのシリアル番号。`STM32_Programmer_CLI -l`で確認できます。設定すると書き込み先が特定され、`UART_DEVICE = auto`が同じST-LINKの仮想COMポートを選びます |
| `STM32CUBE_N6_DIR` | `Drivers/`を含むSTM32CubeN6パッケージのルート |
| `STEDGEAI_LIB_DIR` | `Inc/`、`Npu/`、`Lib/`を含む`Middlewares/ST/AI`ディレクトリ |
| `STEDGEAI_BIN` | `stedgeai`実行ファイルのあるディレクトリ。`/opt/ST/STEdgeAI/4.0`への標準インストールは自動検出されます |
| `CUBEMX_EXECUTABLE` | STM32CubeMXの起動スクリプト |
| `UART_DEVICE`、`UART_BAUD` | UARTモニタのデバイスと速度。複数の仮想COMポートがある場合は`/dev/ttyACM0`などを明示します |

コマンドラインでの指定（`make UART_DEVICE=/dev/ttyACM1 monitor`）は`local.mk`より優先されます。

## ビルドと実行

リポジトリルートから、アプリのMakefileを指定して操作します。

```sh
make -C userspace/ai-app setup
make -C userspace/ai-app build
```

`setup`は次を順に行います。

1. `ai-models`: 3モデルをSTの公式リポジトリから取得し、`stedgeai`でNPU向けコードを生成します。ダウンロードできない環境では、モデルを`userspace/ai-app/models/source/<model>/`に置いてから実行してください（[models/README.md](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/models/README.md)）。
2. `cubemx-generate`: IOCからHAL初期化コードを`<build>/cubemx`へ生成します。
3. `configure`: CMakeを構成し、`compile_commands.json`を出力します。

`build`では、コンパイルの前にメモリ配置を解決してヘッダとリンカスクリプトを生成します。生成物はすべてGit管理外で、ビルド先は`build-ai-app-person/`です。

### UARTを開いてから実行する

UARTモニタを先に起動し、確認が終わるまで開いたままにします。

```sh
make -C userspace/ai-app monitor
```

別の端末で、モデルを外部NORへ書き込んでからアプリをRAMで実行します。

```sh
make -C userspace/ai-app ai-load   # 初回とモデルを変えたとき
make -C userspace/ai-app ram-run
```

| ターゲット | 動作 |
| --- | --- |
| `ai-load` | 3モデルの重みとcommand blobを外部NORへ書き込みます。電源を切っても残ります |
| `ram-run` | ビルドしたアプリを`0x34000400`へ転送して実行します。リセット後は再実行が必要です |
| `ai-run` | `ai-load`と`ram-run`を続けて実行します |

### 起動確認

UARTにμT-Kernelとアプリの起動ログが出て、次のような行が含まれていれば動いています。

```text
camera: pipe1=started pipe2=started
ai: model registered=person
ai: model registered=face
ai: model registered=segmentation
ai: 3-model pipeline enabled (person/face/segmentation)
```

`camera: pipe1=started pipe2=started`がカメラの2系統が動き出した合図です。その後、LCDにカメラ映像と検出結果が表示され、1秒ごとの`ai: model stats`で各モデルの推論完了数が増えていくことを確認します。

### 性能を調べる

実行中のボードからST-LINKで記録を読み出し、図にします。PSRAMは揮発性なので、リセット前に取得してください。

```sh
make -C userspace/ai-app thread-monitor      # AI model monitor
make -C userspace/ai-app cpu-task-monitor    # CPU task monitor
```

出力は`build-ai-app-person/`に置かれます。ツールの詳細は[host_app/README.md](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md)を参照してください。

## 開発サイクル

| 変更した内容 | 実行するターゲット |
| --- | --- |
| C/C++ソース | `build`、`ram-run` |
| CubeMXのIOC | `setup`から |
| モデル（取得元、変換オプション） | `setup`、`ai-load`、`ram-run` |
| メモリ配置のJSON | `build`（配置の解決はビルドに含まれます）、`ram-run` |

利用できるターゲットは`make -C userspace/ai-app help`で確認できます。各ターゲットの詳細は[ビルド構成](kernel/build.md)にあります。

## 外部Flashから起動する

FSBL、署名済みアプリ、モデルを外部NORへ書き込み、電源投入時にFSBLからアプリを起動します。`STM32_SigningTool_CLI`がPATHにない場合は`local.mk`の`STM32_SIGNING_TOOL_CLI`で指定します。

```sh
make -C userspace/ai-app program
```

書き込み後に各領域を検証します。BOOTスイッチをFlash起動にしてリセットすると、外部Flashから起動します。FSBLについては[fsbl/README.md](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/fsbl/README.md)を参照してください。

## うまくいかないとき

| 症状 | 確認すること |
| --- | --- |
| `no ST-LINK VCP found` | ボードの接続と、`/dev/ttyACM*`へのアクセス権。複数のST-LINKがある場合は`STM32_PROGRAM_SERIAL`を設定します |
| `STM32_Programmer_CLI`が接続できない | BOOTスイッチが開発モードか。別のプロセス（minicom以外のデバッガなど）がST-LINKを使っていないか |
| `Possible mismatch in ll_aton library used` | `stedgeai`とランタイムのバージョンの不一致。同じSTEdgeAIを指すように`STEDGEAI_BIN`と`STEDGEAI_LIB_DIR`を見直し、`ai-models`を再実行します |
| `camera: no frame for 5000 ms`が繰り返される | カメラモジュールの接続。同じログに出るCSI/DCMIPPのエラー数 |
| CubeMXの生成が止まる | `CUBEMX_EXECUTABLE`がGUIを起動していないか。初回はGUIでライセンスに同意し、STM32CubeN6パッケージを導入してください |
