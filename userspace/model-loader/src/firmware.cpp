#include "extension.hpp"
#include "protocol.hpp"
#include "model_expected.hpp"
#ifdef MODEL_LOADER_NPU
#ifdef MODEL_LOADER_MULTI
#include "npu_multi.hpp"
#include "model_registry.hpp"
#else
#include "npu_backend.hpp"
#endif
#endif
extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_xspi.h"
}

namespace experiment {
namespace {
alignas(32) __attribute__((section(".experiment_weights"))) std::uint8_t weights[0x800000];
#ifdef MODEL_LOADER_NPU
auto *const blob = reinterpret_cast<std::uint8_t *>(0x91a00000);
#else
alignas(32) __attribute__((section(".experiment_blob"))) std::uint8_t blob[0x200000];
#endif
constexpr std::size_t blob_capacity = 0x200000;
model::Manifest expected{};
model::Staging stage(
    {0x91200000,
     sizeof(weights),
     weights},
    {0x91a00000,
        blob_capacity,
     blob},
    expected
);
model::Session session{&stage, HAL_GetTick};
#ifdef MODEL_LOADER_NPU
#ifdef MODEL_LOADER_MULTI
model::Manifest catalog[catalog_count]{};
model::MultiBackend backend;
#else
model::NpuBackend backend;
#endif
model::Execution execution(stage, backend);
#endif
console::Writer output{};
bool ready = false;

console::Status Execute(
    void *context,
    int count,
    const char *const *arguments,
    const console::Writer &writer
)
{
    if (!ready) {
        return console::Status::kInvalidState;
    }
    if (count == 2 && std::strcmp(arguments[1], "list") == 0) {
#if defined(MODEL_LOADER_NPU) && defined(MODEL_LOADER_MULTI)
        for (unsigned index = 0; index < catalog_count; ++index) {
            char digest_text[65]{};
            constexpr char hex[] = "0123456789abcdef";
            for (unsigned byte = 0; byte < 32; ++byte) {
                digest_text[byte * 2] = hex[catalog[index].contract_digest[byte] >> 4];
                digest_text[byte * 2 + 1] = hex[catalog[index].contract_digest[byte] & 15];
            }
            char line[256];
            std::snprintf(line, sizeof(line), "MODEL CATALOG id=%s kind=%lu tag=%08lx contract=%s\n",
                          model_loader::registry[index].name, static_cast<unsigned long>(catalog[index].kind),
                          static_cast<unsigned long>(catalog[index].tag), digest_text);
            writer.Write(line);
        }
#else
        writer.Write("MODEL CATALOG legacy single-model contract\n");
#endif
        writer.Write("MODEL OK\n");
        return console::Status::kOk;
    }
    return model::Command(context, count, arguments, writer);
}
}

std::size_t Register(
    const Services &provided,
    console::Command *commands,
    std::size_t capacity
)
{
    if (!model::Decode(expected_header, sizeof(expected_header), expected)) {
        return 0;
    }
    stage = model::Staging({0x91200000, sizeof(weights), weights}, {0x91a00000, blob_capacity, blob}, expected);
#ifdef MODEL_LOADER_NPU
#ifdef MODEL_LOADER_MULTI
    if (!catalog_count) {
        return 0;
    }
    for (unsigned index = 0; index < catalog_count; ++index) {
        if (!model::Decode(catalog_headers[index], catalog_sizes[index], catalog[index])) {
            return 0;
        }
    }
    stage.Catalog(catalog, catalog_count);
#endif
    session.execution = &execution;
#endif
    output = provided.output;
        session.discard = []() {
        SCB_CleanInvalidateDCache_by_Addr(weights, sizeof(weights));
        SCB_CleanInvalidateDCache_by_Addr(blob, blob_capacity);
    #ifdef MODEL_LOADER_MULTI
        SCB_CleanInvalidateDCache_by_Addr(reinterpret_cast<std::uint8_t *>(0x91000000), 0x200000);
    #endif
        __DSB();
        };
    stage.BeforePublish(
        nullptr,
        [](void *,
           const std::uint8_t *weight_data,
           std::size_t weight_bytes,
           const std::uint8_t *blob_data,
           std::size_t blob_bytes) {
            const auto aligned_weights = (weight_bytes + 31U) & ~std::size_t(31U);
            const auto aligned_blob = (blob_bytes + 31U) & ~std::size_t(31U);
            SCB_CleanDCache_by_Addr(const_cast<std::uint8_t *>(weight_data), aligned_weights);
            SCB_CleanDCache_by_Addr(const_cast<std::uint8_t *>(blob_data), aligned_blob);
            __DSB();
            return true;
        }
    );
    stage.BeforeAdopt(
        nullptr,
        [](void *, const std::uint8_t *weight_data, std::size_t weight_bytes,
           const std::uint8_t *blob_data, std::size_t blob_bytes) {
            const auto aligned_weights = (weight_bytes + 31U) & ~std::size_t(31U);
            const auto aligned_blob = (blob_bytes + 31U) & ~std::size_t(31U);
            SCB_InvalidateDCache_by_Addr(const_cast<std::uint8_t *>(weight_data), aligned_weights);
            SCB_InvalidateDCache_by_Addr(const_cast<std::uint8_t *>(blob_data), aligned_blob);
            __DSB();
            return true;
        }
    );
    ready = BSP_XSPI_RAM_Init(0) == BSP_ERROR_NONE && BSP_XSPI_RAM_EnableMemoryMappedMode(0) == BSP_ERROR_NONE;
    if (!capacity) {
        return 0;
    }
    commands[0] = {
        "model", "model list|stat|header <offset> <hex>|begin|adopt|chunk weights|blob <offset> <hex>|commit|input begin|chunk|commit|run|result <offset> <bytes>|abort", Execute, &session
    };
    provided.output.Write(ready ? "MODEL READY\n" : "MODEL ERROR psram-initialization\n");
    return 1;
}

void Tick(
    std::uint32_t milliseconds,
    std::uint32_t
)
{
    if ((stage.Receiving() || session.header_bytes) && milliseconds - session.last_input > session.timeout_ms) {
        stage.Cancel();
        session.header_bytes = 0;
    }
    if (session.execution && session.execution->InputReceiving() && milliseconds - session.last_input > session.timeout_ms) {
        session.execution->InputCancel();
    }
    if (session.execution && session.execution->Tick(milliseconds)) {
        char line[128];
        const auto *model = stage.Verified();
        std::snprintf(line, sizeof(line), "MODEL RESULT npu=%s output_crc=%08lx elapsed_ms=%lu kind=%lu input=%s\n",
                      session.execution->Name(), static_cast<unsigned long>(session.execution->Crc()),
                  static_cast<unsigned long>(session.execution->Elapsed()), static_cast<unsigned long>(model ? model->kind : 0),
                  model && model->tag ? "uploaded" : "zeros");
        output.Write(line);
    }
}
}
