#include "driver/psram_driver/psram_driver.hpp"

namespace uai::ai::driver::xspi {

bool PsramDriver::Initialize()
{
    if (initialized_) {
        return true;
    }

    if (!hardware_.Initialize()) {
        return false;
    }

    initialized_ = true;
    return true;
}

} // namespace uai::ai::driver::xspi
