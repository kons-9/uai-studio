#ifndef UAI_DRIVER_CAMERA_DRIVER_HPP
#define UAI_DRIVER_CAMERA_DRIVER_HPP

#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::driver {

class CameraDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Start();
    DriverStatus Start(std::uintptr_t first_buffer,
                       std::uintptr_t second_buffer);
    DriverStatus Stop();
    DriverStatus Process();
    std::uintptr_t TakeCompletedFrame();

private:
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace uai::driver

#endif
