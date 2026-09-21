#ifndef UAI_DRIVER_DISPLAY_DRIVER_HPP
#define UAI_DRIVER_DISPLAY_DRIVER_HPP

#include "driver/driver_status.hpp"

namespace uai::driver {

class DisplayDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Process();

private:
    bool initialized_ = false;
};

} // namespace uai::driver

#endif
