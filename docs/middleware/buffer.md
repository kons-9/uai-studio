# buffer

`kernel/middleware/buffer`は、領域の格納・記述・貸出所有を表す型をまとめます。タスク、推論、表示などの利用先ではなく、型が提供する契約で配置しています。各ヘッダはOS/HALをincludeしません。

| ヘッダ | 型と契約 |
| --- | --- |
| `owned_buffer.hpp` | `common::OwnedBuffer<Type, Capacity>`: 固定容量の配列を値として所有し、コピー時に要素数を検査する |
| `stable_aligned_bytes.hpp` | `common::StableAlignedBytes<Bytes>`: 8 byte整列した領域を所有し、コピー・移動を禁止してアドレスを固定する |
| `fixed_pool.hpp` | `memory_allocator::FixedPool<Entry, Capacity>`: 固定容量の要素配列と添字アクセス。割当・返却・状態遷移は行わない |
| `buffer_types.hpp` | `memory_allocator::Buffer`: アドレス、サイズ、整列、スロット番号、領域種別を持つ非所有の記述子 |
| `buffer_pointer.hpp` | `memory_manager::UniquePointer` / `SharedPointer`: 割当器が保持する制御ブロックを通じて貸出の寿命と返却を管理する |

今回の整理は配置とincludeの変更です。既存の名前空間、値コピー、参照カウント、配置、エラー動作は維持しています。`Region`と`BufferState`は引き続きcapture/display/inferenceの区別を持ち、ポインタの制御ブロック生成も`FixedPoolAllocator`と連携します。任意の割当器へ一般化したものではありません。

## 他のモジュールとの境界

- [memory](memory.md): メモリ配置へのアクセスと割当インターフェース。
- [memory_manager](memory_manager.md): 具体的な割当、貸出検証、フレームごとの状態遷移。
- [message_channel](message_channel.md): 配送と、OSのメッセージ形式に依存する`FixedMessageSlots`。
- [image_resizer](image_resizer.md): RGB形式、幅・高さ・strideを持つ画像ビュー。
- `pipeline`: フレーム番号や貸出トークンなど、パイプライン固有のデータ。

`OwnedBuffer`でマスクを値として保持したり、`StableAlignedBytes`をスタックとして使っても、これらの型が推論やタスクモジュールに所属するわけではありません。

## テスト

```sh
make -C kernel/middleware/buffer/tests test
```

値コピーと容量超過、固定領域の整列・サイズ・コピー/移動禁止を既存テストで確認します。割当器との連携は`memory_manager_test`とai-appの`frame_channels_test`で併せて検証します。