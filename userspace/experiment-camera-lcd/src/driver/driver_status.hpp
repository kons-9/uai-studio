#pragma once

namespace uai::camera_lcd::driver {

enum class DriverStatus {
    kOk = 0,
    kAlreadyInitialized,
    kNotInitialized,
    kAlreadyStarted,
    kNotStarted,
    kInvalidArgument,
    kHardwareFailure,
};

constexpr bool IsOk(DriverStatus status)
{
    return status == DriverStatus::kOk;
}

} // namespace uai::camera_lcd::driver
