#ifndef UAI_AI_PSRAM_DRIVER_HPP
#define UAI_AI_PSRAM_DRIVER_HPP

#include "driver/psram_driver/registers/psram_registers.hpp"

namespace uai::ai::psram {

class PsramDriver final {
public:
    /* Initialize APS256XX PSRAM and expose it through the memory aperture. */
    bool Initialize();
    void KeepClocksOnSleep() const;

private:
    registers::PsramRegisterLayer registers_{};
    bool initialized_ = false;
};

} // namespace uai::ai::psram

#endif // UAI_AI_PSRAM_DRIVER_HPP
