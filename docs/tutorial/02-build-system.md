# 2. カーネルコンポーネントを有効にする

## 目的

`kernel/middleware`と`kernel/driver`をリンクできるようにします。この2つは、アプリが生成するメモリ配置ヘッダ（`memory_config.hpp`など）を含むため、アプリ自身の`config/build.cmake`で有効にしたビルドでだけ追加されます。ルートCMakeのアプリ一覧を変更する必要はありません。

## 変更するファイル

| ファイル | 変更 |
| --- | --- |
| `userspace/mini-ai-app/config/build.cmake` | 共通コンポーネント、カメラ/LCDクロック、RAM起動値、IOC、インクルード先を指定する |
| `userspace/mini-ai-app/CMakeLists.txt` | 共通ドライバをリンクし、HAL上書きのオブジェクトを直接追加する |
| `userspace/mini-ai-app/config/build.mk` | `ENABLE_CPU_TASK_MONITOR := 1` |

アプリの`config/build.cmake`に次の設定を置きます。

```cmake
set(UAI_USE_KERNEL_COMPONENTS ON)
set(UAI_CAMERA_LCD_CLOCKS ON)
set(UAI_DEFAULT_RAM_ENTRY "0x34060001")
set(UAI_DEFAULT_RAM_STACK "0x34100000")
set(UAI_DEFAULT_CUBEMX_IOC "${CMAKE_CURRENT_LIST_DIR}/stm32n6570-dk-mini-ai-app.ioc")
set(UAI_BOARD_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../src/board" "${CMAKE_CURRENT_LIST_DIR}")
```

ルートの`CMakeLists.txt`は`project()`より前にこのファイルを読み込み、その設定に応じて共通コンポーネントを追加します。

```cmake
set(UAI_USE_KERNEL_COMPONENTS OFF)
set(UAI_DEFAULT_BUILD_TYPE "")
include("${UAI_APP_DIR}/config/build.cmake")
...
if(UAI_USE_KERNEL_COMPONENTS)
    set(UAI_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
    add_subdirectory(kernel/middleware)
    add_subdirectory(kernel/driver)
endif()
```

共通のビルド処理は、このアプリ設定を次のように使います。

| 場所 | 内容 |
| --- | --- |
| `stm32_cli.cmake` | `UAI_DEFAULT_CUBEMX_IOC`を既定IOCに、`UAI_DEFAULT_RAM_ENTRY`/`UAI_DEFAULT_RAM_STACK`を`STM32_RAM_ENTRY`/`STM32_RAM_STACK`の既定値に使う。mini-ai-appは`0x34060001`/`0x34100000` |
| `pre_kernel` | `UAI_BOARD_INCLUDE_DIRS`をインクルードパスに加える。`UAI_CAMERA_LCD_CLOCKS`でカメラ・LCDのクロックとRIF設定を`main()`で行う |

`src/hal_time.c`は削除します。`HAL_GetTick`/`HAL_Delay`は`uai::driver_overrides`（`kernel/driver/board/hal_time.c`）が提供します。`uai_systick_count`だけはアプリに残るので、`main.cpp`で定義します。

HAL上書きは`OBJECT`ライブラリです。完成形と同じく、アプリのソースにそのオブジェクトを直接渡し、共通ドライバもリンクします。

```cmake
add_executable(mini-ai-app.elf
    src/main.cpp
    $<TARGET_OBJECTS:uai-driver-overrides>)
target_link_libraries(mini-ai-app.elf PRIVATE
    uai::utkernel uai::drivers uai::stm32n6570_dk)
```

```cpp
/* Incremented by the µT-Kernel SysTick handler; the kernel declares it extern. */
extern "C" {
volatile std::uint32_t uai_systick_count = 0U;
}
```

## なぜそうするか

- ミドルウェアは`memory_config.hpp`（バッファ数、モデル出力サイズ）に依存し、ドライバは`static_memory_layout`（PSRAM上のアドレス）に依存します。どちらもアプリの設定から生成されるので、アプリなしにはビルドできません。アプリ自身が共有コンポーネントの使用を宣言し、ルートはその宣言を読み込んでターゲットを追加します。
- この章の`mini-ai-app`は`.text.uai_ram_entry`を`0x34060000`へ固定配置し、`UAI_DEFAULT_RAM_ENTRY`にThumb bit込みの`0x34060001`を指定します。通常の`ai-app`はUnder Reset接続と最終ELFからのシンボル取得を使い、トランポリンを実行時テーブルの後ろへ順次配置するため、手動でアドレスを動かしません。
- `UAI_CAMERA_LCD_CLOCKS`はCubeMXの`main()`にカメラ・LCDのクロック有効化とDCMIPP/LTDCのRIF設定を追加します（[起動の流れ](../kernel/boot.md)）。

## 確認

共通コンポーネントは生成ヘッダに依存するため、この設定だけではビルドできません。第3章のメモリ配置生成と、第7章のモデル生成物を用意してから確認します。完成形にはこの設定がすでに入っています。
