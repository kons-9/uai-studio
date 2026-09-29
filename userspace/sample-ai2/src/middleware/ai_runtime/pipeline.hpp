#ifndef UAI_AI_RUNTIME_PIPELINE_HPP
#define UAI_AI_RUNTIME_PIPELINE_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"

namespace uai::ai::ai_runtime {

/* Execution lanes are distinct even though two of them execute on a CPU.
 * The NPU lane serializes submissions; waiters do not occupy its queue. */
enum class ExecutionContext : std::uint8_t {
    kPreprocessCpu,
    kNpu,
    kPostprocessCpu,
};

enum class WaitBitFlag : std::uint32_t {
    kNone = 0U,
    kNpuCompletion = 1U << 0,
    kExternal = 1U << 1,
};

constexpr WaitBitFlag operator|(WaitBitFlag lhs, WaitBitFlag rhs)
{
    return static_cast<WaitBitFlag>(static_cast<std::uint32_t>(lhs) |
                                    static_cast<std::uint32_t>(rhs));
}

enum class WaitMode : std::uint8_t { kAll, kAny };

struct NextStep {
    ExecutionContext context = ExecutionContext::kPreprocessCpu;
    WaitBitFlag wait_flags = WaitBitFlag::kNone;
    WaitMode wait_mode = WaitMode::kAll;
};

/* On error, next is ignored. When completed is true, there is no next step. */
struct AiRuntimeResult {
    common::Error error{};
    NextStep next{};
    bool completed = false;
    bool Ok() const { return error.Ok(); }
};

enum class AiModelId : std::uint32_t;

/* Model owns this object and its frame/output buffers until the done callback.
 * Evaluate executes exactly one step and mutates the model's pipeline state. */
class AiFuture {
public:
    virtual ~AiFuture() = default;
    virtual AiModelId model_id() const = 0;
    virtual std::uint32_t step_id() const = 0;
    virtual bool is_ready() const = 0;
    virtual AiRuntimeResult Evaluate() = 0;
};

struct StepTrace {
    std::uint32_t inference_id = 0;
    AiModelId model_id{};
    std::uint32_t step_id = 0;
    ExecutionContext context = ExecutionContext::kPreprocessCpu;
    std::uint32_t timestamp = 0;
    bool begin = false;
};

enum class DispatchResult : std::uint8_t { kIdle, kNotReady, kRan, kFailed };

/* Fixed-capacity routing engine. Scheduler submits to the first lane; three
 * dispatchers each consume a different lane. Host tests call RunOne directly.
 * When driven by concurrent RTOS tasks/ISRs, install an enter/exit critical
 * section before submitting; callbacks and Evaluate run outside that section. */
class PipelineRuntime final {
public:
    static constexpr std::size_t kCapacity = 8U;
    using DoneCallback = void (*)(void *, AiFuture &, common::Error);
    using TraceCallback = void (*)(void *, const StepTrace &);
    using Clock = std::uint32_t (*)(void *);
    using CriticalSection = void (*)(void *);

    void SetObserver(DoneCallback callback, void *context);
    void SetTrace(TraceCallback callback, void *context, Clock clock,
                  void *clock_context);
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

    Slot slots_[kCapacity]{};
    Queue queues_[3]{};
    std::uint32_t next_inference_id_ = 0;
    DoneCallback done_ = nullptr;
    void *done_context_ = nullptr;
    TraceCallback trace_ = nullptr;
    void *trace_context_ = nullptr;
    Clock clock_ = nullptr;
    void *clock_context_ = nullptr;
    CriticalSection enter_ = nullptr;
    CriticalSection exit_ = nullptr;
    void *lock_context_ = nullptr;
};

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