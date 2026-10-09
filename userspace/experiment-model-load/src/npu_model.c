#define ECBLOB_CONST_SECTION __attribute__((section(".model_command_blob")))
#include "network.c"
#include "stai_network.c"
#include "stai_ext.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_rif.h"
#include "npu_cache.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if STAI_NETWORK_IN_NUM != 1 || STAI_NETWORK_OUT_NUM != 1
#error "experiment-model-load requires one input and one output"
#endif

_Static_assert(STAI_NETWORK_IN_1_SIZE_BYTES > 0 && STAI_NETWORK_IN_1_SIZE_BYTES <= 0x80000,
               "input exceeds experiment buffer");
_Static_assert(STAI_NETWORK_OUT_1_SIZE_BYTES > 0 && STAI_NETWORK_OUT_1_SIZE_BYTES <= 0x80000,
               "output exceeds experiment buffer");
_Static_assert(STAI_NETWORK_IN_1_SIZE_BYTES == EXPERIMENT_MODEL_INPUT_BYTES, "input contract mismatch");
_Static_assert(STAI_NETWORK_OUT_1_SIZE_BYTES == EXPERIMENT_MODEL_OUTPUT_BYTES, "output contract mismatch");

STAI_NETWORK_CONTEXT_DECLARE(model_context, STAI_NETWORK_CONTEXT_SIZE)
static uint8_t input[(STAI_NETWORK_IN_1_SIZE_BYTES + 31U) & ~31U] __attribute__((aligned(32)));
static uint8_t output[(STAI_NETWORK_OUT_1_SIZE_BYTES + 31U) & ~31U] __attribute__((aligned(32)));
static bool runtime_initialized;
static bool model_initialized;
static bool completed;

static void configure_npu_security(void)
{
    __HAL_RCC_RIFSC_CLK_ENABLE();
    __HAL_RCC_RISAF_CLK_ENABLE();

    RIMC_MasterConfig_t master = {
        .MasterCID = RIF_CID_1,
        .SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV,
    };
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &master);
    HAL_RIF_RISC_SetSlaveSecureAttributes(
        RIF_RISC_PERIPH_INDEX_NPU,
        RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV
    );

    const RISAF_BaseRegionConfig_t region_template = {
        .Filtering = RISAF_FILTER_ENABLE,
        .Secure = RIF_ATTRIBUTE_SEC,
        .PrivWhitelist = RIF_CID_NONE,
        .ReadWhitelist = RIF_CID_MASK,
        .WriteWhitelist = RIF_CID_MASK,
        .StartAddress = 0,
    };
    const struct {
        RISAF_TypeDef *instance;
        uint32_t end_address;
    } protected_spaces[] = {
        {RISAF4_S, RISAF4_LIMIT_ADDRESS_SPACE_SIZE},  // NPU master 0
        {RISAF5_S, RISAF5_LIMIT_ADDRESS_SPACE_SIZE},  // NPU master 1
        {RISAF6_S, RISAF6_LIMIT_ADDRESS_SPACE_SIZE},  // AXI SRAM3/4/5/6
        {RISAF7_S, RISAF7_LIMIT_ADDRESS_SPACE_SIZE},  // FLEXRAM
        {RISAF8_S, RISAF8_LIMIT_ADDRESS_SPACE_SIZE},  // NPU cache RAM
        {RISAF11_S, RISAF11_LIMIT_ADDRESS_SPACE_SIZE}, // XSPI1 PSRAM
        {RISAF15_S, RISAF15_LIMIT_ADDRESS_SPACE_SIZE}, // NPU cache config
    };
    for (size_t index = 0; index < sizeof(protected_spaces) / sizeof(protected_spaces[0]); ++index) {
        RISAF_BaseRegionConfig_t region = region_template;
        region.EndAddress = protected_spaces[index].end_address;
        HAL_RIF_RISAF_ConfigBaseRegion(protected_spaces[index].instance, RISAF_REGION_1, &region);
        region.Secure = RIF_ATTRIBUTE_NSEC;
        HAL_RIF_RISAF_ConfigBaseRegion(protected_spaces[index].instance, RISAF_REGION_2, &region);
    }

    const uint32_t secure_memories[] = {
        RIF_RCC_PERIPH_INDEX_CACHEAXIRAM,
        RIF_RCC_PERIPH_INDEX_CACHECONFIG,
        RIF_RCC_PERIPH_INDEX_NPURAM0,
        RIF_RCC_PERIPH_INDEX_NPURAM1,
        RIF_RCC_PERIPH_INDEX_NPURAM2,
        RIF_RCC_PERIPH_INDEX_NPURAM3,
        RIF_RCC_PERIPH_INDEX_AXISRAM1,
        RIF_RCC_PERIPH_INDEX_AXISRAM2,
        RIF_RCC_PERIPH_INDEX_FLEXRAM,
    };
    for (size_t index = 0; index < sizeof(secure_memories) / sizeof(secure_memories[0]); ++index) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(
            secure_memories[index],
            RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV
        );
    }
}

extern stai_return_code stai_runtime_init(void);
extern stai_return_code stai_runtime_deinit(void);
static bool success(stai_return_code code)
{
    return code < STAI_ERROR_GENERIC;
}

