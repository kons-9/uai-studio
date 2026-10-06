#include "middleware/ai_runtime/pipeline_dispatcher.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

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
            return {{common::ErrorCode::kModel}, {}, false};
        }
        const NextStep next = steps[index++];
        if (notify_during_evaluate != nullptr) {
            EXPECT_TRUE(notify_during_evaluate->Signal(*this,
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

TEST(AiRuntime, ThreeQueuesAndTargetedWakeup)
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
    ASSERT_TRUE(scheduler.Submit(future).Ok());
    EXPECT_EQ(post.RunOnce(), DispatchResult::kIdle);
    EXPECT_EQ(pre.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(future.index, 1U);
    EXPECT_NE(observed.inference_id, 0U);
    EXPECT_EQ(observed.step, 10U);
    EXPECT_LT(observed.start, observed.finish);
    EXPECT_EQ(npu.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(future.index, 2U);
    EXPECT_EQ(observed.done, 0);
    EXPECT_EQ(post.RunOnce(), DispatchResult::kIdle);
    FakeFuture stranger;
    EXPECT_EQ(runtime.Signal(stranger, WaitBitFlag::kNpuCompletion).Code(),
              common::ErrorCode::kInvalidState);
    EXPECT_EQ(post.RunOnce(), DispatchResult::kIdle);
    ASSERT_TRUE(runtime.Signal(future, WaitBitFlag::kNpuCompletion).Ok());
    EXPECT_EQ(post.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(future.index, 3U);
    EXPECT_EQ(observed.done, 1);
    EXPECT_TRUE(observed.error.Ok());
    EXPECT_EQ(observed.begin, 3);
    EXPECT_EQ(observed.end, 3);
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPreprocessCpu), DispatchResult::kIdle);
}

TEST(AiRuntime, AnyWaitReleasesOnlyRequestedFuture)
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
    ASSERT_TRUE(scheduler.Submit(first).Ok());
    ASSERT_TRUE(scheduler.Submit(second).Ok());
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPreprocessCpu), DispatchResult::kRan);
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPreprocessCpu), DispatchResult::kRan);
    ASSERT_TRUE(runtime.Signal(first, WaitBitFlag::kExternal).Ok());
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPostprocessCpu), DispatchResult::kRan);
    EXPECT_EQ(first.index, 2U);
    EXPECT_EQ(second.index, 1U);
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPostprocessCpu), DispatchResult::kIdle);
    ASSERT_TRUE(runtime.Signal(second, WaitBitFlag::kNpuCompletion).Ok());
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPostprocessCpu), DispatchResult::kRan);
}

TEST(AiRuntime, NotificationDuringEvaluateIsNotLost)
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    FakeFuture future;
    future.count = 2;
    future.steps[0] = {ExecutionContext::kNpu, WaitBitFlag::kNpuCompletion};
    future.notify_during_evaluate = &runtime;
    ASSERT_TRUE(scheduler.Submit(future).Ok());
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPreprocessCpu), DispatchResult::kRan);
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kNpu), DispatchResult::kRan);
    EXPECT_EQ(future.index, 2U);
}

TEST(AiRuntime, MultipleWaitsAndErrors)
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
    ASSERT_TRUE(scheduler.Submit(future).Ok());
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPreprocessCpu), DispatchResult::kRan);
    ASSERT_TRUE(runtime.Signal(future, WaitBitFlag::kNpuCompletion).Ok());
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPostprocessCpu), DispatchResult::kIdle);
    ASSERT_TRUE(runtime.Signal(future, WaitBitFlag::kExternal).Ok());
    future.fail = true;
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPostprocessCpu), DispatchResult::kFailed);
    EXPECT_EQ(observed.done, 1);
    EXPECT_EQ(observed.error.Code(), common::ErrorCode::kModel);
}

TEST(AiRuntime, NotReadyAndCapacity)
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    FakeFuture future;
    future.count = 1;
    future.ready = false;
    ASSERT_TRUE(scheduler.Submit(future).Ok());
    EXPECT_EQ(scheduler.Submit(future).Code(), common::ErrorCode::kInvalidState);
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPreprocessCpu), DispatchResult::kNotReady);
    EXPECT_EQ(future.index, 0U);
    future.ready = true;
    EXPECT_EQ(runtime.RunOne(ExecutionContext::kPreprocessCpu), DispatchResult::kRan);
    ASSERT_TRUE(scheduler.Submit(future).Ok()); // terminal future can be reused
    FakeFuture others[PipelineRuntime::kCapacity];
    for (std::size_t i = 0; i < PipelineRuntime::kCapacity - 1; ++i) {
        ASSERT_TRUE(scheduler.Submit(others[i]).Ok());
    }
    EXPECT_EQ(scheduler.Submit(others[PipelineRuntime::kCapacity - 1]).Code(),
              common::ErrorCode::kQueueFull);
}

