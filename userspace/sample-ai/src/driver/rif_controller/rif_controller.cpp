#include "driver/rif_controller/rif_controller.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::driver {

common::Error RifController::Initialize()
{
    using common::ErrorCode;
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "rif.initialize"};
    }

    __HAL_RCC_RIFSC_CLK_ENABLE();
    __HAL_RCC_RISAF_CLK_ENABLE();
    __HAL_RCC_IAC_CLK_ENABLE();
    __HAL_RCC_IAC_FORCE_RESET();
    __HAL_RCC_IAC_RELEASE_RESET();

    const auto configure_region = [](RISAF_TypeDef *risaf,
                                     std::uint32_t end_address) {
        risaf->REG[0].CFGR = 0U;
        risaf->REG[0].STARTR = 0U;
        risaf->REG[0].ENDR = end_address;
        risaf->REG[0].CIDCFGR = 0x000F000FUL;
        risaf->REG[0].CFGR = 0x00FF0101UL;
    };
    configure_region(RISAF2, 0x000FFFFFUL);
    configure_region(RISAF3, 0x000FFFFFUL);
    configure_region(RISAF4, 0xFFFFFFFFUL);
    configure_region(RISAF5, 0xFFFFFFFFUL);
    configure_region(RISAF6, 0xFFFFFFFFUL);
    configure_region(RISAF7, 0x00063FFFUL);

    RISAF12->REG[0].CFGR = 0U;
    RISAF12->REG[0].STARTR = 0U;
    RISAF12->REG[0].ENDR = RISAF12_LIMIT_ADDRESS_SPACE_SIZE;
    RISAF12->REG[0].CIDCFGR = (RIF_CID_MASK << 16) | RIF_CID_MASK;
    RISAF12->REG[0].CFGR = 0x00FF0101UL;

    RIMC_MasterConfig_t media_master = {};
    media_master.MasterCID = RIF_CID_1;
    media_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    RIMC_MasterConfig_t npu_master = media_master;
    npu_master.MasterCID = RIF_CID_1;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &npu_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2, &media_master);

    constexpr std::uint32_t secure_privileged =
        RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    const std::uint32_t peripherals[] = {
        RIF_RISC_PERIPH_INDEX_XSPI1, RIF_RISC_PERIPH_INDEX_XSPI2,
        RIF_RISC_PERIPH_INDEX_XSPIM, RIF_RISC_PERIPH_INDEX_NPU,
        RIF_RISC_PERIPH_INDEX_DMA2D, RIF_RISC_PERIPH_INDEX_CSI,
        RIF_RISC_PERIPH_INDEX_DCMIPP, RIF_RISC_PERIPH_INDEX_LTDC,
        RIF_RISC_PERIPH_INDEX_LTDCL1, RIF_RISC_PERIPH_INDEX_LTDCL2,
    };
    for (const std::uint32_t peripheral : peripherals) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(peripheral, secure_privileged);
    }
    const std::uint32_t memories[] = {
        RIF_RCC_PERIPH_INDEX_CACHEAXIRAM, RIF_RCC_PERIPH_INDEX_CACHECONFIG,
        RIF_RCC_PERIPH_INDEX_NPURAM0, RIF_RCC_PERIPH_INDEX_NPURAM1,
        RIF_RCC_PERIPH_INDEX_NPURAM2, RIF_RCC_PERIPH_INDEX_NPURAM3,
        RIF_RCC_PERIPH_INDEX_AXISRAM1, RIF_RCC_PERIPH_INDEX_AXISRAM2,
        RIF_RCC_PERIPH_INDEX_FLEXRAM,
    };
    for (const std::uint32_t memory : memories) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(memory, secure_privileged);
    }

    HAL_RIF_IAC_EnableIT(RIF_RISC_PERIPH_INDEX_XSPI2);
    HAL_RIF_IAC_EnableIT(RIF_RISC_PERIPH_INDEX_NPU);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF2);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF3);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF4);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF5);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF7);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF12);
    HAL_NVIC_SetPriority(IAC_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(IAC_IRQn);

    initialized_ = true;
    return {ErrorCode::kOk, 0U, "rif.initialize"};
}

} // namespace uai::ai::driver
