#include "driver/peripheral_driver/peripheral_driver.hpp"
#include "stm32n6xx_hal.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
namespace peripheral = uai::ai::peripheral;
using Code = uai::ai::common::ErrorCode;

struct HalState {
    std::string failing;
    HAL_StatusTypeDef failure = HAL_ERROR;
    std::uint32_t tick = 0;
    std::uint32_t bytes = 0;
    std::uint32_t timeout = 0;
    std::uint32_t source = 0;
    std::uint32_t destination = 0;
    std::uint32_t random = 1;
    ER lock = E_OK;
    bool abort_fails = false;
    DMA_Channel_TypeDef *channel = nullptr;
    RTC_DateTypeDef date{};
    RTC_TimeTypeDef time{};
    std::vector<std::string> events;
} hal;

HAL_StatusTypeDef Event(const char *event)
{
    hal.events.emplace_back(event);
    return hal.failing == event ? hal.failure : HAL_OK;
}

bool Has(const char *event)
{
    return std::find(hal.events.begin(), hal.events.end(), event) != hal.events.end();
}

class PeripheralTest : public testing::Test {
protected:
    void SetUp() override
    {
        hal = {};
        test_tim = {};
        test_rtc = {};
        test_gpdma = {};
        test_hpdma = {};
        test_dwt = {};
        test_debug = {};
    }
    peripheral::PeripheralManagement::Accessor Open()
    {
        peripheral::PeripheralManagement::Accessor accessor;
        EXPECT_TRUE(peripheral::PeripheralManagement::Instance().Acquire(&accessor, 100).Ok());
        hal.events.clear();
        return accessor;
    }
};

TEST_F(
    PeripheralTest,
    RejectsInvalidArgumentsAndForeignOwnershipBeforeHal
)
{
    auto accessor = Open();
    std::uint32_t word = 0;
    peripheral::CounterSample sample;
    alignas(4) std::uint8_t input[4]{}, digest[32]{};
    EXPECT_EQ(accessor->RandomWords(&word, 1, 100, {}).Code(), Code::kOwnership);
    EXPECT_EQ(accessor->RandomWords(nullptr, 1, 100, accessor.Ownership()).Code(), Code::kInvalidArgument);
    EXPECT_EQ(accessor->RandomWords(&word, 1, 0, accessor.Ownership()).Code(), Code::kInvalidArgument);
    EXPECT_EQ(accessor->Sha256({input, 3}, 3, digest, 100, accessor.Ownership()).Code(), Code::kInvalidArgument);
    EXPECT_EQ(accessor->Crc32Mpeg2({input, 4}, 5, &word, accessor.Ownership()).Code(), Code::kInvalidArgument);
    EXPECT_EQ(accessor->ReadCounter(&sample, accessor.Ownership()).Code(), Code::kNotInitialized);
    EXPECT_EQ(accessor->ReadCalendar(nullptr, accessor.Ownership()).Code(), Code::kInvalidArgument);
    EXPECT_TRUE(hal.events.empty());
}

TEST_F(
    PeripheralTest,
    RandomWordsCheckHardwareAndCloseOnTimeout
)
{
    auto accessor = Open();
    std::uint32_t words[64]{};
    ASSERT_TRUE(accessor->RandomWords(words, 64, 500, accessor.Ownership()).Ok());
    EXPECT_NE(words[0], words[63]);
    EXPECT_TRUE(Has("rng-deinit"));
    EXPECT_TRUE(Has("rng-clock-off"));
    hal = {};
    EXPECT_EQ(accessor->RandomWords(words, 64, 2, accessor.Ownership()).Code(), Code::kTimeout);
    EXPECT_TRUE(Has("rng-deinit"));
    EXPECT_TRUE(Has("rng-clock-off"));
}

