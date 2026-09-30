#ifndef UAI_AI_RUNTIME_PIPELINE_DISPATCHER_HPP
#define UAI_AI_RUNTIME_PIPELINE_DISPATCHER_HPP

#include "middleware/ai_runtime/pipeline_runtime.hpp"

namespace uai::ai::ai_runtime {

/* Scheduler is the only entrance for new inferences. Model selection policy
 * stays outside this queue engine; no implicit reordering is performed. */
class Scheduler final {
public:
    explicit Scheduler(PipelineRuntime &runtime) : runtime_(runtime) {}
    common::Error Submit(AiFuture &future) { return runtime_.Submit(future); }
private:
    PipelineRuntime &runtime_;
};

/* Each worker/RTOS task owns one dispatcher. RunOnce never blocks. */
class Dispatcher final {
public:
    Dispatcher(PipelineRuntime &runtime, ExecutionContext lane)
        : runtime_(runtime), lane_(lane) {}
    DispatchResult RunOnce() { return runtime_.RunOne(lane_); }
private:
    PipelineRuntime &runtime_;
    ExecutionContext lane_;
};

} // namespace uai::ai::ai_runtime

#endif
