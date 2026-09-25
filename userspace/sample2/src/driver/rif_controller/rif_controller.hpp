#ifndef UAI_SAMPLE2_RIF_CONTROLLER_HPP
#define UAI_SAMPLE2_RIF_CONTROLLER_HPP

#include "common/error.hpp"

namespace uai::sample2::driver {

/* Configures STM32N6 Resource Isolation Framework access policy for the
 * peripherals and address regions used by sample2. */
class RifController final {
public:
    common::Error Initialize();

private:
    bool initialized_ = false;
};

} // namespace uai::sample2::driver

#endif
