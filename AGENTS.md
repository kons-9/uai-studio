実装後は、実機へ書き込み、UARTで起動とPipe1/2の開始を確認するところまで実施すること
sandboxの制限を適切に設定し、デバイスが見えるようにすること
UARTの準備をしてから書き込むこと

CubeMXは適切な権限があれば実行できる。

## 確認手順（ai-app）

1. `make -C userspace/ai-app monitor`でUARTを開いたままにする
2. 別の端末で`make -C userspace/ai-app ram-run`を実行する。モデルを変えた場合は先に`ai-load`を実行する
3. UARTに`camera: pipe1=started pipe2=started`が出れば、Pipe1/2の開始を確認できたとする
4. 性能を調べる場合は`thread-monitor`（AI model monitor）と`cpu-task-monitor`を使う

## weak/strongシンボルのリンク確認

- 意図した初期化コードや上書き関数が最終 ELF に入ったかは、コンパイル対象になったことだけで判断しない。最終リンクマップでシンボルの配置元を確認し、`arm-none-eabi-nm`でも最終 ELF のシンボル種別を照合する。
- strong 実装を期待するシンボルは最終 ELF で `T`（またはデータなら `D`/`B`）となり、リンクマップの配置元が意図したオブジェクトであることを確認する。`W`/`w`ならweak実装が選択されているため、期待したstrong実装の参照・リンク方法を調べる。
- weak 定義が選ばれること自体は必ずしも異常ではない。weak/strongどちらが実行されるべきかを呼び出し経路と合わせて確認し、起動処理はベクタテーブル、`Reset_Handler`、エントリ関数まで辿る。
- `OBJECT`ライブラリは通常の静的ライブラリと異なりオブジェクトが最終リンクへ直接渡る。weak/strongの問題を診断するときは、CMake上のターゲット名だけでなく実際のリンク行とマップを確認する。

## experimentの独立性

- 各`userspace/experiment-*`は別のuserspace、`kernel/middleware`、`kernel/driver`の実装ソースやヘッダーに依存させない。必要な実装はそのexperiment内へコピーし、CMake・Makefile・スクリプト・設定ファイルもローカルなコピーを参照する。
- 共有のμT-Kernel/pre-kernelとSTM32CubeのベンダーHAL/BSP/ISPは基盤依存として扱う。experiment固有のdriverやmiddlewareを共有ターゲットからリンクしない。
- 依存監査では、ビルド定義のパスだけでなくPython等のツールimport、CubeMX IOC、ボード設定、リンカスクリプトも確認する。

## monitor
モニターを複数台起動させるとおかしくなる。一度ps aux | grep makeで確認し、killすること
