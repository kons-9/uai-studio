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

} // namespace uai::driver
