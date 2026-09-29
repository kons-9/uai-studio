#ifndef UAI_AI_NPU_DRIVER_HPP
#define UAI_AI_NPU_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/registers/npu_registers.hpp"
#include "models/model.hpp"

namespace uai::ai::npu {

enum class ExecutionState : std::uint8_t {
    kUninitialized,
    kReady,
    kSubmitted,
    kRunning,
    kCompleted,
    kFaulted,
    kTimedOut,
};

struct ExecutionSnapshot {
    ExecutionState state = ExecutionState::kUninitialized;
    std::uint32_t stai_status = 0U;
    std::uint32_t run_id = 0U;
    std::uint32_t start_ms = 0U;
    std::uint32_t end_ms = 0U;
    std::uint32_t elapsed_ms = 0U;
    bool timing_valid = false;
    /* Diagnostic breakdown of the asynchronous STAI run. */
    std::uint32_t status_poll_count = 0U;
    std::uint32_t irq_wait_count = 0U;
    std::uint32_t irq_wait_elapsed_ms = 0U;
    std::uint32_t irq_wait_max_elapsed_ms = 0U;
    std::uint32_t continue_count = 0U;
    std::uint32_t continue_elapsed_ms = 0U;
    std::uint32_t continue_max_elapsed_ms = 0U;
    std::uint32_t continue_slow_count = 0U;
    std::uint32_t progress_count = 0U;
    std::uint32_t progress_elapsed_ms = 0U;
    std::uint32_t progress_max_elapsed_ms = 0U;
    std::uint32_t submit_elapsed_ms = 0U;
    std::uint32_t irq_count_start = 0U;
    std::uint32_t irq_count_end = 0U;
};

struct Status {
    common::Error error{};
    ExecutionSnapshot execution{};

    bool Ok() const { return error.Ok(); }
};

/*
 * NPU execution operations.  This layer owns the STAI/Neural-ART lifecycle and
 * the asynchronous run protocol. Hardware observation used by diagnostics is
 * kept in this implementation as part of the same driver boundary.
 */
class NpuDriver final {
public:
    using RunProgressCallback = void (*)(void *context);

    static common::Error InitializeMemory();
    static void KeepMemoryClocksOnSleep();

    Status Initialize(models::ModelRuntime &model);
    /* Initialize a second generated network while the shared ATON runtime is
     * already alive.  This copies its command blob into its runtime buffer so
     * a later model switch does not have to read the external flash again. */
    Status Preload(models::ModelRuntime &model);
    Status SelectModel(models::ModelRuntime &model);
    bool IsLoaded(const models::ModelRuntime &model) const;

    Status GetInfo(stai_network_info *info) const;
    Status GetInputs(stai_ptr *inputs, stai_size *count) const;
    Status SetInput(stai_ptr input, stai_size size) const;
    Status GetOutputs(stai_ptr *outputs, stai_size *count) const;
    Status SetOutputs(const stai_ptr *outputs, stai_size count) const;

    /* StartRun submits the current model and returns while the NPU is
     * executing. WaitRun completes the same submission. Run remains the
     * blocking convenience operation for callers without prefetching. */
    Status StartRun();
    Status WaitRun(RunProgressCallback progress = nullptr,
                   void *progress_context = nullptr);
    Status Run();
    Status NewInference();
    Status Shutdown();

    bool Initialized() const { return initialized_; }
    const ExecutionSnapshot &LastExecution() const { return last_execution_; }
private:
    static bool IsError(stai_return_code code);
    Status InvalidState(const char *operation) const;

    models::ModelRuntime *model_ = nullptr;
    models::ModelRuntime *loaded_models_[3] = {};
    std::uint32_t loaded_model_count_ = 0U;
    registers::NpuRegisterLayer registers_{};
    ExecutionSnapshot last_execution_{};
    std::uint32_t last_error_ = 0U;
    bool initialized_ = false;
};

} // namespace uai::ai::npu

#endif
