#ifndef UAI_SAMPLE2_NPU_DRIVER_HPP
#define UAI_SAMPLE2_NPU_DRIVER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "model_manager/model_api.h"
#include "npu_driver/npu_hardware.hpp"

namespace uai::sample2::npu_driver {

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
    HardwareSnapshot hardware{};
};

struct Status {
    common::Error error{};
    ExecutionSnapshot execution{};

    bool Ok() const { return error.Ok(); }
};

/*
 * NPU execution use cases.  This layer owns the STAI/Neural-ART lifecycle and
 * the asynchronous run protocol.  It does not access NPU registers directly;
 * all hardware observation goes through NpuHardware.
 */
class NpuDriver final {
public:
    Status Initialize(const sample2_model_api &model);

    Status GetInfo(stai_network_info *info) const;
    Status GetInputs(stai_ptr *inputs, stai_size *count) const;
    Status GetOutputs(stai_ptr *outputs, stai_size *count) const;

    Status Run();
    Status NewInference();
    Status Shutdown();

    bool Initialized() const { return initialized_; }
    const ExecutionSnapshot &LastExecution() const { return last_execution_; }
    HardwareSnapshot ReadHardware() const { return hardware_.ReadSnapshot(); }

private:
    static bool IsError(stai_return_code code);
    Status InvalidState(const char *operation) const;

    const sample2_model_api *model_ = nullptr;
    NpuHardware hardware_{};
    ExecutionSnapshot last_execution_{};
    std::uint32_t last_error_ = 0U;
    bool initialized_ = false;
};

} // namespace uai::sample2::npu_driver

#endif
