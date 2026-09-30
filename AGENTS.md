実装後は、実機へ書き込み、UARTで起動とPipe1/2の開始を確認するところまで実施すること
sandboxの制限を適切に設定し、デバイスが見えるようにすること
UARTの準備をしてから書き込むこと

CubeMXは適切な権限があれば実行できる。

## 確認手順（ai-app）

1. `make -C userspace/ai-app monitor`でUARTを開いたままにする
2. 別の端末で`make -C userspace/ai-app ram-run`を実行する。モデルを変えた場合は先に`ai-load`を実行する
3. UARTに`camera: pipe1=started pipe2=started`が出れば、Pipe1/2の開始を確認できたとする
4. 性能を調べる場合は`thread-monitor`（AI model monitor）と`cpu-task-monitor`を使う
