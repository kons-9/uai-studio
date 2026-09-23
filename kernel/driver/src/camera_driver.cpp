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

DriverStatus CameraDriver::Start(std::uintptr_t first_buffer,
                                 std::uintptr_t second_buffer)
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    if (started_) {
        return DriverStatus::kAlreadyStarted;
    }

    const DriverStatus status =
        arch::StartCamera(first_buffer, second_buffer);
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

std::uintptr_t CameraDriver::TakeCompletedFrame()
{
    return arch::TakeCompletedCameraFrame();
}

} // namespace uai::driver
