#include "driver_arch.hpp"

#include <cstdint>

#include <tk/tkernel.h>

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6xx_ll_bus.h"
}

namespace uai::driver::arch {

namespace {

bool media_initialized = false;

} // namespace

DriverStatus InitializeMedia()
{
    if (media_initialized) {
        return DriverStatus::kOk;
    }

    LL_MEM_EnableClock(LL_MEM_AXISRAM3);
    LL_MEM_EnableClock(LL_MEM_AXISRAM4);
    LL_MEM_EnableClock(LL_MEM_AXISRAM5);
    LL_MEM_EnableClock(LL_MEM_AXISRAM6);

    RAMCFG_HandleTypeDef ramcfg = {};
    ramcfg.Instance = RAMCFG_SRAM3_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);

    ramcfg.Instance = RAMCFG_SRAM4_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);

    ramcfg.Instance = RAMCFG_SRAM5_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);

    ramcfg.Instance = RAMCFG_SRAM6_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);

    __HAL_RCC_RIFSC_CLK_ENABLE();

    RIMC_MasterConfig_t master = {};
    master.MasterCID = RIF_CID_1;
    master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;

    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2, &master);

    constexpr std::uint32_t secure_privileged =
        RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_DMA2D, secure_privileged);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_DCMIPP, secure_privileged);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_CSI, secure_privileged);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_LTDC, secure_privileged);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_LTDCL1, secure_privileged);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_LTDCL2, secure_privileged);

    media_initialized = true;
    return DriverStatus::kOk;
}

} // namespace uai::driver::arch

/* BSP drivers use HAL timeouts, while µT-Kernel owns the SysTick exception.
 * This object is always linked through InitializeMedia(). */
extern "C" uint32_t HAL_GetTick(void)
{
    SYSTIM time = {};
    if (tk_get_otm(&time) != E_OK) {
        return 0U;
    }
    return time.lo;
}

extern "C" void HAL_Delay(uint32_t delay)
{
    if (delay != 0U) {
        tk_dly_tsk(delay);
    }
}
