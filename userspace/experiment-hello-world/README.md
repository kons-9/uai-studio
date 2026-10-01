# experiment-hello-world

µT-Kernel 3.0の起動を確かめるために使った実験用ディレクトリです。STM32N6570-DK上でµT-Kernelを起動し、T-MonitorのUART（USART1）へhello worldを出力します。最小構成なので、新しいアプリを作るときの参考になります。

必要なツールとホスト設定は[docs/getting-started.md](../../docs/getting-started.md)を参照してください。アプリの追加方法は[docs/kernel/new-app.md](../../docs/kernel/new-app.md)にあります。

```sh
make -C userspace/experiment-hello-world generate
make -C userspace/experiment-hello-world build
make -C userspace/experiment-hello-world monitor   # 別端末で先に起動
make -C userspace/experiment-hello-world ram-run
```
