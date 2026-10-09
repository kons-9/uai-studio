#include "extension.hpp"
#include "protocol.hpp"
#include "model_expected.hpp"
#ifdef EXPERIMENT_MODEL_NPU
#include "npu_backend.hpp"
#endif
extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_xspi.h"
}

namespace experiment {
namespace {
alignas(32) __attribute__((section(".experiment_weights"))) std::uint8_t weights[0x140000];
#ifdef EXPERIMENT_MODEL_NPU
auto *const blob = reinterpret_cast<std::uint8_t *>(0x91340000);
#else
alignas(32) __attribute__((section(".experiment_blob"))) std::uint8_t blob[0x40000];
#endif
constexpr std::size_t blob_capacity = 0x40000;
model::Manifest expected{};
model::Staging stage(
    {0x91200000,
     sizeof(weights),
     weights},
    {0x91340000,
        blob_capacity,
     blob},
    expected
);
model::Session session{&stage, HAL_GetTick};
#ifdef EXPERIMENT_MODEL_NPU
model::NpuBackend backend;
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
    stage = model::Staging({0x91200000, sizeof(weights), weights}, {0x91340000, blob_capacity, blob}, expected);
#ifdef EXPERIMENT_MODEL_NPU
    session.execution = &execution;
#endif
    output = provided.output;
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
        "model", "model stat|verify|run|adopt <manifest-hex>|begin <manifest-hex>|chunk weights|blob <offset> <hex>|commit|abort", Execute, &session
    };
    provided.output.Write(ready ? "MODEL READY\n" : "MODEL ERROR psram-initialization\n");
    return 1;
}

void Tick(
    std::uint32_t milliseconds,
    std::uint32_t
)
{
    if (stage.Receiving() && milliseconds - session.last_input > session.timeout_ms) {
        stage.Cancel();
    }
    if (session.execution && session.execution->Tick(milliseconds)) {
        char line[128];
        std::snprintf(line, sizeof(line), "MODEL RESULT npu=%s output_crc=%08lx elapsed_ms=%lu input=zeros\n",
                      session.execution->Name(), static_cast<unsigned long>(session.execution->Crc()),
                      static_cast<unsigned long>(session.execution->Elapsed()));
        output.Write(line);
    }
}
}
