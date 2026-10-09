#include "driver/peripheral_driver/peripheral_driver.hpp"

#include <limits>

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::peripheral {
namespace {

TIM_HandleTypeDef counter_handle{};
RTC_HandleTypeDef calendar_handle{};

common::Error Status(HAL_StatusTypeDef status)
{
    return {
        status == HAL_OK            ? common::ErrorCode::kOk
            : status == HAL_TIMEOUT ? common::ErrorCode::kTimeout
                                    : common::ErrorCode::kHardware
    };
}

void Secure(std::uint32_t peripheral)
{
    __HAL_RCC_RIFSC_CLK_ENABLE();
    HAL_RIF_RISC_SetSlaveSecureAttributes(peripheral, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
}

bool TimeoutValid(std::uint32_t timeout_ms)
{
    return timeout_ms > 0 && timeout_ms <= 1000;
}

bool MemoryValid(
    const Memory &memory,
    std::size_t alignment
)
{
    const auto address = reinterpret_cast<std::uintptr_t>(memory.data);
    return memory.data != nullptr && memory.size > 0 && address % alignment == 0
        && memory.size <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        && address <= std::numeric_limits<std::uintptr_t>::max() - memory.size;
}

bool InputValid(
    const Memory &input,
    std::size_t bytes
)
{
    return MemoryValid(input, 4) && bytes > 0 && bytes <= input.size && (bytes + 3) / 4 * 4 <= input.size;
}

common::Error Ownership(const PeripheralDriver::Writer &writer)
{
    return PeripheralManagement::Instance().Validate(writer);
}

}

std::uint32_t CycleCount()
{
    return DWT->CYCCNT;
}

common::Error PeripheralDriver::RandomWords(
    std::uint32_t *output,
    std::size_t count,
    std::uint32_t timeout_ms,
    const Writer &writer
)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (output == nullptr || count == 0 || !TimeoutValid(timeout_ms)) {
        return {common::ErrorCode::kInvalidArgument};
    }
    Secure(RIF_RISC_PERIPH_INDEX_RNG);
    __HAL_RCC_RNG_CLK_ENABLE();
    __HAL_RCC_RNG_FORCE_RESET();
    __HAL_RCC_RNG_RELEASE_RESET();
    RNG_HandleTypeDef handle{};
    handle.Instance = RNG;
    handle.Init.ClockErrorDetection = RNG_CED_ENABLE;
    auto result = Status(HAL_RNG_Init(&handle));
    const auto begin = HAL_GetTick();
    for (std::size_t index = 0; result.Ok() && index < count; ++index) {
        if (HAL_GetTick() - begin >= timeout_ms) {
            result = {common::ErrorCode::kTimeout};
            break;
        }
        result = Status(HAL_RNG_GenerateRandomNumber(&handle, output + index));
        if (result.Ok()
            && (HAL_RNG_GetError(&handle) != HAL_RNG_ERROR_NONE || (RNG->CR & RNG_CR_RNGEN) == 0U
                || (RNG->SR & (RNG_SR_CEIS | RNG_SR_SEIS)) != 0U)) {
            result = {common::ErrorCode::kHardware};
        }
    }
    const auto closed = Status(HAL_RNG_DeInit(&handle));
    __HAL_RCC_RNG_CLK_DISABLE();
    return result.Ok() ? closed : result;
}

common::Error PeripheralDriver::Sha256(
    const Memory &input,
    std::size_t bytes,
    std::uint8_t (&digest)[32],
    std::uint32_t timeout_ms,
    const Writer &writer
)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (!InputValid(input, bytes) || !TimeoutValid(timeout_ms) || reinterpret_cast<std::uintptr_t>(digest) % 4 != 0) {
        return {common::ErrorCode::kInvalidArgument};
    }
    Secure(RIF_RISC_PERIPH_INDEX_HASH);
    __HAL_RCC_HASH_CLK_ENABLE();
    __HAL_RCC_HASH_FORCE_RESET();
    __HAL_RCC_HASH_RELEASE_RESET();
    HASH_HandleTypeDef handle{};
    handle.Instance = HASH;
    handle.Init.DataType = HASH_BYTE_SWAP;
    handle.Init.Algorithm = HASH_ALGOSELECTION_SHA256;
    auto result = Status(HAL_HASH_Init(&handle));
    constexpr auto mask = HASH_CR_ALGO | HASH_CR_DATATYPE;
    constexpr auto expected = HASH_ALGOSELECTION_SHA256 | HASH_BYTE_SWAP;
    if (result.Ok() && (HASH->CR & mask) != expected)
        result = {common::ErrorCode::kHardware};
    if (result.Ok())
        result = Status(HAL_HASH_Start(&handle, input.data, bytes, digest, timeout_ms));
    if (result.Ok() && ((HASH->CR & mask) != expected || (HASH->SR & HASH_SR_BUSY) != 0U)) {
        result = {common::ErrorCode::kHardware};
    }
    const auto closed = Status(HAL_HASH_DeInit(&handle));
    __HAL_RCC_HASH_CLK_DISABLE();
    return result.Ok() ? closed : result;
}

