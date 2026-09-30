# カーネルとビルド構成

## ビルド構成

ルートの`CMakeLists.txt`は`APP_TARGET`で選んだ`userspace/<APP_TARGET>/CMakeLists.txt`を読み込みます。各アプリの`Makefile`は`APP_TARGET`とビルド先を決めて`build-system/make/common.mk`を読み込むだけなので、通常は`make -C userspace/<app> <target>`で操作します。ホスト固有の値は`build-system/host-config/local.mk`に置きます。

### CMakeターゲット

| ターゲット | 種類 | 内容 |
| --- | --- | --- |
| `uai::utkernel` | STATIC | µT-Kernel 3.0本体とT-Monitor（`kernel/utkernel`） |
| `uai::stm32n6570_dk` | OBJECT | CubeMX生成コード、HAL、RAM起動コード（`kernel/pre_kernel/stm32n6570-dk`） |
| `uai::middleware` | STATIC | `kernel/middleware`。`uai::utkernel`に依存 |
| `uai::drivers` | STATIC | `kernel/driver`。`uai::utkernel`と`uai::middleware`に依存 |
| `uai::driver_overrides` | OBJECT | HALの弱シンボル（`HAL_GetTick`、`HAL_Delay`、DCMIPPコールバック）を上書きするコード。実行ファイルへ直接入れます |

`uai::middleware`と`uai::drivers`は`APP_TARGET=ai-app`のときだけ追加されます。生成したメモリ配置ヘッダを置く`UAI_GENERATED_INCLUDE_DIR`（`<build>/generated`）もai-appでだけ定義されます。

### CMakeオプション

| オプション | Make変数 | 内容 |
| --- | --- | --- |
| `UAI_CPU_TASK_MONITOR` | `ENABLE_CPU_TASK_MONITOR=1` | CPU task monitorを有効にします。µT-Kernelのディスパッチフックも有効になります |
| `UAI_KERNEL_TRACE_HOOKS` | なし | µT-Kernelの実行フックだけを有効にします |

### Makeターゲット

| ターゲット | 内容 |
| --- | --- |
| `setup` | AI依存の確認とモデル生成（AIアプリのみ）、CubeMX生成、CMake構成 |
| `generate` | CubeMX生成とCMake構成 |
| `configure`、`build`、`clean` | CMakeの構成、ビルド、クリーン |
| `monitor` | UARTモニタを開きます |
| `ram-run` | ビルドしてRAMへロードし、実行します |
| `program`（`flash`）、`sign` | 外部Flashへの書き込み、署名イメージの作成 |
| `ai-models`、`ai-load`、`ai-run` | モデル生成、モデルの外部NOR書き込み、書き込み後のRAM実行（AIアプリのみ） |
| `thread-monitor`、`cpu-task-monitor` | 実機のトレースを取得して可視化（`ENABLE_THREAD_MONITOR`、`ENABLE_CPU_TASK_MONITOR`が1のアプリ） |

## 起動の流れ

RAM実行時の流れです。

1. `ram-run`が`STM32_Programmer_CLI`で`<app>.bin`を`STM32_RAM_ADDRESS`（`0x34000400`）へ書き、`MSP=STM32_RAM_STACK`、`PC=STM32_RAM_ENTRY`でCPUを再開します。
2. `uai_ram_entry`（`ram_entry.S`）が割り込みを止めてスタックを設定し、`uai_prepare_ram_launch()`でVTORを`0x34000400`にして、前回のイメージから残ったキャッシュを無効化します。
3. CubeMXの`Reset_Handler`がCランタイムを初期化し、`cubemx_entry.c`の`main()`を呼びます。
4. `main()`はHAL、クロック、GPIO、CACHEAXI、RAMCFG、USART1、XSPI1/2、RIFを初期化し、`knl_start_mtkernel()`でµT-Kernelを起動します。カメラを使うアプリではカメラ用クロックとDCMIPP/LTDCのRIF設定も行います。
5. µT-Kernelの初期タスクがアプリの`usermain()`を呼びます。

`STM32_RAM_ENTRY`と`STM32_RAM_STACK`の既定値はアプリごとに`build-system/host-config/local.mk.example`で決まります。独自のリンカスクリプトを使う場合は合わせて変更してください。

