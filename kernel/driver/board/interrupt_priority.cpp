#include "driver/board/interrupt_priority.hpp"

#include <cstdint>

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::driver::board {

void ConfigureReferenceInterruptPriorities()
{
    std::uint32_t preempt_priority = 0U;
    std::uint32_t sub_priority = 0U;
    HAL_NVIC_GetPriority(SysTick_IRQn, HAL_NVIC_GetPriorityGrouping(),
                         &preempt_priority, &sub_priority);
    for (IRQn_Type irq = PVD_PVM_IRQn; irq <= LTDC_UP_ERR_IRQn;
         irq = static_cast<IRQn_Type>(static_cast<std::int32_t>(irq) + 1)) {
        HAL_NVIC_SetPriority(irq, preempt_priority, sub_priority);
    }
}

} // namespace uai::ai::driver::board