# 3. メモリ配置

## 目的

PSRAMとNOR上のバッファ配置を宣言し、ビルド時にC++ヘッダとリンカスクリプトを生成させます。ドライバとミドルウェアはこの生成物を通じてだけアドレスを知ります。

## 追加するファイル

```text
userspace/mini-ai-app/
  config/board_memory.json         物理メモリ領域と command blob の置き場所
  config/application_memory.json   バッファの数・サイズと予約領域
  config/model_layout.json         モデル一覧（person）と重みアドレス
  config/network_blobs_config.h    生成コードが参照するセクション名（ai-appと同じ）
  stm32n6570-dk-npu-ram.ld         リンカスクリプトの雛形
  CMakeLists.txt                   生成コマンドの追加
```

### board_memory.json

ボード固有の物理アドレスです。ai-appから`PSRAM_MASK`（セグメンテーション用）とperson以外のcommand blobを削っただけで、ほかは同じ値です。

```json
"memory_regions": [
  {"name": "APP", "attributes": "rwx", "origin": "0x34000400", "length": "1023K"},
  {"name": "MODEL_FLASH", "attributes": "rx", "origin": "0x70500000", "length": "0x00E00000"},
  {"name": "PSRAM_CAPTURE", "attributes": "rwx", "origin": "0x91000000", "length": "0x00200000"},
  {"name": "PSRAM_DISPLAY", "attributes": "rwx", "origin": "0x91200000", "length": "0x00200000"},
  {"name": "PSRAM_INFERENCE", "attributes": "rwx", "origin": "0x91400000", "length": "0x00300000"},
  ...
],
"command_blobs": [
  {"model": "person", "section": ".network_blobs_person",
   "address": "0x70500000", "capacity": "0x00060000", "alignment": 64}
]
```

| 領域 | 使い手 |
| --- | --- |
| `APP` | コード、データ、スタック（AXISRAM1） |
| `MODEL_FLASH` | Neural-ARTのcommand blob（NOR、`ai-load`で書く） |
| `PSRAM_CAPTURE`/`PSRAM_DISPLAY` | Pipe1のDMA先とLCDの表示面、各2枚 |
| `PSRAM_INFERENCE`/`PSRAM_INFERENCE_SOURCE`/`PSRAM_SCRATCH` | Pipe2のDMA先（モデル出力領域込み）、スナップショット、作業領域 |
| `PSRAM_RAW_DUMP`/`PSRAM_PIPE2_DROP` | カメラドライバ内部（RAWダンプ、全推論バッファ使用中のDMA捨て先） |
| `PSRAM_TRACE`/`PSRAM_CPU_TRACE` | モニタのリングバッファ |

### application_memory.json

バッファの数とサイズ、予約領域です。`memory_manager`は`kCapture{index}`、`kDisplay{index}`、`kInference{index}`、`kInferenceSource{index}`、`kInferenceScratch`、`kRawDump`、`kPipe2Drop`を、モニタは`kThreadMonitor`、`kCpuTaskMonitor`を要求します。これ以外（ai-appの`kSegmentationMask`）は不要です。

```json
"runtime": {
  "capture":   {"width": 800, "height": 480, "bytes_per_pixel": 2, "count": 2},
  "display":   {"width": 800, "height": 480, "bytes_per_pixel": 2, "count": 2},
  "inference": {"width": 480, "height": 480, "bytes_per_pixel": 3, "count": 3},
  "inference_scratch": {"width": 480, "height": 288, "bytes_per_pixel": 3},
  "inference_source": {"count": 3}
}
```

### model_layout.json

```json
{
  "schema_version": 1,
  "model_order": ["person"],
  "models": {"person": "st_yolo_x_nano_480_1.0_0.25_3_st_int8.tflite"},
  "weight_addresses": {"person": "0x70380000"}
}
```

