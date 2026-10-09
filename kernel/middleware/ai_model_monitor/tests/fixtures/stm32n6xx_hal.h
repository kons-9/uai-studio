#pragma once

#include <cstdint>
std::uint32_t __get_PRIMASK();
void __disable_irq();
void __set_PRIMASK(std::uint32_t primask);
inline void SCB_CleanDCache_by_Addr(
    std::uint32_t *,
    std::int32_t
)
{}
