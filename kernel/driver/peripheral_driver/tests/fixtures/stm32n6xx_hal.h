#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_OK,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;
typedef struct {
    uint32_t CR, SR;
} RNG_TypeDef;
typedef struct {
    uint32_t CR, SR;
} HASH_TypeDef;
typedef struct {
    uint32_t CR, POL, INIT;
} CRC_TypeDef;
typedef struct {
    uint32_t CR1, PSC, ARR, CNT;
} TIM_TypeDef;
typedef struct {
    uint32_t PRER, CR, ICSR;
} RTC_TypeDef;
typedef struct {
    uint32_t CCR, CTR1, CTR2, CSR;
} DMA_Channel_TypeDef;
typedef struct {
    uint32_t CTRL, CYCCNT;
} DWT_TypeDef;
typedef struct {
    uint32_t DEMCR;
} CoreDebug_TypeDef;
typedef struct {
    uint32_t ClockErrorDetection;
} RNG_InitTypeDef;
typedef struct {
    RNG_TypeDef *Instance;
    RNG_InitTypeDef Init;
    uint32_t ErrorCode;
} RNG_HandleTypeDef;
typedef struct {
    uint32_t DataType, Algorithm;
} HASH_InitTypeDef;
typedef struct {
    HASH_TypeDef *Instance;
    HASH_InitTypeDef Init;
} HASH_HandleTypeDef;
typedef struct {
    uint32_t DefaultPolynomialUse, DefaultInitValueUse, InputDataInversionMode, OutputDataInversionMode;
} CRC_InitTypeDef;
typedef struct {
    CRC_TypeDef *Instance;
    CRC_InitTypeDef Init;
    uint32_t InputDataFormat;
} CRC_HandleTypeDef;
typedef struct {
    uint32_t Prescaler, CounterMode, Period, ClockDivision, AutoReloadPreload;
} TIM_Base_InitTypeDef;
typedef struct {
    TIM_TypeDef *Instance;
    TIM_Base_InitTypeDef Init;
} TIM_HandleTypeDef;
typedef struct {
    uint32_t HourFormat, AsynchPrediv, SynchPrediv, OutPut, OutPutPolarity, OutPutType, BinMode;
} RTC_InitTypeDef;
typedef struct {
    RTC_TypeDef *Instance;
    RTC_InitTypeDef Init;
} RTC_HandleTypeDef;
typedef struct {
    uint8_t Hours, Minutes, Seconds;
} RTC_TimeTypeDef;
typedef struct {
    uint8_t Year, Month, Date, WeekDay;
} RTC_DateTypeDef;
typedef struct {
    uint32_t OscillatorType, LSIState;
} RCC_OscInitTypeDef;
typedef struct {
    uint32_t PeriphClockSelection, RTCClockSelection;
} RCC_PeriphCLKInitTypeDef;
typedef struct {
    uint32_t Request, BlkHWRequest, Direction, SrcInc, DestInc, SrcDataWidth, DestDataWidth, Priority;
    uint32_t SrcBurstLength, DestBurstLength, TransferAllocatedPort, TransferEventMode, Mode;
} DMA_InitTypeDef;
typedef struct {
    DMA_Channel_TypeDef *Instance;
    DMA_InitTypeDef Init;
    uint32_t ErrorCode;
} DMA_HandleTypeDef;

extern RNG_TypeDef test_rng;
extern HASH_TypeDef test_hash;
extern CRC_TypeDef test_crc;
extern TIM_TypeDef test_tim;
extern RTC_TypeDef test_rtc;
extern DMA_Channel_TypeDef test_gpdma, test_hpdma;
extern DWT_TypeDef test_dwt;
extern CoreDebug_TypeDef test_debug;
extern uint32_t SystemCoreClock;

#define RNG (&test_rng)
#define HASH (&test_hash)
#define CRC (&test_crc)
#define TIM2 (&test_tim)
#define RTC (&test_rtc)
#define GPDMA1_Channel0 (&test_gpdma)
#define HPDMA1_Channel0 (&test_hpdma)
#define DWT (&test_dwt)
#define CoreDebug (&test_debug)

