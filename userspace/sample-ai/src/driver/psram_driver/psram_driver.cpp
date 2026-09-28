#include "driver/psram_driver/psram_driver.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::psram {

bool PsramDriver::Initialize()
{
    if (initialized_) {
        return true;
    }

    if (!registers_.Initialize()) {
        return false;
    }

    initialized_ = true;
    return true;
}

void PsramDriver::KeepClocksOnSleep() const
{
    __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();
}

} // namespace uai::ai::psram
