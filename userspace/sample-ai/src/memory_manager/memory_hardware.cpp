#include "memory_manager/memory_hardware.hpp"

#include <cstdint>

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::memory_manager {
namespace {

void EnableNpuMemory()
{
    __HAL_RCC_NPU_CLK_ENABLE();
    __HAL_RCC_NPU_FORCE_RESET();
    __HAL_RCC_NPU_RELEASE_RESET();
    __HAL_RCC_FLEXRAM_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM1_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM2_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
    __HAL_RCC_RAMCFG_CLK_ENABLE();

    RAMCFG_HandleTypeDef ramcfg = {};
    ramcfg.Instance = RAMCFG_SRAM2_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM3_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM4_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM5_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM6_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_SYSCFG_EnableInterleavingCpuRam();
}

} // namespace

common::Error MemoryHardware::Initialize()
{
    using common::ErrorCode;
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U,
                "memory_hardware.initialize"};
    }
    EnableNpuMemory();
    const common::Error cache_status = cache_.Initialize();
    if (!cache_status.Ok()) {
        return cache_status;
    }
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "memory_hardware.initialize"};
}

common::Error MemoryHardware::InitializeExternalMemory(int *nor_status)
{
    using common::ErrorCode;
    if (!initialized_) {
        return {ErrorCode::kNotInitialized, 0U,
                "memory_hardware.external_initialize"};
    }
    if (nor_status == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U,
                "memory_hardware.external_initialize"};
    }

    *nor_status = -1;
    if (!psram_.Initialize()) {
        return {ErrorCode::kHardware, 0U,
                "memory_hardware.psram_initialize"};
    }

    /* NOR failure is non-fatal: live camera preview can run without model
     * weights, while callers use nor_status to disable inference. */
    *nor_status = nor_.Initialize();
    return {ErrorCode::kOk, static_cast<std::uint32_t>(*nor_status),
            "memory_hardware.external_initialize"};
}

common::Error MemoryHardware::InitializePeripheralAccess()
{
    using common::ErrorCode;
    if (!initialized_) {
        return {ErrorCode::kNotInitialized, 0U,
                "memory_hardware.initialize_access"};
    }
    return rif_.Initialize();
}

common::Error MemoryHardware::PrepareForDmaWrite(const Buffer &buffer) const
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "memory_hardware.dma_write"};
    }
    return cache_.PrepareForDmaWrite(buffer);
}

common::Error MemoryHardware::PrepareForCpuRead(const Buffer &buffer) const
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "memory_hardware.cpu_read"};
    }
    return cache_.PrepareForCpuRead(buffer);
}

common::Error MemoryHardware::PrepareForPeripheralRead(
    const Buffer &buffer) const
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "memory_hardware.peripheral_read"};
    }
    return cache_.PrepareForPeripheralRead(buffer);
}

void MemoryHardware::KeepInferenceClocksOnSleep() const
{
    __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();
    __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();
    __HAL_RCC_NPU_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_LTDC_CLK_SLEEP_ENABLE();
    __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE();
    __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();
    __HAL_RCC_CSI_CLK_SLEEP_ENABLE();
    __HAL_RCC_RAMCFG_CLK_SLEEP_ENABLE();
    __HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM1_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM2_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
}

} // namespace uai::ai::memory_manager
