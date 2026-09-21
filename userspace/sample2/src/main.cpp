#include <stdint.h>

#include <tk/tkernel.h>

extern "C" {
#include <tm/tmonitor.h>

#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_xspi.h"
#include "npu_cache.h"
#include "stai_network.h"
}

/* The generated model owns the concrete context size and alignment. */
STAI_NETWORK_CONTEXT_DECLARE(network_context, STAI_NETWORK_CONTEXT_SIZE)

extern "C" void npu_cache_enable_clocks_and_reset(void)
{
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
    __HAL_RCC_CACHEAXI_CLK_ENABLE();
    __HAL_RCC_CACHEAXI_FORCE_RESET();
    __HAL_RCC_CACHEAXI_RELEASE_RESET();
}

extern "C" void npu_cache_disable_clocks_and_reset(void)
{
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_DISABLE();
    __HAL_RCC_CACHEAXI_CLK_DISABLE();
    __HAL_RCC_CACHEAXI_FORCE_RESET();
}

/* cubemx_entry.c suspends the HAL SysTick before µT-Kernel starts.  The DK
 * NOR BSP uses HAL_GetTick()/HAL_Delay() while entering memory-mapped mode,
 * so bridge those two weak HAL hooks to the running µT-Kernel clock. */
extern "C" uint32_t HAL_GetTick(void)
{
    SYSTIM time = {};
    if (tk_get_otm(&time) != E_OK) {
        return 0;
    }
    return time.lo;
}

extern "C" void HAL_Delay(uint32_t milliseconds)
{
    if (milliseconds != 0U) {
        tk_dly_tsk(milliseconds);
    }
}

namespace {

void put(const char *message)
{
    tm_putstring(reinterpret_cast<const UB *>(message));
}

[[noreturn]] void stop(const char *message)
{
    put("sample2: ");
    put(message);
    put("\n");
    for (;;) {
        tk_dly_tsk(1000);
    }
}

void check_status(stai_return_code status, const char *where)
{
    if (status != STAI_SUCCESS) {
        tm_printf(reinterpret_cast<const UB *>("sample2: %s failed (%x)\n"),
                  where, static_cast<unsigned int>(status));
        stop("STEdgeAI returned an error");
    }
}

void check_hal(HAL_StatusTypeDef status, const char *where)
{
    if (status != HAL_OK) {
        tm_printf(reinterpret_cast<const UB *>("sample2: %s failed (%x)\n"),
                  where, static_cast<unsigned int>(status));
        stop("HAL returned an error");
    }
}

void check_bsp(int32_t status, const char *where)
{
    if (status != BSP_ERROR_NONE) {
        tm_printf(reinterpret_cast<const UB *>("sample2: %s failed (%x)\n"),
                  where, static_cast<unsigned int>(status));
        stop("BSP returned an error");
    }
}

void cache_clean(void *address, stai_size size)
{
    if (address == nullptr || size == 0) {
        return;
    }

    const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~uintptr_t(31);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + 31U) & ~uintptr_t(31);
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(start),
                             static_cast<int32_t>(end - start));
}

void cache_invalidate(void *address, stai_size size)
{
    if (address == nullptr || size == 0) {
        return;
    }

    const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~uintptr_t(31);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + 31U) & ~uintptr_t(31);
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(start),
                                  static_cast<int32_t>(end - start));
}

void enable_npu_ram()
{
    __HAL_RCC_NPU_CLK_ENABLE();
    __HAL_RCC_NPU_FORCE_RESET();
    __HAL_RCC_NPU_RELEASE_RESET();

    __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
    __HAL_RCC_RAMCFG_CLK_ENABLE();

    RAMCFG_HandleTypeDef ramcfg = {};
    ramcfg.Instance = RAMCFG_SRAM3_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM4_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM5_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM6_AXI;
    HAL_RAMCFG_EnableAXISRAM(&ramcfg);
}

void configure_security()
{
    __HAL_RCC_RIFSC_CLK_ENABLE();

    RIMC_MasterConfig_t master = {};
    master.MasterCID = RIF_CID_1;
    master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &master);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_NPU,
                                          RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
}

void configure_xspi_clock()
{
    RCC_PeriphCLKInitTypeDef clocks = {};
    clocks.PeriphClockSelection = RCC_PERIPHCLK_XSPI1 | RCC_PERIPHCLK_XSPI2;
    clocks.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;
    clocks.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;
    check_hal(HAL_RCCEx_PeriphCLKConfig(&clocks), "XSPI2 clock");
}