common::Error PeripheralDriver::Crc32Mpeg2(
    const Memory &input,
    std::size_t bytes,
    std::uint32_t *value,
    const Writer &writer
)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (!InputValid(input, bytes) || value == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    Secure(RIF_RISC_PERIPH_INDEX_CRC);
    __HAL_RCC_CRC_CLK_ENABLE();
    __HAL_RCC_CRC_FORCE_RESET();
    __HAL_RCC_CRC_RELEASE_RESET();
    CRC_HandleTypeDef handle{};
    handle.Instance = CRC;
    handle.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
    handle.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
    handle.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
    handle.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
    handle.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
    auto result = Status(HAL_CRC_Init(&handle));
    constexpr auto mask = CRC_CR_RTYPE_IN | CRC_CR_REV_IN | CRC_CR_RTYPE_OUT | CRC_CR_REV_OUT;
    if (result.Ok() && ((CRC->CR & mask) != 0U || CRC->POL != DEFAULT_CRC32_POLY || CRC->INIT != DEFAULT_CRC_INITVALUE))
        result = {common::ErrorCode::kHardware};
    if (result.Ok())
        *value = HAL_CRC_Calculate(&handle, reinterpret_cast<std::uint32_t *>(input.data), bytes);
    const auto closed = Status(HAL_CRC_DeInit(&handle));
    __HAL_RCC_CRC_CLK_DISABLE();
    return result.Ok() ? closed : result;
}

