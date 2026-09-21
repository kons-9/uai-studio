#include <cstdint>

#include <tk/tkernel.h>

extern "C" {
#include <tm/tmonitor.h>
}

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6xx_ll_bus.h"
#include "stm32n6570_discovery_camera.h"
#include "stm32n6570_discovery_lcd.h"
}

namespace {

constexpr uintptr_t kCameraFrameBuffer = 0x34200000UL;

void halt_with_message(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

void enable_media_sram()
{
    LL_MEM_EnableClock(LL_MEM_AXISRAM3);
    LL_MEM_EnableClock(LL_MEM_AXISRAM4);

    RAMCFG_HandleTypeDef ramcfg = {};
    ramcfg.Instance = RAMCFG_SRAM3_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);

    ramcfg.Instance = RAMCFG_SRAM4_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);
}

void configure_media_security()
{
    __HAL_RCC_RIFSC_CLK_ENABLE();

    RIMC_MasterConfig_t master = {};
    master.MasterCID = RIF_CID_1;
    master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;

    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2, &master);

    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_DMA2D, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_DCMIPP, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_CSI, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_LTDC, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_LTDCL1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_LTDCL2, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
}

} // namespace

/* BSP drivers use HAL timeouts, while µT-Kernel owns the SysTick exception. */
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

extern "C" HAL_StatusTypeDef MX_LTDC_ClockConfig(LTDC_HandleTypeDef *hltdc)
{
    UNUSED(hltdc);

    // sample0 enables PLL1 but not PLL4. Use PLL1's 1200 MHz VCO divided by
    // 48 to provide the 25 MHz pixel clock expected by the RK050HR18 panel.
    RCC_PeriphCLKInitTypeDef clock = {};
    clock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    clock.LtdcClockSelection = RCC_LTDCCLKSOURCE_IC16;
    clock.ICSelection[RCC_IC16].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC16].ClockDivider = 48;
    return HAL_RCCEx_PeriphCLKConfig(&clock);
}

extern "C" INT usermain(void)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "sample1: initializing LCD and IMX335 camera\n")));

    enable_media_sram();
    configure_media_security();

    if (BSP_LCD_Init(0, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        halt_with_message("sample1: BSP_LCD_Init failed\n");
    }
    BSP_LCD_DisplayOn(0);

    if (BSP_CAMERA_Init(0, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) !=
        BSP_ERROR_NONE) {
        halt_with_message("sample1: BSP_CAMERA_Init failed\n");
    }

    if (BSP_CAMERA_Start(0, reinterpret_cast<uint8_t *>(kCameraFrameBuffer),
                         CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE) {
        halt_with_message("sample1: BSP_CAMERA_Start failed\n");
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "sample1: camera preview started\n")));

    for (;;) {
        if (BSP_CAMERA_BackgroundProcess() != BSP_ERROR_NONE) {
            halt_with_message("sample1: camera background process failed\n");
        }
        tk_dly_tsk(1);
    }
}