TEST_F(
    PeripheralTest,
    HashAndCrcUseLogicalByteCountAndPropagateErrors
)
{
    auto accessor = Open();
    alignas(4) std::uint8_t input[12] = "123456789", digest[32]{};
    ASSERT_TRUE(accessor->Sha256({input, sizeof(input)}, 9, digest, 75, accessor.Ownership()).Ok());
    EXPECT_EQ(hal.bytes, 9U);
    EXPECT_EQ(hal.timeout, 75U);
    EXPECT_EQ(digest[0], 0xa5);
    EXPECT_TRUE(Has("hash-deinit"));
    std::uint32_t value = 0;
    ASSERT_TRUE(accessor->Crc32Mpeg2({input, sizeof(input)}, 9, &value, accessor.Ownership()).Ok());
    EXPECT_EQ(value, 0x12345678U);
    EXPECT_EQ(hal.bytes, 9U);
    hal.failing = "hash-start";
    hal.failure = HAL_TIMEOUT;
    EXPECT_EQ(accessor->Sha256({input, sizeof(input)}, 9, digest, 75, accessor.Ownership()).Code(), Code::kTimeout);
    EXPECT_TRUE(Has("hash-clock-off"));
}

TEST_F(
    PeripheralTest,
    DmaCopiesAllLengthsUsingSelectedChannelAndCacheOrder
)
{
    auto accessor = Open();
    const peripheral::Memory source{reinterpret_cast<std::uint8_t *>(0x34000000U), 320};
    const peripheral::Memory destination{reinterpret_cast<std::uint8_t *>(0x34100000U), 320};
    for (const auto controller :
         {peripheral::DmaController::kGeneralPurpose, peripheral::DmaController::kHighPerformance}) {
        for (const auto bytes : {1U, 3U, 31U, 32U, 33U, 255U, 256U}) {
            hal.events.clear();
            ASSERT_TRUE(accessor->Copy(controller, source, destination, 32, bytes, 75, accessor.Ownership()).Ok());
            EXPECT_EQ(
                hal.channel,
                controller == peripheral::DmaController::kGeneralPurpose ? GPDMA1_Channel0 : HPDMA1_Channel0
            );
            EXPECT_EQ(hal.bytes, bytes);
            EXPECT_EQ(hal.source, 0x34000020U);
            EXPECT_EQ(hal.destination, 0x34100020U);
            const auto clean = std::find(hal.events.begin(), hal.events.end(), "clean");
            const auto start = std::find(hal.events.begin(), hal.events.end(), "dma-start");
            const auto poll = std::find(hal.events.begin(), hal.events.end(), "dma-poll");
            const auto inspect = std::find(hal.events.begin(), hal.events.end(), "invalidate");
            ASSERT_LT(clean, start);
            ASSERT_LT(start, poll);
            ASSERT_LT(poll, inspect);
            EXPECT_TRUE(Has("dma-deinit"));
        }
    }
    hal.events.clear();
    EXPECT_EQ(
        accessor->Copy(peripheral::DmaController::kGeneralPurpose, source, source, 0, 32, 100, accessor.Ownership())
            .Code(),
        Code::kInvalidArgument
    );
    EXPECT_EQ(
        accessor
            ->Copy(peripheral::DmaController::kGeneralPurpose, source, destination, 319, 2, 100, accessor.Ownership())
            .Code(),
        Code::kInvalidArgument
    );
    EXPECT_TRUE(hal.events.empty());
}

TEST_F(
    PeripheralTest,
    DmaTimeoutAbortsBeforeInspectionAndDeinitialization
)
{
    auto accessor = Open();
    hal.failing = "dma-poll";
    hal.failure = HAL_TIMEOUT;
    EXPECT_EQ(
        accessor
            ->Copy(
                peripheral::DmaController::kGeneralPurpose,
                {reinterpret_cast<std::uint8_t *>(0x34000000U), 320},
                {reinterpret_cast<std::uint8_t *>(0x34100000U), 320},
                33,
                31,
                100,
                accessor.Ownership()
            )
            .Code(),
        Code::kTimeout
    );
    const auto abort = std::find(hal.events.begin(), hal.events.end(), "dma-abort");
    const auto inspect = std::find(hal.events.begin(), hal.events.end(), "invalidate");
    ASSERT_LT(abort, inspect);
    EXPECT_TRUE(Has("dma-deinit"));
}

