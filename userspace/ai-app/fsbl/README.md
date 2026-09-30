# STM32N6570-DK FSBL

`stm32n6570-dk-ai_fsbl.hex`は外部Flash起動に使うFSBLイメージです。STM32N6570-DK公式サンプル（`ref/STM32N6_Survivor_Detection/Binaries/ai_fsbl.hex`）から取り込んだもので、`make -C userspace/ai-app program`が書き込みます。

| 項目 | 値 |
| --- | --- |
| FSBL | `0x70000000` |
| LRUNアプリ | `0x70100000` |
| BOOT0、BOOT1 | LOW |

アプリとモデルのイメージはビルド時に生成されます。このディレクトリはFSBLだけを管理します。
