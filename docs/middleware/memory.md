# memory

ビルド時に`host_app/auto_static_memory_layout`が解決したメモリ配置を、C++から参照するためのモジュールです。アプリやドライバーがアドレスやサイズを直接書くことはなく、すべてこの配置から取ります。

## 配置の解決

入力は3つのJSONと生成済みモデルです。詳細は[host_app/README.md](https://github.com/kons-9/uai-studio/blob/main/host_app/README.md)を参照してください。

```text
config/board_memory.json ────────┐   物理メモリ領域、モデル重みのアドレス、command blobのセクション
config/application_memory.json ──┼─▶ resolve ─▶ memory_layout.json ─┬─▶ key.hpp、raw.hpp、memory_config.hpp
config/model_layout.json ────────┤                                    ├─▶ stm32n6570-dk-npu-ram.ld
models/<model>/stai_network.h ───┘                                    └─▶ memory_layout.yml（確認用）
```

領域の重なり、容量不足、command blobの枠あふれはこの段階でエラーになり、ビルドが止まります。

## 生成されるヘッダ

`<build>/generated/middleware/memory/generated/`に次を生成します。

| ヘッダ | 内容 |
| --- | --- |
| `static_memory_layout/key.hpp` | 領域のキー`static_memory_layout::Key` |
| `static_memory_layout/raw.hpp` | リンカシンボルから作った領域の表 |
| `memory_config.hpp` | `kMemoryConfig`（アライメント、モデル出力サイズ）と各バッファ数（`kCaptureBufferCount`、`kInferenceBufferCount`など） |

## 領域の取得

領域は`static_memory_layout.hpp`の`Region::GetRegionFromKey()`で取得します。

```cpp
#include "middleware/memory/static_memory_layout.hpp"

const auto region = static_memory_layout::Region::GetRegionFromKey(
    static_memory_layout::Key::kThreadMonitor);
void *base = reinterpret_cast<void *>(region.address());
std::size_t size = region.size();
```

キーは`config/application_memory.json`の`reservations`の`key`から作られます（`kCapture0`、`kInference0`、`kInferenceScratch`、`kThreadMonitor`、`kCpuTaskMonitor`など）。

## 領域を追加する

専用の領域を追加するときは、`application_memory.json`の`reservations`に名前、キー、配置先のメモリ（ベースのリンカスクリプトの`MEMORY`名）、セクション名、サイズを追加します。

```json
{
  "name": "my_buffer",
  "key": "kMyBuffer",
  "memory": "PSRAM_TRACE",
  "section": "sample_ai_my_buffer",
  "size": "0x00010000",
  "alignment": 32
}
```

次のビルドで`Key::kMyBuffer`が生成され、リンカスクリプトに領域が追加されます。複数個の領域は`"key": "kMyBuffer{index}"`と`count_ref`で`runtime`の個数を参照します。ソースにアドレスを書く必要はありません。

## バッファの型

`memory/buffer_types.hpp`の`memory_allocator::Buffer`は、アドレス、サイズ、アライメント、スロット番号、`Region`（`kCapture`、`kDisplay`、`kInference`）を持つバッファ記述子です。キャッシュ操作（`CacheManagement`）やフレーム型（[memory_manager](memory_manager.md)）はこの型でバッファを受け渡します。
