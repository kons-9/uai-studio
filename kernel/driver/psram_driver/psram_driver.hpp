#pragma once

#include "driver/psram_driver/registers/psram_registers.hpp"
#include "driver/driver_ownership.hpp"

namespace uai::ai::psram {

class PsramManagement;

class PsramDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    void KeepClocksOnSleep(const Writer &writer) const;
private:
    friend class PsramManagement;
    PsramDriver(driver::ResourceManagement &management) : management_(&management) {}
    ~PsramDriver() = default;
    /* Initialize APS256XX PSRAM and expose it through the memory aperture. */
    bool Initialize();
    driver::ResourceManagement *management_;
    registers::PsramRegisterLayer registers_{};
    bool initialized_ = false;
public:
    void KeepClocksOnSleep() const;
};

class PsramManagement final {
public:
    using Accessor = driver::ResourceAccessor<PsramDriver>;
    static PsramManagement &Instance() { static PsramManagement m; return m; }
    bool Initialize() { return driver_.Initialize(); }
    common::Error Acquire(Accessor *a, TMO timeout = TMO_FEVR)
    { if (!a) return {common::ErrorCode::kInvalidArgument}; *a = {}; Writer w; auto s = ownership_.Acquire(&w, timeout); if (s.Ok()) *a = Accessor(driver_, static_cast<Writer &&>(w)); return s; }
    void KeepClocksOnSleep() const { driver_.KeepClocksOnSleep(); }
    PsramManagement(const PsramManagement &) = delete;
    PsramManagement &operator=(const PsramManagement &) = delete;
private:
    using Writer = PsramDriver::Writer;
    PsramManagement() : driver_(ownership_) {} ~PsramManagement() = default;
    driver::ResourceManagement ownership_{}; PsramDriver driver_;
};

} // namespace uai::ai::psram
