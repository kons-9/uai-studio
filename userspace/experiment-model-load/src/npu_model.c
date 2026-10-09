#define ECBLOB_CONST_SECTION __attribute__((section(".model_command_blob")))
#include "network.c"
#include "stai_network.c"
#include "stm32n6xx_hal.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if STAI_NETWORK_IN_NUM != 1 || STAI_NETWORK_OUT_NUM != 1
#error "experiment-model-load requires one input and one output"
#endif

_Static_assert(STAI_NETWORK_IN_1_SIZE_BYTES > 0 && STAI_NETWORK_IN_1_SIZE_BYTES <= 0x10000,
               "input exceeds experiment buffer");
_Static_assert(STAI_NETWORK_OUT_1_SIZE_BYTES > 0 && STAI_NETWORK_OUT_1_SIZE_BYTES <= 0x10000,
               "output exceeds experiment buffer");
_Static_assert(STAI_NETWORK_IN_1_SIZE_BYTES == EXPERIMENT_MODEL_INPUT_BYTES, "input contract mismatch");
_Static_assert(STAI_NETWORK_OUT_1_SIZE_BYTES == EXPERIMENT_MODEL_OUTPUT_BYTES, "output contract mismatch");

STAI_NETWORK_CONTEXT_DECLARE(model_context, STAI_NETWORK_CONTEXT_SIZE)
static uint8_t input[(STAI_NETWORK_IN_1_SIZE_BYTES + 31U) & ~31U] __attribute__((aligned(32)));
static uint8_t output[(STAI_NETWORK_OUT_1_SIZE_BYTES + 31U) & ~31U] __attribute__((aligned(32)));
static bool runtime_initialized;
static bool model_initialized;
static bool completed;

extern stai_return_code stai_runtime_init(void);
extern stai_return_code stai_runtime_deinit(void);
extern void LL_ATON_NPU0_IRQHandler(void);

static bool success(stai_return_code code)
{
    return code < STAI_ERROR_GENERIC;
}

void NPU0_IRQHandler(void)
{
    LL_ATON_NPU0_IRQHandler();
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
    __HAL_RCC_RAMCFG_CLK_ENABLE();
    __HAL_RCC_RAMCFG_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
    RAMCFG_HandleTypeDef ram = {0};
    ram.Instance = RAMCFG_SRAM5_AXI;
    if (HAL_RAMCFG_EnableAXISRAM(&ram) != HAL_OK) {
        return false;
    }
    ram.Instance = RAMCFG_SRAM6_AXI;
    if (HAL_RAMCFG_EnableAXISRAM(&ram) != HAL_OK) {
        return false;
    }
    HAL_NVIC_SetPriority(NPU0_IRQn, 8, 0);
    HAL_NVIC_ClearPendingIRQ(NPU0_IRQn);
    completed = false;
    runtime_initialized = true;
    if (!success(stai_runtime_init())) {
        return false;
    }
    model_initialized = true;
    if (!success(stai_network_init(model_context))) {
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
    return success(stai_network_run(model_context, STAI_MODE_ASYNC));
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
    if (code != STAI_RUNNING_WFE && !success(stai_ext_network_run_continue(model_context))) {
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