common::Error PeripheralDriver::Copy(
    DmaController controller,
    const Memory &source,
    const Memory &destination,
    std::size_t offset,
    std::size_t bytes,
    std::uint32_t timeout_ms,
    const Writer &writer
)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    const auto source_address = reinterpret_cast<std::uintptr_t>(source.data);
    const auto destination_address = reinterpret_cast<std::uintptr_t>(destination.data);
    if ((controller != DmaController::kGeneralPurpose && controller != DmaController::kHighPerformance)
        || !MemoryValid(source, 32) || !MemoryValid(destination, 32) || source.size % 32 != 0
        || destination.size % 32 != 0 || bytes == 0 || bytes > 0xffff || !TimeoutValid(timeout_ms)
        || offset > source.size || bytes > source.size - offset || offset > destination.size
        || bytes > destination.size - offset || source_address > std::numeric_limits<std::uint32_t>::max() - source.size
        || destination_address > std::numeric_limits<std::uint32_t>::max() - destination.size
        || (source_address < destination_address + destination.size
            && destination_address < source_address + source.size)) {
        return {common::ErrorCode::kInvalidArgument};
    }
    DMA_HandleTypeDef handle{};
    if (controller == DmaController::kGeneralPurpose) {
        Secure(RIF_RCC_PERIPH_INDEX_GPDMA1);
        __HAL_RCC_GPDMA1_CLK_ENABLE();
        handle.Instance = GPDMA1_Channel0;
    } else {
        Secure(RIF_RCC_PERIPH_INDEX_HPDMA1);
        __HAL_RCC_HPDMA1_CLK_ENABLE();
        handle.Instance = HPDMA1_Channel0;
    }
    handle.Init.Request = DMA_REQUEST_SW;
    handle.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle.Init.Direction = DMA_MEMORY_TO_MEMORY;
    handle.Init.SrcInc = DMA_SINC_INCREMENTED;
    handle.Init.DestInc = DMA_DINC_INCREMENTED;
    handle.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle.Init.Priority = DMA_HIGH_PRIORITY;
    handle.Init.SrcBurstLength = 1;
    handle.Init.DestBurstLength = 1;
    handle.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1 | DMA_DEST_ALLOCATED_PORT1;
    handle.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle.Init.Mode = DMA_NORMAL;
    auto result = Status(HAL_DMA_Init(&handle));
    if (result.Ok())
        result = Status(HAL_DMA_ConfigChannelAttributes(
            &handle, DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC
        ));
    constexpr auto ctr1_mask = DMA_CTR1_DINC | DMA_CTR1_DDW_LOG2 | DMA_CTR1_SINC | DMA_CTR1_SDW_LOG2 | DMA_CTR1_DAP
        | DMA_CTR1_SAP | DMA_CTR1_DBL_1 | DMA_CTR1_SBL_1;
    constexpr auto ctr2_mask = DMA_CTR2_TCEM | DMA_CTR2_BREQ | DMA_CTR2_REQSEL | DMA_CTR2_DREQ | DMA_CTR2_SWREQ
        | DMA_CTR2_TRIGPOL | DMA_CTR2_TRIGSEL | DMA_CTR2_TRIGM | DMA_CTR2_PFREQ;
    const auto expected_ctr1 = handle.Init.DestInc | handle.Init.DestDataWidth | handle.Init.SrcInc
        | handle.Init.SrcDataWidth | handle.Init.TransferAllocatedPort;
    const auto expected_ctr2 = handle.Init.BlkHWRequest | (handle.Init.Request & DMA_CTR2_REQSEL)
        | handle.Init.TransferEventMode | handle.Init.Mode | DMA_CTR2_SWREQ;
    const auto *channel = handle.Instance;
    if (result.Ok()
        && ((channel->CCR & DMA_CCR_PRIO) != handle.Init.Priority || (channel->CCR & DMA_CCR_EN) != 0U
            || (channel->CTR1 & ctr1_mask) != (expected_ctr1 & ctr1_mask)
            || (channel->CTR2 & ctr2_mask) != (expected_ctr2 & ctr2_mask)))
        result = {common::ErrorCode::kHardware};
    if (result.Ok()) {
        SCB_CleanDCache_by_Addr(source.data, source.size);
        SCB_CleanInvalidateDCache_by_Addr(destination.data, destination.size);
        __DSB();
        result = Status(HAL_DMA_Start(&handle, source_address + offset, destination_address + offset, bytes));
        if (result.Ok())
            result = Status(HAL_DMA_PollForTransfer(&handle, HAL_DMA_FULL_TRANSFER, timeout_ms));
        if (!result.Ok() && HAL_DMA_Abort(&handle) != HAL_OK) {
            handle.Instance->CCR |= DMA_CCR_RESET;
            __DSB();
        }
        if (result.Ok() && handle.ErrorCode != HAL_DMA_ERROR_NONE)
            result = {common::ErrorCode::kHardware};
        __DSB();
        SCB_InvalidateDCache_by_Addr(destination.data, destination.size);
        __DSB();
    }
    const auto closed = Status(HAL_DMA_DeInit(&handle));
    if (!closed.Ok()) {
        handle.Instance->CCR |= DMA_CCR_RESET;
        __DSB();
    }
    return result.Ok() ? closed : result;
}

