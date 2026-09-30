#ifndef UAI_AI_DRIVER_STATUS_HPP
#define UAI_AI_DRIVER_STATUS_HPP

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

#endif
