#ifndef UAI_DRIVER_STM32N6570_DK_ARCH_HPP
#define UAI_DRIVER_STM32N6570_DK_ARCH_HPP

#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::driver::arch {

DriverStatus InitializeMedia();
DriverStatus InitializeDisplay();
DriverStatus SynchronizeDisplay();
DriverStatus InitializeCamera();
DriverStatus StartCamera();
DriverStatus StartCamera(std::uintptr_t first_buffer,
                         std::uintptr_t second_buffer);
DriverStatus StopCamera();
DriverStatus ProcessCamera();
std::uintptr_t TakeCompletedCameraFrame();
DriverStatus ProcessDisplay();
DriverStatus ProcessDisplay(std::uintptr_t buffer);

} // namespace uai::driver::arch

#endif
