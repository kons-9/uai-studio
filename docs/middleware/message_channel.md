# メッセージチャネル

`kernel/middleware/message_channel`は、型付きメッセージのFIFOと、キューが詰まったときに最新値を優先する送受信を提供します。OSへの依存は`MicroTKernelBackend`に閉じ、コアはOSの型を含みません。メッセージの意味やバッファの返却方法は利用側が決めます。

| ヘッダ | 内容 |
| --- | --- |
| `message_channel.hpp` | `MessageChannel<Message, Depth, Backend>`: 型付きFIFOのコア。`common::Error`で結果を返す |
| `utkernel_backend.hpp` | `MicroTKernelBackend<Message, Depth>`: μT-Kernelのメッセージバッファ（`tk_cre_mbf`、`TA_TFIFO | TA_USERBUF`）を所有し、ER/INT/TMOを`common::Error`に変換する |
| `fixed_message_slots.hpp` | `FixedMessageSlots<Message, Depth>`: μT-Kernelの4 byteヘッダと4 byte丸めを含む領域を8 byte境界で確保する。バックエンドが使う |
| `fixed_event_queue.hpp` | `FixedEventQueue<Event, Capacity>`: OSを使わない固定容量のリング。単一の消費者と、呼び出し側で直列化した生産者向け。満杯なら捨てて数える |
| `latest_value_channel.hpp` | `LatestValueChannel<Message, Depth, Backend>`: 最新値優先の送信と、溜まった中から最新だけを取る受信 |

## MessageChannel

```cpp
using Frames = message_channel::MessageChannel<
    pipeline::InferenceFrame, kFrameQueueDepth,
    message_channel::MicroTKernelBackend<pipeline::InferenceFrame, kFrameQueueDepth>>;
Frames frames;
frames.Create();                       // 失敗は kHardware
frames.TrySend(frame);                 // 満杯なら kBufferOverflow
pipeline::InferenceFrame received{};
frames.Receive(&received);             // 永久待ち。TryReceive() は空なら kNoFrame
```

| 結果 | エラー |
| --- | --- |
| `Create()`前の送受信 | `kNotInitialized` |
| 非待機送信で満杯 | `kBufferOverflow` |
| 非待機受信で空 | `kNoFrame` |
| その他のOSエラー | `kHardware` |

メッセージはtrivially copyableである必要があります。コアとバックエンドはコピー・ムーブ禁止で、OSに登録した領域のアドレスは動きません。

## LatestValueChannel

送受信はすべて非待機です（`ReceiveBlocking()`のみ永久待ち）。

| 関数 | 内容 |
| --- | --- |
| `TrySend(message)` | `MessageChannel::TrySend()`と同じ |
| `SendReplacingOldestOnce(message)` | 満杯のときだけ最古を一件捨てて一回再送する |
| `SendReplacingOldest(message, on_discard)` | 失敗の種類を問わず最古を捨てて再送する。捨てた要素は`on_discard`に渡す。再送は`Depth + 1`回までで、送れなければ最後の送信または受信のエラーを返す |
| `DrainLatest(latest, accept)` | 空になるまで消費し、`accept`が真の最後の要素を`*latest`へ書く。戻り値の`DrainResult`は`error`と`updated`を別々に持つ |
| `TryReceive(message)`、`ReceiveBlocking(message)` | 1件受信。`common::Error`を返す |

`DrainResult::error`は、正常に空になるまで受信できた場合、更新があれば`kOk`、なければ`kNoFrame`です。途中の受信エラーはそのまま返します。`Depth + 1`件受信しても空にならない（送信側が補充し続けている）ときは`kTimeout`です。`updated`は出力を書き換えたかを表し、更新後に受信エラーが起きた場合も`true`です。取得済みの最新値は保持します。ai-appの`TryGetLatestResult()`もこの結果型を返し、描画側でエラー処理と更新判定を分けます。

排他やこれら複合操作の原子性は保証しません。ai-appでは結果の有効判定（`*_valid`）とフレームの貸出返却・ログを[アプリ側](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/src/task/pipeline_task.hpp)に残します。

## テスト

```sh
make -C kernel/middleware/message_channel/tests test
```

バックエンドのOS属性・サイズ・alignment・TMO選択と、OS戻り値からエラーコードへの変換を確認します。最新値優先の動作とバッファ返却は`frame_channels_test`がai-appのチャネル経由で検証します。コアはOSヘッダなしでコンパイルできます。
