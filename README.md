# μAI-Studio: ai-app

STM32N6570-DKのカメラ映像をLCDへ表示しながら、Neural-ART NPUで推論するµT-Kernel 3.0アプリです。このREADMEでは、クリーンなLinux環境の準備からビルド、RAM実行までを説明します。

- カメラのPipe1でLCD表示用フレームを取得します。
- Pipe2でNPU推論用のフレームを取得します。
- person、face、segmentationの3モデルを使います。
- アプリ本体はRAMへロードして起動します。モデルの重みとcommand blobは外部NOR Flashに置きます。

## 必要な機材とソフトウェア

### 機材

- STM32N6570-DK
- ST-LINK USB接続
- カメラモジュールとLCD
- UARTログを見るためのLinuxホスト

本プロジェクトの書き込み手順はネイティブLinuxを前提にしています。USBパススルーを使うWSL環境ではなく、ボードを直接認識するLinuxで実行してください。

### Linuxパッケージ

Ubuntu系Linuxでは次をインストールします。

```sh
sudo apt update
sudo apt install build-essential cmake git python3 minicom \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
```

### STの開発ツール

次のツールを別途インストールします。

1. **STM32CubeMX 6.x** — `userspace/ai-app/config/stm32n6570-dk-ai-app.ioc`からHAL・BSP・FSBL用ソースを生成します。STM32N6用の従来版CubeMXを使ってください。CubeMX2は対象外です。
2. **STM32CubeN6 Firmware Package** — N6のHAL、CMSIS、BSPヘッダーとソースを提供します。ai-appは`STM32CUBE_N6_DIR`に指定したパッケージを参照します。
3. **STEdgeAI 4.0** — `stedgeai`モデル生成CLIとNeural-ARTランタイムを提供します。モデル生成コードとランタイムの`ll_aton`バージョンを一致させてください。ランタイムのライブラリはリポジトリに含まれません。
4. **STM32CubeProgrammer** — RAMロード用の`STM32_Programmer_CLI`を使います。外部NORへ書く場合はSTM32N6570-DK用External Loaderも必要です。永続Flash起動用イメージを作る場合は`STM32_SigningTool_CLI`も必要です。

標準的なインストール先以外に配置した場合は、次節の`config/local.mk`に実際のパスを設定します。

## 初回セットアップ

リポジトリをサブモジュール込みで取得します。すでにclone済みなら、サブモジュールを初期化します。

```sh
git clone --recurse-submodules https://github.com/kons-9/uai-studio.git
cd uai-studio

# 既存checkoutの場合はこちら
git submodule update --init --recursive
```

ホスト固有設定ファイルを用意します。すでに`config/local.mk`がある場合はそのまま編集してください。

```sh
test -f config/local.mk || cp config/local.mk.example config/local.mk
```

`config/local.mk`で、インストール先と接続するボードに合わせて設定します。

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

## セットアップ、ビルド、RAM実行

リポジトリルートから、ai-appのMakefileを指定して操作します。

```sh
make -C userspace/ai-app setup
make -C userspace/ai-app build
```

初回の`setup`は、依存パスを確認し、person・face・segmentationのモデルデータを取得してNeural-ARTコードを生成し、CubeMXソースを生成してCMakeを構成します。生成物とビルド結果はローカルに作られ、Gitには登録されません。初回はモデル取得とCubeMX生成に時間がかかります。

モデルファイルはSTMicroelectronicsの各公式モデルリポジトリから取得します。ホストからダウンロードできない場合は、モデルを`userspace/ai-app/models/source/<model>/`へ置いてから`setup`を実行するか、[モデルの説明](userspace/ai-app/models/README.md)にある手動生成手順を使ってください。モデルの配布条件も同ページを確認してください。

### UARTを起動してからRAMへロード

まずUARTモニタを起動します。UARTを開いた端末は実行確認が終わるまで開いたままにします。

```sh
make -C userspace/ai-app monitor
```

別の端末でアプリをRAMへロードし、実行します。

