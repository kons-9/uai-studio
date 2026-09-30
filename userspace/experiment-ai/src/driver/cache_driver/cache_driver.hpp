#ifndef UAI_AI_CACHE_DRIVER_HPP
#define UAI_AI_CACHE_DRIVER_HPP

#include "common/error.hpp"
#include "memory_allocator/memory_allocator.hpp"

namespace uai::ai::cache {

/* Owns CACHEAXI startup and cache maintenance for DMA/NPU memory transfers. */
class CacheDriver final {
public:
    common::Error Initialize();
    common::Error PrepareForDmaWrite(
        const memory_allocator::Buffer &buffer) const;
    common::Error PrepareForCpuRead(
        const memory_allocator::Buffer &buffer) const;
    common::Error PrepareForPeripheralRead(
        const memory_allocator::Buffer &buffer) const;
    void KeepClocksOnSleep() const;

private:
    bool initialized_ = false;
};

} // namespace uai::ai::cache

#endif
