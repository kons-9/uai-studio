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

std::uintptr_t AlignUp(
    std::uintptr_t value,
    std::size_t alignment
)
{
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1U);
    return (value + mask) & ~mask;
}

common::Error CacheOperation(
    const buffer::Buffer &buffer,
    bool invalidate,
    bool clean
)
{
    const bool valid_region = buffer.region == buffer::Region::kCapture || buffer.region == buffer::Region::kDisplay
        || buffer.region == buffer::Region::kInference;
    if (!buffer || !valid_region || buffer.size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        || buffer.address > std::numeric_limits<std::uintptr_t>::max() - buffer.size) {
        return {common::ErrorCode::kInvalidArgument};
    }

    const std::uintptr_t start = buffer.address & ~static_cast<std::uintptr_t>(kCacheLineSize - 1U);
    const std::uintptr_t unaligned_end = buffer.address + buffer.size;
    if (unaligned_end > std::numeric_limits<std::uintptr_t>::max() - (kCacheLineSize - 1U)) {
        return {common::ErrorCode::kInvalidArgument};
    }
    const std::uintptr_t end = AlignUp(unaligned_end, kCacheLineSize);
    if (end - start > static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max())) {
        return {common::ErrorCode::kInvalidArgument};
    }
    const int32_t length = static_cast<int32_t>(end - start);
    auto *address = reinterpret_cast<std::uint32_t *>(start);

    if (clean) {
        SCB_CleanDCache_by_Addr(address, length);
    }
    if (invalidate) {
        SCB_InvalidateDCache_by_Addr(address, length);
    }
    return {common::ErrorCode::kOk};
}

} // namespace

common::Error CacheDriver::Initialize(const Writer &writer)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized};
    }
    common::Error management_status = CacheManagement::Instance().Validate(writer);
    if (!management_status.Ok())
        return management_status;

    SCB_EnableDCache();
    npu_cache_enable_clocks_and_reset();
    npu_cache_enable();
    initialized_ = true;
    return {common::ErrorCode::kOk};
}

void CacheDriver::KeepClocksOnSleep() const
{
    CacheManagement::Accessor accessor;
    if (!CacheManagement::Instance().Acquire(&accessor).Ok())
        return;
    accessor->KeepClocksOnSleep(accessor.Ownership());
}

void CacheDriver::KeepClocksOnSleep(const Writer &writer) const
{
    if (!CacheManagement::Instance().Validate(writer).Ok())
        return;
    __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();
}

common::Error CacheDriver::PrepareForDmaWrite(const buffer::Buffer &buffer) const
{
    CacheManagement::Accessor accessor;
    common::Error status = CacheManagement::Instance().Acquire(&accessor);
    if (!status.Ok())
        return status;
    return accessor->PrepareForDmaWrite(buffer, accessor.Ownership());
}

common::Error CacheDriver::PrepareForDmaWrite(
    const buffer::Buffer &buffer,
    const Writer &writer
) const
{
    common::Error status = CacheManagement::Instance().Validate(writer);
    if (!status.Ok())
        return status;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized};
    }
    return CacheOperation(buffer, true, true);
}

common::Error CacheDriver::PrepareForCpuRead(const buffer::Buffer &buffer) const
{
    CacheManagement::Accessor accessor;
    common::Error status = CacheManagement::Instance().Acquire(&accessor);
    if (!status.Ok())
        return status;
    return accessor->PrepareForCpuRead(buffer, accessor.Ownership());
}

common::Error CacheDriver::PrepareForCpuRead(
    const buffer::Buffer &buffer,
    const Writer &writer
) const
{
    common::Error status = CacheManagement::Instance().Validate(writer);
    if (!status.Ok())
        return status;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized};
    }
    return CacheOperation(buffer, true, false);
}

common::Error CacheDriver::PrepareForPeripheralRead(const buffer::Buffer &buffer) const
{
    CacheManagement::Accessor accessor;
    common::Error status = CacheManagement::Instance().Acquire(&accessor);
    if (!status.Ok())
        return status;
    return accessor->PrepareForPeripheralRead(buffer, accessor.Ownership());
}

common::Error CacheDriver::PrepareForPeripheralRead(
    const buffer::Buffer &buffer,
    const Writer &writer
) const
{
    common::Error status = CacheManagement::Instance().Validate(writer);
    if (!status.Ok())
        return status;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized};
    }
    return CacheOperation(buffer, false, true);
}

} // namespace uai::ai::cache
