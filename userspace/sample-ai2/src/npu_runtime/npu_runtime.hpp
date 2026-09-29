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

/* NPU owner chooses a model before a request crosses to the CPU task.
 * The worker never inspects or mutates the NPU scheduler. */
struct PreparationTarget {
    const models::Model *model = nullptr;
    models::ModelKind kind = models::ModelKind::kPerson;
    explicit operator bool() const { return model != nullptr; }
};

/*
 * NPU-owner-task facade. It owns model registration, scheduling and the STAI
 * execution protocol. CPU input preparation is delegated via task messages;
 * only InputTarget() is exposed so the owner can request the correct model.
 */
class NpuRuntime final {
public:
    common::Error RegisterModel(const models::ModelBinding &binding);
    common::Error Initialize(cache::CacheDriver &cache);
    common::Error Begin(memory_allocator::InferenceFrame &frame,
                        bool select_model = false);
    common::Error Wait(InferenceCompletion *completion);
    common::Error Complete(const InferenceCompletion &completion,
                           memory_allocator::BoxSet *result);
    common::Error Run(memory_allocator::InferenceFrame &frame,
                      memory_allocator::BoxSet *result);
    common::Error Shutdown();

    PreparationTarget InputTarget(bool following_current) const;

    bool Initialized() const { return initialized_; }
    const npu::Status &LastNpuStatus() const;

private:
    scheduler::Scheduler scheduler_{};
    npu::NpuDriver npu_{};
    InferenceDispatcher dispatcher_{};
    ThreadMonitor thread_monitor_{};
    InferenceTiming last_inference_timing_{};
    bool monitor_operation_active_ = false;
    bool initialized_ = false;
    bool inference_started_ = false;
    npu::Status last_npu_status_{};
};

} // namespace uai::ai::npu_runtime

#endif