common::Error PeripheralDriver::StartCounter(
    std::uint32_t frequency,
    const Writer &writer
)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (counter_open_)
        return {common::ErrorCode::kAlreadyInitialized};
    const auto timer_hz = LL_RCC_CALC_TIMG_FREQ(HAL_RCC_GetSysClockFreq(), __HAL_RCC_GET_TIMCLKPRESCALER());
    if (frequency == 0 || timer_hz < frequency || timer_hz / frequency > 0x10000U) {
        return {common::ErrorCode::kInvalidArgument};
    }
    Secure(RIF_RISC_PERIPH_INDEX_TIM2);
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    counter_handle = {};
    counter_handle.Instance = TIM2;
    counter_handle.Init.Prescaler = timer_hz / frequency - 1;
    counter_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    counter_handle.Init.Period = 0xffffffffU;
    counter_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    counter_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    auto result = Status(HAL_TIM_Base_Init(&counter_handle));
    if (result.Ok())
        result = Status(HAL_TIM_Base_Start(&counter_handle));
    if (result.Ok()
        && ((TIM2->CR1 & TIM_CR1_CEN) == 0U || TIM2->PSC != counter_handle.Init.Prescaler
            || TIM2->ARR != counter_handle.Init.Period))
        result = {common::ErrorCode::kHardware};
    if (!result.Ok()) {
        (void)HAL_TIM_Base_DeInit(&counter_handle);
        __HAL_RCC_TIM2_CLK_DISABLE();
        return result;
    }
    saved_debug_control_ = CoreDebug->DEMCR;
    saved_cycle_control_ = DWT->CTRL;
    if ((saved_cycle_control_ & DWT_CTRL_NOCYCCNT_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    cycles_available_ = (DWT->CTRL & (DWT_CTRL_NOCYCCNT_Msk | DWT_CTRL_CYCCNTENA_Msk)) == DWT_CTRL_CYCCNTENA_Msk
        && SystemCoreClock != 0;
    counter_frequency_ = timer_hz / (counter_handle.Init.Prescaler + 1);
    counter_open_ = true;
    return {};
}

common::Error PeripheralDriver::ReadCounter(
    CounterSample *sample,
    const Writer &writer
) const
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (sample == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    if (!counter_open_)
        return {common::ErrorCode::kNotInitialized};
    *sample = {
        __HAL_TIM_GET_COUNTER(&counter_handle),
        cycles_available_ ? DWT->CYCCNT : 0,
        counter_frequency_,
        SystemCoreClock,
        cycles_available_
    };
    return {};
}

common::Error PeripheralDriver::StopCounter(const Writer &writer)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (!counter_open_)
        return {common::ErrorCode::kNotInitialized};
    const auto result = Status(HAL_TIM_Base_Stop(&counter_handle));
    return result.Ok() && (TIM2->CR1 & TIM_CR1_CEN) != 0U ? common::Error{common::ErrorCode::kHardware} : result;
}

common::Error PeripheralDriver::OpenCalendar(const Writer &writer)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (calendar_open_)
        return {common::ErrorCode::kAlreadyInitialized};
    Secure(RIF_RCC_PERIPH_INDEX_RTC);
    HAL_PWR_EnableBkUpAccess();
    RCC_OscInitTypeDef oscillator{};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    oscillator.LSIState = RCC_LSI_ON;
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    clock.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    auto result = Status(HAL_RCC_OscConfig(&oscillator));
    if (result.Ok())
        result = Status(HAL_RCCEx_PeriphCLKConfig(&clock));
    if (!result.Ok()) {
        HAL_PWR_DisableBkUpAccess();
        return result;
    }
    __HAL_RCC_RTC_CLK_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_RTC_FORCE_RESET();
    __HAL_RCC_RTC_RELEASE_RESET();
    calendar_handle = {};
    calendar_handle.Instance = RTC;
    calendar_handle.Init.HourFormat = RTC_HOURFORMAT_24;
    calendar_handle.Init.AsynchPrediv = 127;
    calendar_handle.Init.SynchPrediv = LSI_VALUE / 128 - 1;
    calendar_handle.Init.OutPut = RTC_OUTPUT_DISABLE;
    calendar_handle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    calendar_handle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    calendar_handle.Init.BinMode = RTC_BINARY_NONE;
    calendar_open_ = true;
    result = Status(HAL_RTC_Init(&calendar_handle));
    const auto expected =
        (calendar_handle.Init.AsynchPrediv << RTC_PRER_PREDIV_A_Pos) | calendar_handle.Init.SynchPrediv;
    if (result.Ok()
        && ((RTC->PRER & (RTC_PRER_PREDIV_A | RTC_PRER_PREDIV_S)) != expected || (RTC->CR & RTC_CR_FMT) != 0U
            || (RTC->ICSR & RTC_ICSR_INITF) != 0U)) {
        result = {common::ErrorCode::kHardware};
    }
    if (!result.Ok()) {
        (void)HAL_RTC_DeInit(&calendar_handle);
        __HAL_RCC_RTC_DISABLE();
        __HAL_RCC_RTCAPB_CLK_DISABLE();
        __HAL_RCC_RTC_CLK_DISABLE();
        HAL_PWR_DisableBkUpAccess();
        calendar_open_ = false;
    }
    return result;
}

