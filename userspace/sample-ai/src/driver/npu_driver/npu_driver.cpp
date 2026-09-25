#include "driver/npu_driver/npu_driver.hpp"
#include "driver/npu_driver/debug.h"

#include <tk/tkernel.h>

/* C ABI のST AIランタイム/HAL関数とリンクする宣言。 */
extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"

stai_return_code stai_runtime_init(void);
}

namespace uai::ai::npu_driver {

namespace {

constexpr std::uint32_t kTimeoutTicks = 5000U;

} // namespace

bool NpuDriver::IsError(stai_return_code code)
{
    return code >= STAI_ERROR_GENERIC;
}

Status NpuDriver::InvalidState(const char *operation) const
{
    return {common::Error{common::ErrorCode::kNotInitialized, 0U, operation},
            last_execution_};
}

Status NpuDriver::Initialize(model_manager::Model &model)
{
    if (initialized_) {
        return {common::Error{common::ErrorCode::kAlreadyInitialized, 0U,
                              "npu.initialize"},
                last_execution_};
    }

    model_ = &model;

    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu init begin irq_en=%u irq_pending=%u\n"),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)));
    HAL_NVIC_SetPriority(NPU0_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(NPU0_IRQn);

    const stai_return_code runtime_code = stai_runtime_init();
    const NpuHardwareSnapshot runtime_hardware = npu_hardware_.ReadSnapshot();
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu runtime init code=%x irq_en=%u pending=%u epoch=%x int=%x bus=%x\n"),
              static_cast<unsigned int>(runtime_code),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(runtime_hardware.epoch_control),
              static_cast<unsigned int>(runtime_hardware.interrupt_status),
              static_cast<unsigned int>(runtime_hardware.busif0_error));
    last_error_ = static_cast<std::uint32_t>(runtime_code);
    if (IsError(runtime_code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.runtime_initialize"},
                last_execution_};
    }

    const stai_return_code model_code = model_->Initialize();
    const NpuHardwareSnapshot model_hardware = npu_hardware_.ReadSnapshot();
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu model init code=%x irq_en=%u pending=%u epoch=%x int=%x bus=%x\n"),
              static_cast<unsigned int>(model_code),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(model_hardware.epoch_control),
              static_cast<unsigned int>(model_hardware.interrupt_status),
              static_cast<unsigned int>(model_hardware.busif0_error));
    last_error_ = static_cast<std::uint32_t>(model_code);
    if (IsError(model_code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
        return {common::Error{common::ErrorCode::kModel, last_error_,
                              "npu.model_initialize"},
                last_execution_};
    }

    initialized_ = true;
    last_error_ = 0U;
    last_execution_ = {};
    last_execution_.state = ExecutionState::kReady;
    last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
    return {common::Error{common::ErrorCode::kOk, 0U, "npu.initialize"},
            last_execution_};
}

Status NpuDriver::GetInfo(stai_network_info *info) const
{
    if (!initialized_ || model_ == nullptr || info == nullptr) {
        return InvalidState("npu.get_info");
    }
    const stai_return_code code = model_->GetInfo(info);
    return IsError(code)
               ? Status{common::Error{common::ErrorCode::kModel,
                                      static_cast<std::uint32_t>(code),
                                      "npu.get_info"},
                        last_execution_}
               : Status{common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.get_info"},
                        last_execution_};
}

Status NpuDriver::GetInputs(stai_ptr *inputs, stai_size *count) const
{
    if (!initialized_ || model_ == nullptr || inputs == nullptr ||
        count == nullptr) {
        return InvalidState("npu.get_inputs");
    }
    const stai_return_code code = model_->GetInputs(inputs, count);
    return IsError(code)
               ? Status{common::Error{common::ErrorCode::kModel,
                                      static_cast<std::uint32_t>(code),
                                      "npu.get_inputs"},
                        last_execution_}
               : Status{common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.get_inputs"},
                        last_execution_};
}

Status NpuDriver::GetOutputs(stai_ptr *outputs, stai_size *count) const
{
    if (!initialized_ || model_ == nullptr || outputs == nullptr ||
        count == nullptr) {
        return InvalidState("npu.get_outputs");
    }
    const stai_return_code code = model_->GetOutputs(outputs, count);
    return IsError(code)
               ? Status{common::Error{common::ErrorCode::kModel,
                                      static_cast<std::uint32_t>(code),
                                      "npu.get_outputs"},
                        last_execution_}
               : Status{common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.get_outputs"},
                        last_execution_};
}

Status NpuDriver::Run()
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.run");
    }

    /* Keep the ref application's asynchronous STAI protocol.  In this
     * project the caller is a µT-Kernel task, so use a timed task sleep while
     * waiting instead of blocking forever in the bare-metal WFE macro.  The
     * NPU IRQ still advances the Neural-ART event state; run_continue() then
     * consumes that state and starts the next epoch. */
    const NpuHardwareSnapshot before_run = npu_hardware_.ReadSnapshot();
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu run begin irq_en=%u pending=%u epoch=%x int=%x bus=%x\n"),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(before_run.epoch_control),
              static_cast<unsigned int>(before_run.interrupt_status),
              static_cast<unsigned int>(before_run.busif0_error));
    stai_return_code code = model_->Run(STAI_MODE_ASYNC);
    last_error_ = static_cast<std::uint32_t>(code);
    last_execution_.state = ExecutionState::kSubmitted;
    last_execution_.stai_status = last_error_;
    if (IsError(code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.run"},
                last_execution_};
    }

    bool completed = false;
    for (std::uint32_t tick = 0U; tick < kTimeoutTicks; ++tick) {
        code = model_->GetRunStatus();
        last_error_ = static_cast<std::uint32_t>(code);
        if (code == STAI_DONE) {
            completed = true;
            break;
        }
        if (IsError(code)) {
            last_execution_.state = ExecutionState::kFaulted;
            last_execution_.stai_status = last_error_;
            last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: npu done error=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(code), g_aton_irq_count,
                      g_aton_last_irqs);
            return {common::Error{common::ErrorCode::kNpu, last_error_,
                                  "npu.run"},
                    last_execution_};
        }

        if ((tick % 100U) == 0U) {
            const NpuHardwareSnapshot waiting = npu_hardware_.ReadSnapshot();
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: npu wait tick=%u status=%x irq=%u last=%x epoch=%x int=%x bus=%x\n"),
                      static_cast<unsigned int>(tick),
                      static_cast<unsigned int>(code), g_aton_irq_count,
                      g_aton_last_irqs,
                      static_cast<unsigned int>(waiting.epoch_control),
                      static_cast<unsigned int>(waiting.interrupt_status),
                      static_cast<unsigned int>(waiting.busif0_error));
        }

        /* STAI distinguishes between a continuation that can run immediately
         * and one that must wait for the ATON event which completes the active
         * epoch.  The generated ref application calls WFE for the latter;
         * delaying the task alone leaves the runtime's triggered-event state
         * untouched and can spin forever in STAI_RUNNING_WFE. */
        if (code == STAI_RUNNING_WFE) {
            model_->WaitForEvent();
        } else {
            tk_dly_tsk(1);
        }
        code = model_->ContinueRun();
        last_error_ = static_cast<std::uint32_t>(code);
        if (IsError(code)) {
            last_execution_.state = ExecutionState::kFaulted;
            last_execution_.stai_status = last_error_;
            last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
            return {common::Error{common::ErrorCode::kNpu, last_error_,
                                  "npu.run_continue"},
                    last_execution_};
        }
    }

    if (!completed) {
        last_execution_.state = ExecutionState::kTimedOut;
        last_execution_.stai_status = last_error_;
        last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: npu done timeout status=%x irq=%u last=%x\n"),
                  static_cast<unsigned int>(last_error_), g_aton_irq_count,
                  g_aton_last_irqs);
        return {common::Error{common::ErrorCode::kTimeout, last_error_,
                              "npu.run"},
                last_execution_};
    }

    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu done code=%x irq=%u last=%x\n"),
              static_cast<unsigned int>(STAI_SUCCESS), g_aton_irq_count,
              g_aton_last_irqs);
    last_execution_.state = ExecutionState::kCompleted;
    last_execution_.stai_status = STAI_DONE;
    last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
    return {common::Error{common::ErrorCode::kOk, 0U, "npu.run"},
            last_execution_};
}

Status NpuDriver::NewInference()
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.new_inference");
    }
    const stai_return_code code = model_->NewInference();
    last_error_ = static_cast<std::uint32_t>(code);
    if (IsError(code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.new_inference"},
                last_execution_};
    }
    last_execution_.state = ExecutionState::kReady;
    last_execution_.stai_status = 0U;
    return {common::Error{common::ErrorCode::kOk, 0U,
                          "npu.new_inference"},
            last_execution_};
}

Status NpuDriver::Shutdown()
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.shutdown");
    }

    const stai_return_code code = model_->Shutdown();
    last_error_ = static_cast<std::uint32_t>(code);
    initialized_ = false;
    last_execution_.state = ExecutionState::kUninitialized;
    last_execution_.stai_status = last_error_;
    last_execution_.npu_hardware = npu_hardware_.ReadSnapshot();
    return IsError(code)
               ? Status{common::Error{common::ErrorCode::kModel, last_error_,
                                      "npu.shutdown"},
                        last_execution_}
               : Status{common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.shutdown"},
                        last_execution_};
}

} // namespace uai::ai::npu_driver
