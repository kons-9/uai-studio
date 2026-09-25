#ifndef UAI_AI_RIF_CONTROLLER_HPP
#define UAI_AI_RIF_CONTROLLER_HPP

#include "common/error.hpp"
#include "driver/rif_controller/rif_hardware.hpp"

namespace uai::ai::driver {

/* Configures STM32N6 Resource Isolation Framework access policy for the
 * peripherals and address regions used by ai. */
class RifController final {
public:
    common::Error Initialize();

private:
    RifHardware hardware_{};
    bool initialized_ = false;
};

} // namespace uai::ai::driver

#endif
