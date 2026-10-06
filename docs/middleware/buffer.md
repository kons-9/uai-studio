# buffer

`kernel/middleware/buffer`は、領域の格納・記述・貸出を表す型をまとめます。タスク、推論、表示などの利用先ではなく、型が提供する契約で配置しています。名前空間は`uai::ai::buffer`（`OwnedBuffer`と`StableAlignedBytes`は`uai::ai::common`）で、`interrupt_guard.hpp`以外はOS/HALをincludeしません。

| ヘッダ | 型と契約 |
| --- | --- |
| `owned_buffer.hpp` | `common::OwnedBuffer<Type, Capacity>`: 固定容量の配列を値として所有し、コピー時に要素数を検査する |
| `stable_aligned_bytes.hpp` | `common::StableAlignedBytes<Bytes>`: 8 byte整列した領域を所有し、コピー・移動を禁止してアドレスを固定する |
| `buffer_types.hpp` | `buffer::Buffer`: アドレス、サイズ、整列、スロット番号、`Region`を持つ非所有の記述子。`==`で同一バッファか比較できる。`BufferState`は貸出の段階 |
| `lease_pool.hpp` | `buffer::LeasePool<Capacity>`: 固定数の`Slot`（`Buffer`、`BufferState`、貸出トークン）。空きスロット・アドレスによる検索、トークン採番付きの`Lease()`、`Release()` |
| `interrupt_guard.hpp` | `buffer::InterruptGuard`: PRIMASKを保存して割り込みを禁止し、破棄時に復元する。ARM以外ではno-op |

## 貸出の仕組み

`LeasePool::Lease()`はスロットの状態を変えてトークンを採番し、`Release()`は`kFree`へ戻してトークンを消します。トークンは0を避けて単調増加するので、同じスロットを再取得しても古いトークンは一致しません。利用側は`InferenceFrame::lease_token`のようにトークンを持ち回り、返却時にスロットのトークンと照合します。

`LeasePool`はロックせず、どの状態を「貸出中」とみなすかも決めません。表示と推論の状態遷移、割り込み禁止区間、生成されたメモリ配置の解決は[memory_manager](memory_manager.md)が担当します。ホストでは`InterruptGuard`がno-opのため、ホストテストの成功は割り込み安全性の証明にはなりません。

## 他のモジュールとの境界

- [memory](memory.md): メモリ配置へのアクセス。
- [memory_manager](memory_manager.md): 表示/推論プールの所有と状態遷移、フレームの貸出と返却。
- [message_channel](message_channel.md): 配送と、OSのメッセージ形式に依存する`FixedMessageSlots`。
- [image_resizer](image_resizer.md): RGB形式、幅・高さ・strideを持つ画像ビュー。
- `pipeline`: フレーム番号や貸出トークンなど、パイプライン固有のデータ。

## テスト

```sh
make -C kernel/middleware/buffer/tests test
```

値コピーと容量超過、固定領域の整列・サイズ・コピー/移動禁止、`LeasePool`のトークン採番と検索を確認します。プール枯渇、古いトークンの拒否、不正なフレームの拒否は`memory_manager_test`が公開APIから検証します。
