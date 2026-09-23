#include "driver/display_driver.hpp"

#include "driver_arch.hpp"

namespace uai::driver {

DriverStatus DisplayDriver::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    const DriverStatus status = arch::InitializeDisplay();
    if (IsOk(status)) {
        initialized_ = true;
    }
    return status;
}

DriverStatus DisplayDriver::Process()
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    return arch::ProcessDisplay();
}

DriverStatus DisplayDriver::Process(std::uintptr_t buffer)
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    return arch::ProcessDisplay(buffer);
}

} // namespace uai::driver
