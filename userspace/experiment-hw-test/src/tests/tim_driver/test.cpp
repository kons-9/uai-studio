#include "tests/board.hpp"

namespace experiment::hwtest::tests::tim_driver {

Result Run(const Context &context)
{
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_TIM2);
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    const auto kernel_hz = LL_RCC_CALC_TIMG_FREQ(HAL_RCC_GetSysClockFreq(), __HAL_RCC_GET_TIMCLKPRESCALER());
    if (kernel_hz < 1000000U) {
        __HAL_RCC_TIM2_CLK_DISABLE();
        return {Outcome::kFail, "tim2-kernel-clock"};
    }
    TIM_HandleTypeDef handle{};
    handle.Instance = TIM2;
    handle.Init.Prescaler = kernel_hz / 1000000U - 1;
    handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    handle.Init.Period = 0xffffffffU;
    handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    Result result{Outcome::kFail, "tim2-initialization-or-start"};
    if (HAL_TIM_Base_Init(&handle) == HAL_OK && HAL_TIM_Base_Start(&handle) == HAL_OK) {
        result = {Outcome::kPass, "tim2-counter-rate-and-stop"};
        static constexpr std::uint32_t intervals[] = {25, 100};
        for (const auto interval : intervals) {
            const auto begin = context.clock();
            const auto initial = __HAL_TIM_GET_COUNTER(&handle);
            context.wait(interval);
            const auto elapsed = context.clock() - begin;
            const auto ticks = static_cast<std::uint32_t>(__HAL_TIM_GET_COUNTER(&handle) - initial);
            const auto expected = static_cast<std::uint64_t>(elapsed) * kernel_hz /
                                  (handle.Init.Prescaler + 1) / 1000;
            if (elapsed == 0 || ticks + 2000U < expected || ticks > expected + 2000U) {
                result = {Outcome::kFail, "tim2-counter-rate-mismatch"};
                break;
            }
        }
        if (HAL_TIM_Base_Stop(&handle) != HAL_OK) {
            result = {Outcome::kFail, "tim2-stop"};
        } else {
            const auto stopped = __HAL_TIM_GET_COUNTER(&handle);
            context.wait(5);
            if (__HAL_TIM_GET_COUNTER(&handle) != stopped) { result = {Outcome::kFail, "tim2-running-after-stop"}; }
        }
    }
    if (HAL_TIM_Base_DeInit(&handle) != HAL_OK) { result = {Outcome::kFail, "tim2-deinitialization"}; }
    __HAL_RCC_TIM2_CLK_DISABLE();
    return result;
}

}