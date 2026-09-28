#ifndef UAI_AI_NPU_DRIVER_HPP
#define UAI_AI_NPU_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/registers/npu_registers.hpp"
#include "model_manager/model_api.hpp"

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
    static common::Error InitializeMemory();
    static void KeepMemoryClocksOnSleep();

    Status Initialize(model_manager::Model &model);

    Status GetInfo(stai_network_info *info) const;
    Status GetInputs(stai_ptr *inputs, stai_size *count) const;
    Status GetOutputs(stai_ptr *outputs, stai_size *count) const;
    Status SetOutputs(const stai_ptr *outputs, stai_size count) const;

    Status Run();
    Status NewInference();
    Status Shutdown();

    bool Initialized() const { return initialized_; }
    const ExecutionSnapshot &LastExecution() const { return last_execution_; }
private:
    static bool IsError(stai_return_code code);
    Status InvalidState(const char *operation) const;

    model_manager::Model *model_ = nullptr;
    registers::NpuRegisterLayer registers_{};
    ExecutionSnapshot last_execution_{};
    std::uint32_t last_error_ = 0U;
    bool initialized_ = false;
};

} // namespace uai::ai::npu

#endif
