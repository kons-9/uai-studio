#ifndef UAI_DRIVER_STM32N6570_DK_ARCH_HPP
#define UAI_DRIVER_STM32N6570_DK_ARCH_HPP

#include "driver/driver_status.hpp"

namespace uai::driver::arch {

DriverStatus InitializeMedia();
DriverStatus InitializeDisplay();
DriverStatus InitializeCamera();
DriverStatus StartCamera();
DriverStatus ProcessCamera();

} // namespace uai::driver::arch

#endif
