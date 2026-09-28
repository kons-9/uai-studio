#include "driver/nor_driver/nor_driver.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

#ifndef AI_MODEL_NOR_PROBE_OFFSET
#define AI_MODEL_NOR_PROBE_OFFSET 0x00380000U
#endif

namespace uai::ai::nor {

int NorDriver::Initialize()
{
    if (initialized_) {
        return 0;
    }

    const int init_status = registers_.Initialize();
    if (init_status != 0) {
        return -1;
    }

    /* Probe the model weights before switching the NOR to memory-mapped mode. */
    uint8_t model_probe[16] = {};
    const int read_status =
        registers_.Read(model_probe, AI_MODEL_NOR_PROBE_OFFSET,
                        sizeof(model_probe));
    if (read_status != 0) {
        return -1;
    }

    initialized_ = registers_.EnableMemoryMappedMode() == 0;
    return initialized_ ? 0 : -1;
}

void NorDriver::KeepClocksOnSleep() const
{
    __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();
}

} // namespace uai::ai::nor
