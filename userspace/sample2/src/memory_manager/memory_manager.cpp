#include "memory_manager/memory_manager.hpp"

#include <cstdint>
#include <cstring>

extern "C" {
#include "npu_cache.h"
#include "stm32n6570_discovery_xspi.h"
#include "stm32n6xx_hal.h"

void npu_cache_enable_clocks_and_reset(void)
{
    __HAL_RCC_CACHEAXI_CLK_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
    __HAL_RCC_CACHEAXI_FORCE_RESET();
    __HAL_RCC_CACHEAXI_RELEASE_RESET();
}
}

namespace uai::sample2::memory_manager {

using common::ErrorCode;
using Error = common::Error;

namespace {

constexpr std::uintptr_t kCapture0 = 0x91000000UL;
constexpr std::uintptr_t kCapture1 = 0x91100000UL;
constexpr std::uintptr_t kDisplay0 = 0x91200000UL;
constexpr std::uintptr_t kDisplay1 = 0x91300000UL;
constexpr std::uintptr_t kInference0 = 0x91400000UL;
constexpr std::uintptr_t kInference1 = 0x91500000UL;
constexpr std::size_t kCacheLineSize = 32U;

std::uintptr_t AlignUp(std::uintptr_t value, std::size_t alignment)
{
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1U);
    return (value + mask) & ~mask;
}

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

void EnableNpuCache()
{
    /* Match ref: CACHEAXI must have its RAM clock enabled and be taken out of
     * reset before the Neural-ART cache handle is initialized.  The generated
     * CubeMX MSP only enables CACHEAXI itself, because this project supplies
     * the HAL MSP externally. */
    npu_cache_enable_clocks_and_reset();
    npu_cache_enable();
}

HAL_StatusTypeDef ConfigureExternalMemoryClocks()
{
    RCC_PeriphCLKInitTypeDef clocks = {};
    clocks.PeriphClockSelection = RCC_PERIPHCLK_XSPI1 | RCC_PERIPHCLK_XSPI2;
    clocks.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;
    clocks.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;
    return HAL_RCCEx_PeriphCLKConfig(&clocks);
}

void ConfigurePeripheralAccess()
{
    __HAL_RCC_RIFSC_CLK_ENABLE();
    __HAL_RCC_RISAF_CLK_ENABLE();
    __HAL_RCC_IAC_CLK_ENABLE();
    __HAL_RCC_IAC_FORCE_RESET();
    __HAL_RCC_IAC_RELEASE_RESET();

    const auto configure_region = [](RISAF_TypeDef *risaf,
                                     std::uint32_t end_address) {
        risaf->REG[0].CFGR = 0U;
        risaf->REG[0].STARTR = 0U;
        risaf->REG[0].ENDR = end_address;
        risaf->REG[0].CIDCFGR = 0x000F000FUL;
        risaf->REG[0].CFGR = 0x00FF0101UL;
    };
    configure_region(RISAF2, 0x000FFFFFUL);
    configure_region(RISAF3, 0x000FFFFFUL);
    configure_region(RISAF4, 0xFFFFFFFFUL);
    configure_region(RISAF5, 0xFFFFFFFFUL);
    configure_region(RISAF6, 0xFFFFFFFFUL);
    configure_region(RISAF7, 0x00063FFFUL);

    RISAF12->REG[0].CFGR = 0U;
    RISAF12->REG[0].STARTR = 0U;
    RISAF12->REG[0].ENDR = RISAF12_LIMIT_ADDRESS_SPACE_SIZE;
    RISAF12->REG[0].CIDCFGR = (RIF_CID_MASK << 16) | RIF_CID_MASK;
    RISAF12->REG[0].CFGR = 0x00FF0101UL;

    RIMC_MasterConfig_t media_master = {};
    media_master.MasterCID = RIF_CID_1;
    media_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    RIMC_MasterConfig_t npu_master = media_master;
    /* The working ref project assigns all media/NPU masters to CID1. */
    npu_master.MasterCID = RIF_CID_1;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &npu_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D,
                                        &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP,
                                        &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1,
                                        &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2,
                                        &media_master);

    constexpr std::uint32_t secure_privileged =
        RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    const std::uint32_t peripherals[] = {
        RIF_RISC_PERIPH_INDEX_XSPI1,
        RIF_RISC_PERIPH_INDEX_XSPI2,
        RIF_RISC_PERIPH_INDEX_XSPIM,
        RIF_RISC_PERIPH_INDEX_NPU,
        RIF_RISC_PERIPH_INDEX_DMA2D,
        RIF_RISC_PERIPH_INDEX_CSI,
        RIF_RISC_PERIPH_INDEX_DCMIPP,
        RIF_RISC_PERIPH_INDEX_LTDC,
        RIF_RISC_PERIPH_INDEX_LTDCL1,
        RIF_RISC_PERIPH_INDEX_LTDCL2,
    };
    for (const std::uint32_t peripheral : peripherals) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(peripheral, secure_privileged);
    }
    const std::uint32_t memories[] = {
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
    for (const std::uint32_t memory : memories) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(memory, secure_privileged);
    }

    HAL_RIF_IAC_EnableIT(RIF_RISC_PERIPH_INDEX_XSPI2);
    HAL_RIF_IAC_EnableIT(RIF_RISC_PERIPH_INDEX_NPU);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF2);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF3);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF4);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF5);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF7);
    HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF12);
    HAL_NVIC_SetPriority(IAC_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(IAC_IRQn);
}

