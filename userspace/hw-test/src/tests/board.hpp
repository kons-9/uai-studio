#pragma once

#include "hwtest.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace experiment::hwtest::tests {

inline void SecurePeripheral(std::uint32_t peripheral)
{
    __HAL_RCC_RIFSC_CLK_ENABLE();
    HAL_RIF_RISC_SetSlaveSecureAttributes(peripheral, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
}

}