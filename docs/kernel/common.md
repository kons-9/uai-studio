# エラー型とログ

`kernel/middleware/foundation`は、ドライバーとミドルウェアが共有するエラー型とログマクロです。名前空間は`uai::ai::common`です。

## common::Error

`kernel/middleware/foundation/error.hpp`の`common::Error`をドライバーとミドルウェアの戻り値に使います。例外は使いません。

```cpp
struct Error {
    ErrorCode code = ErrorCode::kOk;
    std::uint32_t detail = 0U;     // HALやST.AIの戻り値など
    const char *operation = "ok";  // 失敗した操作名
    constexpr bool Ok() const;
};
```

| `ErrorCode` | 意味 |
| --- | --- |
| `kOk` | 成功 |
| `kInvalidArgument` | 引数が不正 |
| `kNotInitialized`、`kAlreadyInitialized` | 初期化前、または二重初期化 |
| `kHardware`、`kCache` | HALやキャッシュ操作の失敗。`detail`にHALの戻り値が入ります |
| `kNoFrame`、`kNoBuffer`、`kQueueFull` | 今は処理対象がない。次のループで再試行すれば済むことがほとんどです |
| `kTimeout` | NPUの応答待ちなどのタイムアウト |
| `kModel`、`kNpu` | STEdgeAIの生成コードやNPUの失敗。`detail`に`stai_return_code`が入ります |
| `kOwnership` | 所有権の不一致（別のタスクが保持している、古いトークンを渡した） |
| `kInvalidState` | 操作できる状態にない |

使うときの指針です。

- 初期化を二重に呼んだときは`kAlreadyInitialized`を返すので、成功と同じに扱えます。
- `kNoFrame`、`kNoBuffer`、`kQueueFull`はエラーログを出さず、次のループへ進みます。
- それ以外の失敗は`operation`と`detail`をログに出すと原因を追えます。

```cpp
common::Error status = camera.Start();
if (!status.Ok()) {
    UAI_LOG_ERROR("camera: %s failed detail=%u\n",
                  status.operation, static_cast<unsigned int>(status.detail));
}
```

## ログ

`kernel/middleware/foundation/log.hpp`はレベル付きのログマクロを提供します。文字列は`const char*`で渡し、出力先のT-Monitorが要求する`UB*`への変換はログ層の中だけで行います。

```cpp
UAI_LOG_INFO("ai: model registered=%s\n", name);
```

| マクロ | 用途 |
| --- | --- |
| `UAI_LOG_ERROR` | 処理を続けられない失敗 |
| `UAI_LOG_WARN` | 復旧できた異常（カメラの再起動など） |
| `UAI_LOG_INFO` | 起動時の状態、1秒ごとの統計 |
| `UAI_LOG_DEBUG`、`UAI_LOG_TRACE` | 調査用。フレーム単位のログはここに置きます |

`kLogLevel`より詳細なレベルはコンパイル時に除かれ、引数の評価も行われません。`kLogLevel`は`log.hpp`で変更します（既定は`kInfo`）。T-Monitorの出力は1文字ずつ送るため、周期的なログは1秒程度の間隔にとどめてください。
