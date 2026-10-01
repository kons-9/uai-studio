# μAI-Studio: ai-app

STM32N6570-DKのカメラ映像をLCDへ表示しながら、Neural-ART NPUでperson、face、segmentationの3モデルを推論するµT-Kernel 3.0アプリです。

- カメラのPipe1でLCD表示用フレーム（RGB565 800x480）を取得します。
- Pipe2でNPU推論用フレーム（RGB888 480x480）を取得します。
- 前処理CPU、NPU、後処理CPUの3タスクでモデルをパイプライン実行し、検出結果をLCDに重ねて表示します。
- アプリ本体はRAMへロードして実行します。モデルの重みとcommand blobは外部NOR Flashに置きます。

アプリの内部構成は[userspace/ai-app/README.md](userspace/ai-app/README.md)、ハードウェアの前提とツールの取得は[docs/getting-started.md](docs/getting-started.md)、カーネル・ドライバー・ミドルウェアの使い方は[docs/index.md](docs/index.md)を参照してください。

## 必要な機材とソフトウェア

### 機材

- STM32N6570-DK
- ST-LINK USB接続
- カメラモジュールとLCD
- UARTログを見るためのLinuxホスト

書き込み手順はボードを直接認識するネイティブLinuxを前提にしています。

### Linuxパッケージ

Ubuntu系Linuxでは次をインストールします。

```sh
sudo apt update
sudo apt install build-essential cmake git python3 minicom \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
```

### STの開発ツール

| ツール | 用途 |
| --- | --- |
| STM32CubeMX 6.x | IOCからHAL・BSP・FSBL用ソースを生成します。CubeMX2は対象外です。 |
| STM32CubeN6 Firmware Package | HAL、CMSIS、BSPを提供します。 |
| STEdgeAI 4.0 | `stedgeai`CLIとNeural-ARTランタイムを提供します。ランタイムはリポジトリに含まれません。 |
| STM32CubeProgrammer | `STM32_Programmer_CLI`とSTM32N6570-DK用External Loaderを使います。Flash起動には`STM32_SigningTool_CLI`も使います。 |

モデル生成に使ったSTEdgeAIと、リンクするランタイムの`ll_aton`バージョンは一致させてください。不一致はCMake configure時にエラーになります。

## 初回セットアップ

リポジトリをサブモジュール込みで取得します。すでにclone済みなら、サブモジュールを初期化します。

```sh
git clone --recurse-submodules https://github.com/kons-9/uai-studio.git
cd uai-studio

# 既存checkoutの場合はこちら
git submodule update --init --recursive
```

ホスト固有設定ファイルを用意します。すでに`build-system/host-config/local.mk`がある場合はそのまま編集してください。

```sh
test -f build-system/host-config/local.mk || cp build-system/host-config/local.mk.example build-system/host-config/local.mk
```

`build-system/host-config/local.mk`で、インストール先と接続するボードに合わせて設定します。

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

`STM32_PROGRAMMER_ROOT`はSTM32CubeProgrammerの`tools`ディレクトリを指定します。`STM32_PROGRAM_SERIAL`を設定すると、書き込み先を特定し、`UART_DEVICE = auto`が同じST-LINKの仮想COMポートを選びます。シリアル番号を使わない場合や複数の仮想COMポートがある場合は、`UART_DEVICE`に`/dev/ttyACM0`などを明示してください。UARTは115200 bps、8N1、フロー制御なしです。

`STM32CUBE_N6_DIR`は`Drivers/`を含むSTM32CubeN6パッケージのルートです。`STEDGEAI_LIB_DIR`は`Inc/`、`Npu/`、`Lib/`を含む`Middlewares/ST/AI`ディレクトリです。`STEDGEAI_BIN`は`stedgeai`実行ファイルがあるディレクトリです。一般的な`/opt/ST/STEdgeAI/4.0`へのインストールは自動検出されます。

## ディレクトリの役割

