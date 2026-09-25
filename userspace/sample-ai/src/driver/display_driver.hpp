#ifndef UAI_AI_DISPLAY_DRIVER_HPP
#define UAI_AI_DISPLAY_DRIVER_HPP

#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::driver {

class DisplayDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Synchronize();
    DriverStatus Process();
    DriverStatus Process(std::uintptr_t buffer);

private:
    bool initialized_ = false;
};

} // namespace uai::driver

#endif
