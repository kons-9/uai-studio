#ifndef UAI_AI_CACHE_CONTROLLER_HPP
#define UAI_AI_CACHE_CONTROLLER_HPP

#include "common/error.hpp"
#include "memory_manager/memory_manager.hpp"

namespace uai::ai::driver {

/* Owns CACHEAXI startup and cache maintenance for DMA/NPU memory transfers. */
class CacheController final {
public:
    common::Error Initialize();
    common::Error PrepareForDmaWrite(
        const memory_manager::Buffer &buffer) const;
    common::Error PrepareForCpuRead(
        const memory_manager::Buffer &buffer) const;
    common::Error PrepareForPeripheralRead(
        const memory_manager::Buffer &buffer) const;

private:
    bool initialized_ = false;
};

} // namespace uai::ai::driver

#endif
