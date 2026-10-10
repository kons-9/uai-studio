# 2. カーネルアプリとして登録する

## 目的

`kernel/middleware`と`kernel/driver`をリンクできるようにします。この2つは、アプリが生成するメモリ配置ヘッダ（`memory_config.hpp`など）を含むため、ルートCMakeで「カーネルアプリ」として登録されたアプリのビルドでだけ追加されます。

## 変更するファイル

| ファイル | 変更 |
| --- | --- |
| `CMakeLists.txt`（ルート） | `UAI_KERNEL_APPS`に`mini-ai-app`を加える |
| `project-tools/cmake/stm32_cli.cmake` | （`UAI_KERNEL_APPS`経由で）既定IOCの場所、`STM32_RAM_ENTRY`/`STACK`が決まる |
| `kernel/pre_kernel/stm32n6570-dk/CMakeLists.txt` | （同上）アプリの`config/`をインクルードパスに加え、カメラ/LCDクロックを有効にする |
| `userspace/mini-ai-app/Makefile` | `ENABLE_CPU_TASK_MONITOR := 1` |

ルートの`CMakeLists.txt`で登録します。

```cmake
# Applications built on the kernel middleware and drivers. They generate the
# memory contract that the kernel components include.
set(UAI_KERNEL_APPS ai-app mini-ai-app)
...
if(APP_TARGET IN_LIST UAI_KERNEL_APPS)
    set(UAI_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
    add_subdirectory(kernel/middleware)
    add_subdirectory(kernel/driver)
endif()
```

`UAI_KERNEL_APPS`に入ると、次が自動で切り替わります。

| 場所 | 内容 |
| --- | --- |
| `stm32_cli.cmake` | 既定IOCが`userspace/<app>/config/stm32n6570-dk-<app>.ioc`になる。`STM32_RAM_ENTRY=0x34060001`（ai-appは`0x34062001`）、`STM32_RAM_STACK=0x34100000` |
| `pre_kernel` | `userspace/<app>/config`をインクルードパスに加える。`UAI_CAMERA_LCD_CLOCKS`でカメラ・LCDのクロックとRIF設定を`main()`で行う |

`src/hal_time.c`は削除します。`HAL_GetTick`/`HAL_Delay`は`uai::driver_overrides`（`kernel/driver/board/hal_time.c`）が提供します。`uai_systick_count`だけはアプリに残るので、`main.cpp`で定義します。

```cpp
/* Incremented by the µT-Kernel SysTick handler; the kernel declares it extern. */
extern "C" {
volatile std::uint32_t uai_systick_count = 0U;
}
```

## なぜそうするか

- ミドルウェアは`memory_config.hpp`（バッファ数、モデル出力サイズ）に依存し、ドライバは`static_memory_layout`（PSRAM上のアドレス）に依存します。どちらもアプリの設定から生成されるので、アプリなしにはビルドできません。そのため「どのアプリの生成物を使うか」をルートで決めています。
- `STM32_RAM_ENTRY`は`uai_ram_entry`の配置アドレスです。章3で入れるリンカスクリプト雛形は`.text.uai_ram_entry`を固定配置し、ai-appは`.rodata`の増加に合わせて`0x34062000`を使います。
- `UAI_CAMERA_LCD_CLOCKS`はCubeMXの`main()`にカメラ・LCDのクロック有効化とDCMIPP/LTDCのRIF設定を追加します（[起動の流れ](../kernel/boot.md)）。

## 確認

この時点ではまだ`config/*.json`がないので、`make configure`はメモリ配置の生成で止まります。章3まで進めてから確認します。
