#ifndef UAI_AI_CACHE_DRIVER_HPP
#define UAI_AI_CACHE_DRIVER_HPP

#include "common/error.hpp"
#include "driver/driver_ownership.hpp"
#include "middleware/memory/buffer_types.hpp"

namespace uai::ai::cache {

/* Owns CACHEAXI startup and cache maintenance for DMA/NPU memory transfers. */
class CacheDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    common::Error Initialize();
    common::Error AcquireWriter(Writer *writer, TMO timeout = TMO_FEVR) const
    { return management_.Acquire(writer, timeout); }
    common::Error PrepareForDmaWrite(
        const memory_allocator::Buffer &buffer) const;
    common::Error PrepareForDmaWrite(
        const memory_allocator::Buffer &buffer, const Writer &writer) const;
    common::Error PrepareForCpuRead(
        const memory_allocator::Buffer &buffer) const;
    common::Error PrepareForCpuRead(
        const memory_allocator::Buffer &buffer, const Writer &writer) const;
    common::Error PrepareForPeripheralRead(
        const memory_allocator::Buffer &buffer) const;
    common::Error PrepareForPeripheralRead(
        const memory_allocator::Buffer &buffer, const Writer &writer) const;
    void KeepClocksOnSleep() const;
    void KeepClocksOnSleep(const Writer &writer) const;

private:
    driver::ResourceManagement management_{};
    bool initialized_ = false;
};

} // namespace uai::ai::cache

#endif
