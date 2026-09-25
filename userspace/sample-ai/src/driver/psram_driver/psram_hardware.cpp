#include "driver/psram_driver/psram_hardware.hpp"

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
}

namespace uai::ai::driver::xspi {

bool PsramHardware::Initialize()
{
    return BSP_XSPI_RAM_Init(0U) == BSP_ERROR_NONE &&
           BSP_XSPI_RAM_EnableMemoryMappedMode(0U) == BSP_ERROR_NONE;
}

} // namespace uai::ai::driver::xspi
