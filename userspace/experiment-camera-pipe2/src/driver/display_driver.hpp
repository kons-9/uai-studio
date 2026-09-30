#ifndef UAI_CAMERA_PIPE2_DISPLAY_DRIVER_HPP
#define UAI_CAMERA_PIPE2_DISPLAY_DRIVER_HPP

#include "driver/driver_status.hpp"

namespace uai::camera_pipe2::driver {

class DisplayDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Process();

private:
    bool initialized_ = false;
};

} // namespace uai::camera_pipe2::driver

#endif
