# μAI-Studio

STM32N6570-DK上でµT-Kernel 3.0のサンプル群を動かすためのプロジェクトです。
ネイティブLinuxからST-LINK経由でRAMへロードし、T-MonitorのUSART1へ出力します。

## 環境構築

### 必須ツール

ネイティブLinuxに、ビルドツール、ARM GCCツールチェーン、シリアル端末を
インストールします。

```sh
sudo apt install build-essential cmake git minicom \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi
```

別途、STM32CubeProgrammer CLIをインストールしてください。STM32CubeIDEに
同梱されたCLIも使用できます。CLIの`tools`ディレクトリを
`config/local.mk`の`STM32_PROGRAMMER_ROOT`に設定します。

ホスト固有の設定は、テンプレートから作成します。

```sh
cp config/local.mk.example config/local.mk
```

`config/local.mk`には、少なくとも次を設定します。

- `STM32_PROGRAMMER_ROOT`: CubeProgrammer CLIの`tools`ディレクトリ
- `STM32_PROGRAM_SERIAL`: 対象ST-LINKのシリアル番号
- `UART_DEVICE`: ボードの仮想COMポート

`config/local.mk`はホスト固有のファイルなので、コミットしません。

各アプリケーションの`Makefile`がCMakeを呼び出す入口です。ルートにMakefileは置かず、
対象アプリケーションのディレクトリを指定して実行します。CubeMX/CubeProgrammerの設定、
ツール検出、引数検証、実行コマンドはCMake側で管理します。

Makefile内部のパスはMakefile自身の場所から解決するため、カレントディレクトリには
依存しません。リポジトリルートからは`make -C userspace/<application> ...`、別の場所からは
アプリケーションのMakefileを絶対パスで指定します。

```sh
make -f /path/to/uai-studio/userspace/ai-app/Makefile build
```

experiment-aiでは、モデルの取得・生成からAIデータの外部Flash書き込み、RAM実行までを
Makefileから実行できます。

## ビルドとRAM実行

```sh
make -C userspace/experiment-hello-world generate       # 初回またはIOC変更後
make -C userspace/experiment-hello-world build
make -C userspace/experiment-hello-world attach
```

experiment-aiを初めて構築する場合は、次の一連の初期化を実行します。

```sh
make -C userspace/experiment-ai setup          # 依存関係、3モデル取得/生成、CubeMX、CMake
make -C userspace/experiment-ai build
```

`make -C userspace/experiment-ai setup` はCubeMXのコード生成も行うため、CubeMXを起動できるGUI環境で実行してください。
ヘッドレス環境では、CubeMX生成済みの状態で`make -C userspace/experiment-ai ai-deps`、
`make -C userspace/experiment-ai ai-models`、`make -C userspace/experiment-ai configure`、
`make -C userspace/experiment-ai build`を個別に実行できます。

端末を先に開きます。`UART_DEVICE`と`UART_BAUD`は`config/local.mk`で変更できます。
デフォルト設定は115200 bps、8N1、フロー制御なしです。

```sh
make -C userspace/experiment-ai monitor
```

別の端末からRAMへロードして実行します。

```sh
make -C userspace/experiment-ai ram-run
```

experiment-aiのモデル重みとcommand blobを外部Flashへ書き込む場合は、RAM実行前に
`make -C userspace/experiment-ai ai-load`を実行します。UARTモニタは別端末で先に起動してください。

```sh
make -C userspace/experiment-ai monitor        # 別端末
make -C userspace/experiment-ai ai-load        # AI重み + command blob
make -C userspace/experiment-ai ram-load       # RAMへアプリをロードして実行
```

`make -C userspace/experiment-ai ai-run`は`ai-load`と`ram-load`を連続して実行します。AIモデルの取得だけを
行う場合は`make -C userspace/experiment-ai ai-models`、通常のアプリケーションビルドだけなら
`make -C userspace/experiment-ai build`を使用します。

成功すると、T-Monitorの起動メッセージに続いて次の出力が表示されます。

```text
Hello from uai-studio / STM32N6570-DK
```

RAMロード先や実行開始アドレスは`config/local.mk.example`に定義しています。
RAM実行はネイティブLinux上のSTM32CubeProgrammer CLIで行います。
WSLやUSB/IPは使用しません。

## CubeMXコード生成

このプロジェクトのSTM32N657向け設定は、従来版STM32CubeMX 6.xの`.ioc`形式です。
STM32CubeMX2はSTM32C5などのHAL2系向けであり、N657には使用しません。

コード生成には、ST公式のスタンドアロン版STM32CubeMX 6.x Linux版を
インストールしてください。CubeIDEの`headless-build.sh`やSTM32CubeMX2は、
このSTM32N6向けの生成には使用しません。CubeMXは初回生成時にIOCが指定する
STM32Cube FW_N6を取得します。

`config/local.mk`で`CUBEMX_EXECUTABLE`にCubeMX本体を指定し、実行します。

```make
CUBEMX_EXECUTABLE ?= /path/to/STM32CubeMX
```

```sh
make -C userspace/experiment-hello-world generate
```

生成物は各アプリケーションのビルドディレクトリ（例: `build-experiment-hello-world/cubemx/FSBL`、
`Drivers`、`Middlewares`）に出力されます。
これらは生成物としてGit管理しないため、初回checkout後は対象アプリケーションの
`make -C userspace/<application> generate`が必要です。
生成後は、生成FSBLのSecure `SystemInit`、クロック、GPIO、USART1初期化、startupを
プロジェクト側の`kernel/pre_kernel/stm32n6570-dk`がリンクし、同ディレクトリの
board adapterからµT-Kernelを起動します。生成ファイル自体は編集しません。

生成後のビルドとRAM実行は次のとおりです。

```sh
make -C userspace/experiment-hello-world build
make -C userspace/experiment-hello-world attach
make -C userspace/experiment-hello-world ram-run
```

## 実機の動作状況

2026-09-30にSTM32N6570-DKで`ram-run`し、UART出力を確認した結果です。

### 動作確認済み

- `experiment-hello-world`: `ram-run`後にUARTへ`Hello from uai-studio / STM32N6570-DK`を出力。
- `experiment-camera-lcd`: UARTに`camera_lcd: camera preview started`を出力。
- `experiment-camera-pipe2`: UARTに`camera_pipe2: preview started`を出力し、Pipe1 VSYNCとPipe2 frameのカウンタがともに増加。
- `experiment-ai`: Pipe1/2の開始を出力し、AIモデル推論が完了。UARTの推論統計は約7 completed/s。
- `ai-app`: Pipe1/2の開始を出力し、人物・顔・セグメンテーション推論が完了。Pipe2 frameカウンタも増加。

AIアプリは今回`ram-run`のみで確認し、モデルデータはボード上の既存データを使用しました。AIデータのFlash書き込みは行っていません。

### 動作しない

- なし。以前の`experiment-camera-pipe2`のBusFault版は削除し、Pipe1/2の連続動作を確認した実装へ置き換えました。

## 関連ファイル

- [`config/local.mk.example`](config/local.mk.example): ホスト固有設定のテンプレート
- [`userspace/experiment-hello-world/config/stm32n6570-dk-fullsecure.ioc`](userspace/experiment-hello-world/config/stm32n6570-dk-fullsecure.ioc): `experiment-hello-world`のSTM32N657向けCubeMX設定
- [`kernel/pre_kernel/stm32n6570-dk/`](kernel/pre_kernel/stm32n6570-dk/): µT-Kernel起動前のCubeMX/HAL初期化と接続
- [`userspace/experiment-hello-world/`](userspace/experiment-hello-world/): RAM実行用サンプル
