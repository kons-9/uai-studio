# アプリの追加

カーネル側を変更せずに、`userspace/<app>/`を追加するだけで新しいアプリをビルドできます。最小構成の例は実験用の[experiment-hello-world](https://github.com/kons-9/uai-studio/tree/main/userspace/experiment-hello-world)です。

## 最小構成

```text
userspace/<app>/
    Makefile            common.mkを読み込む1行のラッパ
  CMakeLists.txt      <app>.elf を定義し、μT-Kernel とボード設定をリンクする
    config/build.mk     Makeの機能フラグ、IOC、追加ターゲット
    config/build.cmake  共通コンポーネント、ボード、IOC、起動値の設定
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

先に`config/build.cmake`でアプリのビルド設定を宣言します。ルートCMakeは`project()`より前にこのファイルを読み込みます。最小アプリなら次の設定で足ります。

```cmake
set(UAI_DEFAULT_CUBEMX_IOC "${CMAKE_CURRENT_LIST_DIR}/stm32n6570-dk-fullsecure.ioc")
set(UAI_BOARD_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../src/board" "${CMAKE_CURRENT_LIST_DIR}")
```

`<app>.elf`と`<app>`ターゲットを定義します。`uai::utkernel`と`uai::stm32n6570_dk`をリンクし、リンカスクリプトは`UAI_STM32N6570_DK_LINKER_SCRIPT`を使えます。ビルド後に`objcopy`で`<app>.bin`を作ると`ram-run`で使えます。

```cmake
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(<app>.elf ${APP_SOURCES})
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

`APP_TARGET`の候補は`userspace/*/CMakeLists.txt`から自動収集されます。共通側のアプリ一覧や条件分岐を変更する必要はありません。設定項目とソース収集の基準は[ビルド構成](build.md)を参照してください。

### 3. Makefileを書く

アプリ名やレシピは書かず、共通入口を読み込むだけにします。

```make
include $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../project-tools/make/common.mk)
```

Make側のアプリ固有値は`config/build.mk`へ置きます。ビルド先は既定で`build-<app>`、機能フラグは既定で無効なので、最小アプリではIOCの指定だけで足ります。

```make
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
```

| 変数 | 内容 |
| --- | --- |
| `ENABLE_AI` | `setup`でモデル生成を行い、`ai-*`ターゲットを有効にします |
| `ENABLE_THREAD_MONITOR` | `thread-monitor`ターゲットを有効にします |
| `ENABLE_CPU_TASK_MONITOR` | CMakeに`UAI_CPU_TASK_MONITOR=ON`を渡し、`cpu-task-monitor`ターゲットを有効にします |
| `AI_MODEL_NAMES` | `ENABLE_AI=1`のときに生成するモデル名を指定します |
| `APP_TARGETS` | アプリ固有の既存ツールを呼ぶ追加ターゲットを指定します |
| `APP_CMAKE_SETTINGS` | CMakeへ渡すアプリ固有の変数名を指定します |

### 4. IOCを置く

CubeMXのIOCを`config/`に置きます。既存アプリのIOCをコピーするのが簡単です。IOCの名前を変えた場合は`SAMPLE_DEFAULT_IOC`と`config/build.cmake`の`UAI_DEFAULT_CUBEMX_IOC`も合わせます。

### 5. 動かす

```sh
make -C userspace/<app> setup
make -C userspace/<app> monitor    # 別端末
make -C userspace/<app> ram-run
```

RAM起動値をアプリ設定で指定しなければ、`STM32_RAM_ENTRY`と`STM32_RAM_STACK`の既定値は`0x34000800`と`0x34200000`です。独自のリンカスクリプトを使う場合は、`config/build.cmake`の`UAI_DEFAULT_RAM_ENTRY`と`UAI_DEFAULT_RAM_STACK`を[起動の流れ](boot.md)に合わせて指定します。

## ドライバーとミドルウェアを使う

`kernel/driver`や`kernel/middleware`は、アプリの`config/build.cmake`で`UAI_USE_KERNEL_COMPONENTS`を有効にしたときだけ追加されます。別のアプリから使うには次が必要です。手順を章ごとに追ったものが[チュートリアル](../tutorial/index.md)で、完成形が`userspace/mini-ai-app`です。

1. `config/build.cmake`で`UAI_USE_KERNEL_COMPONENTS`を`ON`にする。これで`UAI_GENERATED_INCLUDE_DIR`が定義され、`kernel/middleware`と`kernel/driver`が追加される。カメラ・LCDを使うなら`UAI_CAMERA_LCD_CLOCKS`も`ON`にし、IOCとRAM起動値はアプリの配置に合わせて別途指定する。
2. メモリ配置の生成を用意する。`config/board_memory.json`、`config/application_memory.json`、`config/model_layout.json`とリンカスクリプトの雛形を置き、mini-ai-appの`CMakeLists.txt`と同じように`auto_static_memory_layout`を呼ぶ（[memory](../middleware/memory.md)）。
3. `uai::drivers`、`uai::middleware`、`uai::driver_overrides`をリンクする。`src/hal_time.c`は`uai::driver_overrides`に含まれるため不要になります。`uai_systick_count`だけはアプリで定義します。
4. ドライバーを[初期化の順序](../driver/common.md)に従って初期化する。

モデルを追加する場合の実装の要点は[ai_runtime](../middleware/ai_runtime.md)と[NPUドライバー](../driver/npu.md)にあります。