Error CacheOperation(const Buffer &buffer, bool invalidate, bool clean)
{
    if (!buffer) {
        return {ErrorCode::kInvalidArgument, 0U, "cache.invalid_buffer"};
    }

    const std::uintptr_t start =
        buffer.address & ~static_cast<std::uintptr_t>(kCacheLineSize - 1U);
    const std::uintptr_t end = AlignUp(buffer.address + buffer.size,
                                       kCacheLineSize);
    const int32_t length = static_cast<int32_t>(end - start);
    auto *address = reinterpret_cast<std::uint32_t *>(start);

    if (clean) {
        SCB_CleanDCache_by_Addr(address, length);
    }
    if (invalidate) {
        SCB_InvalidateDCache_by_Addr(address, length);
    }
    return {ErrorCode::kOk, 0U, "cache"};
}

} // namespace

Error MemoryManager::Make(ErrorCode code, std::uint32_t detail,
                          const char *operation)
{
    return {code, detail, operation};
}

bool MemoryManager::SameBuffer(const Buffer &lhs, const Buffer &rhs)
{
    return lhs.address == rhs.address && lhs.size == rhs.size &&
           lhs.index == rhs.index && lhs.region == rhs.region;
}

Error MemoryManager::Initialize()
{
    if (initialized_) {
        return Make(ErrorCode::kAlreadyInitialized, 0U, "memory.initialize");
    }

    EnableNpuMemory();
    EnableNpuCache();

    if (BSP_XSPI_RAM_Init(0U) != BSP_ERROR_NONE ||
        BSP_XSPI_RAM_EnableMemoryMappedMode(0U) != BSP_ERROR_NONE) {
        return Make(ErrorCode::kHardware, 0U,
                    "memory.external_psram");
    }
    for (std::uint8_t i = 0U; i < 2U; ++i) {
        display_[i].buffer = {
            (i == 0U) ? kDisplay0 : kDisplay1, kFrameBytes, i,
            Region::kDisplay};
        display_[i].state = BufferState::kFree;
        inference_[i].buffer = {
            (i == 0U) ? kInference0 : kInference1, kFrameBytes, i,
            Region::kInference};
        inference_[i].state = BufferState::kFree;
    }
    current_display_ = -1;
    capture_sequence_ = 0U;
    initialized_ = true;
    return Make(ErrorCode::kOk, 0U, "memory.initialize");
}

Error MemoryManager::InitializePeripheralAccess()
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.initialize_access");
    }
    ConfigurePeripheralAccess();
    return Make(ErrorCode::kOk, 0U, "memory.initialize_access");
}

Error MemoryManager::CaptureBuffers(std::uintptr_t *first,
                                    std::uintptr_t *second) const
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U, "memory.capture_buffers");
    }
    if (first == nullptr || second == nullptr) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.capture_buffers");
    }
    *first = kCapture0;
    *second = kCapture1;
    return Make(ErrorCode::kOk, 0U, "memory.capture_buffers");
}

Error MemoryManager::ImportCompletedCapture(std::uintptr_t address,
                                            CaptureFrame *frame)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.import_capture");
    }
    if (frame == nullptr) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.import_capture");
    }

    std::uint8_t index = 0U;
    if (address == kCapture1) {
        index = 1U;
    } else if (address != kCapture0) {
        return Make(ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.import_capture");
    }

    ++capture_sequence_;
    frame->buffer = {address, kFrameBytes, index, Region::kCapture};
    frame->sequence = capture_sequence_;
    return Make(ErrorCode::kOk, frame->sequence, "memory.import_capture");
}

Error MemoryManager::AcquireDisplayBuffer(DisplayBuffer *buffer)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.acquire_display");
    }
    if (buffer == nullptr) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.acquire_display");
    }

    for (std::uint8_t i = 0U; i < 2U; ++i) {
        if (static_cast<std::int8_t>(i) == current_display_) {
            continue;
        }
        if (display_[i].state == BufferState::kFree) {
            display_[i].state = BufferState::kFilling;
            buffer->buffer = display_[i].buffer;
            return Make(ErrorCode::kOk, i, "memory.acquire_display");
        }
    }
    return Make(ErrorCode::kNoBuffer, 0U, "memory.acquire_display");
}