TEST_F(
    PeripheralTest,
    CounterPreservesCycleStateAndRemainsReadableAfterStop
)
{
    test_debug.DEMCR = 0x40;
    test_dwt.CTRL = 0x80;
    auto accessor = Open();
    ASSERT_TRUE(accessor->StartCounter(1000000, accessor.Ownership()).Ok());
    test_tim.CNT = 25000;
    test_dwt.CYCCNT = 20000000;
    peripheral::CounterSample sample{};
    ASSERT_TRUE(accessor->ReadCounter(&sample, accessor.Ownership()).Ok());
    EXPECT_EQ(sample.frequency, 1000000U);
    EXPECT_EQ(sample.cycle_frequency, SystemCoreClock);
    EXPECT_TRUE(sample.cycles_available);
    ASSERT_TRUE(accessor->StopCounter(accessor.Ownership()).Ok());
    ASSERT_TRUE(accessor->ReadCounter(&sample, accessor.Ownership()).Ok());
    EXPECT_EQ(sample.ticks, 25000U);
    EXPECT_EQ(test_tim.CR1 & TIM_CR1_CEN, 0U);
    ASSERT_TRUE(accessor.Close().Ok());
    EXPECT_EQ(test_debug.DEMCR, 0x40U);
    EXPECT_EQ(test_dwt.CTRL, 0x80U);
    EXPECT_TRUE(Has("timer-clock-off"));
}

TEST_F(
    PeripheralTest,
    AbortFailureResetsOnlyTheSelectedDmaChannel
)
{
    auto accessor = Open();
    hal.failing = "dma-poll";
    hal.failure = HAL_TIMEOUT;
    hal.abort_fails = true;
    EXPECT_EQ(
        accessor
            ->Copy(
                peripheral::DmaController::kGeneralPurpose,
                {reinterpret_cast<std::uint8_t *>(0x34000000U), 320},
                {reinterpret_cast<std::uint8_t *>(0x34100000U), 320},
                32,
                32,
                100,
                accessor.Ownership()
            )
            .Code(),
        Code::kTimeout
    );
    EXPECT_NE(test_gpdma.CCR & DMA_CCR_RESET, 0U);
    EXPECT_EQ(test_hpdma.CCR, 0U);
    EXPECT_TRUE(Has("dma-deinit"));
}

TEST_F(
    PeripheralTest,
    MovedAccessorClosesCalendarOnceAndUnlocksAfterCleanup
)
{
    auto accessor = Open();
    ASSERT_TRUE(accessor->OpenCalendar(accessor.Ownership()).Ok());
    ASSERT_TRUE(accessor->SetCalendar({25, 1, 1, 3, 23, 59, 59}, accessor.Ownership()).Ok());
    EXPECT_EQ(accessor->SetCalendar({25, 2, 29, 6, 0, 0, 0}, accessor.Ownership()).Code(), Code::kInvalidArgument);
    peripheral::Calendar calendar{};
    ASSERT_TRUE(accessor->ReadCalendar(&calendar, accessor.Ownership()).Ok());
    EXPECT_EQ(calendar.day, 1U);
    EXPECT_EQ(calendar.seconds, 59U);
    EXPECT_LT(
        std::find(hal.events.begin(), hal.events.end(), "rtc-get-time"),
        std::find(hal.events.begin(), hal.events.end(), "rtc-get-date")
    );
    {
        auto moved = std::move(accessor);
        EXPECT_TRUE(moved.Valid());
        EXPECT_FALSE(accessor.Valid());
    }
    EXPECT_EQ(std::count(hal.events.begin(), hal.events.end(), "rtc-deinit"), 1);
    EXPECT_TRUE(Has("backup-off"));
    EXPECT_EQ(hal.events.back(), "unlock");
}

