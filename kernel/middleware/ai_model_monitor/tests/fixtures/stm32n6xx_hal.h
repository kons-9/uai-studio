#ifndef UAI_TEST_STM32_HAL_H
#define UAI_TEST_STM32_HAL_H

#include <cstdint>
std::uint32_t __get_PRIMASK();
void __disable_irq();
void __set_PRIMASK(std::uint32_t primask);
inline void SCB_CleanDCache_by_Addr(std::uint32_t *, std::int32_t) {}

#endif