struct FrameOwner {
    int completed = 0;
    AiFuture *last = nullptr;
};

void ReleaseFrame(void *context, AiFuture &future, common::Error error)
{
    auto &owner = *static_cast<FrameOwner *>(context);
    EXPECT_TRUE(error.Ok());
    ++owner.completed;
    owner.last = &future;
}

TEST(AiRuntime, PersonFramesKeepOwnershipThroughPostprocess)
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
    ASSERT_TRUE(scheduler.Submit(first).Ok());
    ASSERT_TRUE(scheduler.Submit(second).Ok());
    EXPECT_EQ(preprocess.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(preprocess.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(npu.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(owner.completed, 0);
    EXPECT_EQ(first.index, 2U);
    EXPECT_EQ(second.index, 1U);
    EXPECT_EQ(postprocess.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(owner.completed, 1);
    EXPECT_EQ(owner.last, &first);
    EXPECT_EQ(npu.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(owner.completed, 1);
    EXPECT_EQ(postprocess.RunOnce(), DispatchResult::kRan);
    EXPECT_EQ(owner.completed, 2);
    EXPECT_EQ(owner.last, &second);
}

void Lock(void *context)
{
    static_cast<std::mutex *>(context)->lock();
}

void Unlock(void *context)
{
    static_cast<std::mutex *>(context)->unlock();
}

void CountDone(void *context, AiFuture &, common::Error error)
{
    EXPECT_TRUE(error.Ok());
    ++*static_cast<std::atomic<int> *>(context);
}

TEST(AiRuntime, ConcurrentSubmissionAndDispatch)
{
    PipelineRuntime runtime;
    Scheduler scheduler(runtime);
    std::mutex mutex;
    std::atomic<int> completed{0};
    runtime.SetCriticalSection(&Lock, &Unlock, &mutex);
    runtime.SetObserver(&CountDone, &completed);
    constexpr std::size_t kJobs = 6U;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(5);
    FakeFuture futures[kJobs];
    for (auto &future : futures) {
        future.count = 3U;
        future.steps[0] = {ExecutionContext::kNpu};
        future.steps[1] = {ExecutionContext::kPostprocessCpu};
    }
    std::thread workers[]{
        std::thread([&] {
            Dispatcher dispatcher(runtime, ExecutionContext::kPreprocessCpu);
            while (completed < static_cast<int>(kJobs) &&
                   std::chrono::steady_clock::now() < deadline) {
                if (dispatcher.RunOnce() == DispatchResult::kIdle) std::this_thread::yield();
            }
        }),
        std::thread([&] {
            Dispatcher dispatcher(runtime, ExecutionContext::kNpu);
            while (completed < static_cast<int>(kJobs) &&
                   std::chrono::steady_clock::now() < deadline) {
                if (dispatcher.RunOnce() == DispatchResult::kIdle) std::this_thread::yield();
            }
        }),
        std::thread([&] {
            Dispatcher dispatcher(runtime, ExecutionContext::kPostprocessCpu);
            while (completed < static_cast<int>(kJobs) &&
                   std::chrono::steady_clock::now() < deadline) {
                if (dispatcher.RunOnce() == DispatchResult::kIdle) std::this_thread::yield();
            }
        }),
    };
    std::thread producers[]{
        std::thread([&] {
            for (std::size_t index = 0U; index < kJobs / 2U; ++index) {
                EXPECT_TRUE(scheduler.Submit(futures[index]).Ok());
            }
        }),
        std::thread([&] {
            for (std::size_t index = kJobs / 2U; index < kJobs; ++index) {
                EXPECT_TRUE(scheduler.Submit(futures[index]).Ok());
            }
        }),
    };
    for (auto &producer : producers) producer.join();
    for (auto &worker : workers) worker.join();
    EXPECT_EQ(completed.load(), static_cast<int>(kJobs));
}

} // namespace
