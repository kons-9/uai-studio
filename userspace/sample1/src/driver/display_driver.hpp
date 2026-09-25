#ifndef UAI_SAMPLE1_DISPLAY_DRIVER_HPP
#define UAI_SAMPLE1_DISPLAY_DRIVER_HPP

#include "driver/driver_status.hpp"

namespace uai::sample1::driver {

class DisplayDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Process();

private:
    bool initialized_ = false;
};

} // namespace uai::sample1::driver

#endif
