#include "driver/psram_driver/registers/psram_registers.hpp"

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
}

namespace uai::ai::psram::registers {

bool PsramRegisterLayer::Initialize()
{
    return BSP_XSPI_RAM_Init(0U) == BSP_ERROR_NONE && BSP_XSPI_RAM_EnableMemoryMappedMode(0U) == BSP_ERROR_NONE;
}

} // namespace uai::ai::psram::registers
