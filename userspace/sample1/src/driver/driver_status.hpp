#ifndef UAI_SAMPLE1_DRIVER_STATUS_HPP
#define UAI_SAMPLE1_DRIVER_STATUS_HPP

namespace uai::sample1::driver {

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

} // namespace uai::sample1::driver

#endif
