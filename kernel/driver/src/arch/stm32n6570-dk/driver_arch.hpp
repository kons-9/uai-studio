#ifndef UAI_DRIVER_STM32N6570_DK_ARCH_HPP
#define UAI_DRIVER_STM32N6570_DK_ARCH_HPP

#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::driver::arch {

DriverStatus InitializeMedia();
DriverStatus InitializeDisplay();
DriverStatus InitializeCamera();
DriverStatus StartCamera();
DriverStatus ProcessCamera();
std::uintptr_t TakeCompletedCameraFrame();
DriverStatus ProcessDisplay();

} // namespace uai::driver::arch

#endif
