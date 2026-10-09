#include "tests/memory_patterns.hpp"

extern "C" {
#include "stm32n6570_discovery_xspi.h"
extern XSPI_RAM_Ctx_t XSPI_Ram_Ctx[];
}

#include <cstdio>

namespace experiment::hwtest::tests::psram_driver {
namespace {
alignas(32) __attribute__((section(".experiment_scratch"))) std::uint8_t scratch[4096];
}

Result Run(const Context &context)
{
    const auto init_status = BSP_XSPI_RAM_Init(0);
    if (init_status != BSP_ERROR_NONE) {
        char detail[64];
        std::snprintf(detail, sizeof(detail), "psram-init-bsp=%ld", static_cast<long>(init_status));
        return {Outcome::kFail, detail};
    }
    const auto mapped_status = BSP_XSPI_RAM_EnableMemoryMappedMode(0);
    if (mapped_status != BSP_ERROR_NONE) {
        char detail[64];
        std::snprintf(detail, sizeof(detail), "psram-mmp-bsp=%ld", static_cast<long>(mapped_status));
        return {Outcome::kFail, detail};
    }
    const auto control = XSPI1->CR;
    const auto status = XSPI1->SR;
    char trace[96];
    std::snprintf(
        trace,
        sizeof(trace),
        "TRACE psram regs ctx=%u cr=%08lx sr=%08lx dcr1=%08lx",
        static_cast<unsigned>(XSPI_Ram_Ctx[0].IsInitialized),
        static_cast<unsigned long>(control),
        static_cast<unsigned long>(status),
        static_cast<unsigned long>(XSPI1->DCR1)
    );
    context.Trace(trace);
    if (XSPI_Ram_Ctx[0].IsInitialized != XSPI_ACCESS_MMP || (control & XSPI_CR_EN) == 0U
        || (control & XSPI_CR_FMODE) != XSPI_CR_FMODE || (status & XSPI_SR_BUSY) != 0U) {
        static char detail[96];
        std::snprintf(
            detail,
            sizeof(detail),
            "psram-reg ctx=%u cr=%08lx sr=%08lx",
            static_cast<unsigned>(XSPI_Ram_Ctx[0].IsInitialized),
            static_cast<unsigned long>(control),
            static_cast<unsigned long>(status)
        );
        return {Outcome::kFail, detail};
    }
    return CheckMemoryPatterns(scratch, sizeof(scratch), true);
}

}
