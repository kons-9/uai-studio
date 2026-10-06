# 7. モデル

## 目的

ST YOLOX nano（person）をSTEdgeAIでNeural-ART向けに変換し、生成されたCコードをアプリに結び付け、重みとcommand blobを外部NORへ書き込みます。

## モデルの生成

生成はai-appの`models/generate_model.sh`を使います。mini-ai-appの`Makefile`は`AI_MODELS_DIR`をai-appの`models`に向け、`AI_MODEL_NAMES := person`で対象を絞っています。

```make
ENABLE_AI := 1
AI_MODELS_DIR := $(PROJECT_ROOT)/userspace/ai-app/models
AI_MODEL_NAMES := person
AI_DEPS_SCRIPT := $(PROJECT_ROOT)/userspace/ai-app/scripts/setup_third_party.sh
```

```sh
make -C userspace/mini-ai-app ai-deps      # STEdgeAIランタイムと後処理ソースの確認
make -C userspace/mini-ai-app ai-models    # personモデルのダウンロードと生成
```

`stedgeai generate`は次のオプションで動きます（[generate_model.sh](https://github.com/kons-9/uai-studio/blob/main/userspace/ai-app/models/generate_model.sh)）。

| オプション | 意味 |
| --- | --- |
| `--target stm32n6 --st-neural-art default@<json>` | Neural-ART向けコンパイル。メモリプール（AXISRAM3-6）と重みの置き場所（xSPI2、`0x70380000`）を指定 |
| `--no-inputs-allocation --no-outputs-allocation` | 入出力バッファはアプリが渡す。Pipe2のDMAバッファをそのまま入力にするために必要 |
| `--input-data-type uint8 --inputs-ch-position chlast` | 入力はRGB888のインターリーブ（Pipe2の出力形式） |
| `--output-data-type int8` | 出力は量子化のまま。後処理でscale/zero_pointを使う |

生成物は`userspace/ai-app/models/person/`に置かれます。

| ファイル | 内容 | 使い手 |
| --- | --- | --- |
| `network.c`、`network_ecblobs.h` | epochごとのcommand blobとランタイム記述 | `person_network.c`が`#include` |
| `stai_network.c`、`stai_network.h` | STAI C API（`stai_network_run`など）とテンソル情報 | 同上、メモリ配置の生成器 |
| `network_data.xSPI2.bin`、`network_data.hex` | 重み。`0x70380000`から配置 | `ai-load` |

## アプリ側のコード

```text
src/model/person_network.c   生成コードを取り込み、名前を付け替え、C APIを公開
src/model/person_network.h   そのC APIの宣言
src/model/person_model.*     NpuNetwork アダプタ（C++）
```

### person_network.c

生成コードは`stai_network_init`のような固定名を使い、command blobを`ECBLOB_CONST_SECTION`に置きます。名前に`person_`を付けてから`#include`し、セクションを`.network_blobs_person`にします。

```c
#define ECBLOB_CONST_SECTION __attribute__((section(".network_blobs_person")))
#define stai_network_init person_stai_network_init
#define stai_network_run person_stai_network_run
...（stai_network_* と LL_ATON_*_network をすべて付け替える）

#include "network.c"          /* インクルードパス: <models-dir>/person */
#include "stai_network.c"

STAI_NETWORK_CONTEXT_DECLARE(person_context, STAI_NETWORK_CONTEXT_SIZE)

stai_return_code person_network_init(void)
{
    return person_stai_network_init(person_context);
}
stai_return_code person_network_set_input(stai_ptr input, stai_size size)
{
    return person_LL_ATON_Set_User_Input_Buffer_network(0U, input, size) ==
                   LL_ATON_User_IO_NOERROR ? STAI_SUCCESS
                                           : STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS;
}
...
```

モデルが1つなら付け替えは必須ではありませんが、ai-appと同じ形にしておくと2つ目のモデルを足すときに衝突しません。

### PersonModel（NpuNetwork）

NPUドライバは`npu::NpuNetwork`インターフェースだけを見ます。各メソッドを`person_network_*`へ転送するだけのクラスです。

```cpp
class PersonModel final : public npu::NpuNetwork {
public:
    stai_return_code Initialize() override { return person_network_init(); }
    stai_return_code SetInput(stai_ptr input, stai_size size) override
    { return person_network_set_input(input, size); }
    stai_return_code Run(stai_run_mode mode) override { return person_network_run(mode); }
    ...
};
```

### CMake

生成コードとNeural-ARTランタイムのソース（`ll_aton_*.c`）、後処理ソース（`od_pp_st_yolox.c`など）を1つの静的ライブラリにまとめ、`-include inttypes.h`と`NPU0_IRQHandler=LL_ATON_NPU0_IRQHandler`を付けてコンパイルします。後者はNPU割り込みをドライバ側のラッパで受けるためです。

```cmake
add_library(mini-ai-app_models STATIC
    "${MINI_SRC_DIR}/model/person_network.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton.c"
    ... )
target_compile_definitions(mini-ai-app_models PRIVATE
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    NPU0_IRQHandler=LL_ATON_NPU0_IRQHandler ...)
```

ビルド後、command blobだけをELFから取り出し、RAMイメージからは除きます。

```cmake
add_custom_command(TARGET mini-ai-app.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary --remove-section=.network_blobs_person
            "$<TARGET_FILE:mini-ai-app.elf>" "${CMAKE_CURRENT_BINARY_DIR}/mini-ai-app.bin"
    COMMAND ${CMAKE_OBJCOPY} -O ihex --only-section=.network_blobs_person
            "$<TARGET_FILE:mini-ai-app.elf>" "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_person.hex")
```

## なぜそうするか

- Neural-ARTのモデルは「重み」（`network_data`、NORの`0x70380000`）と「command blob」（epochごとのNPU命令列、NORの`0x70500000`）と「ランタイム記述」（`network.c`、アプリにリンク）の3つに分かれます。前2つはNORに書き、アプリを変えても書き直さなくてよいようにしています。
- `network.c`が生成されたときのll_atonバージョンと、リンクするランタイムのバージョンが違うと実機で動きません。CMakeは`ll_aton_version.h`と`network.c`を比べて構成時に止めます。
- `--no-outputs-allocation`のため、`GetOutputs()`はnullptrを返します。章8でこれを確認してから、推論バッファ内の出力領域を`SetOutputs()`で渡します。

## 確認

```sh
make -C userspace/mini-ai-app build
make -C userspace/mini-ai-app ai-load     # 重み（network_data.hex）とblob（network_blobs_person.hex）をNORへ
```

`ai-load`は`STM32_Programmer_CLI`と外部ローダ（`MX66UW1G45G_STM32N6570-DK.stldr`）で書き込み、`-v`で検証します。`Programming AI weights: .../person/network_data.hex`と`Programming AI command blob: .../network_blobs_person.hex`の両方が成功すれば完了です。モデルを変えない限り再実行は不要です。
