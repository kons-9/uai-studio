# タッチ（GT911 / I2C2）

## HWの役割

GT911はSTM32N6570-DKの静電容量式タッチコントローラです。I2C2経由で座標を取得し、共有ドライバーでは割り込みを使わずタスクからポーリングします。表示の描画やボタンの判定はドライバーではなく[ui](../middleware/ui.md)が担当します。

公開ヘッダー: [touch_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/touch_driver/touch_driver.hpp)。名前空間は`uai::ai::touch`です。

## 初期化とAPI

LCDとリセット線を共有するため、[LCD](lcd.md)を先に初期化します。`Initialize()`はリセットの解除、GT911のID確認などを行います。

| API | 戻り値・動作 |
| --- | --- |
| `TouchManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize()` | `common::Error`。初期化済みは`kAlreadyInitialized`、HW初期化失敗は`kHardware` |
| `Read(&sample)` | `ui::TouchPoint`の`active`、`x`、`y`を取得。activeな座標は800x480内にクリップ |
| `ReadRaw(&sample)` | クリップ前の座標を取得。配線・座標の診断用 |
| `Acquire(&accessor, timeout = TMO_FEVR)` | 所有権を取得。Driverの`Read()` / `ReadRaw()`へWriterを渡せる |

`Read()`はactive時の`x`を0〜799、`y`を0〜479へ収めます。軸交換・回転・アプリ固有のキャリブレーションを行うAPIではありません。未初期化は`kNotInitialized`、nullの出力先は`kInvalidArgument`です。`KeepClocksOnSleep()`は公開されていません。

## ユースケース: タッチ状態をUI処理へ渡す

初期化済みのコントローラをUIタスクから定期的に読みます。読み取り成功と、接触中かどうかは別の情報です。

```cpp
#include "driver/touch_driver/touch_driver.hpp"

namespace ai = uai::ai;

ai::common::Error PollTouch(ai::ui::TouchPoint *sample)
{
    return ai::touch::TouchManagement::Instance().Read(sample);
}
```

戻り値が成功したサンプルをUIへ渡します。`active == false`も離された状態として処理し、座標だけで押下を判定しないでください。前回のサンプルとの差分による押下・移動・解放の扱いはUI側で行います。

## 制約と診断

- LCD初期化前のタッチ初期化は避けます。ai-appではタッチ初期化の失敗時に画面操作だけを無効化し、カメラと推論は継続します。
- `touch: gt911 init failed ...`の各段階の状態を確認します。初期化ログと`ReadRaw()`を使って、I2C通信失敗と座標の問題を分けます。
- I2C2を汎用周辺APIやBSPから並行して操作しないでください。ドライバーの所有権は別のDriverや直接HAL操作までは排他しません。
- 部品設定は[gt911_conf.h](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/config/gt911_conf.h)、BSP設定は[stm32n6570_discovery_conf.h](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/board/include/stm32n6570_discovery_conf.h)の`TS_TOUCH_NBR`などです。この公開APIは`TouchPoint`を1つ返し、複数接触点の一覧を返すAPIではありません。