void initialize_nor()
{
    check_bsp(BSP_XSPI_RAM_Init(0), "XSPI1 PSRAM init");
    check_bsp(BSP_XSPI_RAM_EnableMemoryMappedMode(0), "XSPI1 memory map");

    BSP_XSPI_NOR_Init_t nor = {};
    nor.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
    nor.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;
    check_bsp(BSP_XSPI_NOR_Init(0, &nor), "XSPI2 NOR init");
    check_bsp(BSP_XSPI_NOR_EnableMemoryMappedMode(0), "XSPI2 memory map");
}

void keep_inference_clocks_on_sleep()
{
    __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();
    __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();
    __HAL_RCC_NPU_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
}

void initialize_hardware()
{
    SCB_EnableICache();
    MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_DCACTIVE_Msk;
    SCB_EnableDCache();

    /* HAL_Init(), clocks, GPIO and the console UART are already done by the
     * pre-kernel CubeMX entry point used by sample0. */
    configure_xspi_clock();
    enable_npu_ram();
    configure_security();
    npu_cache_enable();
    initialize_nor();
    keep_inference_clocks_on_sleep();

    HAL_NVIC_SetPriority(NPU0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(NPU0_IRQn);
}

void fill_input(stai_ptr input, stai_size size, uint8_t seed)
{
    if (input == nullptr) {
        stop("generated model returned a null input");
    }

    for (stai_size index = 0; index < size; ++index) {
        input[index] = static_cast<uint8_t>(seed + (index * 13U));
    }
    cache_clean(input, size);
}

void print_output(const stai_tensor &tensor, stai_ptr output)
{
    if (output == nullptr) {
        stop("generated model returned a null output");
    }

    cache_invalidate(output, tensor.size_bytes);
    tm_printf(reinterpret_cast<const UB *>("sample2: output bytes=%u format=0x%x\n"),
              static_cast<unsigned int>(tensor.size_bytes),
              static_cast<unsigned int>(tensor.format));

    const stai_size preview_size = tensor.size_bytes < 16U ? tensor.size_bytes : 16U;
    put("sample2: output[0..15]=");
    for (stai_size index = 0; index < preview_size; ++index) {
        tm_printf(reinterpret_cast<const UB *>("%02x "),
                  static_cast<unsigned int>(output[index]));
    }
    put("\n");

    if (tensor.format == STAI_FORMAT_FLOAT32 && tensor.size_bytes >= sizeof(float)) {
        const float *values = reinterpret_cast<const float *>(output);
        const stai_size count = tensor.size_bytes / sizeof(float);
        stai_size best = 0;
        for (stai_size index = 1; index < count; ++index) {
            if (values[index] > values[best]) {
                best = index;
            }
        }
        tm_printf(reinterpret_cast<const UB *>("sample2: float32 argmax=%u\n"),
                  static_cast<unsigned int>(best));
    }
}

void run_network()
{
    stai_network_info info = {};
    check_status(stai_network_get_info(network_context, &info), "stai_network_get_info");

    stai_ptr inputs[STAI_NETWORK_IN_NUM] = {};
    stai_size input_count = STAI_NETWORK_IN_NUM;
    check_status(stai_network_get_inputs(network_context, inputs, &input_count),
                 "stai_network_get_inputs");
    if (input_count != info.n_inputs) {
        stop("generated model input count is inconsistent");
    }
    for (stai_size index = 0; index < input_count; ++index) {
        fill_input(inputs[index], info.inputs[index].size_bytes,
                   static_cast<uint8_t>(0x20U + index));
    }

    stai_ptr outputs[STAI_NETWORK_OUT_NUM] = {};
    stai_size output_count = STAI_NETWORK_OUT_NUM;
    check_status(stai_network_get_outputs(network_context, outputs, &output_count),
                 "stai_network_get_outputs");
    if (output_count != info.n_outputs) {
        stop("generated model output count is inconsistent");
    }

    check_status(stai_network_run(network_context, STAI_MODE_SYNC),
                 "stai_network_run");

    for (stai_size index = 0; index < output_count; ++index) {
        print_output(info.outputs[index], outputs[index]);
    }
}

} // namespace

extern "C" INT usermain(void)
{
    put("sample2: Neural-ART inference smoke test\n");
    initialize_hardware();

    check_status(stai_runtime_init(), "stai_runtime_init");
    check_status(stai_network_init(network_context), "stai_network_init");

    stai_network_info info = {};
    check_status(stai_network_get_info(network_context, &info), "stai_network_get_info");
    tm_printf(reinterpret_cast<const UB *>("sample2: model=%s inputs=%u outputs=%u\n"),
              info.c_model_name != nullptr ? info.c_model_name : "(unknown)",
              static_cast<unsigned int>(info.n_inputs),
              static_cast<unsigned int>(info.n_outputs));

    for (;;) {
        run_network();
        tk_dly_tsk(2000);
    }
}
