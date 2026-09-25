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

ホスト固有の設定は、サンプルから作成します。

```sh
cp config/local.mk.example config/local.mk
```

`config/local.mk`には、少なくとも次を設定します。

- `STM32_PROGRAMMER_ROOT`: CubeProgrammer CLIの`tools`ディレクトリ
- `STM32_PROGRAM_SERIAL`: 対象ST-LINKのシリアル番号
- `UART_DEVICE`: ボードの仮想COMポート

`config/local.mk`はホスト固有のファイルなので、コミットしません。

MakefileはCMakeを呼び出す入口です。CubeMX/CubeProgrammerの設定、ツール検出、
引数検証、実行コマンドはCMake側で管理します。

## ビルドとRAM実行

```sh
make generate       # 初回またはIOC変更後
make build
make attach
```

端末を先に開きます。`UART_DEVICE`と`UART_BAUD`は`config/local.mk`で変更できます。
デフォルト設定は115200 bps、8N1、フロー制御なしです。

```sh
make monitor
```

別の端末からRAMへロードして実行します。

```sh
make ram-run
```

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
make generate
```

生成物は`build/cubemx/FSBL`、`build/cubemx/Drivers`、
`build/cubemx/Middlewares`などに出力されます。
これらは生成物としてGit管理しないため、初回checkout後は`make generate`が必要です。
生成後は、生成FSBLのSecure `SystemInit`、クロック、GPIO、USART1初期化、startupを
プロジェクト側の`kernel/pre_kernel/stm32n6570-dk`がリンクし、同ディレクトリの
board adapterからµT-Kernelを起動します。生成ファイル自体は編集しません。

生成後のビルドとRAM実行は次のとおりです。

```sh
make build
make attach
make ram-run
```

## 関連ファイル

- [`config/local.mk.example`](config/local.mk.example): ホスト固有設定のテンプレート
- [`userspace/sample-hello-world/config/stm32n6570-dk-fullsecure.ioc`](userspace/sample-hello-world/config/stm32n6570-dk-fullsecure.ioc): `sample-hello-world`のSTM32N657向けCubeMX設定
- [`kernel/pre_kernel/stm32n6570-dk/`](kernel/pre_kernel/stm32n6570-dk/): µT-Kernel起動前のCubeMX/HAL初期化と接続
- [`userspace/sample-hello-world/`](userspace/sample-hello-world/): RAM実行用サンプル
