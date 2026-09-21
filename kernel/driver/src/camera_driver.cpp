#include "driver/camera_driver.hpp"

#include "driver_arch.hpp"

namespace uai::driver {

DriverStatus CameraDriver::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    const DriverStatus status = arch::InitializeCamera();
    if (IsOk(status)) {
        initialized_ = true;
    }
    return status;
}

DriverStatus CameraDriver::Start()
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    if (started_) {
        return DriverStatus::kAlreadyStarted;
    }

    const DriverStatus status = arch::StartCamera();
    if (IsOk(status)) {
        started_ = true;
    }
    return status;
}

DriverStatus CameraDriver::Process()
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    if (!started_) {
        return DriverStatus::kNotStarted;
    }
    return arch::ProcessCamera();
}

} // namespace uai::driver
