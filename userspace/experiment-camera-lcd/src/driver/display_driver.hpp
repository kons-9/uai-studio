#ifndef UAI_CAMERA_LCD_DISPLAY_DRIVER_HPP
#define UAI_CAMERA_LCD_DISPLAY_DRIVER_HPP

#include "driver/driver_status.hpp"

namespace uai::camera_lcd::driver {

class DisplayDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Process();

private:
    bool initialized_ = false;
};

} // namespace uai::camera_lcd::driver

#endif
