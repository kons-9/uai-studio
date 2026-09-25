#ifndef UAI_SAMPLE1_CAMERA_DRIVER_HPP
#define UAI_SAMPLE1_CAMERA_DRIVER_HPP

#include "driver/driver_status.hpp"

namespace uai::sample1::driver {

class CameraDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Start();
    DriverStatus Process();

private:
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::sample1::driver

#endif
