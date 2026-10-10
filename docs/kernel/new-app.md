# アプリの追加

カーネル側を変更せずに、`userspace/<app>/`を追加するだけで新しいアプリをビルドできます。最小構成の例は実験用の[experiment-hello-world](https://github.com/kons-9/uai-studio/tree/main/userspace/experiment-hello-world)です。

## 最小構成

```text
userspace/<app>/
  Makefile            APP_TARGET と ENABLE_* を決めて common.mk を読み込む
  CMakeLists.txt      <app>.elf を定義し、μT-Kernel とボード設定をリンクする
  config/*.ioc        CubeMX の設定
  src/main.cpp        usermain()
  src/hal_time.c      HAL_GetTick/HAL_Delay を μT-Kernel で置き換える（ドライバーを使わない場合）
```

### 1. usermain()を書く

μT-Kernelの初期タスクから`usermain()`が呼ばれます。

```cpp
#include <tk/tkernel.h>
extern "C" {
#include <tm/tmonitor.h>
}

extern "C" INT usermain(void)
{
    tm_putstring((UB *)"hello\n");
    for (;;) {
        tk_dly_tsk(1000);
    }
}
```

### 2. CMakeLists.txtを書く

`<app>.elf`と`<app>`ターゲットを定義します。`uai::utkernel`と`uai::stm32n6570_dk`をリンクし、リンカスクリプトは`UAI_STM32N6570_DK_LINKER_SCRIPT`を使えます。ビルド後に`objcopy`で`<app>.bin`を作ると`ram-run`で使えます。

```cmake
add_executable(<app>.elf src/main.cpp src/hal_time.c)
target_link_libraries(<app>.elf PRIVATE uai::utkernel uai::stm32n6570_dk)
target_link_options(<app>.elf PRIVATE
    "-T${UAI_STM32N6570_DK_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/<app>.map")
set_target_properties(<app>.elf PROPERTIES
    LINK_DEPENDS "${UAI_STM32N6570_DK_LINKER_SCRIPT}")
add_custom_command(TARGET <app>.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:<app>.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/<app>.bin"
    VERBATIM)
add_custom_target(<app> DEPENDS <app>.elf)
```

ルートの`CMakeLists.txt`の`APP_TARGET`の候補（`set_property(CACHE APP_TARGET PROPERTY STRINGS ...)`）に`<app>`を加えておくと、CMake GUIなどから選べます。必須ではありません。

### 3. Makefileを書く

```make
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/<app>
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= <app>
BUILD_DIR ?= $(PROJECT_ROOT)/build-<app>
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0

include $(PROJECT_ROOT)/project-tools/make/common.mk
```

| 変数 | 内容 |
| --- | --- |
| `ENABLE_AI` | `setup`でモデル生成を行い、`ai-*`ターゲットを有効にします |
| `ENABLE_THREAD_MONITOR` | `thread-monitor`ターゲットを有効にします |
| `ENABLE_CPU_TASK_MONITOR` | CMakeに`UAI_CPU_TASK_MONITOR=ON`を渡し、`cpu-task-monitor`ターゲットを有効にします |

### 4. IOCを置く

CubeMXのIOCを`config/`に置きます。既存アプリのIOCをコピーするのが簡単です。IOCの名前を変えた場合は`SAMPLE_DEFAULT_IOC`も合わせます。

### 5. 動かす

```sh
make -C userspace/<app> setup
make -C userspace/<app> monitor    # 別端末
make -C userspace/<app> ram-run
```

`STM32_RAM_ENTRY`と`STM32_RAM_STACK`の既定値（`local.mk.example`）は、`UAI_KERNEL_APPS`のアプリ（ai-app、mini-ai-app）以外では`0x34000800`と`0x34200000`です。独自のリンカスクリプトを使う場合は[起動の流れ](boot.md)を参照して合わせてください。

## ドライバーとミドルウェアを使う

`kernel/driver`や`kernel/middleware`は、ルートの`CMakeLists.txt`の`UAI_KERNEL_APPS`に登録したアプリ（ai-app、mini-ai-app）のときだけ追加されます。別のアプリから使うには次が必要です。手順を章ごとに追ったものが[チュートリアル](../tutorial/index.md)で、完成形が`userspace/mini-ai-app`です。

1. ルートの`CMakeLists.txt`の`UAI_KERNEL_APPS`に`<app>`を加える。これで`UAI_GENERATED_INCLUDE_DIR`の定義、`kernel/middleware`と`kernel/driver`の追加、既定IOC（`config/stm32n6570-dk-<app>.ioc`）、`STM32_RAM_ENTRY`/`STACK`、`UAI_CAMERA_LCD_CLOCKS`が切り替わる。
2. メモリ配置の生成を用意する。`config/board_memory.json`、`config/application_memory.json`、`config/model_layout.json`とリンカスクリプトの雛形を置き、mini-ai-appの`CMakeLists.txt`と同じように`auto_static_memory_layout`を呼ぶ（[memory](../middleware/memory.md)）。
3. `uai::drivers`、`uai::middleware`、`uai::driver_overrides`をリンクする。`src/hal_time.c`は`uai::driver_overrides`に含まれるため不要になります。`uai_systick_count`だけはアプリで定義します。
4. ドライバーを[初期化の順序](../driver.md)に従って初期化する。

モデルを追加する場合の実装の要点は[ai_runtime](../middleware/ai_runtime.md)と[NPUドライバー](../driver.md)にあります。
