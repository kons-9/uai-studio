#include "tests/memory_patterns.hpp"

extern "C" {
#include "stm32n6570_discovery_xspi.h"
extern XSPI_RAM_Ctx_t XSPI_Ram_Ctx[];
}

#include <cstdio>

namespace experiment::hwtest::tests::psram_driver {
namespace {
alignas(32) __attribute__((section(".experiment_scratch"))) std::uint8_t scratch[4096];

void ConfigureMemoryAccess()
{
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_XSPI1);
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_XSPIM);
    __HAL_RCC_RISAF_CLK_ENABLE();

    RISAF_BaseRegionConfig_t region{};
    region.Filtering = RISAF_FILTER_ENABLE;
    region.Secure = RIF_ATTRIBUTE_SEC;
    region.PrivWhitelist = RIF_CID_NONE;
    region.ReadWhitelist = RIF_CID_MASK;
    region.WriteWhitelist = RIF_CID_MASK;
    region.StartAddress = 0;
    region.EndAddress = RISAF11_LIMIT_ADDRESS_SPACE_SIZE;
    HAL_RIF_RISAF_ConfigBaseRegion(RISAF11_S, RISAF_REGION_1, &region);
    region.Secure = RIF_ATTRIBUTE_NSEC;
    HAL_RIF_RISAF_ConfigBaseRegion(RISAF11_S, RISAF_REGION_2, &region);
}
}

Result Run(const Context &context)
{
    ConfigureMemoryAccess();

    if (XSPI_Ram_Ctx[0].IsInitialized == XSPI_ACCESS_NONE) {
        const auto init_status = BSP_XSPI_RAM_Init(0);
        if (init_status != BSP_ERROR_NONE) {
            char detail[64];
            std::snprintf(detail, sizeof(detail), "psram-init-bsp=%ld", static_cast<long>(init_status));
            return {Outcome::kFail, detail};
        }
    }
    if (XSPI_Ram_Ctx[0].IsInitialized != XSPI_ACCESS_MMP) {
        const auto mapped_status = BSP_XSPI_RAM_EnableMemoryMappedMode(0);
        if (mapped_status != BSP_ERROR_NONE) {
            char detail[64];
            std::snprintf(detail, sizeof(detail), "psram-mmp-bsp=%ld", static_cast<long>(mapped_status));
            return {Outcome::kFail, detail};
        }
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
    // The busy bit can remain asserted while memory-mapped requests are in
    // flight; the pattern readback below is the functional access check.
    if (XSPI_Ram_Ctx[0].IsInitialized != XSPI_ACCESS_MMP || (control & XSPI_CR_EN) == 0U
        || (control & XSPI_CR_FMODE) != XSPI_CR_FMODE) {
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
