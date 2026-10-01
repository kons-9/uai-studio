# μT-Kernel

μT-Kernel 3.0 BSP2は`kernel/utkernel/mtk3_bsp2`のサブモジュールです。CMakeターゲットは`uai::utkernel`で、APIは`#include <tk/tkernel.h>`で使えます。

## 設定

タスク、イベントフラグ、メッセージバッファなどのオブジェクト数の上限は`kernel/utkernel/mtk3_bsp2/config/config.h`（`CNF_MAX_TSKID`など）で決まります。各ドライバーは初期化時にミューテックスを1つ作るため、ミューテックスの上限は上流の4から8に増やしています。

| 項目 | 内容 |
| --- | --- |
| タイマ | SysTickはμT-Kernelが使います。ティック周期は`CNF_TIMER_PERIOD`（10 ms）です |
| 割り込みベクタ | μT-Kernelが独自のベクタテーブルを持ちます。CubeMXの`startup_*.s`のベクタは起動直後にだけ使われます |
| UART | T-Monitorが使用（USART1、115200 bps） |
| デバッガサポート（μT-Kernel/DS） | 既定では無効。`UAI_KERNEL_TRACE_HOOKS`または`UAI_CPU_TASK_MONITOR`を有効にすると`USE_DBGSPT_TRACE=1`が定義され、有効になります |

## HALとの共存

HALの`HAL_GetTick()`と`HAL_Delay()`は`tk_get_tim()`と`tk_dly_tsk()`で置き換えています（`kernel/driver/board/hal_time.c`、`uai::driver_overrides`）。これによりHALのタイムアウトやウェイトがタスクのブロックとして動き、他のタスクに影響しません。

起動直後はHALのtickが止まっているため、HALのタイムアウトを使う前に`HAL_ResumeTick()`を呼びます。

## 割り込みの登録

C++のハンドラを登録するときは`tk_def_int()`を使います。CubeMXの弱シンボル（`NPU0_IRQHandler`など）を定義するだけでは、μT-Kernelのベクタからは呼ばれません。ai-appではNPUの割り込みを次のように登録しています。

```cpp
T_DINT npu_interrupt = {};
npu_interrupt.intatr = TA_HLNG;
npu_interrupt.inthdr = reinterpret_cast<FP>(NPU0_IRQHandler);
tk_def_int(static_cast<UINT>(NPU0_IRQn), &npu_interrupt);
```

HALのコールバック（DCMIPPのフレーム完了など）はドライバー側で`uai::driver_overrides`として実装しています。

## UART出力

UART出力はT-Monitorの`tm_printf()`と`tm_putstring()`を使います。1文字ずつ送信するため、頻繁に呼ぶとタスクの処理時間に影響します。フレーム単位の診断ログは調査時だけ有効にしてください。レベル付きのマクロは[エラー型とログ](common.md)を参照してください。

## フックAPI（td_hok_dsp、td_hok_int）

μT-Kernel 3.0のμT-Kernel/DSには、タスクのディスパッチと割り込みの前後で呼ばれるフックAPIが宣言されていますが、上流のBSP2では実装がありませんでした。μAI-Studioではこれを実装し、[cpu_task_monitor](../middleware/cpu_task_monitor.md)の土台にしています。

| API | フック | 呼ばれる場所 |
| --- | --- | --- |
| `td_hok_dsp(const TD_HDSP *)` | `exec(ID tskid, ID lsid)`、`stop(ID tskid, ID lsid, UINT tskstat)` | `dispatch.S`のタスク切り替え |
| `td_hok_int(const TD_HINT *)` | `enter(UINT dintno)`、`leave(UINT dintno)` | `interrupt.c`の割り込みハンドラの前後 |

`NULL`を渡すとフックを解除します。フックは割り込み禁止状態や割り込みコンテキストで呼ばれるため、システムコールを呼ばず、短く終わるようにしてください。

```cpp
#include <tk/dbgspt.h>

static void OnExec(ID task_id, ID) { /* タスク開始 */ }
static void OnStop(ID task_id, ID, UINT state) { /* タスク停止 */ }

TD_HDSP hook = {};
hook.exec = reinterpret_cast<FP>(&OnExec);
hook.stop = reinterpret_cast<FP>(&OnStop);
td_hok_dsp(&hook);
```

有効にするには、CMakeに`-DUAI_KERNEL_TRACE_HOOKS=ON`を渡します。`UAI_CPU_TASK_MONITOR`（Makeでは`ENABLE_CPU_TASK_MONITOR=1`）でも有効になります。無効なビルドではフックの呼び出しは組み込まれず、オーバーヘッドはありません。

BSP2と本体への変更内容は[THIRD_PARTY_NOTICES.md](https://github.com/kons-9/uai-studio/blob/main/THIRD_PARTY_NOTICES.md)にまとめています。
