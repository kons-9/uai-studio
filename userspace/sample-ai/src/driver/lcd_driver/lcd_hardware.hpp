#ifndef UAI_AI_LCD_HARDWARE_HPP
#define UAI_AI_LCD_HARDWARE_HPP

#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::driver {

/* LCD BSP boundary.  Rendering and frame composition belong to LcdDriver;
 * this class only controls the display peripheral and its active buffer. */
class LcdHardware final {
public:
    DriverStatus Initialize();
    DriverStatus Synchronize();
    DriverStatus Process();
    DriverStatus Process(std::uintptr_t buffer);

private:
    bool initialized_ = false;
    bool reload_pending_ = false;
};

} // namespace uai::driver

#endif // UAI_AI_LCD_HARDWARE_HPP
