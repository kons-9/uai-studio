# 共通基盤（foundation）

`kernel/middleware/foundation`は、ドライバー、ミドルウェア、アプリが共有する小さな部品です。名前空間は`uai::ai::common`です。型付きメッセージ通信は[message_channel](../middleware/message_channel.md)、領域の格納・記述・貸出所有は[buffer](../middleware/buffer.md)に分離しています。

| ヘッダ | 内容 |
| --- | --- |
| `error.hpp` | 戻り値の`common::Error`とエラーコード、エラーのログ出力 |
| `log.hpp` | レベル付きのログマクロ`UAI_LOG_*` |

## common::Error

`error.hpp`の`common::Error`をドライバーとミドルウェアの戻り値に使います。例外は使いません。

```cpp
class Error {
public:
    constexpr Error(ErrorCode code = ErrorCode::kOk);
    constexpr ErrorCode Code() const;
    constexpr bool Ok() const;
    constexpr bool IsRoutine() const;           // kNoFrame、kNoBuffer、kQueueFull
    void LogStatus(const char *component) const;
    void LogStatus(const char *component, LogLevel level) const;
};
```

| `ErrorCode` | 意味 |
| --- | --- |
| `kOk` | 成功 |
| `kInvalidArgument` | 引数が不正 |
| `kNotInitialized`、`kAlreadyInitialized` | 初期化前、または二重初期化 |
| `kHardware`、`kCache` | HALやキャッシュ操作の失敗 |
| `kNoFrame`、`kNoBuffer`、`kQueueFull` | 今は処理対象がない。次のループで再試行すれば済むことがほとんどです |
| `kBufferOverflow` | 固定長バッファやキューに入りきらない。`OwnedBuffer::CopyFrom()`や、ai-appの結果キューが満杯のときに返します |
| `kTimeout` | NPUの応答待ちなどのタイムアウト |
| `kModel`、`kNpu` | STEdgeAIの生成コードやNPUの失敗 |
| `kOwnership` | 所有権の不一致（別のタスクが保持している、古いトークンを渡した） |
| `kInvalidState` | 操作できる状態にない |

使うときの指針です。

- 初期化を二重に呼んだときは`kAlreadyInitialized`を返すので、成功と同じに扱えます。
- `IsRoutine()`が真になる`kNoFrame`、`kNoBuffer`、`kQueueFull`はエラーログを出さず、次のループへ進みます。
- 失敗を記録するときは`LogStatus("component")`を呼びます。`IsRoutine()`なら`UAI_LOG_DEBUG`、それ以外は`UAI_LOG_ERROR`で、呼び出し元の`component`とコード名を1行に出します。レベルを変えたいときは`LogStatus("component", LogLevel::kWarn)`のように指定します。成功時は何もしないため、戻り値にそのまま付けられます。

```cpp
common::Error status = camera.Start();
status.LogStatus("camera");
// error: component=camera code=hardware(4)
```

## ログ

`log.hpp`はレベル付きのログマクロを提供します。文字列は`const char*`で渡し、出力先のT-Monitorが要求する`UB*`への変換はログ層の中だけで行います。

```cpp
UAI_LOG_INFO("ai: model registered=%s\n", name);
```

| マクロ | 用途 |
| --- | --- |
| `UAI_LOG_ERROR` | 処理を続けられない失敗 |
| `UAI_LOG_WARN` | 復旧できた異常（カメラの再起動など） |
| `UAI_LOG_INFO` | 起動時の状態、1秒ごとの統計 |
| `UAI_LOG_DEBUG`、`UAI_LOG_TRACE` | 調査用。フレーム単位のログはここに置きます |

`kLogLevel`より詳細なレベルはコンパイル時に除かれ、引数の評価も行われません。`kLogLevel`は`log.hpp`で変更します（既定は`kInfo`）。レベルを実行時の値で選ぶときは`UAI_LOGF(level, ...)`、出力の前に重い処理を省きたいときは`common::IsLogEnabled(level)`を使います。T-Monitorの出力は1文字ずつ送るため、周期的なログは1秒程度の間隔にとどめてください。

μT-Kernelタスクの起動とループは、OSに依存するため[task](../middleware/task.md)に分けています。

## ホストテスト

`common::Error`のログ分類をホストで確認します。格納型は[buffer](../middleware/buffer.md)、メッセージの容量と送受信は[message_channel](../middleware/message_channel.md)のテストで確認します。

```sh
make -C kernel/middleware/foundation/tests test
```
