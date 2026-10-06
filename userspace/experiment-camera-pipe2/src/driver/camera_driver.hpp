#pragma once

#include "driver/driver_status.hpp"

namespace uai::camera_pipe2::driver {

class CameraDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Start();
    DriverStatus Process();

private:
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::camera_pipe2::driver
