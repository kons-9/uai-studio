#ifndef UAI_AI_PSRAM_DRIVER_HPP
#define UAI_AI_PSRAM_DRIVER_HPP

#include "driver/psram_driver/registers/psram_registers.hpp"
#include "driver/driver_ownership.hpp"

namespace uai::ai::psram {

class PsramDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    /* Initialize APS256XX PSRAM and expose it through the memory aperture. */
    bool Initialize();
    common::Error AcquireWriter(Writer *writer, TMO timeout = TMO_FEVR) const
    { return management_.Acquire(writer, timeout); }
    void KeepClocksOnSleep() const;
    void KeepClocksOnSleep(const Writer &writer) const;

private:
    driver::ResourceManagement management_{};
    registers::PsramRegisterLayer registers_{};
    bool initialized_ = false;
};

} // namespace uai::ai::psram

#endif // UAI_AI_PSRAM_DRIVER_HPP
