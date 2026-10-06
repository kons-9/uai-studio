#pragma once

#include "driver/nor_driver/registers/nor_registers.hpp"

namespace uai::ai::nor {

/* Application-facing C++ driver for the STM32N6570-DK model NOR.
 * The board-specific HAL work remains in the STM32 BSP C implementation. */
class NorDriver final {
public:
    /* Initialize the NOR, verify the model-data aperture, and enable mapping.
     * Call after PSRAM initialization because BSP_XSPI_RAM_Init resets XSPIM. */
    int Initialize();
    void KeepClocksOnSleep() const;

private:
    registers::NorRegisterLayer registers_{};
    bool initialized_ = false;
};

} // namespace uai::ai::nor
