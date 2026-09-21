# sample2外の変更記録

この実装で新たに変更したファイルは `userspace/sample2/` 以下だけです。
`sample0`、実装中の `sample1`、kernel、root の CMake/Makefile、CubeMX生成物は
変更していません。

## sample2外から読み込むもの

次のものはリポジトリ外の既存資産です。sample2のCMakeからソースを参照しますが、
コピーや改変はしません。

- `STM32CubeN6` の HAL/CMSIS と STM32N6570-DK NOR BSP
- 同BSPのXSPI PSRAMドライバとAPS256XXコンポーネント
- `stedgeai-lib` の Neural-ART ランタイム、NPU cache 実装、静的ライブラリ
- `build/cubemx` にある sample0 用 CubeMX FSBL 出力

生成 command blob が大きくなるため、sample2専用のリンカスクリプト
`stm32n6570-dk-npu-ram.ld` は `userspace/sample2/` 内に追加しています。
sample0の `kernel/pre_kernel/stm32n6570-dk-cubemx-ram.ld` は変更していません。

## CubeMXについて

既存の `kernel/pre_kernel/stm32n6570-dk/cubemx_entry.c` は、sample0 の起動に
必要な HAL、クロック、GPIO、UARTを初期化します。一方、sample2はモデル重みを
読む必要があるため、`sample2/src/main.cpp` から DK公式の `BSP_XSPI_NOR_Init`
と memory-mapped 設定も呼び出します。これにより `cubemx_entry.c` を変更せずに
XSPI2を使えます。

なお、pre-kernelがHALのSysTickを停止した後にµT-KernelのSysTickを使う構成の
ため、sample2は `HAL_GetTick`/`HAL_Delay` のweak実装を同じ `main.cpp` 内で
µT-Kernel時刻へ橋渡しします。これもkernelや生成HALの変更ではありません。

CubeMXのIOCはsample2に複製していません。後でrootのMake経由で `generate` を
使う場合だけ、既存の動作確認済みIOCを明示的に指定してください。

```sh
export CUBEMX_IOC="$PWD/userspace/sample0/config/stm32n6570-dk-fullsecure.ioc"
```

sample2は、既存の `build/cubemx` を使うCMake configureでも動かせます。上書き
を避けるため、作業時は `build-sample2` のような別ビルドディレクトリを使って
ください。

## 実機で追加準備が必要な理由

STM32N6はモデル重みを通常のアプリケーションRAMに置かず、Neural-ARTの生成
コードが指定する外部XSPI2アドレスから読みます。そのため、C/C++をビルドする
だけでは推論できず、選択したモデルの `network_data.hex` を外部Flashへ一度
書き込む必要があります。model1/model2を切り替える場合は、対応するhexも
切り替えてください。

STEdgeAI CLIが未導入、またはモデル生成物が未配置の状態では、sample2のCMakeは
意図的に不足ファイル名を表示して停止します。これは、別モデルの古い生成物を
誤ってリンクすることを防ぐためです。