```sh
make -C userspace/ai-app ram-run
```

`ram-run`はアプリのビルド後、STM32CubeProgrammer CLIでバイナリをRAMアドレス`0x34000400`へ転送し、指定されたスタックと実行アドレスでCPUを再開します。これはアプリ本体を外部NORへ保存する操作ではありません。リセットや電源断後は、再度`ram-run`が必要です。

ai-appはモデルの重みとcommand blobを外部NORから読みます。ボードに今回のビルドと一致するモデルデータがすでにある場合は、そのまま`ram-run`できます。モデルデータがない、または更新が必要な場合は、UARTを先に起動した状態で次を実行してから`ram-run`してください。

```sh
make -C userspace/ai-app ai-load
make -C userspace/ai-app ram-run
```

`ai-load`は外部NORのモデル領域へ重みとcommand blobを書き込みます。アプリ本体やFSBLは書き込みません。モデルデータは電源を切っても残ります。`ai-run`は`ai-load`の後にRAM実行する短縮コマンドです。

### 起動確認

UARTにµT-Kernelとアプリの起動ログが出て、次のような行が続くことを確認します。

```text
ai: model registered=person
ai: model registered=face
ai: model registered=segmentation
ai: 3-model pipeline enabled (person/face/segmentation)
```

その後、LCDにカメラ映像が表示され、UARTのモデル統計で`capture`と`pipe2`のフレーム数、各モデルの推論完了数が増えていくことを確認します。ログの表示間隔や詳細度は診断設定によって変わります。LCDの表示確認も行ってください。

`camera: no frame for 5000 ms`が繰り返され、`vsync=0`と`pipe2=0`のままなら、カメラからのフレーム取得が始まっていません。カメラモジュールと接続を確認し、同じログに出るCSI/DCMIPPエラーを切り分けてください。モデルの`ai-load`はカメラ入力の問題を解決しません。

## 以降の開発サイクル

ソースを変更した後は、ビルドとRAM実行を繰り返します。設定やCubeMXのIOCを変更した場合は`setup`も再実行してください。

```sh
make -C userspace/ai-app build
make -C userspace/ai-app ram-run
```

ホスト固有の設定を変えずに使うコマンドは、`make -C userspace/ai-app help`で確認できます。

## おまけ: 外部Flashから起動する場合

RAM実行ではアプリ本体を揮発性RAMへ転送します。Flash起動では、FSBL、署名済みアプリ、モデル重み、command blobを外部NORへ保存し、ボードのリセット後にFSBLからアプリを起動します。アプリのRAM転送後も外部NORをモデルデータ置き場として使う点は共通です。

Flash起動には、STM32CubeProgrammer CLI、STM32N6570-DK用External Loader、`STM32_SigningTool_CLI`が必要です。Signing ToolがPATHにない場合は`config/local.mk`の`STM32_SIGNING_TOOL_CLI`で実行ファイルを指定してください。ボードのBOOT0とBOOT1をLOWにします。

UARTモニタを別端末で先に起動したうえで、次を実行します。

```sh
make -C userspace/ai-app program
```

`program`（同義の`flash`ターゲットもあります）は署名済みLRUNアプリイメージを生成し、External Loader経由でFSBL、アプリ、3モデル分の重みとcommand blobを外部NORへ書き込み、各書き込みを検証します。書き込み完了後にボードをリセットすると、外部Flashから起動します。

Flashからの自動起動が不要で、RAM実行用のモデルデータだけを書き換える場合は`program`ではなく`ai-load`を使います。逆に、Flash起動に必要な一式を更新する場合は`ai-load`だけでは足りません。

## 関連ファイル

- [ホスト設定テンプレート](config/local.mk.example)
- [ai-appのモデルと生成手順](userspace/ai-app/models/README.md)
- [ai-appのCubeMX設定](userspace/ai-app/config/stm32n6570-dk-ai-app.ioc)
- [Flash起動用FSBLについて](userspace/ai-app/fsbl/README.md)