bool experiment_npu_stop(void)
{
    HAL_NVIC_DisableIRQ(NPU0_IRQn);
    if (runtime_initialized && !completed) {
        __HAL_RCC_NPU_FORCE_RESET();
        __DSB();
        __HAL_RCC_NPU_RELEASE_RESET();
    }
    bool stopped = true;
    if (model_initialized) {
        stopped = success(stai_network_deinit(model_context));
    }
    if (runtime_initialized) {
        stopped = success(stai_runtime_deinit()) && stopped;
    }
    HAL_NVIC_ClearPendingIRQ(NPU0_IRQn);
    if (stopped) {
        runtime_initialized = false;
        model_initialized = false;
        completed = false;
    }
    return stopped;
}

bool experiment_npu_start(uint32_t input_bytes, uint32_t output_bytes)
{
    if (runtime_initialized || model_initialized
        || input_bytes != STAI_NETWORK_IN_1_SIZE_BYTES || output_bytes != STAI_NETWORK_OUT_1_SIZE_BYTES) {
        return false;
    }
    __HAL_RCC_NPU_CLK_ENABLE();
    __HAL_RCC_NPU_CLK_SLEEP_ENABLE();
    __HAL_RCC_NPU_FORCE_RESET();
    __HAL_RCC_NPU_RELEASE_RESET();
    __HAL_RCC_FLEXRAM_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
    __HAL_RCC_RAMCFG_CLK_ENABLE();
    __HAL_RCC_RAMCFG_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
    __HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();
    configure_npu_security();
    RCC->MEMENR |= RCC_MEMENR_AXISRAM3EN | RCC_MEMENR_AXISRAM4EN
        | RCC_MEMENR_AXISRAM5EN | RCC_MEMENR_AXISRAM6EN | RCC_MEMENR_CACHEAXIRAMEN;
    RAMCFG_SRAM3_AXI->CR &= ~RAMCFG_CR_SRAMSD;
    RAMCFG_SRAM4_AXI->CR &= ~RAMCFG_CR_SRAMSD;
    RAMCFG_SRAM5_AXI->CR &= ~RAMCFG_CR_SRAMSD;
    RAMCFG_SRAM6_AXI->CR &= ~RAMCFG_CR_SRAMSD;
    MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_DCACTIVE_Msk | MEMSYSCTL_MSCR_ICACTIVE_Msk;
    npu_cache_enable();
    RAMCFG_HandleTypeDef ram = {0};
    ram.Instance = RAMCFG_SRAM5_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ram);
    ram.Instance = RAMCFG_SRAM6_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ram);
    HAL_NVIC_SetPriority(NPU0_IRQn, 8, 0);
    HAL_NVIC_ClearPendingIRQ(NPU0_IRQn);
    completed = false;
    runtime_initialized = true;
    const stai_return_code runtime_status = stai_runtime_init();
    if (!success(runtime_status)) {
        return false;
    }
    model_initialized = true;
    const stai_return_code network_status = stai_network_init(model_context);
    if (!success(network_status)) {
        return false;
    }
    const stai_return_code reset_status = stai_ext_network_new_inference(model_context);
    /* The first reset after stai_network_init() reports NETWORK_STILL_RUNNING
     * in the ST reference application; it still starts that first inference. */
    if (!success(reset_status) && reset_status != STAI_ERROR_NETWORK_STILL_RUNNING) {
        return false;
    }
    memset(input, 0, sizeof(input));
    memset(output, 0, sizeof(output));
    SCB_CleanDCache_by_Addr(input, (sizeof(input) + 31U) & ~31U);
    SCB_CleanInvalidateDCache_by_Addr(output, (sizeof(output) + 31U) & ~31U);
    __DSB();
    stai_ptr outputs[] = {output};
    if (LL_ATON_Set_User_Input_Buffer_network(0, input, STAI_NETWORK_IN_1_SIZE_BYTES) != LL_ATON_User_IO_NOERROR
        || !success(stai_network_set_outputs(model_context, outputs, 1))) {
        return false;
    }
    HAL_NVIC_EnableIRQ(NPU0_IRQn);
    const stai_return_code run_status = stai_network_run(model_context, STAI_MODE_ASYNC);
    return success(run_status);
}

int experiment_npu_poll(void)
{
    if (!runtime_initialized || !model_initialized) {
        return -1;
    }
    const stai_return_code code = stai_ext_network_get_nn_run_status(model_context);
    if (code == STAI_DONE) {
        completed = true;
        return 1;
    }
    if (!success(code)) {
        return -1;
    }
    if (code == STAI_RUNNING_WFE) {
        stai_ext_wfe();
    }
    if (!success(stai_ext_network_run_continue(model_context))) {
        return -1;
    }
    return 0;
}

const uint8_t *experiment_npu_output(size_t *bytes)
{
    if (!bytes || !completed) {
        return NULL;
    }
    SCB_InvalidateDCache_by_Addr(output, (sizeof(output) + 31U) & ~31U);
    __DSB();
    *bytes = STAI_NETWORK_OUT_1_SIZE_BYTES;
    return output;
}
