#include "tests/board.hpp"

#include <cstdio>

extern "C" {
extern std::uint32_t SystemCoreClock;
}

namespace experiment::hwtest::tests::tim_driver {

namespace {

void TraceClock(const Context &context, std::uint32_t timer_hz)
{
    char line[160];
    std::snprintf(
        line,
        sizeof(line),
        "TRACE tim clock sys=%lu core=%lu hclk=%lu pclk1=%lu timclk=%lu timpre=%lu",
        static_cast<unsigned long>(HAL_RCC_GetSysClockFreq()),
        static_cast<unsigned long>(SystemCoreClock),
        static_cast<unsigned long>(HAL_RCC_GetHCLKFreq()),
        static_cast<unsigned long>(HAL_RCC_GetPCLK1Freq()),
        static_cast<unsigned long>(timer_hz),
        static_cast<unsigned long>(__HAL_RCC_GET_TIMCLKPRESCALER())
    );
    context.Trace(line);

    std::snprintf(
        line,
        sizeof(line),
        "TRACE tim rcc cfgr1=%08lx cfgr2=%08lx ic1=%08lx ic2=%08lx",
        static_cast<unsigned long>(RCC->CFGR1),
        static_cast<unsigned long>(RCC->CFGR2),
        static_cast<unsigned long>(RCC->IC1CFGR),
        static_cast<unsigned long>(RCC->IC2CFGR)
    );
    context.Trace(line);
}

}

Result Run(const Context &context)
{
    const auto sysclk_hz = HAL_RCC_GetSysClockFreq();
    const auto timer_hz = LL_RCC_CALC_TIMG_FREQ(sysclk_hz, __HAL_RCC_GET_TIMCLKPRESCALER());
    TraceClock(context, timer_hz);
    context.Trace("TRACE tim stage=secure-peripheral begin");
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_TIM2);
    context.Trace("TRACE tim stage=secure-peripheral done");

    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    if (timer_hz < 1000000U) {
        __HAL_RCC_TIM2_CLK_DISABLE();
        return {Outcome::kFail, "tim2-kernel-clock"};
    }

    TIM_HandleTypeDef handle{};
    handle.Instance = TIM2;
    handle.Init.Prescaler = timer_hz / 1000000U - 1U;
    handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    handle.Init.Period = 0xffffffffU;
    handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    context.Trace("TRACE tim stage=base-init begin");
    const auto init_status = HAL_TIM_Base_Init(&handle);
    char line[192];
    std::snprintf(
        line,
        sizeof(line),
        "TRACE tim stage=base-init status=%u state=%u psc=%lu arr=%lu",
        static_cast<unsigned>(init_status),
        static_cast<unsigned>(handle.State),
        static_cast<unsigned long>(handle.Init.Prescaler),
        static_cast<unsigned long>(handle.Init.Period)
    );
    context.Trace(line);

