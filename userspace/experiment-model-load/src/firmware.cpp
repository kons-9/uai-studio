#include "extension.hpp"
#include "protocol.hpp"
#include "model_expected.hpp"
extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_xspi.h"
}

namespace experiment {
namespace {
alignas(32) __attribute__((section(".experiment_weights"))) std::uint8_t weights[0x10000];
alignas(32) __attribute__((section(".experiment_blob"))) std::uint8_t blob[0x10000];
model::Manifest expected{};
model::Staging stage(
    {0x91010000,
     sizeof(weights),
     weights},
    {0x91020000,
     sizeof(blob),
     blob},
    expected
);
model::Session session{&stage, HAL_GetTick};
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
    stage = model::Staging({0x91010000, sizeof(weights), weights}, {0x91020000, sizeof(blob), blob}, expected);
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
    ready = BSP_XSPI_RAM_Init(0) == BSP_ERROR_NONE && BSP_XSPI_RAM_EnableMemoryMappedMode(0) == BSP_ERROR_NONE;
    if (!capacity) {
        return 0;
    }
    commands[0] = {
        "model", "model stat|begin <manifest-hex>|chunk weights|blob <offset> <hex>|commit|abort", Execute, &session
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
}
}