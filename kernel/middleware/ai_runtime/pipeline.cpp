#include "middleware/ai_runtime/pipeline_runtime.hpp"

namespace uai::ai::ai_runtime {
namespace {

std::uint32_t Bits(WaitBitFlag flags)
{
    return static_cast<std::uint32_t>(flags);
}

} // namespace

void PipelineRuntime::Queue::Push(std::size_t index)
{
    items[(head + count) % kCapacity] = index;
    ++count;
}

std::size_t PipelineRuntime::Queue::Pop()
{
    const std::size_t index = items[head];
    head = (head + 1U) % kCapacity;
    --count;
    return index;
}

std::size_t PipelineRuntime::LaneIndex(ExecutionContext lane)
{
    return static_cast<std::size_t>(lane);
}

bool PipelineRuntime::Satisfied(const Slot &slot)
{
    const std::uint32_t requested = Bits(slot.next.wait_flags);
    const std::uint32_t received = Bits(slot.received) & requested;
    return requested == 0U ||
           (slot.next.wait_mode == WaitMode::kAll
                ? received == requested
                : received != 0U);
}

void PipelineRuntime::Enqueue(std::size_t index, ExecutionContext lane)
{
    queues_[LaneIndex(lane)].Push(index);
    slots_[index].state = State::kQueued;
    slots_[index].received = WaitBitFlag::kNone;
}

void PipelineRuntime::SetObserver(DoneCallback callback, void *context)
{
    Guard guard(*this);
    done_ = callback;
    done_context_ = context;
}

void PipelineRuntime::SetTrace(TraceCallback callback, void *context,
                               Clock clock, void *clock_context)
{
    Guard guard(*this);
    trace_ = callback;
    trace_context_ = context;
    clock_ = clock;
    clock_context_ = clock_context;
}

void PipelineRuntime::SetWakeCallback(WakeCallback callback, void *context)
{
    Guard guard(*this);
    wake_ = callback;
    wake_context_ = context;
}

void PipelineRuntime::SetCriticalSection(CriticalSection enter,
                                         CriticalSection exit, void *context)
{
    /* Configure before dispatchers start, never while tasks are running. */
    enter_ = enter;
    exit_ = exit;
    lock_context_ = context;
}

void PipelineRuntime::Wake(ExecutionContext lane) const
{
    if (wake_ != nullptr) wake_(wake_context_, lane);
}

common::Error PipelineRuntime::RegisterModelName(AiModelId model_id,
                                                 const char *name)
{
    return ai_model_monitor_.RegisterModelName(model_id, name);
}

common::Error PipelineRuntime::StartAiModelMonitor()
{
    return ai_model_monitor_.Start();
}

common::Error PipelineRuntime::Submit(AiFuture &future)
{
    {
        Guard guard(*this);
        std::size_t free_index = kCapacity;
        for (std::size_t i = 0; i < kCapacity; ++i) {
            if (slots_[i].future == &future) {
                return common::Error{common::ErrorCode::kInvalidState};
            }
            if (free_index == kCapacity && slots_[i].state == State::kFree) {
                free_index = i;
            }
        }
        if (free_index == kCapacity) {
            return common::Error{common::ErrorCode::kQueueFull};
        }
        ++next_inference_id_;
        if (next_inference_id_ == 0U) ++next_inference_id_;
        slots_[free_index] = {};
        slots_[free_index].future = &future;
        slots_[free_index].inference_id = next_inference_id_;
        Enqueue(free_index, ExecutionContext::kPreprocessCpu);
    }
    Wake(ExecutionContext::kPreprocessCpu);
    return {};
}

common::Error PipelineRuntime::Signal(AiFuture &future, WaitBitFlag flags)
{
    bool wake = false;
    bool found = false;
    ExecutionContext wake_lane = ExecutionContext::kPreprocessCpu;
    {
        Guard guard(*this);
        for (std::size_t i = 0; i < kCapacity; ++i) {
            Slot &slot = slots_[i];
            if (slot.future != &future) continue;
            found = true;
            if (slot.state != State::kExecuting &&
                slot.state != State::kWaiting) {
                return common::Error{common::ErrorCode::kInvalidState};
            }
            slot.received = slot.received | flags;
            if (slot.state == State::kWaiting && Satisfied(slot)) {
                wake_lane = slot.next.context;
                Enqueue(i, wake_lane);
                wake = true;
            }
            break;
        }
    }
    if (!found) {
        return common::Error{common::ErrorCode::kInvalidState};
    }
    if (wake) Wake(wake_lane);
    return {};
}

DispatchResult PipelineRuntime::RunOne(ExecutionContext lane)
{
    const std::size_t lane_index = LaneIndex(lane);
    if (lane_index >= 3U) return DispatchResult::kFailed;
    std::size_t index = kCapacity;
    AiFuture *future = nullptr;
    std::uint32_t inference_id = 0U;
    {
        Guard guard(*this);
        Queue &queue = queues_[lane_index];
        if (queue.count == 0U) return DispatchResult::kIdle;
        index = queue.Pop();
        Slot &slot = slots_[index];
        slot.state = State::kExecuting;
        future = slot.future;
        inference_id = slot.inference_id;
    }

    if (!future->is_ready()) {
        {
            Guard guard(*this);
            Enqueue(index, lane);
        }
        Wake(lane);
        return DispatchResult::kNotReady;
    }
    const StepTrace event{inference_id, future->model_id(), future->step_id(),
                          lane, clock_ == nullptr ? 0U : clock_(clock_context_),
                          true};
    ai_model_monitor_.ObserveAiRuntimeStep(event);
    if (trace_ != nullptr) trace_(trace_context_, event);
    const AiRuntimeResult result = future->Evaluate();
    StepTrace end = event;
    end.begin = false;
    end.timestamp = clock_ == nullptr ? 0U : clock_(clock_context_);
    ai_model_monitor_.ObserveAiRuntimeStep(end);
    if (trace_ != nullptr) trace_(trace_context_, end);

    const bool terminal = !result.Ok() || result.completed;
    const common::Error error = result.error;
    bool wake = false;
    ExecutionContext wake_lane = ExecutionContext::kPreprocessCpu;
    {
        Guard guard(*this);
        Slot &slot = slots_[index];
        if (terminal) {
            slot = {};
        } else if (LaneIndex(result.next.context) >= 3U) {
            slot = {};
        } else {
            slot.next = result.next;
            if (Satisfied(slot)) {
                wake_lane = result.next.context;
                Enqueue(index, wake_lane);
                wake = true;
            } else {
                slot.state = State::kWaiting;
            }
        }
    }
    if (wake) Wake(wake_lane);
    if (!terminal && LaneIndex(result.next.context) >= 3U) {
        if (done_ != nullptr) {
            done_(done_context_, *future,
                  common::Error{common::ErrorCode::kInvalidArgument});
        }
        return DispatchResult::kFailed;
    }
    if (terminal && done_ != nullptr) done_(done_context_, *future, error);
    return error.Ok() ? DispatchResult::kRan : DispatchResult::kFailed;
}

} // namespace uai::ai::ai_runtime
