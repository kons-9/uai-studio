#include "driver/board/time.hpp"
extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::driver::board {
std::uint32_t Milliseconds()
{
    return HAL_GetTick();
}
void EnableCycleCounter()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
}