    Result result{Outcome::kFail, "tim2-initialization-or-start"};
    if (init_status == HAL_OK) {
        context.Trace("TRACE tim stage=base-start begin");
        const auto start_status = HAL_TIM_Base_Start(&handle);
        std::snprintf(
            line,
            sizeof(line),
            "TRACE tim stage=base-start status=%u state=%u cr1=%08lx psc_reg=%lu cnt=%lu",
            static_cast<unsigned>(start_status),
            static_cast<unsigned>(handle.State),
            static_cast<unsigned long>(TIM2->CR1),
            static_cast<unsigned long>(TIM2->PSC),
            static_cast<unsigned long>(TIM2->CNT)
        );
        context.Trace(line);

        if (start_status == HAL_OK) {
            result = {Outcome::kPass, "tim2-counter-rate-and-stop"};
            const auto started_cr1 = TIM2->CR1;
            const auto started_psc = TIM2->PSC;
            const auto started_arr = TIM2->ARR;
            const auto expected_psc = handle.Init.Prescaler;
            const auto expected_arr = handle.Init.Period;
            const bool started_registers_ok = (started_cr1 & TIM_CR1_CEN) != 0U && started_psc == expected_psc
                                              && started_arr == expected_arr;

            const auto saved_demcr = CoreDebug->DEMCR;
            const auto saved_dwt_ctrl = DWT->CTRL;
            const bool dwt_supported = (saved_dwt_ctrl & DWT_CTRL_NOCYCCNT_Msk) == 0U;
            if (dwt_supported) {
                CoreDebug->DEMCR = saved_demcr | CoreDebug_DEMCR_TRCENA_Msk;
                DWT->CTRL = saved_dwt_ctrl | DWT_CTRL_CYCCNTENA_Msk;
            }
            const bool dwt_enabled = dwt_supported && (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
            std::snprintf(
                line,
                sizeof(line),
                "TRACE tim cycle-counter supported=%u enabled=%u core_hz=%lu",
                static_cast<unsigned>(dwt_supported),
                static_cast<unsigned>(dwt_enabled),
                static_cast<unsigned long>(SystemCoreClock)
            );
            context.Trace(line);

            static constexpr std::uint32_t intervals[] = {25U, 100U};
            for (const auto interval : intervals) {
                const auto cycles_begin = dwt_enabled ? DWT->CYCCNT : 0U;
                const auto begin = context.clock();
                const auto initial = __HAL_TIM_GET_COUNTER(&handle);
                context.wait(interval);
                const auto cycles = dwt_enabled ? static_cast<std::uint32_t>(DWT->CYCCNT - cycles_begin) : 0U;
                const auto elapsed = context.clock() - begin;
                const auto ticks = static_cast<std::uint32_t>(__HAL_TIM_GET_COUNTER(&handle) - initial);
                const auto expected = dwt_enabled
                                          ? static_cast<std::uint64_t>(cycles) * timer_hz / SystemCoreClock
                                                / (handle.Init.Prescaler + 1U)
                                          : static_cast<std::uint64_t>(elapsed) * timer_hz
                                                / (handle.Init.Prescaler + 1U) / 1000U;
                const auto measured_core_ms = dwt_enabled
                                                  ? static_cast<std::uint64_t>(cycles) * 1000U / SystemCoreClock
                                                  : 0U;
                std::snprintf(
                    line,
                    sizeof(line),
                    "TRACE tim sample request=%lu hal_ms=%lu core_ms=%llu ticks=%lu expect=%llu cycles=%lu",
                    static_cast<unsigned long>(interval),
                    static_cast<unsigned long>(elapsed),
                    static_cast<unsigned long long>(measured_core_ms),
                    static_cast<unsigned long>(ticks),
                    static_cast<unsigned long long>(expected),
                    static_cast<unsigned long>(cycles)
                );
                context.Trace(line);

                if ((dwt_enabled ? cycles == 0U : elapsed == 0U)
                    || ticks + 2000U < expected || ticks > expected + 2000U) {
                    static char detail[112];
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "rate interval=%lu hal_ms=%lu core_ms=%llu ticks=%lu expected=%llu clk=%lu div=%lu",
                        static_cast<unsigned long>(interval),
                        static_cast<unsigned long>(elapsed),
                        static_cast<unsigned long long>(measured_core_ms),
                        static_cast<unsigned long>(ticks),
                        static_cast<unsigned long long>(expected),
                        static_cast<unsigned long>(timer_hz),
                        static_cast<unsigned long>(handle.Init.Prescaler + 1U)
                    );
                    result = {Outcome::kFail, detail};
                    break;
                }
            }

            if (HAL_TIM_Base_Stop(&handle) != HAL_OK) {
                result = {Outcome::kFail, "tim2-stop"};
            } else {
                const auto stopped = __HAL_TIM_GET_COUNTER(&handle);
                context.wait(5U);
                const auto after_wait = __HAL_TIM_GET_COUNTER(&handle);
                std::snprintf(
                    line,
                    sizeof(line),
                    "TRACE tim stop stopped=%lu after_wait=%lu",
                    static_cast<unsigned long>(stopped),
                    static_cast<unsigned long>(after_wait)
                );
                context.Trace(line);
                if (after_wait != stopped) {
                    result = {Outcome::kFail, "tim2-running-after-stop"};
                }
            }
            if (!started_registers_ok || (TIM2->CR1 & TIM_CR1_CEN) != 0U) {
                static char detail[96];
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "tim-reg cr1=%08lx/%08lx psc=%lu/%lu arr=%lu/%lu",
                    static_cast<unsigned long>(TIM2->CR1),
                    static_cast<unsigned long>(started_cr1),
                    static_cast<unsigned long>(started_psc),
                    static_cast<unsigned long>(expected_psc),
                    static_cast<unsigned long>(started_arr),
                    static_cast<unsigned long>(expected_arr)
                );
                result = {Outcome::kFail, detail};
            }

            if (dwt_supported) {
                DWT->CTRL = saved_dwt_ctrl;
                CoreDebug->DEMCR = saved_demcr;
            }
        }
    }

    context.Trace("TRACE tim stage=deinit begin");
    const auto deinit_status = HAL_TIM_Base_DeInit(&handle);
    std::snprintf(
        line,
        sizeof(line),
        "TRACE tim stage=deinit status=%u state=%u",
        static_cast<unsigned>(deinit_status),
        static_cast<unsigned>(handle.State)
    );
    context.Trace(line);
    if (deinit_status != HAL_OK) {
        result = {Outcome::kFail, "tim2-deinitialization"};
    }
    __HAL_RCC_TIM2_CLK_DISABLE();
    return result;
}

}
