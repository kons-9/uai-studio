#ifndef UAI_AI_RUNTIME_PIPELINE_TYPES_HPP
#define UAI_AI_RUNTIME_PIPELINE_TYPES_HPP

#include <cstdint>

#include "middleware/foundation/error.hpp"

namespace uai::ai::ai_runtime {

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

} // namespace uai::ai::ai_runtime

#endif
