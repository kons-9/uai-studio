#ifndef UAI_CAMERA_LCD_DRIVER_STATUS_HPP
#define UAI_CAMERA_LCD_DRIVER_STATUS_HPP

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

#endif
