#include "driver/nor_driver/nor_driver.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>
}

namespace uai::ai::driver::xspi {
namespace {

void DebugPrint(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
}

} // namespace

int NorDriver::Initialize()
{
    if (initialized_) {
        return 0;
    }

    /* Configure the board's MX66UW1G45G in OPI-DTR mode. */
    BSP_XSPI_NOR_Init_t nor_init = {};
    nor_init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
    nor_init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;

    const int32_t init_status = BSP_XSPI_NOR_Init(0U, &nor_init);
    char message[160];
    (void)std::snprintf(
        message, sizeof(message),
        "boot: nor bsp init=%ld stage=%ld reset=%ld/%ld err=%lx sr=%lx cr=%lx iom=%lx\r\n",
        static_cast<long>(init_status), static_cast<long>(uai_nor_bsp_stage),
        static_cast<long>(uai_nor_reset_stage),
        static_cast<long>(uai_nor_reset_result),
        static_cast<unsigned long>(uai_nor_reset_hal_error),
        static_cast<unsigned long>(uai_nor_reset_sr),
        static_cast<unsigned long>(uai_nor_reset_cr),
        static_cast<unsigned long>(XSPIM->CR));
    DebugPrint(message);
    if (init_status != BSP_ERROR_NONE) {
        return -1;
    }

    /* Probe the model weights before switching the NOR to memory-mapped mode. */
    uint8_t model_probe[16] = {};
    const int32_t read_status =
        BSP_XSPI_NOR_Read(0U, model_probe, 0x00380000U, sizeof(model_probe));
    uint32_t probe_words[4] = {};
    std::memcpy(probe_words, model_probe, sizeof(probe_words));
    (void)std::snprintf(
        message, sizeof(message),
        "boot: nor indirect read=%ld data=%lx,%lx,%lx,%lx sr=%lx\r\n",
        static_cast<long>(read_status), static_cast<unsigned long>(probe_words[0]),
        static_cast<unsigned long>(probe_words[1]),
        static_cast<unsigned long>(probe_words[2]),
        static_cast<unsigned long>(probe_words[3]),
        static_cast<unsigned long>(XSPI2->SR));
    DebugPrint(message);
    if (read_status != BSP_ERROR_NONE) {
        return -1;
    }

    const int32_t map_status = BSP_XSPI_NOR_EnableMemoryMappedMode(0U);
    (void)std::snprintf(
        message, sizeof(message),
        "boot: nor bsp map=%ld stage=%ld sr=%lx cr=%lx\r\n",
        static_cast<long>(map_status), static_cast<long>(uai_nor_bsp_stage),
        static_cast<unsigned long>(XSPI2->SR),
        static_cast<unsigned long>(XSPI2->CR));
    DebugPrint(message);

    initialized_ = map_status == BSP_ERROR_NONE;
    return initialized_ ? 0 : -1;
}

} // namespace uai::ai::driver::xspi
