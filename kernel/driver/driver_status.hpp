#pragma once

namespace uai::driver {

enum class DriverStatus {
    kOk,
    kAlreadyInitialized,
    kAlreadyStarted,
    kNotInitialized,
    kNotStarted,
    kHardwareError,
    kBusy,
};

constexpr bool IsOk(DriverStatus status)
{
    return status == DriverStatus::kOk;
}

} // namespace uai::driver
