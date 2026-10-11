# ボード共通（時刻・割り込み・診断）

## HWの役割

`kernel/driver/board`は、個別Driverに属さないCortex-M55・NVIC・HAL時刻・診断の処理です。C++の名前空間は`uai::ai::driver::board`で、`HAL_GetTick()` / `HAL_Delay()`はCのHAL APIを上書きします。

## 初期化とAPI

| ファイル・API | 動作 |
| --- | --- |
| [time.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/board/time.hpp): `Milliseconds()` | `HAL_GetTick()`の32bit ms値を取得 |
| `EnableCycleCounter()` | CoreDebugのtraceとDWT CYCCNTを有効化。カウンターをゼロへリセットしない |
| [interrupt_priority.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/board/interrupt_priority.hpp): `ConfigureReferenceInterruptPriorities()` | 周辺IRQの優先度をSysTickと同じ値へ揃える |
| [register_diagnostics.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/board/register_diagnostics.hpp): `DumpCoreRegisters(stage)` | コア、fault状態、ベクタなどをログ出力 |
| `DumpPeripheralRegisters(stage)` | RCC、キャッシュ、IAC/RIFSC/RISAF、NPUなどをログ出力 |
| [hal_time.c](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/board/hal_time.c): `HAL_GetTick()` | `tk_get_tim()`の下位32bitを返す。取得失敗時は0 |
| `HAL_Delay(delay_ms)` | 非ゼロの待ち時間を`tk_dly_tsk()`へ渡す |

時刻・待機APIはμT-Kernel起動後に使います。SysTickをHAL独自のtickと二重管理せず、HALのtimeoutはカーネルのms時刻に接続します。起動タスクでは`HAL_ResumeTick()`を行ってから周辺機能を初期化します。

優先度の共通設定は`PVD_PVM_IRQn`から`LTDC_UP_ERR_IRQn`までを対象にします。後でDriverが個別の優先度を設定する場合もあるため、最終値が常に同一とは限りません。IRQハンドラーをμT-Kernelへ登録する処理は、この関数とは別です。

## ユースケース: 起動のどこで止まったかを調べる

Debugログを有効にしたうえで、初期化の前後でstage名を変えてダンプします。

```cpp
#include "driver/board/register_diagnostics.hpp"

void DumpAfterDriverInitialization()
{
    uai::ai::driver::board::DumpCoreRegisters("after_driver_init");
    uai::ai::driver::board::DumpPeripheralRegisters("after_driver_init");
}
```

コアダンプではVTORやfaultレジスタ、周辺ダンプではクロック、RIFのアクセス違反、NPU状態を確認します。カメラのDCMIPP/CSIやセンサー露出の詳細には[カメラ診断](camera.md)を併用します。

ai-appでは`DiagnosticsConfig::register_dump`を使います。関数自身にも`kDebug`のログレベル判定があるため、フラグだけを有効にしてもログレベルが低い場合は出力されません。

## ユースケース: HAL時刻の上書きがリンクされたか確認する

`uai::driver_overrides`はOBJECTライブラリで、HALのweak定義に対するstrong実装を最終リンクへ渡します。コンパイル対象に含まれたことだけで判断せず、実機向けの最終リンクマップで`HAL_GetTick` / `HAL_Delay`の配置元が`board/hal_time.c`のオブジェクトであることを確認します。

併せて`arm-none-eabi-nm`で最終ELFのシンボル種別を確認します。strong関数なら`T`、weakなら`W` / `w`です。weakが選ばれた場合は実際のリンク行と呼び出し経路を確認します。これは実機ビルド時の確認方法であり、ドキュメントのビルドがstrongリンクを証明するわけではありません。

## 制約と診断

- `HAL_Delay()`はタスク待機です。ISR、カーネル起動前、待機できない文脈から呼ばないでください。
- 32bitのms値やサイクル値はwrapします。時間差はunsigned差分で扱い、利用可能な計測幅内で使います。
- DWTの有効化はコア全体の状態を変えます。NPU traceや[汎用周辺カウンター](peripheral.md)と開始・終了を調整します。
- レジスタダンプは長いUART出力です。他タスクのログと混在しないようにし、通常の性能測定時は無効にします。
- 初期化前のHWへ直接アクセスする診断を不用意に増やさず、読み出し可能な状態で使います。追加のレジスタ診断はHW構成とアクセス権を確認して実装します。

関連: [共通ルール](common.md)、[μT-Kernel](../kernel/utkernel.md)、[コンソール](console.md)。