common::Error PeripheralDriver::SetCalendar(
    const Calendar &calendar,
    const Writer &writer
)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (!calendar_open_)
        return {common::ErrorCode::kNotInitialized};
    const bool leap = calendar.year % 4 == 0;
    constexpr std::uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (calendar.year > 99 || calendar.month < 1 || calendar.month > 12 || calendar.day < 1
        || calendar.day > month_days[calendar.month - 1] + (calendar.month == 2 && leap ? 1 : 0) || calendar.weekday < 1
        || calendar.weekday > 7 || calendar.hours > 23 || calendar.minutes > 59 || calendar.seconds > 59)
        return {common::ErrorCode::kInvalidArgument};
    RTC_DateTypeDef date{};
    date.Year = calendar.year;
    date.Month = calendar.month;
    date.Date = calendar.day;
    date.WeekDay = calendar.weekday;
    RTC_TimeTypeDef time{};
    time.Hours = calendar.hours;
    time.Minutes = calendar.minutes;
    time.Seconds = calendar.seconds;
    auto result = Status(HAL_RTC_SetDate(&calendar_handle, &date, RTC_FORMAT_BIN));
    return result.Ok() ? Status(HAL_RTC_SetTime(&calendar_handle, &time, RTC_FORMAT_BIN)) : result;
}

common::Error PeripheralDriver::ReadCalendar(
    Calendar *calendar,
    const Writer &writer
) const
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    if (calendar == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    if (!calendar_open_)
        return {common::ErrorCode::kNotInitialized};
    RTC_TimeTypeDef time{};
    RTC_DateTypeDef date{};
    auto result = Status(HAL_RTC_GetTime(&calendar_handle, &time, RTC_FORMAT_BIN));
    if (result.Ok())
        result = Status(HAL_RTC_GetDate(&calendar_handle, &date, RTC_FORMAT_BIN));
    if (result.Ok())
        *calendar = {date.Year, date.Month, date.Date, date.WeekDay, time.Hours, time.Minutes, time.Seconds};
    return result;
}

common::Error PeripheralDriver::Close(const Writer &writer)
{
    const auto ownership = Ownership(writer);
    if (!ownership.Ok())
        return ownership;
    common::Error result{};
    if (counter_open_) {
        result = Status(HAL_TIM_Base_DeInit(&counter_handle));
        __HAL_RCC_TIM2_CLK_DISABLE();
        DWT->CTRL = saved_cycle_control_;
        CoreDebug->DEMCR = saved_debug_control_;
        counter_open_ = false;
    }
    if (calendar_open_) {
        const auto closed = Status(HAL_RTC_DeInit(&calendar_handle));
        if (result.Ok())
            result = closed;
        __HAL_RCC_RTC_DISABLE();
        __HAL_RCC_RTCAPB_CLK_DISABLE();
        __HAL_RCC_RTC_CLK_DISABLE();
        HAL_PWR_DisableBkUpAccess();
        calendar_open_ = false;
    }
    return result;
}

}