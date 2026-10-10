# 1. 最小アプリ

## 目的

`usermain()`だけのアプリをビルドして実機で動かし、ツールチェーン、CubeMX生成、`ram-run`、UARTの経路がつながっていることを確かめます。ここで作るファイル構成は、以降の章でそのまま育てていきます。

## 追加するファイル

```text
userspace/mini-ai-app/
  Makefile
  CMakeLists.txt
  config/stm32n6570-dk-mini-ai-app.ioc
  src/main.cpp
  src/hal_time.c          （この章だけ。章2でドライバ側の実装に置き換わる）
```

### Makefile

アプリの`Makefile`は変数を決めて`common.mk`を読み込むだけです。この章ではAIとモニタを無効にしておきます。

```make
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/mini-ai-app
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= mini-ai-app
BUILD_DIR ?= $(PROJECT_ROOT)/build-mini-ai-app
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-mini-ai-app.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0
CUBEMX_GENERATOR := script

include $(PROJECT_ROOT)/project-tools/make/common.mk
```

### IOC

CubeMXの設定はai-appのものをコピーし、プロジェクト名だけ変えます。ai-appのIOCにはカメラ（CSI、I2C1）、LCD（LTDC）、外部メモリ（XSPI1/2）、NPU用のクロックが入っているので、以降の章で変更せずに済みます。

```sh
sed 's/stm32n6570-dk-ai-app/stm32n6570-dk-mini-ai-app/g' \
    userspace/ai-app/config/stm32n6570-dk-ai-app.ioc \
    > userspace/mini-ai-app/config/stm32n6570-dk-mini-ai-app.ioc
```

### CMakeLists.txt（この章の版）

experiment-hello-worldと同じです。`uai::utkernel`（μT-Kernel）と`uai::stm32n6570_dk`（CubeMX生成コードとRAM起動）をリンクし、`objcopy`で`.bin`を作ります。

```cmake
cmake_minimum_required(VERSION 3.16)
add_executable(mini-ai-app.elf src/main.cpp src/hal_time.c)
target_link_libraries(mini-ai-app.elf PRIVATE uai::utkernel uai::stm32n6570_dk)
target_link_options(mini-ai-app.elf PRIVATE
    "-T${UAI_STM32N6570_DK_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/mini-ai-app.map")
set_target_properties(mini-ai-app.elf PROPERTIES
    LINK_DEPENDS "${UAI_STM32N6570_DK_LINKER_SCRIPT}")
add_custom_command(TARGET mini-ai-app.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:mini-ai-app.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/mini-ai-app.bin"
    VERBATIM)
add_custom_target(mini-ai-app DEPENDS mini-ai-app.elf)
```

### src/main.cpp と src/hal_time.c

μT-Kernelは初期タスクからC ABIの`usermain`を呼びます。HALの`HAL_GetTick()`はμT-Kernelの時計で置き換えます（SysTickはカーネルが使うため）。

```cpp
#include <tk/tkernel.h>
extern "C" {
#include <tm/tmonitor.h>
}

extern "C" INT usermain(void)
{
    tm_putstring((UB *)"hello from mini-ai-app\n");
    for (;;) tk_dly_tsk(1000);
}
```

```c
#include <stdint.h>
#include <tk/tkernel.h>

volatile UW uai_systick_count;   /* カーネルのSysTickハンドラが加算する */

uint32_t HAL_GetTick(void)
{
    SYSTIM time = {0};
    return tk_get_tim(&time) == E_OK ? time.lo : 0U;
}

void HAL_Delay(uint32_t delay_ms)
{
    if (delay_ms != 0U) (void)tk_dly_tsk(delay_ms);
}
```

## なぜそうするか

- ルートの`CMakeLists.txt`は`APP_TARGET`で`userspace/<app>/CMakeLists.txt`を読み込むだけなので、アプリ追加にカーネル側の変更は要りません（[アプリの追加](../kernel/new-app.md)）。
- `uai_systick_count`はμT-Kernelの移植層が`extern`参照している唯一のアプリ側シンボルです。章2以降では`main.cpp`で定義します。
- 起動の流れ（`ram-run`→`uai_ram_entry`→`Reset_Handler`→`main()`→`knl_start_mtkernel()`→`usermain()`）は[起動の流れ](../kernel/boot.md)にあります。

## 確認

```sh
make -C userspace/mini-ai-app setup      # CubeMX生成とCMake構成
make -C userspace/mini-ai-app monitor    # 別端末
make -C userspace/mini-ai-app ram-run
```

UARTに`hello from mini-ai-app`が出れば完了です。この章では`STM32_RAM_ENTRY`/`STM32_RAM_STACK`の既定値（`0x34000800`/`0x34200000`）が使われます。章2でカーネルアプリとして登録すると、mini-ai-appは`0x34060001`/`0x34100000`、ai-appは`0x34062001`/`0x34100000`に切り替わります。

!!! note "リポジトリ上の完成形との関係"
    リポジトリの`mini-ai-app`は章2の登録（`UAI_KERNEL_APPS`）が済んでいるため、この章の状態のまま`mini-ai-app`という名前でビルドするとミドルウェアの生成物を要求されます。この章だけを試すなら、同じ内容の[experiment-hello-world](https://github.com/kons-9/uai-studio/tree/main/userspace/experiment-hello-world)を`make -C userspace/experiment-hello-world ram-run`で動かすか、別名のディレクトリで作業してください。
