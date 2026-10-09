#include "driver/nor_driver/nor_driver.hpp"
#include "sample_ai_config.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

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
    const int read_status = registers_.Read(model_probe, config::kModelNorProbeOffset, sizeof(model_probe));
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
