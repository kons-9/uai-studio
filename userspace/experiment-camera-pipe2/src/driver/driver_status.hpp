#ifndef UAI_CAMERA_PIPE2_DRIVER_STATUS_HPP
#define UAI_CAMERA_PIPE2_DRIVER_STATUS_HPP

namespace uai::camera_pipe2::driver {

enum class DriverStatus {
    kOk = 0,
    kAlreadyInitialized,
    kNotInitialized,
    kAlreadyStarted,
    kNotStarted,
    kHardwareFailure,
};

constexpr bool IsOk(DriverStatus status)
{
    return status == DriverStatus::kOk;
}

} // namespace uai::camera_pipe2::driver

#endif
