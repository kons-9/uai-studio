#ifndef UAI_AI_NPU_RUNTIME_INFERENCE_DISPATCHER_HPP
#define UAI_AI_NPU_RUNTIME_INFERENCE_DISPATCHER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "npu_runtime/inference_timing.hpp"
#include "npu_runtime/scheduler/scheduler.hpp"

namespace uai::ai::npu_runtime {

using PrefetchProvider = memory_allocator::InferenceFrame *(*)(void *context);

/*
 * Bridges a selected scheduler model to the camera buffers and application
 * result format. It owns the per-inference protocol because input cache
 * maintenance, dynamic outputs, and decoder invocation are too low-level for
 * Scheduler. Model-specific input preparation and result conversion are
 * delegated through the selected Model interface.
 */
class InferenceDispatcher final {
public:
    common::Error Initialize(scheduler::Scheduler &scheduler,
                             npu::NpuDriver &npu,
                             cache::CacheDriver &cache);
    common::Error RefreshSelectedModel();
    common::Error TryInfer(memory_allocator::InferenceFrame &frame,
                           memory_allocator::BoxSet *result,
                           PrefetchProvider prefetch_provider = nullptr,
                           void *prefetch_context = nullptr);
    common::Error PrepareInputFor(const models::ModelBinding &binding,
                                  memory_allocator::InferenceFrame &frame);
    common::Error Shutdown();

    bool Initialized() const { return initialized_; }
    const npu::Status &LastNpuStatus() const { return last_npu_status_; }
    const InferenceTiming &LastTiming() const { return last_timing_; }

private:
    struct PrefetchState {
        InferenceDispatcher *dispatcher = nullptr;
        PrefetchProvider provider = nullptr;
        void *provider_context = nullptr;
        memory_allocator::InferenceFrame *frame = nullptr;
        common::Error error{};
        bool prepared = false;
    };

    static void PreparePrefetch(void *context);
    common::Error ConfigureCurrentModel();

    scheduler::Scheduler *scheduler_ = nullptr;
    npu::NpuDriver *npu_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    stai_network_info info_{};
    stai_ptr outputs_[memory_allocator::kConfig.model_output_bytes.size()]{};
    bool dynamic_outputs_ = false;
    bool initialized_ = false;
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
    npu::Status last_npu_status_{};
    InferenceTiming last_timing_{};
};

} // namespace uai::ai::npu_runtime

#endif
