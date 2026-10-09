#include "tests/memory_patterns.hpp"

extern "C" {
#include "stm32n6570_discovery_xspi.h"
}

namespace experiment::hwtest::tests::psram_driver {
namespace {
alignas(32) __attribute__((section(".experiment_scratch"))) std::uint8_t scratch[4096];
}

Result Run(const Context &)
{
    if (BSP_XSPI_RAM_Init(0) != BSP_ERROR_NONE || BSP_XSPI_RAM_EnableMemoryMappedMode(0) != BSP_ERROR_NONE) {
        return {Outcome::kFail, "psram-initialization"};
    }
    return CheckMemoryPatterns(scratch, sizeof(scratch), true);
}

}