#ifndef UAI_AI_MIDDLEWARE_BUFFER_INTERRUPT_GUARD_HPP
#define UAI_AI_MIDDLEWARE_BUFFER_INTERRUPT_GUARD_HPP

#include <cstdint>

#if defined(__arm__) || defined(__thumb__)
extern "C" {
#include "stm32n6xx_hal.h"
}
#endif

namespace uai::ai::buffer {

/* Masks interrupts for the lifetime of the object and restores the previous
 * PRIMASK. On non-ARM hosts it is a no-op, so host tests do not prove ISR
 * safety. */
class InterruptGuard final {
public:
    InterruptGuard()
    {
#if defined(__arm__) || defined(__thumb__)
        primask_ = __get_PRIMASK();
        __disable_irq();
#endif
    }

    ~InterruptGuard()
    {
#if defined(__arm__) || defined(__thumb__)
        __set_PRIMASK(primask_);
#endif
    }

    InterruptGuard(const InterruptGuard &) = delete;
    InterruptGuard &operator=(const InterruptGuard &) = delete;

private:
#if defined(__arm__) || defined(__thumb__)
    std::uint32_t primask_ = 0U;
#endif
};

} // namespace uai::ai::buffer

#endif