enum {
    RIF_RISC_PERIPH_INDEX_RNG = 1,
    RIF_RISC_PERIPH_INDEX_HASH,
    RIF_RISC_PERIPH_INDEX_CRC,
    RIF_RISC_PERIPH_INDEX_TIM2,
    RIF_RCC_PERIPH_INDEX_RTC,
    RIF_RCC_PERIPH_INDEX_GPDMA1,
    RIF_RCC_PERIPH_INDEX_HPDMA1,
    RIF_ATTRIBUTE_SEC = 0x100,
    RIF_ATTRIBUTE_PRIV = 0x200,
    RNG_CED_ENABLE = 1,
    RNG_CR_RNGEN = 1,
    RNG_SR_CEIS = 2,
    RNG_SR_SEIS = 4,
    HAL_RNG_ERROR_NONE = 0,
    HASH_CR_ALGO = 3,
    HASH_CR_DATATYPE = 12,
    HASH_ALGOSELECTION_SHA256 = 2,
    HASH_BYTE_SWAP = 4,
    HASH_SR_BUSY = 1,
    DEFAULT_POLYNOMIAL_ENABLE = 0,
    DEFAULT_INIT_VALUE_ENABLE = 0,
    CRC_INPUTDATA_INVERSION_NONE = 0,
    CRC_OUTPUTDATA_INVERSION_DISABLE = 0,
    CRC_INPUTDATA_FORMAT_BYTES = 1,
    CRC_CR_RTYPE_IN = 1,
    CRC_CR_REV_IN = 2,
    CRC_CR_RTYPE_OUT = 4,
    CRC_CR_REV_OUT = 8,
    TIM_COUNTERMODE_UP = 0,
    TIM_CLOCKDIVISION_DIV1 = 0,
    TIM_AUTORELOAD_PRELOAD_DISABLE = 0,
    TIM_CR1_CEN = 1,
    DWT_CTRL_NOCYCCNT_Msk = 2,
    DWT_CTRL_CYCCNTENA_Msk = 1,
    CoreDebug_DEMCR_TRCENA_Msk = 1,
    RCC_OSCILLATORTYPE_LSI = 1,
    RCC_LSI_ON = 1,
    RCC_PERIPHCLK_RTC = 1,
    RCC_RTCCLKSOURCE_LSI = 1,
    RTC_HOURFORMAT_24 = 0,
    RTC_OUTPUT_DISABLE = 0,
    RTC_OUTPUT_POLARITY_HIGH = 0,
    RTC_OUTPUT_TYPE_OPENDRAIN = 0,
    RTC_BINARY_NONE = 0,
    RTC_FORMAT_BIN = 0,
    RTC_PRER_PREDIV_A_Pos = 16,
    RTC_PRER_PREDIV_A = 0x7f0000,
    RTC_PRER_PREDIV_S = 0x7fff,
    RTC_CR_FMT = 1,
    RTC_ICSR_INITF = 1,
    DMA_REQUEST_SW = 0,
    DMA_BREQ_SINGLE_BURST = 0,
    DMA_MEMORY_TO_MEMORY = 0,
    DMA_SINC_INCREMENTED = 1,
    DMA_DINC_INCREMENTED = 2,
    DMA_SRC_DATAWIDTH_BYTE = 0,
    DMA_DEST_DATAWIDTH_BYTE = 0,
    DMA_HIGH_PRIORITY = 0x30,
    DMA_SRC_ALLOCATED_PORT1 = 4,
    DMA_DEST_ALLOCATED_PORT1 = 8,
    DMA_TCEM_BLOCK_TRANSFER = 0,
    DMA_NORMAL = 0,
    DMA_CHANNEL_PRIV = 1,
    DMA_CHANNEL_SEC = 2,
    DMA_CHANNEL_SRC_SEC = 4,
    DMA_CHANNEL_DEST_SEC = 8,
    DMA_CCR_PRIO = 0x30,
    DMA_CCR_EN = 1,
    DMA_CCR_RESET = 2,
    DMA_CTR1_DINC = 2,
    DMA_CTR1_DDW_LOG2 = 0x30,
    DMA_CTR1_SINC = 1,
    DMA_CTR1_SDW_LOG2 = 0xc0,
    DMA_CTR1_DAP = 8,
    DMA_CTR1_SAP = 4,
    DMA_CTR1_DBL_1 = 0x100,
    DMA_CTR1_SBL_1 = 0x200,
    DMA_CTR2_TCEM = 3,
    DMA_CTR2_BREQ = 4,
    DMA_CTR2_REQSEL = 0x38,
    DMA_CTR2_DREQ = 0x40,
    DMA_CTR2_SWREQ = 0x80,
    DMA_CTR2_TRIGPOL = 0x100,
    DMA_CTR2_TRIGSEL = 0x200,
    DMA_CTR2_TRIGM = 0x400,
    DMA_CTR2_PFREQ = 0x800,
    HAL_DMA_FULL_TRANSFER = 0,
    HAL_DMA_ERROR_NONE = 0
};
#define DEFAULT_CRC32_POLY 0x04c11db7U
#define DEFAULT_CRC_INITVALUE 0xffffffffU
#define LSI_VALUE 32000U

