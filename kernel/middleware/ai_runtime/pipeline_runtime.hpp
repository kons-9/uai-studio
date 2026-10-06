#ifndef UAI_AI_RUNTIME_PIPELINE_RUNTIME_HPP
#define UAI_AI_RUNTIME_PIPELINE_RUNTIME_HPP

#include <cstddef>
#include <cstdint>

#include "middleware/ai_runtime/pipeline_types.hpp"

namespace uai::ai::ai_runtime {

/* Fixed-capacity routing engine. Scheduler submits to the first lane; three
 * dispatchers each consume a different lane. Host tests call RunOne directly.
 * When driven by concurrent RTOS tasks/ISRs, install an enter/exit critical
 * section before submitting; callbacks and Evaluate run outside that section.
 * Step tracing goes through SetTrace() only; whoever owns a monitor forwards
 * the events from that callback. */
class PipelineRuntime final {
public:
    static constexpr std::size_t kCapacity = 8U;
    using DoneCallback = void (*)(void *, AiFuture &, common::Error);
    using TraceCallback = void (*)(void *, const StepTrace &);
    using WakeCallback = void (*)(void *, ExecutionContext);
    using Clock = std::uint32_t (*)(void *);
    using CriticalSection = void (*)(void *);

    void SetObserver(DoneCallback callback, void *context);
    void SetTrace(TraceCallback callback, void *context, Clock clock,
                  void *clock_context);
    void SetWakeCallback(WakeCallback callback, void *context);
    void SetCriticalSection(CriticalSection enter, CriticalSection exit,
                            void *context);
    common::Error Submit(AiFuture &future);
    /* Events belong to a specific future, not to every NPU waiter. */
    common::Error Signal(AiFuture &future, WaitBitFlag flags);
    DispatchResult RunOne(ExecutionContext lane);

private:
    enum class State : std::uint8_t { kFree, kQueued, kExecuting, kWaiting };
    struct Slot {
        AiFuture *future = nullptr;
        std::uint32_t inference_id = 0;
        NextStep next{};
        WaitBitFlag received = WaitBitFlag::kNone;
        State state = State::kFree;
    };
    struct Queue {
        std::size_t items[kCapacity]{};
        std::size_t head = 0;
        std::size_t count = 0;
        void Push(std::size_t index);
        std::size_t Pop();
    };
    class Guard {
    public:
        explicit Guard(PipelineRuntime &runtime) : runtime_(runtime)
        {
            if (runtime_.enter_ != nullptr) runtime_.enter_(runtime_.lock_context_);
        }
        ~Guard()
        {
            if (runtime_.exit_ != nullptr) runtime_.exit_(runtime_.lock_context_);
        }
    private:
        PipelineRuntime &runtime_;
    };

    static std::size_t LaneIndex(ExecutionContext lane);
    static bool Satisfied(const Slot &slot);
    void Enqueue(std::size_t index, ExecutionContext lane);
    void Wake(ExecutionContext lane) const;

    Slot slots_[kCapacity]{};
    Queue queues_[3]{};
    std::uint32_t next_inference_id_ = 0;
    DoneCallback done_ = nullptr;
    void *done_context_ = nullptr;
    TraceCallback trace_ = nullptr;
    void *trace_context_ = nullptr;
    WakeCallback wake_ = nullptr;
    void *wake_context_ = nullptr;
    Clock clock_ = nullptr;
    void *clock_context_ = nullptr;
    CriticalSection enter_ = nullptr;
    CriticalSection exit_ = nullptr;
    void *lock_context_ = nullptr;
};

} // namespace uai::ai::ai_runtime

#endif