Error MemoryManager::CommitDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.commit_display");
    }
    if (buffer.buffer.region != Region::kDisplay || buffer.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.commit_display");
    }

    Slot &slot = display_[buffer.buffer.index];
    if (!SameBuffer(slot.buffer, buffer.buffer) ||
        slot.state != BufferState::kFilling) {
        return Make(ErrorCode::kInvalidArgument, buffer.buffer.index,
                    "memory.commit_display");
    }

    if (current_display_ >= 0) {
        display_[static_cast<std::uint8_t>(current_display_)].state =
            BufferState::kFree;
    }
    slot.state = BufferState::kScanning;
    current_display_ = static_cast<std::int8_t>(buffer.buffer.index);
    return Make(ErrorCode::kOk, buffer.buffer.index, "memory.commit_display");
}

Error MemoryManager::ReleaseDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.release_display");
    }
    if (buffer.buffer.region != Region::kDisplay || buffer.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.release_display");
    }
    Slot &slot = display_[buffer.buffer.index];
    if (!SameBuffer(slot.buffer, buffer.buffer) ||
        slot.state != BufferState::kFilling) {
        return Make(ErrorCode::kInvalidArgument, buffer.buffer.index,
                    "memory.release_display");
    }
    slot.state = BufferState::kFree;
    return Make(ErrorCode::kOk, buffer.buffer.index, "memory.release_display");
}

Error MemoryManager::AcquireInferenceBuffer(const CaptureFrame &capture,
                                            InferenceFrame *frame)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.acquire_inference");
    }
    if (!capture || frame == nullptr || capture.buffer.region != Region::kCapture) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.acquire_inference");
    }

    for (std::uint8_t i = 0U; i < 2U; ++i) {
        if (inference_[i].state == BufferState::kFree) {
            inference_[i].state = BufferState::kReadyForAi;
            frame->buffer = inference_[i].buffer;
            frame->capture_sequence = capture.sequence;
            return Make(ErrorCode::kOk, i, "memory.acquire_inference");
        }
    }
    return Make(ErrorCode::kNoBuffer, 0U, "memory.acquire_inference");
}

Error MemoryManager::SnapshotForInference(const CaptureFrame &capture,
                                          InferenceFrame *frame)
{
    Error status = AcquireInferenceBuffer(capture, frame);
    if (!status.Ok()) {
        return status;
    }

    status = PrepareForCpuRead(capture.buffer);
    if (!status.Ok()) {
        (void)ReleaseInferenceBuffer(*frame);
        return status;
    }
    std::memcpy(reinterpret_cast<void *>(frame->buffer.address),
                reinterpret_cast<const void *>(capture.buffer.address),
                kFrameBytes);
    return Make(ErrorCode::kOk, frame->capture_sequence,
                "memory.snapshot_inference");
}

Error MemoryManager::ClaimInferenceBuffer(const InferenceFrame &frame)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.claim_inference");
    }
    if (frame.buffer.region != Region::kInference || frame.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.claim_inference");
    }
    Slot &slot = inference_[frame.buffer.index];
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        slot.state != BufferState::kReadyForAi) {
        return Make(ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.claim_inference");
    }
    slot.state = BufferState::kInUseByAi;
    return Make(ErrorCode::kOk, frame.buffer.index,
                "memory.claim_inference");
}

Error MemoryManager::ReleaseInferenceBuffer(const InferenceFrame &frame)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.release_inference");
    }
    if (frame.buffer.region != Region::kInference || frame.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.release_inference");
    }
    Slot &slot = inference_[frame.buffer.index];
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        (slot.state != BufferState::kInUseByAi &&
         slot.state != BufferState::kReadyForAi)) {
        return Make(ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.release_inference");
    }
    slot.state = BufferState::kFree;
    return Make(ErrorCode::kOk, frame.buffer.index,
                "memory.release_inference");
}

Error MemoryManager::PrepareForDmaWrite(const Buffer &buffer) const
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U, "memory.dma_write");
    }
    return CacheOperation(buffer, true, true);
}

Error MemoryManager::PrepareForCpuRead(const Buffer &buffer) const
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U, "memory.cpu_read");
    }
    return CacheOperation(buffer, true, false);
}

Error MemoryManager::PrepareForPeripheralRead(const Buffer &buffer) const
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.peripheral_read");
    }
    return CacheOperation(buffer, false, true);
}

void MemoryManager::KeepInferenceClocksOnSleep() const
{
    /* The ref application keeps every memory/bus clock used by the async
     * epoch controller alive while the CPU executes WFE.  In particular, the
     * command blobs live in AXISRAM1 and the weights are read through XSPI2;
     * leaving either path out makes the first epoch remain RUNNING forever. */
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

} // namespace uai::sample2::memory_manager
