#include "driver/rif_driver/rif_driver.hpp"
#include "driver/rif_driver/registers/rif_registers.hpp"

#include <cstdint>

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::rif {
namespace {


constexpr std::uint32_t kRegionConfiguration = 0x00FF0101UL;
constexpr std::uint32_t kGenericCidConfiguration = 0x000F000FUL;
constexpr std::uint32_t kExternalMemoryCidConfiguration =
    (RIF_CID_MASK << 16U) | RIF_CID_MASK;

void WriteRegister(RISAF_Region_TypeDef *region, uai::ai::rif::registers::RisafRegister address,
                   std::uint32_t value)
{
    switch (address) {
    case uai::ai::rif::registers::RisafRegister::kConfiguration:
        region->CFGR = value;
        break;
    case uai::ai::rif::registers::RisafRegister::kStartAddress:
        region->STARTR = value;
        break;
    case uai::ai::rif::registers::RisafRegister::kEndAddress:
        region->ENDR = value;
        break;
    case uai::ai::rif::registers::RisafRegister::kCidConfiguration:
        region->CIDCFGR = value;
        break;
    default:
        break;
    }
}

void ConfigureRegion(RISAF_TypeDef *risaf, std::uint32_t end_address,
                     std::uint32_t cid_configuration)
{
    RISAF_Region_TypeDef *region = &risaf->REG[0];
    WriteRegister(region, uai::ai::rif::registers::RisafRegister::kConfiguration, 0U);
    WriteRegister(region, uai::ai::rif::registers::RisafRegister::kStartAddress, 0U);
    WriteRegister(region, uai::ai::rif::registers::RisafRegister::kEndAddress, end_address);
    WriteRegister(region, uai::ai::rif::registers::RisafRegister::kCidConfiguration,
                  cid_configuration);
    WriteRegister(region, uai::ai::rif::registers::RisafRegister::kConfiguration,
                  kRegionConfiguration);
}

} // namespace

common::Error RifDriver::Initialize()
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U, "rif.initialize"};
    }
    common::Error management_status = management_.Initialize("rif.management");
    if (!management_status.Ok() &&
        management_status.code != common::ErrorCode::kAlreadyInitialized) {
        return management_status;
    }
    Writer writer;
    management_status = management_.Acquire(&writer);
    if (!management_status.Ok()) return management_status;

    __HAL_RCC_RIFSC_CLK_ENABLE();
    __HAL_RCC_RISAF_CLK_ENABLE();
    __HAL_RCC_IAC_CLK_ENABLE();
    __HAL_RCC_IAC_FORCE_RESET();
    __HAL_RCC_IAC_RELEASE_RESET();

    ConfigureRegion(RISAF2, RISAF2_LIMIT_ADDRESS_SPACE_SIZE,
                    kGenericCidConfiguration);
    ConfigureRegion(RISAF3, RISAF3_LIMIT_ADDRESS_SPACE_SIZE,
                    kGenericCidConfiguration);
    ConfigureRegion(RISAF4, RISAF4_LIMIT_ADDRESS_SPACE_SIZE,
                    kGenericCidConfiguration);
    ConfigureRegion(RISAF5, RISAF5_LIMIT_ADDRESS_SPACE_SIZE,
                    kGenericCidConfiguration);
    ConfigureRegion(RISAF6, RISAF6_LIMIT_ADDRESS_SPACE_SIZE,
                    kGenericCidConfiguration);
    ConfigureRegion(RISAF7, 0x00063FFFUL, kGenericCidConfiguration);

    /* Camera and LCD frame buffers live in the XSPI1 PSRAM aperture
     * (0x90000000, local RISAF11 address space).  RISAF12 is XSPI2/NOR;
     * configuring only RISAF12 leaves DCMIPP's PSRAM writes filtered. */
    ConfigureRegion(RISAF11, RISAF11_LIMIT_ADDRESS_SPACE_SIZE,
                    kExternalMemoryCidConfiguration);

    /* The model is stored in the XSPI2 NOR aperture. */
    ConfigureRegion(RISAF12, RISAF12_LIMIT_ADDRESS_SPACE_SIZE,
                    kExternalMemoryCidConfiguration);

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
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF11);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF12);
    HAL_NVIC_SetPriority(IAC_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(IAC_IRQn);

    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "rif.initialize"};
}

} // namespace uai::ai::rif
