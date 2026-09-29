#ifndef UAI_AI_NPU_RUNTIME_HPP
#define UAI_AI_NPU_RUNTIME_HPP

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "models/model.hpp"
#include "npu_runtime/inference_timing.hpp"
#include "npu_runtime/inference_dispatcher/inference_dispatcher.hpp"
#include "npu_runtime/scheduler/scheduler.hpp"
#include "npu_runtime/thread_monitor/thread_monitor.hpp"

namespace uai::ai::npu_runtime {

/*
 * Application-facing NPU facade. It owns model registration, scheduling, and
 * the inference protocol so application tasks do not depend on either the
 * scheduler or the dispatcher implementation.
 */
class NpuRuntime final {
public:
    common::Error RegisterModel(const models::ModelBinding &binding);
    common::Error Initialize(cache::CacheDriver &cache);
    common::Error Run(memory_allocator::InferenceFrame &frame,
                      memory_allocator::BoxSet *result,
                      PrefetchProvider prefetch_provider = nullptr,
                      void *prefetch_context = nullptr);
    common::Error Shutdown();

    bool Initialized() const { return initialized_; }
    const npu::Status &LastNpuStatus() const;

private:
    scheduler::Scheduler scheduler_{};
    npu::NpuDriver npu_{};
    InferenceDispatcher dispatcher_{};
    ThreadMonitor thread_monitor_{};
    InferenceTiming last_inference_timing_{};
    bool initialized_ = false;
    bool inference_started_ = false;
    npu::Status last_npu_status_{};
};

} // namespace uai::ai::npu_runtime

#endif
