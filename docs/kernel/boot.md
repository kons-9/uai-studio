# 起動の流れ

STM32N6には内蔵Flashがないため、開発中はアプリをSRAMに直接ロードして実行します。ここではRAM実行（`ram-run`）の流れを説明します。

```text
STM32_Programmer_CLI        <app>.bin を 0x34000400 へ書き、MSP/PC を設定して再開
  -> uai_ram_entry           ram_entry.S。割り込み禁止、スタック設定
  -> uai_prepare_ram_launch  VTOR を 0x34000400 に。前回イメージのキャッシュを無効化
  -> Reset_Handler           CubeMX生成。Cランタイムの初期化
  -> main()                  cubemx_entry.c。HAL、クロック、周辺の初期化
  -> knl_start_mtkernel()    μT-Kernel 起動
  -> usermain()              アプリ
```

1. `ram-run`が`STM32_Programmer_CLI`で`<app>.bin`を`STM32_RAM_ADDRESS`（`0x34000400`）へ書き、`MSP=STM32_RAM_STACK`、`PC=STM32_RAM_ENTRY`でCPUを再開します。
2. `uai_ram_entry`（`ram_entry.S`）が割り込みを止めてスタックを設定し、`uai_prepare_ram_launch()`でVTORを`0x34000400`にして、前回のイメージから残ったキャッシュを無効化します。
3. CubeMXの`Reset_Handler`がCランタイムを初期化し、`cubemx_entry.c`の`main()`を呼びます。
4. `main()`はHAL、クロック、GPIO、CACHEAXI、RAMCFG、USART1、XSPI1/2、RIFを初期化し、`knl_start_mtkernel()`でμT-Kernelを起動します。カメラを使うアプリではカメラ用クロックとDCMIPP/LTDCのRIF設定も行います。
5. μT-Kernelの初期タスクがアプリの`usermain()`を呼びます。

## RAM配置の設定

`STM32_RAM_ENTRY`と`STM32_RAM_STACK`の既定値はアプリごとに`project-tools/host-config/local.mk.example`で決まります。

| 変数 | 内容 | ai-appの値 |
| --- | --- | --- |
| `STM32_RAM_ADDRESS` | `.bin`を書き込む先頭アドレス | `0x34000400` |
| `STM32_RAM_ENTRY` | 再開時のPC。`uai_ram_entry`のアドレス（Thumbのため最下位ビットが1） | ai-app: `0x34062001` |
| `STM32_RAM_STACK` | 再開時のMSP | `0x34100000` |

独自のリンカスクリプトを使う場合は、`uai_ram_entry`の配置に合わせてこれらを変更してください。値はELFの`arm-none-eabi-nm <app>.elf | grep uai_ram_entry`で確認できます。

## 外部Flashからの起動

`program`で書き込んだ場合は、ブートROMが外部NORの`0x70000000`にあるFSBLを読み込み、FSBLが`0x70100000`の署名済みアプリをSRAMへ展開して起動します。以降の流れはRAM実行と同じです。FSBLはSTのサンプルのバイナリをそのまま使っています（[fsbl/README.md](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/fsbl/README.md)）。

## 外部メモリとRIF

`main()`はRIF（Resource Isolation Framework）の基本設定（`SystemIsolation_Config()`）と、カメラを使うアプリではDCMIPP/LTDCのマスター属性の設定を行います。ただしコールドブート直後のXSPI1/XSPI2はまだ保護されたままなので、アプリの初期化タスクが`rif::RifManagement`でアクセス権を設定し、PSRAMとNORのドライバーを初期化して初めて、`0x90000000`（PSRAM）と`0x70000000`（NOR）がメモリマップされます。この順序は[ドライバーの初期化の順序](../driver.md)にまとめています。
