#include "driver/cache_controller/cache_controller.hpp"

#include <cstdint>
#include <limits>

extern "C" {
#include "npu_cache.h"
#include "stm32n6xx_hal.h"

void npu_cache_enable_clocks_and_reset(void)
{
    __HAL_RCC_CACHEAXI_CLK_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
    __HAL_RCC_CACHEAXI_FORCE_RESET();
    __HAL_RCC_CACHEAXI_RELEASE_RESET();
}
}

namespace uai::sample2::driver {
namespace {

constexpr std::size_t kCacheLineSize = 32U;

std::uintptr_t AlignUp(std::uintptr_t value, std::size_t alignment)
{
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1U);
    return (value + mask) & ~mask;
}

common::Error CacheOperation(const memory_manager::Buffer &buffer,
                             bool invalidate, bool clean,
                             const char *operation)
{
    using common::ErrorCode;
    using memory_manager::Region;
    const bool valid_region = buffer.region == Region::kCapture ||
                              buffer.region == Region::kDisplay ||
                              buffer.region == Region::kInference;
    if (!buffer || !valid_region || buffer.size > static_cast<std::size_t>(
                                     std::numeric_limits<std::int32_t>::max()) ||
        buffer.address > std::numeric_limits<std::uintptr_t>::max() -
                             buffer.size) {
        return {ErrorCode::kInvalidArgument, 0U, operation};
    }

    const std::uintptr_t start =
        buffer.address & ~static_cast<std::uintptr_t>(kCacheLineSize - 1U);
    const std::uintptr_t unaligned_end = buffer.address + buffer.size;
    if (unaligned_end > std::numeric_limits<std::uintptr_t>::max() -
                            (kCacheLineSize - 1U)) {
        return {ErrorCode::kInvalidArgument, 0U, operation};
    }
    const std::uintptr_t end = AlignUp(unaligned_end, kCacheLineSize);
    if (end - start > static_cast<std::uintptr_t>(
                          std::numeric_limits<std::int32_t>::max())) {
        return {ErrorCode::kInvalidArgument, 0U, operation};
    }
    const int32_t length = static_cast<int32_t>(end - start);
    auto *address = reinterpret_cast<std::uint32_t *>(start);

    if (clean) {
        SCB_CleanDCache_by_Addr(address, length);
    }
    if (invalidate) {
        SCB_InvalidateDCache_by_Addr(address, length);
    }
    return {ErrorCode::kOk, 0U, operation};
}

} // namespace

common::Error CacheController::Initialize()
{
    using common::ErrorCode;
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "cache.initialize"};
    }
    npu_cache_enable_clocks_and_reset();
    npu_cache_enable();
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "cache.initialize"};
}

common::Error CacheController::PrepareForDmaWrite(
    const memory_manager::Buffer &buffer) const
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U, "cache.dma_write"};
    }
    return CacheOperation(buffer, true, true, "cache.dma_write");
}

common::Error CacheController::PrepareForCpuRead(
    const memory_manager::Buffer &buffer) const
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U, "cache.cpu_read"};
    }
    return CacheOperation(buffer, true, false, "cache.cpu_read");
}

common::Error CacheController::PrepareForPeripheralRead(
    const memory_manager::Buffer &buffer) const
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "cache.peripheral_read"};
    }
    return CacheOperation(buffer, false, true, "cache.peripheral_read");
}

} // namespace uai::sample2::driver