#define __HAL_RCC_RIFSC_CLK_ENABLE() TestPeripheralEvent("rif-clock")
#define __HAL_RCC_RNG_CLK_ENABLE() TestPeripheralEvent("rng-clock")
#define __HAL_RCC_RNG_FORCE_RESET() TestPeripheralEvent("rng-reset")
#define __HAL_RCC_RNG_RELEASE_RESET() TestPeripheralEvent("rng-release")
#define __HAL_RCC_RNG_CLK_DISABLE() TestPeripheralEvent("rng-clock-off")
#define __HAL_RCC_HASH_CLK_ENABLE() TestPeripheralEvent("hash-clock")
#define __HAL_RCC_HASH_FORCE_RESET() TestPeripheralEvent("hash-reset")
#define __HAL_RCC_HASH_RELEASE_RESET() TestPeripheralEvent("hash-release")
#define __HAL_RCC_HASH_CLK_DISABLE() TestPeripheralEvent("hash-clock-off")
#define __HAL_RCC_CRC_CLK_ENABLE() TestPeripheralEvent("crc-clock")
#define __HAL_RCC_CRC_FORCE_RESET() TestPeripheralEvent("crc-reset")
#define __HAL_RCC_CRC_RELEASE_RESET() TestPeripheralEvent("crc-release")
#define __HAL_RCC_CRC_CLK_DISABLE() TestPeripheralEvent("crc-clock-off")
#define __HAL_RCC_GPDMA1_CLK_ENABLE() TestPeripheralEvent("gpdma-clock")
#define __HAL_RCC_HPDMA1_CLK_ENABLE() TestPeripheralEvent("hpdma-clock")
#define __HAL_RCC_TIM2_CLK_ENABLE() TestPeripheralEvent("timer-clock")
#define __HAL_RCC_TIM2_FORCE_RESET() TestPeripheralEvent("timer-reset")
#define __HAL_RCC_TIM2_RELEASE_RESET() TestPeripheralEvent("timer-release")
#define __HAL_RCC_TIM2_CLK_DISABLE() TestPeripheralEvent("timer-clock-off")
#define __HAL_RCC_RTC_CLK_ENABLE() TestPeripheralEvent("rtc-clock")
#define __HAL_RCC_RTCAPB_CLK_ENABLE() TestPeripheralEvent("rtc-apb")
#define __HAL_RCC_RTC_ENABLE() TestPeripheralEvent("rtc-enable")
#define __HAL_RCC_RTC_FORCE_RESET() TestPeripheralEvent("rtc-reset")
#define __HAL_RCC_RTC_RELEASE_RESET() TestPeripheralEvent("rtc-release")
#define __HAL_RCC_RTC_DISABLE() TestPeripheralEvent("rtc-disable")
#define __HAL_RCC_RTCAPB_CLK_DISABLE() TestPeripheralEvent("rtc-apb-off")
#define __HAL_RCC_RTC_CLK_DISABLE() TestPeripheralEvent("rtc-clock-off")
#define __HAL_RCC_GET_TIMCLKPRESCALER() 0U
#define LL_RCC_CALC_TIMG_FREQ(clock, prescaler) (clock)
#define __HAL_TIM_GET_COUNTER(handle) ((handle)->Instance->CNT)
#define __DSB() TestPeripheralEvent("barrier")

void TestPeripheralEvent(const char *event);
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetSysClockFreq(void);
void HAL_RIF_RISC_SetSlaveSecureAttributes(
    uint32_t peripheral,
    uint32_t attributes
);
HAL_StatusTypeDef HAL_RNG_Init(RNG_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_RNG_DeInit(RNG_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(
    RNG_HandleTypeDef *handle,
    uint32_t *value
);
uint32_t HAL_RNG_GetError(RNG_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_HASH_Init(HASH_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_HASH_DeInit(HASH_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_HASH_Start(
    HASH_HandleTypeDef *handle,
    const uint8_t *input,
    uint32_t bytes,
    uint8_t *digest,
    uint32_t timeout
);
HAL_StatusTypeDef HAL_CRC_Init(CRC_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_CRC_DeInit(CRC_HandleTypeDef *handle);
uint32_t HAL_CRC_Calculate(
    CRC_HandleTypeDef *handle,
    uint32_t *input,
    uint32_t bytes
);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_DMA_ConfigChannelAttributes(
    DMA_HandleTypeDef *handle,
    uint32_t attributes
);
HAL_StatusTypeDef HAL_DMA_Start(
    DMA_HandleTypeDef *handle,
    uint32_t source,
    uint32_t destination,
    uint32_t bytes
);
HAL_StatusTypeDef HAL_DMA_PollForTransfer(
    DMA_HandleTypeDef *handle,
    uint32_t level,
    uint32_t timeout
);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *handle);
void SCB_CleanDCache_by_Addr(
    void *address,
    int32_t bytes
);
void SCB_CleanInvalidateDCache_by_Addr(
    void *address,
    int32_t bytes
);
void SCB_InvalidateDCache_by_Addr(
    void *address,
    int32_t bytes
);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_DeInit(TIM_HandleTypeDef *handle);
void HAL_PWR_EnableBkUpAccess(void);
void HAL_PWR_DisableBkUpAccess(void);
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *config);
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *config);
HAL_StatusTypeDef HAL_RTC_Init(RTC_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_RTC_DeInit(RTC_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_RTC_SetDate(
    RTC_HandleTypeDef *handle,
    RTC_DateTypeDef *date,
    uint32_t format
);
HAL_StatusTypeDef HAL_RTC_SetTime(
    RTC_HandleTypeDef *handle,
    RTC_TimeTypeDef *time,
    uint32_t format
);
HAL_StatusTypeDef HAL_RTC_GetDate(
    RTC_HandleTypeDef *handle,
    RTC_DateTypeDef *date,
    uint32_t format
);
HAL_StatusTypeDef HAL_RTC_GetTime(
    RTC_HandleTypeDef *handle,
    RTC_TimeTypeDef *time,
    uint32_t format
);

#ifdef __cplusplus
}
#endif