生成器は`<models-dir>/person/stai_network.h`から入出力テンソルのサイズを読み、`memory_config.hpp`の`model_output_bytes`（推論バッファ内の出力領域）を決めます。

### リンカスクリプト雛形

`stm32n6570-dk-npu-ram.ld`は生成器が書き換える雛形です。`MEMORY`ブロック、`.sample_ai_*`の予約セクション、`.network_blobs_person`のアドレスが、JSONの値で置き換えられます。ai-appの雛形から`.network_blobs_segmentation`/`face`と`.sample_ai_segmentation_mask`を削ったものです。

雛形で守る必要があるもの:

- `ENTRY(uai_ram_entry)`と`.text.uai_ram_entry 0x34060000`。`STM32_RAM_ENTRY`と対応します。
- `__end = .;`と`/DISCARD/ :`。生成器が予約セクションを差し込む目印です。
- JSONの`reservations`にある各`section`名のセクションが1つずつあること。無いとエラーになります。

### CMakeLists.txt

生成コマンドを`add_custom_command`で定義し、カーネル側ターゲットに依存させます。

```cmake
set(MINI_LAYOUT_DIR "${UAI_GENERATED_INCLUDE_DIR}")
add_custom_command(
    OUTPUT ${MINI_LAYOUT_OUTPUTS}
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/host_app/auto_static_memory_layout" all
            --board "${CMAKE_CURRENT_SOURCE_DIR}/config/board_memory.json"
            --application "${CMAKE_CURRENT_SOURCE_DIR}/config/application_memory.json"
            --models-dir "${MINI_AI_MODELS_DIR}"
            --model-config "${CMAKE_CURRENT_SOURCE_DIR}/config/model_layout.json"
            --output-dir "${MINI_LAYOUT_DIR}"
            --linker-base "${CMAKE_CURRENT_SOURCE_DIR}/stm32n6570-dk-npu-ram.ld"
    DEPENDS ... VERBATIM)
add_custom_target(mini-ai-app-memory-layout DEPENDS ${MINI_LAYOUT_OUTPUTS})
add_dependencies(uai-memory-layout mini-ai-app-memory-layout)
add_dependencies(uai-middleware mini-ai-app-memory-layout)
add_dependencies(uai-drivers mini-ai-app-memory-layout)
add_dependencies(uai-driver-overrides mini-ai-app-memory-layout)
```

`MINI_AI_MODELS_DIR`の既定は`userspace/ai-app/models`です。モデルの生成（章7）はai-appのスクリプトを使うので、生成物も共有します。

## なぜそうするか

- アドレスをソースに書かないため、領域の追加や移動はJSONの変更だけで済み、重なりや容量不足はビルド時に検出されます（[memory](../middleware/memory.md)）。
- 推論バッファは「入力画像＋モデル出力」を1スロットに持ちます。出力サイズはモデルから読むので、モデルを差し替えても`application_memory.json`は変わりません。
- `MODEL_FLASH`の内容（command blob）はELFの`.network_blobs_person`セクションに入り、ビルド後に`objcopy`で`network_blobs_person.hex`として取り出します。RAMイメージ（`.bin`）からは除外します（章7）。

## 確認

モデル生成物がまだないので、ここでは生成器だけを手で動かして確認します。`stai_network.h`の形だけ用意すれば動きます（出力は捨ててよい）。

```sh
python3 host_app/auto_static_memory_layout all \
  --board userspace/mini-ai-app/config/board_memory.json \
  --application userspace/mini-ai-app/config/application_memory.json \
  --models-dir userspace/ai-app/models \
  --model-config userspace/mini-ai-app/config/model_layout.json \
  --output-dir build/mini-layout-check \
  --linker-base userspace/mini-ai-app/stm32n6570-dk-npu-ram.ld
```

`build/mini-layout-check/middleware/memory/generated/memory_config.hpp`に`model_output_bytes`が3要素で出ていれば成功です。ai-appのモデルを生成済みなら、そのまま通ります。
