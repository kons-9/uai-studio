#include "driver/psram_driver/psram_driver.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::psram {

bool PsramDriver::Initialize()
{
    if (initialized_) {
        return true;
    }

    common::Error management_status = management_->Initialize("psram.management");
    if (!management_status.Ok() &&
        management_status.code != common::ErrorCode::kAlreadyInitialized) {
        return false;
    }
    Writer writer;
    if (!management_->Acquire(&writer).Ok()) return false;

    if (!registers_.Initialize()) {
        return false;
    }

    initialized_ = true;
    return true;
}

void PsramDriver::KeepClocksOnSleep() const
{
    Writer writer;
    if (!management_->Acquire(&writer).Ok()) return;
    KeepClocksOnSleep(writer);
}

void PsramDriver::KeepClocksOnSleep(const Writer &writer) const
{
    if (!management_->Validate(writer, "psram.keep_clocks").Ok()) return;
    __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();
}

} // namespace uai::ai::psram
