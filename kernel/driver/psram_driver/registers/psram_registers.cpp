#include "driver/psram_driver/registers/psram_registers.hpp"

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
extern XSPI_RAM_Ctx_t XSPI_Ram_Ctx[];
}

namespace uai::ai::psram::registers {

bool PsramRegisterLayer::Initialize()
{
    __HAL_RCC_RIFSC_CLK_ENABLE();
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_XSPI1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_XSPIM, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    __HAL_RCC_RISAF_CLK_ENABLE();
    RISAF_BaseRegionConfig_t region{};
    region.Filtering = RISAF_FILTER_ENABLE;
    region.Secure = RIF_ATTRIBUTE_SEC;
    region.PrivWhitelist = RIF_CID_NONE;
    region.ReadWhitelist = RIF_CID_MASK;
    region.WriteWhitelist = RIF_CID_MASK;
    region.EndAddress = RISAF11_LIMIT_ADDRESS_SPACE_SIZE;
    HAL_RIF_RISAF_ConfigBaseRegion(RISAF11_S, RISAF_REGION_1, &region);
    region.Secure = RIF_ATTRIBUTE_NSEC;
    HAL_RIF_RISAF_ConfigBaseRegion(RISAF11_S, RISAF_REGION_2, &region);
    if (XSPI_Ram_Ctx[0].IsInitialized == XSPI_ACCESS_NONE && BSP_XSPI_RAM_Init(0U) != BSP_ERROR_NONE) {
        return false;
    }
    if (XSPI_Ram_Ctx[0].IsInitialized != XSPI_ACCESS_MMP && BSP_XSPI_RAM_EnableMemoryMappedMode(0U) != BSP_ERROR_NONE) {
        return false;
    }
    return XSPI_Ram_Ctx[0].IsInitialized == XSPI_ACCESS_MMP && (XSPI1->CR & XSPI_CR_EN) != 0U
        && (XSPI1->CR & XSPI_CR_FMODE) == XSPI_CR_FMODE;
}

} // namespace uai::ai::psram::registers
