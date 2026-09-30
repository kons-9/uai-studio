#include "driver/cache_driver/cache_driver.hpp"

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

namespace uai::ai::cache {
namespace {

constexpr std::size_t kCacheLineSize = 32U;

std::uintptr_t AlignUp(std::uintptr_t value, std::size_t alignment)
{
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1U);
    return (value + mask) & ~mask;
}

common::Error CacheOperation(const memory_allocator::Buffer &buffer,
                             bool invalidate, bool clean,
                             const char *operation)
{
    const bool valid_region =
        buffer.region == memory_allocator::Region::kCapture ||
        buffer.region == memory_allocator::Region::kDisplay ||
        buffer.region == memory_allocator::Region::kInference;
    if (!buffer || !valid_region || buffer.size > static_cast<std::size_t>(
                                     std::numeric_limits<std::int32_t>::max()) ||
        buffer.address > std::numeric_limits<std::uintptr_t>::max() -
                             buffer.size) {
        return {common::ErrorCode::kInvalidArgument, 0U, operation};
    }

    const std::uintptr_t start =
        buffer.address & ~static_cast<std::uintptr_t>(kCacheLineSize - 1U);
    const std::uintptr_t unaligned_end = buffer.address + buffer.size;
    if (unaligned_end > std::numeric_limits<std::uintptr_t>::max() -
                            (kCacheLineSize - 1U)) {
        return {common::ErrorCode::kInvalidArgument, 0U, operation};
    }
    const std::uintptr_t end = AlignUp(unaligned_end, kCacheLineSize);
    if (end - start > static_cast<std::uintptr_t>(
                          std::numeric_limits<std::int32_t>::max())) {
        return {common::ErrorCode::kInvalidArgument, 0U, operation};
    }
    const int32_t length = static_cast<int32_t>(end - start);
    auto *address = reinterpret_cast<std::uint32_t *>(start);

    if (clean) {
        SCB_CleanDCache_by_Addr(address, length);
    }
    if (invalidate) {
        SCB_InvalidateDCache_by_Addr(address, length);
    }
    return {common::ErrorCode::kOk, 0U, operation};
}

} // namespace

common::Error CacheDriver::Initialize()
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U, "cache.initialize"};
    }
    common::Error management_status = management_.Initialize("cache.management");
    if (!management_status.Ok() &&
        management_status.code != common::ErrorCode::kAlreadyInitialized) {
        return management_status;
    }
    Writer writer;
    management_status = management_.Acquire(&writer);
    if (!management_status.Ok()) return management_status;

    SCB_EnableDCache();
    npu_cache_enable_clocks_and_reset();
    npu_cache_enable();
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "cache.initialize"};
}

void CacheDriver::KeepClocksOnSleep() const
{
    Writer writer;
    if (!management_.Acquire(&writer).Ok()) return;
    KeepClocksOnSleep(writer);
}

void CacheDriver::KeepClocksOnSleep(const Writer &writer) const
{
    if (!management_.Validate(writer, "cache.keep_clocks").Ok()) return;
    __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();
}

common::Error CacheDriver::PrepareForDmaWrite(
    const memory_allocator::Buffer &buffer) const
{
    Writer writer;
    common::Error status = management_.Acquire(&writer);
    if (!status.Ok()) return status;
    return PrepareForDmaWrite(buffer, writer);
}

common::Error CacheDriver::PrepareForDmaWrite(
    const memory_allocator::Buffer &buffer, const Writer &writer) const
{
    common::Error status = management_.Validate(writer, "cache.dma_write");
    if (!status.Ok()) return status;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U, "cache.dma_write"};
    }
    return CacheOperation(buffer, true, true, "cache.dma_write");
}

common::Error CacheDriver::PrepareForCpuRead(
    const memory_allocator::Buffer &buffer) const
{
    Writer writer;
    common::Error status = management_.Acquire(&writer);
    if (!status.Ok()) return status;
    return PrepareForCpuRead(buffer, writer);
}

common::Error CacheDriver::PrepareForCpuRead(
    const memory_allocator::Buffer &buffer, const Writer &writer) const
{
    common::Error status = management_.Validate(writer, "cache.cpu_read");
    if (!status.Ok()) return status;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U, "cache.cpu_read"};
    }
    return CacheOperation(buffer, true, false, "cache.cpu_read");
}

common::Error CacheDriver::PrepareForPeripheralRead(
    const memory_allocator::Buffer &buffer) const
{
    Writer writer;
    common::Error status = management_.Acquire(&writer);
    if (!status.Ok()) return status;
    return PrepareForPeripheralRead(buffer, writer);
}

common::Error CacheDriver::PrepareForPeripheralRead(
    const memory_allocator::Buffer &buffer, const Writer &writer) const
{
    common::Error status =
        management_.Validate(writer, "cache.peripheral_read");
    if (!status.Ok()) return status;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "cache.peripheral_read"};
    }
    return CacheOperation(buffer, false, true, "cache.peripheral_read");
}

} // namespace uai::ai::cache
