#ifndef UAI_AI_INFERENCE_DISPATCHER_HPP
#define UAI_AI_INFERENCE_DISPATCHER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "npu_scheduler/npu_scheduler.hpp"

namespace uai::ai {

/*
 * Bridges a selected scheduler model to the camera buffers and application
 * result format. It owns the per-inference protocol because input cache
 * maintenance, dynamic outputs, decoder invocation, and BoxSet conversion
 * are too application-specific for NpuScheduler.
 */
class InferenceDispatcher final {
public:
    common::Error Initialize(npu_scheduler::NpuScheduler &scheduler,
                             memory_allocator::MemoryAllocator &memory,
                             cache::CacheDriver &cache);
    common::Error SelectNextModel();
    common::Error TryInfer(const memory_allocator::InferenceFrame &frame,
                           memory_allocator::BoxSet *result);

    bool Initialized() const { return initialized_; }
    models::ModelKind CurrentModel() const;
    const models::ModelDescriptor *CurrentDescriptor() const;
    const npu::Status &LastNpuStatus() const;

private:
    common::Error ConfigureCurrentModel();

    npu_scheduler::NpuScheduler *scheduler_ = nullptr;
    memory_allocator::MemoryAllocator *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    stai_network_info info_{};
    stai_ptr outputs_[memory_allocator::kMaxModelOutputs]{};
    bool dynamic_outputs_ = false;
    bool initialized_ = false;
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
};

} // namespace uai::ai

#endif