| パス | 内容 |
| --- | --- |
| `kernel/utkernel` | µT-Kernel 3.0 BSP2（サブモジュール） |
| `kernel/pre_kernel` | CubeMX生成コードとµT-Kernel起動をつなぐボード依存コード |
| `kernel/driver` | カメラ、LCD、NPU、PSRAM、NORなどのドライバー |
| `kernel/middleware` | AIランタイム、メモリ管理、モニターなどの共通処理 |
| `userspace/ai-app` | 本アプリ |
| `userspace/experiment-*` | ドライバーなどを実装する際に使った実験用ディレクトリ |
| `build-system` | CMake・Makeの共通定義、CubeMX生成とUARTのスクリプト、ホスト設定 |
| `host_app` | PCで動かすトレース解析とメモリ配置生成ツール（[host_app/README.md](host_app/README.md)） |
| `docs` | 開発ガイド（ハードウェアの前提とツールの取得、カーネル、ドライバー、ミドルウェア） |

## セットアップ、ビルド、RAM実行

リポジトリルートから、ai-appのMakefileを指定して操作します。

```sh
make -C userspace/ai-app setup
make -C userspace/ai-app build
```

`setup`は依存パスを確認し、3モデルを取得してNeural-ARTコードを生成し、CubeMXソースを生成してCMakeを構成します。生成物はGit管理外で、ビルド先は`build-ai-app-person/`です。

モデルはSTMicroelectronicsの公式リポジトリから自動で取得します。ダウンロードできない環境では、モデルを`userspace/ai-app/models/source/<model>/`へ置いてから`setup`を実行してください。詳細は[userspace/ai-app/models/README.md](userspace/ai-app/models/README.md)にあります。

### UARTを起動してからRAMへロード

UARTモニタを先に起動し、実行確認が終わるまで開いたままにします。

```sh
make -C userspace/ai-app monitor
```

別の端末で、モデルデータを外部NORへ書き込んでからアプリをRAMで実行します。

```sh
make -C userspace/ai-app ai-load
make -C userspace/ai-app ram-run
```

| ターゲット | 動作 |
| --- | --- |
| `ai-load` | 3モデルの重みとcommand blobを外部NORへ書き込みます。電源を切っても残るため、モデルを変えない限り再実行は不要です。 |
| `ram-run` | ビルドしたアプリを`0x34000400`へ転送して実行します。リセット後は再実行が必要です。 |
| `ai-run` | `ai-load`と`ram-run`を続けて実行します。 |

### 起動確認

UARTにµT-Kernelとアプリの起動ログが出て、次のような行が続くことを確認します。

```text
ai: model registered=person
ai: model registered=face
ai: model registered=segmentation
ai: 3-model pipeline enabled (person/face/segmentation)
```

その後、LCDにカメラ映像と検出結果が表示され、1秒ごとの`ai: model stats`で`capture`、`pipe2`、各モデルの推論完了数が増えていくことを確認します。

`camera: no frame for 5000 ms`が繰り返され、`vsync=0`と`pipe2=0`のままなら、カメラからフレームが届いていません。カメラモジュールの接続と、同じログに出るCSI/DCMIPPエラーを確認してください。

## 以降の開発サイクル

ソースを変更したら`build`と`ram-run`を繰り返します。IOCやモデルを変更した場合は`setup`から、モデルを再生成した場合は`ai-load`も実行してください。

```sh
make -C userspace/ai-app build
make -C userspace/ai-app ram-run
```

利用できるターゲットは`make -C userspace/ai-app help`で確認できます。

## 外部Flashから起動する場合

FSBL、署名済みアプリ、モデル重み、command blobを外部NORへ書き込み、リセット後にFSBLからアプリを起動します。`STM32_SigningTool_CLI`がPATHにない場合は`local.mk`の`STM32_SIGNING_TOOL_CLI`で指定します。

ボードのBOOT0とBOOT1をLOWにし、UARTモニタを起動してから次を実行します。

```sh
make -C userspace/ai-app program
```

`program`（別名`flash`）は書き込み後に各領域を検証します。完了後にリセットすると外部Flashから起動します。FSBLについては[userspace/ai-app/fsbl/README.md](userspace/ai-app/fsbl/README.md)を参照してください。

## ドキュメントサイト

`docs/`と紹介スライド（`introduction.md`）は、`main`へのpush時にGitHub Actionsが[Zensical](https://zensical.org/)とMarpでビルドし、[GitHub Pages](https://kons-9.github.io/uai-studio/)へ公開します。設定は`mkdocs.yml`で、MkDocs（Material for MkDocs）でもビルドできます。

ローカルで確認する場合:

```sh
uvx zensical serve
```

## ライセンス

- [ライセンス（MIT）](LICENSE)
- [利用している既存ソフトウェアとμT-Kernelへの変更](THIRD_PARTY_NOTICES.md)
