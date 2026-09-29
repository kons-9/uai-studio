#include "middleware/ai_runtime/pipeline.hpp"

#include <cassert>
#include <cstdint>

using namespace uai::ai;
using namespace uai::ai::ai_runtime;

namespace {

struct FakeFuture final : AiFuture {
    NextStep steps[4]{};
    std::size_t count = 0;
    std::size_t index = 0;
    bool ready = true;
    bool fail = false;
    PipelineRuntime *notify_during_evaluate = nullptr;

    AiModelId model_id() const override { return static_cast<AiModelId>(3U); }
    std::uint32_t step_id() const override { return static_cast<std::uint32_t>(index + 10U); }
    bool is_ready() const override { return ready; }
    AiRuntimeResult Evaluate() override
    {
        if (fail) {
            return {{common::ErrorCode::kModel, 0U, "test.failure"}, {}, false};
        }
        const NextStep next = steps[index++];
        if (notify_during_evaluate != nullptr) {
            assert(notify_during_evaluate->Signal(*this,
                                                  WaitBitFlag::kNpuCompletion).Ok());
            notify_during_evaluate = nullptr;
        }
        return {{}, next, index == count};
    }
};

struct Observations {
    int begin = 0;
    int end = 0;
    int done = 0;
    common::Error error{};
    std::uint32_t inference_id = 0;
    std::uint32_t step = 0;
    ExecutionContext lane = ExecutionContext::kPreprocessCpu;
    std::uint32_t start = 0;
    std::uint32_t finish = 0;
};

std::uint32_t Clock(void *context)
{
    auto *time = static_cast<std::uint32_t *>(context);
    return ++*time;
}
void Trace(void *context, const StepTrace &event)
{
    auto &observed = *static_cast<Observations *>(context);
    observed.inference_id = event.inference_id;
    observed.step = event.step_id;
    observed.lane = event.context;
    if (event.begin) {
        ++observed.begin;
        observed.start = event.timestamp;
    } else {
        ++observed.end;
        observed.finish = event.timestamp;
    }
}
void Done(void *context, AiFuture &, common::Error error)
{
    auto &observed = *static_cast<Observations *>(context);
    ++observed.done;
    observed.error = error;
}

void ThreeQueuesAndTargetedWakeup()
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    Dispatcher pre(runtime, ExecutionContext::kPreprocessCpu);
    Dispatcher npu(runtime, ExecutionContext::kNpu);
    Dispatcher post(runtime, ExecutionContext::kPostprocessCpu);
    Observations observed;
    std::uint32_t ticks = 0;
    runtime.SetObserver(&Done, &observed);
    runtime.SetTrace(&Trace, &observed, &Clock, &ticks);
    FakeFuture future;
    future.count = 3;
    future.steps[0] = {ExecutionContext::kNpu};
    future.steps[1] = {ExecutionContext::kPostprocessCpu,
                       WaitBitFlag::kNpuCompletion};
    assert(scheduler.Submit(future).Ok());
    assert(post.RunOnce() == DispatchResult::kIdle);
    assert(pre.RunOnce() == DispatchResult::kRan);
    assert(future.index == 1 && observed.inference_id != 0);
    assert(observed.step == 10 && observed.start < observed.finish);
    assert(npu.RunOnce() == DispatchResult::kRan);
    assert(future.index == 2 && observed.done == 0);
    assert(post.RunOnce() == DispatchResult::kIdle);
    FakeFuture stranger;
    assert(runtime.Signal(stranger, WaitBitFlag::kNpuCompletion).code ==
           common::ErrorCode::kInvalidState);
    assert(post.RunOnce() == DispatchResult::kIdle);
    assert(runtime.Signal(future, WaitBitFlag::kNpuCompletion).Ok());
    assert(post.RunOnce() == DispatchResult::kRan);
    assert(future.index == 3 && observed.done == 1 && observed.error.Ok());
    assert(observed.begin == 3 && observed.end == 3);
    assert(runtime.RunOne(ExecutionContext::kPreprocessCpu) == DispatchResult::kIdle);
}

void AnyWaitReleasesOnlyRequestedFuture()
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    FakeFuture first;
    FakeFuture second;
    first.count = second.count = 2;
    first.steps[0] = {ExecutionContext::kPostprocessCpu,
                      WaitBitFlag::kNpuCompletion | WaitBitFlag::kExternal,
                      WaitMode::kAny};
    second.steps[0] = first.steps[0];
    assert(scheduler.Submit(first).Ok() && scheduler.Submit(second).Ok());
    assert(runtime.RunOne(ExecutionContext::kPreprocessCpu) == DispatchResult::kRan);
    assert(runtime.RunOne(ExecutionContext::kPreprocessCpu) == DispatchResult::kRan);
    assert(runtime.Signal(first, WaitBitFlag::kExternal).Ok());
    assert(runtime.RunOne(ExecutionContext::kPostprocessCpu) == DispatchResult::kRan);
    assert(first.index == 2 && second.index == 1);
    assert(runtime.RunOne(ExecutionContext::kPostprocessCpu) == DispatchResult::kIdle);
    assert(runtime.Signal(second, WaitBitFlag::kNpuCompletion).Ok());
    assert(runtime.RunOne(ExecutionContext::kPostprocessCpu) == DispatchResult::kRan);
}

