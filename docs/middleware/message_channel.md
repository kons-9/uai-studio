# メッセージチャネル

`kernel/middleware/message_channel`は、型付きメッセージの固定領域、μT-Kernelでの送受信、送信失敗時の置換順序をまとめます。メッセージの意味やバッファの返却方法は利用側が決めます。

`message_channel::MessageChannel<Message, Depth>`は、`Depth`件分の格納領域を所有し、`tk_cre_mbf`へ`TA_TFIFO | TA_USERBUF`で渡します。メッセージはtrivially copyableである必要があります。`FixedMessageSlots`はμT-Kernelの4 byteヘッダと4 byte丸めを含む領域を8 byte境界で確保し、コピーとムーブを禁止します。

```cpp
message_channel::MessageChannel<pipeline::InferenceFrame, kFrameQueueDepth> frames;
frames.Create();                            // 戻り値はメッセージバッファID
frames.Send(frame, TMO_POL);                // 満杯なら E_TMOUT
pipeline::InferenceFrame received{};
if (frames.Receive(&received, TMO_FEVR) == sizeof(received)) { /* ... */ }
```

`message_channel::LatestValueChannel<Message, Depth>`は`MessageChannel`を包み、キューが詰まったときに新しいメッセージを優先する送信と、溜まった中から最新だけを取り出す受信を提供します。送受信は`TMO_POL`で待ちません。

| 関数 | 内容 |
| --- | --- |
| `TrySend(message)` | 未作成なら`kNotInitialized`、満杯なら`kBufferOverflow`、他の失敗は`kHardware` |
| `SendReplacingOldestOnce(message)` | 満杯のときだけ最古を一件捨てて一回再送する |
| `SendReplacingOldest(message, on_discard)` | 失敗の種類を問わず最古を捨てて再送する。捨てた要素は`on_discard`に渡す。何も捨てられなければ`false` |
| `DrainLatest(latest, accept)` | 空になるまで消費し、`accept`が真の最後の要素を`*latest`へ書く |
| `TryReceive(message)`、`ReceiveBlocking(message)` | 1件受信（ポーリング／永久待ち） |

排他やこれら複合操作の原子性は保証しません。ai-appでは結果の有効判定（`*_valid`）とフレームの貸出返却・ログを[アプリ側](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/src/task/pipeline_task.hpp)に残します。

```sh
make -C kernel/middleware/message_channel/tests test
```