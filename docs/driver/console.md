# コンソール（USART1 / T-Monitor UART）

## HWの役割

USART1をコンソールとして使い、ログ・シェル応答を送信し、入力文字を割り込みで受信します。実装はGPIOEのPE5/PE6をAF7に設定し、送信にはT-Monitorの`tm_putstring()` / `tm_putchar()`を使います。PC側はボードのUART接続を開きます。USB CDCドライバーではありません。

公開ヘッダー: [console_driver.hpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/console_driver/console_driver.hpp)。名前空間は`uai::ai::console`です。UART初期化とISRは[console_registers.cpp](https://github.com/kons-9/uai-studio/blob/main/kernel/driver/console_driver/registers/console_registers.cpp)にあります。通信設定は`tm_com_init()`側の設定とPC側を合わせます。

## 初期化とAPI

カーネル起動後に初期化し、アプリの割り込み登録経路で`USART1_IRQHandler`を使えるようにします。`Initialize()`はUSART1のクロックとGPIO、受信・エラー割り込みを用意します。

| API・型 | 戻り値・動作 |
| --- | --- |
| `ConsoleManagement::Instance()` | 唯一の管理インスタンス |
| `Initialize(notifier = {})` | `common::Error`。初期化済みは`kAlreadyInitialized` |
| `Read(&input)` | 受信キューから1要素を取り出す。空の場合は`kNoFrame` |
| `Write(text, size)` | 指定長のデータをT-Monitor経由で送信。`common::Error` |
| `Acquire(&accessor, timeout = TMO_FEVR)` | 連続操作の所有権を取得 |
| `accessor->Read(&input, writer)` / `accessor->Write(text, size, writer)` | 保持中の所有権で読み書き |
| `Input` | `value`は受信文字、`error`は受信エラー通知 |
| `Notifier` | `context`と`wake(void *)`。受信ISRが待機タスクを起こすために呼ぶ |

`Read()` / `Write()`の未初期化は`kNotInitialized`、`Read(nullptr)`や非ゼロ長の`Write(nullptr, size)`は`kInvalidArgument`です。`KeepClocksOnSleep()`は公開されていません。

## ユースケース: 起動メッセージを送信する

以下はカーネル起動後のタスクから呼ぶ例です。受信通知を使うシェルでは、最初の初期化時に適切なNotifierを渡します。

```cpp
#include "driver/console_driver/console_driver.hpp"

namespace ai = uai::ai;

ai::common::Error AnnounceReady()
{
    auto &console = ai::console::ConsoleManagement::Instance();
    const auto initialized = console.Initialize();
    if (!initialized.Ok() &&
        initialized.Code() != ai::common::ErrorCode::kAlreadyInitialized) {
        return initialized;
    }
    constexpr char message[] = "application ready\n";
    return console.Write(message, sizeof(message) - 1);
}
```

## ユースケース: シェルの行入力

通知で起きたタスクから`Read(&input)`を繰り返し、`kNoFrame`になったら待機へ戻ります。`input.error`が立っていれば、壊れた行を実行しないよう行入力を破棄・再同期します。通常の文字はmiddlewareのシェルや行編集へ渡します。`Read()`自身は改行待ちやコマンド解析を行いません。

## 制約と診断

- `Notifier::wake`はISRで実行されます。ブロック、mutex取得、長い処理をせず、ISRで使える通知操作だけを行います。contextはIRQが使う期間中、有効である必要があります。
- 受信は有限のキューです。タスクが読み出さないまま長い入力を送るとoverflowし、受信エラー扱いになります。UARTのoverrun、framing、noise、parityエラーも通知されます。
- `Write()`はCRLFのCRを取り除き、T-Monitorの改行処理へ渡します。C文字列の終端を必要とせず、埋め込まれたNULも明示的に送ります。
- 送信は同期的です。長い出力は呼び出しタスクを占有します。T-Monitorの送信関数はこのI/FにHWエラーを返さないため、`Write().Ok()`をPC到着の確認とは扱いません。
- Driverの所有権は、別経路のログ出力やT-Monitor直接呼び出しまでは排他しません。レジスタダンプ中などは他タスクの出力が混ざらないようにします。
- PC側のmonitorは同じUARTに複数起動しないでください。ai-appは`make -C userspace/ai-app monitor`で開き、実機書き込みより先にUARTを準備します。