#include "tests/board.hpp"

#include <cstdio>

namespace experiment::hwtest::tests::rtc_driver {

Result Run(const Context &context)
{
    SecurePeripheral(RIF_RCC_PERIPH_INDEX_RTC);
    HAL_PWR_EnableBkUpAccess();
    RCC_OscInitTypeDef oscillator{};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    oscillator.LSIState = RCC_LSI_ON;
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    clock.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK || HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) {
        HAL_PWR_DisableBkUpAccess();
        return {Outcome::kFail, "rtc-lsi-clock"};
    }
    __HAL_RCC_RTC_CLK_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_RTC_FORCE_RESET();
    __HAL_RCC_RTC_RELEASE_RESET();
    RTC_HandleTypeDef handle{};
    handle.Instance = RTC;
    handle.Init.HourFormat = RTC_HOURFORMAT_24;
    handle.Init.AsynchPrediv = 127;
    handle.Init.SynchPrediv = LSI_VALUE / 128 - 1;
    handle.Init.OutPut = RTC_OUTPUT_DISABLE;
    handle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    handle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    handle.Init.BinMode = RTC_BINARY_NONE;
    RTC_TimeTypeDef time{};
    time.Hours = 23;
    time.Minutes = 59;
    time.Seconds = 59;
    RTC_DateTypeDef date{};
    date.Year = 25;
    date.Month = RTC_MONTH_JANUARY;
    date.Date = 1;
    date.WeekDay = RTC_WEEKDAY_WEDNESDAY;
    Result result{Outcome::kFail, "rtc-initialization-or-calendar-write"};
    if (HAL_RTC_Init(&handle) == HAL_OK) {
        const auto prer = RTC->PRER;
        const auto control = RTC->CR;
        const auto status = RTC->ICSR;
        const auto prer_mask = RTC_PRER_PREDIV_A | RTC_PRER_PREDIV_S;
        const auto expected_prer =
            (handle.Init.AsynchPrediv << RTC_PRER_PREDIV_A_Pos) | (handle.Init.SynchPrediv << RTC_PRER_PREDIV_S_Pos);
        char trace[128];
        std::snprintf(
            trace,
            sizeof(trace),
            "TRACE rtc regs prer=%08lx cr=%08lx icsr=%08lx",
            static_cast<unsigned long>(prer),
            static_cast<unsigned long>(control),
            static_cast<unsigned long>(status)
        );
        context.Trace(trace);
        if ((prer & prer_mask) != expected_prer || (control & RTC_CR_FMT) != 0U || (status & RTC_ICSR_INITF) != 0U) {
            static char detail[104];
            std::snprintf(
                detail,
                sizeof(detail),
                "rtc-reg prer=%08lx exp=%08lx cr=%08lx icsr=%08lx",
                static_cast<unsigned long>(prer & prer_mask),
                static_cast<unsigned long>(expected_prer),
                static_cast<unsigned long>(control),
                static_cast<unsigned long>(status)
            );
            result = {Outcome::kFail, detail};
        } else if (HAL_RTC_SetDate(&handle, &date, RTC_FORMAT_BIN) == HAL_OK
                   && HAL_RTC_SetTime(&handle, &time, RTC_FORMAT_BIN) == HAL_OK) {
            result = {Outcome::kFail, "rtc-midnight-rollover-timeout"};
            const auto begin = context.clock();
            while (!context.Expired(begin, 2500)) {
                if (HAL_RTC_GetTime(&handle, &time, RTC_FORMAT_BIN) != HAL_OK
                    || HAL_RTC_GetDate(&handle, &date, RTC_FORMAT_BIN) != HAL_OK) {
                    result = {Outcome::kFail, "rtc-calendar-read"};
                    break;
                }
                if (time.Hours == 0 && time.Minutes == 0 && time.Seconds <= 1 && date.Year == 25
                    && date.Month == RTC_MONTH_JANUARY && date.Date == 2 && date.WeekDay == RTC_WEEKDAY_THURSDAY) {
                    result = {Outcome::kPass, "lsi-calendar-midnight-date-rollover"};
                    break;
                }
                context.wait(10);
            }
        }
    }
    if (HAL_RTC_DeInit(&handle) != HAL_OK) {
        result = {Outcome::kFail, "rtc-deinitialization"};
    }
    __HAL_RCC_RTC_DISABLE();
    __HAL_RCC_RTCAPB_CLK_DISABLE();
    __HAL_RCC_RTC_CLK_DISABLE();
    HAL_PWR_DisableBkUpAccess();
    return result;
}

}
