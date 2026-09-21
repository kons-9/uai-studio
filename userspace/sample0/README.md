# sample0

STM32N6570-DK 上で µT-Kernel 3.0 を起動し、T-Monitor UART に hello world
を出力する最小サンプルです。現在は STM32N6 の開発モード向けに Secure
AXI SRAM へリンクします。

```sh
make build
```

生成物は `build/userspace/sample0/sample0.elf` と `sample0.bin` です。

## RAMへロードして実行

STM32N6570-DKの開発モードでは、Flashへ保存するのではなく、ST-LINK経由で
Secure AXI SRAMへロードして実行します。pyOCDとSTM32N6用CMSIS-DAPパックを
プロジェクト外または一時ディレクトリに用意してください。

```sh
./.venv/bin/python -m pip install pyocd
curl -L -o /tmp/Keil.STM32N6xx_DFP.1.2.0.pack \
  https://www.keil.com/pack/Keil.STM32N6xx_DFP.1.2.0.pack

make ram-run PYOCD_PACK=/tmp/Keil.STM32N6xx_DFP.1.2.0.pack
```

複数のデバッグプローブを接続している場合は、`-DPYOCD_PROBE=<UID>`を追加
します。これはデバッガからRAMへロードして実行する開発用経路であり、電源断後
も残るFlash書き込みとは別物です。

## CubeMX / CubeProgrammer CLI

CubeMXの`.ioc`生成スクリプトはリポジトリ外のボード設定を上書きし得るため、
スクリプトの場所を明示して実行します。

```sh
make generate CUBEMX_SCRIPT=/absolute/path/to/generate-project.txt
```

署名と外部Flash書き込みもCMakeターゲットです。STM32N6ではFSBL、アプリ、
外部Flashローダー、セキュリティ設定に応じて引数と配置が変わるため、鍵や
アドレスは固定していません。

```sh
make sign \
  STM32_SIGN_INPUT=/absolute/path/to/input.bin \
  STM32_SIGN_OUTPUT=/absolute/path/to/signed.bin \
  'STM32_SIGNING_ARGS=-nk -of 0x80000000 -t fsbl -hv 2.3 -align'

make program \
  STM32_EXTERNAL_LOADER=/absolute/path/to/MX66UW1G45G_STM32N6570-DK.stldr \
  STM32_PROGRAM_IMAGE=/absolute/path/to/signed.bin \
  STM32_PROGRAM_ADDRESS=0x70000000
```

CLIがPATH上にない場合は、`STM32_PROGRAMMER_CLI=/absolute/path/to/STM32_Programmer_CLI`
のように実ファイルを指定します。`/path/to/...`はプレースホルダーです。

WSLでUSB/IP経由のST-LINKを使う場合は、Windows版ではなくSTM32CubeProgrammerの
Linux版をWSL側にインストールするのが扱いやすいです。STの公式配布ページから
`STM32CubeProg-Linux`を取得し、インストーラーを実行します。

```sh
chmod +x SetupSTM32CubeProgrammer-*.linux
./SetupSTM32CubeProgrammer-*.linux

find /home/toshiki /usr/local -type f -name STM32_Programmer_CLI 2>/dev/null
```

見つかった`bin/STM32_Programmer_CLI`をPATHに追加するか、`make program`の
`STM32_PROGRAMMER_CLI`に指定します。STM32N6570-DK用の外部ローダーは通常、同じ
インストール先の`bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr`です。

`make flash`は`make program`の別名です。現在の`sample0.bin`はRAM用にリンク
されたイメージなので、これを外部Flashへ書き込む設定にはしていません。FSBLと
XIP/外部Flash用の正式なCubeMXプロジェクトを追加した段階で、
`STM32_PROGRAM_IMAGE`にその生成物を指定します。

STM32N6 は内蔵 Flash を持たないため、電源断後も保持する実機運用では
FSBL と外部 Flash/XIP 用のイメージ構成が別途必要です。最初の段階では
Cube で用意した Secure 側の初期化（UART1、TrustZone、クロック）からこの
RAM イメージへ制御を渡す構成を想定しています。
