#include "driver/psram_driver/psram_driver.hpp"

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
}

namespace uai::ai::driver::xspi {

bool PsramDriver::Initialize()
{
    if (initialized_) {
        return true;
    }

    if (BSP_XSPI_RAM_Init(0U) != BSP_ERROR_NONE ||
        BSP_XSPI_RAM_EnableMemoryMappedMode(0U) != BSP_ERROR_NONE) {
        return false;
    }

    initialized_ = true;
    return true;
}

} // namespace uai::ai::driver::xspi