TEST_F(
    PeripheralTest,
    CalendarInitializationFailureStillClosesOnScopeExit
)
{
    {
        auto accessor = Open();
        hal.failing = "rtc-init";
        EXPECT_EQ(accessor->OpenCalendar(accessor.Ownership()).Code(), Code::kHardware);
        peripheral::Calendar calendar{};
        EXPECT_EQ(accessor->ReadCalendar(&calendar, accessor.Ownership()).Code(), Code::kNotInitialized);
    }
    EXPECT_TRUE(Has("rtc-deinit"));
    EXPECT_TRUE(Has("backup-off"));
    EXPECT_EQ(hal.events.back(), "unlock");
}

}

ID tk_cre_mtx(const T_CMTX *)
{
    return 1;
}
ER tk_loc_mtx(
    ID,
    TMO
)
{
    return hal.lock;
}
ER tk_unl_mtx(ID)
{
    hal.events.emplace_back("unlock");
    return E_OK;
}

extern "C" {
RNG_TypeDef test_rng{};
HASH_TypeDef test_hash{};
CRC_TypeDef test_crc{};
TIM_TypeDef test_tim{};
RTC_TypeDef test_rtc{};
DMA_Channel_TypeDef test_gpdma{}, test_hpdma{};
DWT_TypeDef test_dwt{};
CoreDebug_TypeDef test_debug{};
std::uint32_t SystemCoreClock = 800000000;

void TestPeripheralEvent(const char *event)
{
    hal.events.emplace_back(event);
}
std::uint32_t HAL_GetTick()
{
    return hal.tick++;
}
std::uint32_t HAL_RCC_GetSysClockFreq()
{
    return 1200000000;
}
void HAL_RIF_RISC_SetSlaveSecureAttributes(
    std::uint32_t,
    std::uint32_t
)
{
    (void)Event("secure");
}
HAL_StatusTypeDef HAL_RNG_Init(RNG_HandleTypeDef *handle)
{
    handle->Instance->CR = RNG_CR_RNGEN;
    return Event("rng-init");
}
HAL_StatusTypeDef HAL_RNG_DeInit(RNG_HandleTypeDef *)
{
    return Event("rng-deinit");
}
HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(
    RNG_HandleTypeDef *,
    std::uint32_t *value
)
{
    *value = hal.random++;
    return Event("rng-read");
}
std::uint32_t HAL_RNG_GetError(RNG_HandleTypeDef *)
{
    return 0;
}
HAL_StatusTypeDef HAL_HASH_Init(HASH_HandleTypeDef *handle)
{
    handle->Instance->CR = handle->Init.DataType | handle->Init.Algorithm;
    return Event("hash-init");
}
HAL_StatusTypeDef HAL_HASH_DeInit(HASH_HandleTypeDef *)
{
    return Event("hash-deinit");
}
HAL_StatusTypeDef HAL_HASH_Start(
    HASH_HandleTypeDef *,
    const std::uint8_t *,
    std::uint32_t bytes,
    std::uint8_t *digest,
    std::uint32_t timeout
)
{
    hal.bytes = bytes;
    hal.timeout = timeout;
    std::memset(digest, 0xa5, 32);
    return Event("hash-start");
}
HAL_StatusTypeDef HAL_CRC_Init(CRC_HandleTypeDef *handle)
{
    handle->Instance->CR = 0;
    handle->Instance->POL = DEFAULT_CRC32_POLY;
    handle->Instance->INIT = DEFAULT_CRC_INITVALUE;
    return Event("crc-init");
}
HAL_StatusTypeDef HAL_CRC_DeInit(CRC_HandleTypeDef *)
{
    return Event("crc-deinit");
}
std::uint32_t HAL_CRC_Calculate(
    CRC_HandleTypeDef *,
    std::uint32_t *,
    std::uint32_t bytes
)
{
    hal.bytes = bytes;
    (void)Event("crc-calculate");
    return 0x12345678;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *handle)
{
    hal.channel = handle->Instance;
    handle->Instance->CCR = handle->Init.Priority;
    handle->Instance->CTR1 = handle->Init.SrcInc | handle->Init.DestInc | handle->Init.TransferAllocatedPort;
    handle->Instance->CTR2 = DMA_CTR2_SWREQ;
    return Event("dma-init");
}
HAL_StatusTypeDef HAL_DMA_ConfigChannelAttributes(
    DMA_HandleTypeDef *,
    std::uint32_t
)
{
    return Event("dma-attributes");
}
HAL_StatusTypeDef HAL_DMA_Start(
    DMA_HandleTypeDef *,
    std::uint32_t source,
    std::uint32_t destination,
    std::uint32_t bytes
)
{
    hal.source = source;
    hal.destination = destination;
    hal.bytes = bytes;
    return Event("dma-start");
}
HAL_StatusTypeDef HAL_DMA_PollForTransfer(
    DMA_HandleTypeDef *,
    std::uint32_t,
    std::uint32_t timeout
)
{
    hal.timeout = timeout;
    return Event("dma-poll");
}
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *)
{
    const auto status = Event("dma-abort");
    return hal.abort_fails ? HAL_ERROR : status;
}
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *)
{
    return Event("dma-deinit");
}
void SCB_CleanDCache_by_Addr(
    void *,
    std::int32_t
)
{
    (void)Event("clean");
}
void SCB_CleanInvalidateDCache_by_Addr(
    void *,
    std::int32_t
)
{
    (void)Event("prepare");
}
void SCB_InvalidateDCache_by_Addr(
    void *,
    std::int32_t
)
{
    (void)Event("invalidate");
}
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *handle)
{
    handle->Instance->PSC = handle->Init.Prescaler;
    handle->Instance->ARR = handle->Init.Period;
    return Event("timer-init");
}
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *handle)
{
    handle->Instance->CR1 |= TIM_CR1_CEN;
    return Event("timer-start");
}
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *handle)
{
    handle->Instance->CR1 &= ~TIM_CR1_CEN;
    return Event("timer-stop");
}
HAL_StatusTypeDef HAL_TIM_Base_DeInit(TIM_HandleTypeDef *)
{
    return Event("timer-deinit");
}
void HAL_PWR_EnableBkUpAccess()
{
    (void)Event("backup-on");
}
void HAL_PWR_DisableBkUpAccess()
{
    (void)Event("backup-off");
}
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *)
{
    return Event("rtc-oscillator");
}
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *)
{
    return Event("rtc-clock-config");
}
HAL_StatusTypeDef HAL_RTC_Init(RTC_HandleTypeDef *handle)
{
    handle->Instance->PRER = (handle->Init.AsynchPrediv << RTC_PRER_PREDIV_A_Pos) | handle->Init.SynchPrediv;
    return Event("rtc-init");
}
HAL_StatusTypeDef HAL_RTC_DeInit(RTC_HandleTypeDef *)
{
    return Event("rtc-deinit");
}
HAL_StatusTypeDef HAL_RTC_SetDate(
    RTC_HandleTypeDef *,
    RTC_DateTypeDef *date,
    std::uint32_t
)
{
    hal.date = *date;
    return Event("rtc-set-date");
}
HAL_StatusTypeDef HAL_RTC_SetTime(
    RTC_HandleTypeDef *,
    RTC_TimeTypeDef *time,
    std::uint32_t
)
{
    hal.time = *time;
    return Event("rtc-set-time");
}
HAL_StatusTypeDef HAL_RTC_GetDate(
    RTC_HandleTypeDef *,
    RTC_DateTypeDef *date,
    std::uint32_t
)
{
    *date = hal.date;
    return Event("rtc-get-date");
}
HAL_StatusTypeDef HAL_RTC_GetTime(
    RTC_HandleTypeDef *,
    RTC_TimeTypeDef *time,
    std::uint32_t
)
{
    *time = hal.time;
    return Event("rtc-get-time");
}
}