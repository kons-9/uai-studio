# experiment-hello-world

STM32N6570-DK上でµT-Kernel 3.0を起動し、T-MonitorのUSART1へhello worldを
出力する最小サンプルです。

必要なツール、ホスト設定、UART端末、RAMロードの手順は、リポジトリルートの
[README](../../README.md)を参照してください。
CubeMXの入力設定は[`config/stm32n6570-dk-fullsecure.ioc`](config/stm32n6570-dk-fullsecure.ioc)です。

リポジトリルートからの基本的な実行手順は次のとおりです。

```sh
cp config/local.mk.example config/local.mk
# config/local.mkにホスト固有の値を設定
make -C userspace/experiment-hello-world generate
make -C userspace/experiment-hello-world build
make -C userspace/experiment-hello-world attach
make -C userspace/experiment-hello-world ram-run
```
