#ifndef UAI_AI_RIF_HARDWARE_HPP
#define UAI_AI_RIF_HARDWARE_HPP

#include "common/error.hpp"

namespace uai::ai::driver {

/* STM32N6 RIF/RISAF register and HAL boundary.  The access policy is a board
 * configuration detail and is intentionally not exposed to callers. */
class RifHardware final {
public:
    common::Error Initialize();

private:
    bool initialized_ = false;
};

} // namespace uai::ai::driver

#endif // UAI_AI_RIF_HARDWARE_HPP
