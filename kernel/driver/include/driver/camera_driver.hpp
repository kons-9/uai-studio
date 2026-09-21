#ifndef UAI_DRIVER_CAMERA_DRIVER_HPP
#define UAI_DRIVER_CAMERA_DRIVER_HPP

#include "driver/driver_status.hpp"

namespace uai::driver {

class CameraDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Start();
    DriverStatus Process();

private:
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::driver

#endif