## µT-Kernel

µT-Kernel 3.0 BSP2は`kernel/utkernel/mtk3_bsp2`のサブモジュールです。APIは`#include <tk/tkernel.h>`で使えます。

- タスク、イベントフラグ、メッセージバッファなどのオブジェクト数の上限は`kernel/utkernel/mtk3_bsp2/config/config.h`（`CNF_MAX_TSKID`など）で決まります。各ドライバーは初期化時にミューテックスを1つ作ります。
- SysTickはµT-Kernelが使います。HALの`HAL_GetTick()`と`HAL_Delay()`は`tk_get_tim()`と`tk_dly_tsk()`で置き換えています（`kernel/driver/board/hal_time.c`）。起動直後はHALのtickが止まっているため、HALのタイムアウトを使う前に`HAL_ResumeTick()`を呼びます。
- µT-Kernelは独自の割り込みベクタを使います。C++のハンドラを登録するときは`tk_def_int()`を使います。ai-appではNPUの割り込みを次のように登録しています。

```cpp
T_DINT npu_interrupt = {};
npu_interrupt.intatr = TA_HLNG;
npu_interrupt.inthdr = reinterpret_cast<FP>(NPU0_IRQHandler);
tk_def_int(static_cast<UINT>(NPU0_IRQn), &npu_interrupt);
```

UART出力はT-Monitorの`tm_printf()`と`tm_putstring()`を使います（USART1、115200 bps）。1文字ずつ送信するため、頻繁に呼ぶとタスクの処理時間に影響します。

## エラー型とログ

`kernel/common/error.hpp`の`common::Error`をドライバーとミドルウェアの戻り値に使います。

```cpp
struct Error {
    ErrorCode code = ErrorCode::kOk;
    std::uint32_t detail = 0U;     // HALやST.AIの戻り値など
    const char *operation = "ok";  // 失敗した操作名
    constexpr bool Ok() const;
};
```

`ErrorCode`には`kInvalidArgument`、`kNotInitialized`、`kAlreadyInitialized`、`kHardware`、`kCache`、`kNoFrame`、`kNoBuffer`、`kQueueFull`、`kTimeout`、`kModel`、`kNpu`、`kOwnership`、`kInvalidState`があります。`kNoFrame`、`kNoBuffer`、`kQueueFull`は「今は処理対象がない」という意味で、多くの場合は次のループで再試行すれば済みます。初期化を二重に呼んだときは`kAlreadyInitialized`を返すので、成功と同じに扱えます。

`kernel/common/log.hpp`はレベル付きのログマクロを提供します。

```cpp
UAI_LOG_INFO(reinterpret_cast<const UB *>("ai: model registered=%s\n"), name);
```

`UAI_LOG_ERROR`、`UAI_LOG_WARN`、`UAI_LOG_INFO`、`UAI_LOG_DEBUG`、`UAI_LOG_TRACE`があり、`kLogLevel`より詳細なレベルはコンパイル時に除かれます。

## アプリの追加

最小構成の例は実験用の[experiment-hello-world](https://github.com/kons-9/uai-studio/tree/main/userspace/experiment-hello-world)です。

1. `userspace/<app>/`を作り、`src/main.cpp`に`usermain()`を書きます。

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

2. `CMakeLists.txt`で`<app>.elf`と`<app>`ターゲットを定義します。`uai::utkernel`と`uai::stm32n6570_dk`をリンクし、リンカスクリプトは`UAI_STM32N6570_DK_LINKER_SCRIPT`を使えます。ビルド後に`objcopy`で`<app>.bin`を作ると`ram-run`で使えます。
3. `Makefile`を次のように書きます。

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

include $(PROJECT_ROOT)/build-system/make/common.mk
```

4. CubeMXのIOCを`config/`に置きます。既存アプリのIOCをコピーするのが簡単です。

`kernel/driver`や`kernel/middleware`を使う場合は、ルートの`CMakeLists.txt`でai-appと同じようにそれらを追加し、メモリ配置の生成（[middleware.md](middleware.md#memory)）を用意する必要があります。カメラを使う場合は`kernel/pre_kernel/stm32n6570-dk/CMakeLists.txt`で`UAI_CAMERA_LCD_CLOCKS`を有効にします。
