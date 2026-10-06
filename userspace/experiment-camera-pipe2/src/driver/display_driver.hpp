#pragma once

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