void NotificationDuringEvaluateIsNotLost()
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    FakeFuture future;
    future.count = 2;
    future.steps[0] = {ExecutionContext::kNpu, WaitBitFlag::kNpuCompletion};
    future.notify_during_evaluate = &runtime;
    assert(scheduler.Submit(future).Ok());
    assert(runtime.RunOne(ExecutionContext::kPreprocessCpu) == DispatchResult::kRan);
    assert(runtime.RunOne(ExecutionContext::kNpu) == DispatchResult::kRan);
    assert(future.index == 2);
}

void MultipleWaitsAndErrors()
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    Observations observed;
    runtime.SetObserver(&Done, &observed);
    FakeFuture future;
    future.count = 2;
    future.steps[0] = {ExecutionContext::kPostprocessCpu,
                       WaitBitFlag::kNpuCompletion | WaitBitFlag::kExternal,
                       WaitMode::kAll};
    assert(scheduler.Submit(future).Ok());
    assert(runtime.RunOne(ExecutionContext::kPreprocessCpu) == DispatchResult::kRan);
    assert(runtime.Signal(future, WaitBitFlag::kNpuCompletion).Ok());
    assert(runtime.RunOne(ExecutionContext::kPostprocessCpu) == DispatchResult::kIdle);
    assert(runtime.Signal(future, WaitBitFlag::kExternal).Ok());
    future.fail = true;
    assert(runtime.RunOne(ExecutionContext::kPostprocessCpu) == DispatchResult::kFailed);
    assert(observed.done == 1 && observed.error.code == common::ErrorCode::kModel);
}

void NotReadyAndCapacity()
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    FakeFuture future;
    future.count = 1;
    future.ready = false;
    assert(scheduler.Submit(future).Ok());
    assert(scheduler.Submit(future).code == common::ErrorCode::kInvalidState);
    assert(runtime.RunOne(ExecutionContext::kPreprocessCpu) == DispatchResult::kNotReady);
    assert(future.index == 0);
    future.ready = true;
    assert(runtime.RunOne(ExecutionContext::kPreprocessCpu) == DispatchResult::kRan);
    assert(scheduler.Submit(future).Ok()); // terminal future can be reused
    FakeFuture others[PipelineRuntime::kCapacity];
    for (std::size_t i = 0; i < PipelineRuntime::kCapacity - 1; ++i) {
        assert(scheduler.Submit(others[i]).Ok());
    }
    assert(scheduler.Submit(others[PipelineRuntime::kCapacity - 1]).code ==
           common::ErrorCode::kQueueFull);
}

struct FrameOwner {
    int completed = 0;
    AiFuture *last = nullptr;
};

void ReleaseFrame(void *context, AiFuture &future, common::Error error)
{
    auto &owner = *static_cast<FrameOwner *>(context);
    assert(error.Ok());
    ++owner.completed;
    owner.last = &future;
}

void PersonFramesKeepOwnershipThroughPostprocess()
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    Dispatcher preprocess(runtime, ExecutionContext::kPreprocessCpu);
    Dispatcher npu(runtime, ExecutionContext::kNpu);
    Dispatcher postprocess(runtime, ExecutionContext::kPostprocessCpu);
    FrameOwner owner{};
    runtime.SetObserver(&ReleaseFrame, &owner);
    FakeFuture first;
    FakeFuture second;
    first.count = second.count = 3;
    first.steps[0] = second.steps[0] = {ExecutionContext::kNpu};
    first.steps[1] = second.steps[1] = {ExecutionContext::kPostprocessCpu};
    assert(scheduler.Submit(first).Ok());
    assert(scheduler.Submit(second).Ok());
    assert(preprocess.RunOnce() == DispatchResult::kRan);
    assert(preprocess.RunOnce() == DispatchResult::kRan);
    assert(npu.RunOnce() == DispatchResult::kRan);
    assert(owner.completed == 0 && first.index == 2 && second.index == 1);
    assert(postprocess.RunOnce() == DispatchResult::kRan);
    assert(owner.completed == 1 && owner.last == &first);
    assert(npu.RunOnce() == DispatchResult::kRan);
    assert(owner.completed == 1);
    assert(postprocess.RunOnce() == DispatchResult::kRan);
    assert(owner.completed == 2 && owner.last == &second);
}

} // namespace

int main()
{
    ThreeQueuesAndTargetedWakeup();
    AnyWaitReleasesOnlyRequestedFuture();
    NotificationDuringEvaluateIsNotLost();
    MultipleWaitsAndErrors();
    NotReadyAndCapacity();
    PersonFramesKeepOwnershipThroughPostprocess();
}