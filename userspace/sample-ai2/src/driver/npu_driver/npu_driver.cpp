#include "driver/npu_driver/npu_driver.hpp"
#include "driver/npu_driver/debug.h"
#include "common/log.hpp"

#include <tk/tkernel.h>

/* C ABI のST AIランタイム/HAL関数とリンクする宣言。 */
extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
#include "ll_aton_NN_interface.h"
void LL_ATON_NPU0_IRQHandler(void);

stai_return_code stai_runtime_init(void);
stai_return_code stai_runtime_deinit(void);
}

namespace {

constexpr UINT kNpuIrqEvent = 0x01U;
ID g_npu_irq_event_flag = 0;

} // namespace
extern "C" void NPU0_IRQHandler(UINT intno)
{
    ++g_aton_irq_count;
    LL_ATON_NPU0_IRQHandler();
    if (g_npu_irq_event_flag > 0) {
        (void)tk_set_flg(g_npu_irq_event_flag, kNpuIrqEvent);
    }
    (void)intno;
}

namespace uai::ai::npu {

namespace {

constexpr std::uint32_t kTimeoutTicks = 5000U;

std::uint32_t NowMs()
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

void EnableCycleCounter()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

std::uint32_t NowCycles()
{
    return DWT->CYCCNT;
}

void BeginExecutionTiming(ExecutionSnapshot &execution)
{
    execution.start_ms = NowMs();
    execution.end_ms = execution.start_ms;
    execution.elapsed_ms = 0U;
    execution.timing_valid = false;
    execution.status_poll_count = 0U;
    execution.irq_wait_count = 0U;
    execution.irq_wait_elapsed_ms = 0U;
    execution.irq_wait_max_elapsed_ms = 0U;
    execution.continue_count = 0U;
    execution.continue_elapsed_ms = 0U;
    execution.continue_max_elapsed_ms = 0U;
    execution.continue_slow_count = 0U;
    execution.progress_count = 0U;
    execution.progress_elapsed_ms = 0U;
    execution.progress_max_elapsed_ms = 0U;
    execution.submit_elapsed_ms = 0U;
    execution.irq_count_start = g_aton_irq_count;
    execution.irq_count_end = execution.irq_count_start;
}

void FinishExecutionTiming(ExecutionSnapshot &execution)
{
    execution.end_ms = NowMs();
    execution.elapsed_ms = execution.end_ms - execution.start_ms;
    execution.irq_count_end = g_aton_irq_count;
    execution.timing_valid = true;
}

void EnableNpuMemory()
{
    __HAL_RCC_NPU_CLK_ENABLE();
    __HAL_RCC_NPU_FORCE_RESET();
    __HAL_RCC_NPU_RELEASE_RESET();
    __HAL_RCC_FLEXRAM_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM1_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM2_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
    __HAL_RCC_RAMCFG_CLK_ENABLE();

    RAMCFG_HandleTypeDef ramcfg = {};
    ramcfg.Instance = RAMCFG_SRAM2_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM3_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM4_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM5_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);
    ramcfg.Instance = RAMCFG_SRAM6_AXI;
    (void)HAL_RAMCFG_EnableAXISRAM(&ramcfg);

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_SYSCFG_EnableInterleavingCpuRam();
}

} // namespace

common::Error NpuDriver::InitializeMemory()
{
    static bool initialized = false;
    if (initialized) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "npu.memory_initialize"};
    }
    EnableNpuMemory();
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "ai: npu clocks npu=%uHz npu_ram=%uHz\n"),
                 static_cast<unsigned int>(HAL_RCC_GetNPUClockFreq()),
                 static_cast<unsigned int>(HAL_RCC_GetNPURAMSClockFreq()));
    initialized = true;
    return {common::ErrorCode::kOk, 0U, "npu.memory_initialize"};
}

void NpuDriver::KeepMemoryClocksOnSleep()
{
    __HAL_RCC_NPU_CLK_SLEEP_ENABLE();
    __HAL_RCC_RAMCFG_CLK_SLEEP_ENABLE();
    __HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM1_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM2_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
}

bool NpuDriver::IsError(stai_return_code code)
{
    return code >= STAI_ERROR_GENERIC;
}

void NpuDriver::EpochTraceThunk(void *context, std::uint32_t callback_type,
                                std::uint32_t epoch_index,
                                std::uint32_t epoch_flags,
                                std::uintptr_t epoch_address)
{
    auto *driver = static_cast<NpuDriver *>(context);
    if (driver != nullptr) {
        driver->ObserveEpochTrace(callback_type, epoch_index, epoch_flags,
                                  epoch_address);
    }
}

void NpuDriver::ObserveEpochTrace(std::uint32_t callback_type,
                                  std::uint32_t epoch_index,
                                  std::uint32_t epoch_flags,
                                  std::uintptr_t epoch_address)
{
    const std::uint32_t pre_start =
        static_cast<std::uint32_t>(LL_ATON_RT_Callbacktype_PRE_START);
    const std::uint32_t post_start =
        static_cast<std::uint32_t>(LL_ATON_RT_Callbacktype_POST_START);
    const std::uint32_t pre_end =
        static_cast<std::uint32_t>(LL_ATON_RT_Callbacktype_PRE_END);
    const std::uint32_t post_end =
        static_cast<std::uint32_t>(LL_ATON_RT_Callbacktype_POST_END);

    if (callback_type == pre_start) {
        epoch_trace_active_ = true;
        epoch_trace_start_cycles_ = NowCycles();
        epoch_trace_last_cycles_ = epoch_trace_start_cycles_;
        epoch_trace_start_epoch_index_ = epoch_index;
        epoch_trace_start_epoch_flags_ = epoch_flags;
        epoch_trace_start_epoch_address_ = epoch_address;
        return;
    }
    if (!epoch_trace_active_ || callback_type < post_start ||
        callback_type > post_end) {
        return;
    }

    const std::uint32_t end_cycles = NowCycles();
    if (epoch_trace_observer_ != nullptr) {
        epoch_trace_observer_(
            epoch_trace_context_, epoch_trace_model_kind_id_, NowMs(),
            end_cycles, end_cycles - epoch_trace_last_cycles_,
            epoch_trace_start_epoch_index_, epoch_trace_start_epoch_flags_,
            epoch_trace_start_epoch_address_, callback_type);
    }
    epoch_trace_last_cycles_ = end_cycles;
    if (callback_type == pre_end) {
        /* PRE_END is the boundary after the NPU/ATON wait interval. Keep the
         * epoch active until POST_END so the CPU cleanup interval is emitted
         * as a separate record. */
        return;
    }
    if (callback_type == post_end) {
        epoch_trace_active_ = false;
    }
}

Status NpuDriver::InvalidState(const char *operation) const
{
    return {common::Error{common::ErrorCode::kNotInitialized, 0U, operation},
            last_execution_};
}

Status NpuDriver::Initialize(models::ModelRuntime &model)
{
    if (initialized_) {
        for (std::uint32_t i = 0U; i < loaded_model_count_; ++i) {
            if (loaded_models_[i] == &model) {
                model_ = &model;
                last_execution_.state = ExecutionState::kReady;
                return {common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.select_model"},
                        last_execution_};
            }
        }
        return {common::Error{common::ErrorCode::kInvalidState, 0U,
                              "npu.initialize.unloaded_model"},
                last_execution_};
    }

    model_ = &model;
    EnableCycleCounter();

    if (g_npu_irq_event_flag == 0) {
        T_CFLG event_flag{};
        event_flag.flgatr = TA_TFIFO;
        g_npu_irq_event_flag = tk_cre_flg(&event_flag);
        if (g_npu_irq_event_flag < E_OK) {
            last_execution_.state = ExecutionState::kFaulted;
            last_execution_.stai_status =
                static_cast<std::uint32_t>(g_npu_irq_event_flag);
            return {common::Error{common::ErrorCode::kNpu,
                                  static_cast<std::uint32_t>(g_npu_irq_event_flag),
                                  "npu.create_irq_event"},
                    last_execution_};
        }
    }

    HAL_NVIC_SetPriority(NPU0_IRQn, 8U, 0U);
    HAL_NVIC_EnableIRQ(NPU0_IRQn);

    const stai_return_code runtime_code = stai_runtime_init();
    last_error_ = static_cast<std::uint32_t>(runtime_code);
    if (IsError(runtime_code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.runtime_initialize"},
                last_execution_};
    }

    const stai_return_code trace_code = model.SetEpochTraceCallback(
        &NpuDriver::EpochTraceThunk, this);
    last_error_ = static_cast<std::uint32_t>(trace_code);
    if (IsError(trace_code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        return {common::Error{common::ErrorCode::kModel, last_error_,
                              "npu.model_trace_callback"},
                last_execution_};
    }

    const stai_return_code model_code = model_->Initialize();
    last_error_ = static_cast<std::uint32_t>(model_code);
    if (IsError(model_code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        return {common::Error{common::ErrorCode::kModel, last_error_,
                              "npu.model_initialize"},
                last_execution_};
    }

    initialized_ = true;
    loaded_models_[0] = &model;
    loaded_model_count_ = 1U;
    last_error_ = 0U;
    last_execution_ = {};
    last_execution_.state = ExecutionState::kReady;
    return {common::Error{common::ErrorCode::kOk, 0U, "npu.initialize"},
            last_execution_};
}

Status NpuDriver::Preload(models::ModelRuntime &model)
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.preload");
    }
    for (std::uint32_t i = 0U; i < loaded_model_count_; ++i) {
        if (loaded_models_[i] == &model) {
            return {common::Error{common::ErrorCode::kOk, 0U,
                                  "npu.preload"},
                    last_execution_};
        }
    }
    if (loaded_model_count_ >=
        static_cast<std::uint32_t>(sizeof(loaded_models_) /
                                   sizeof(loaded_models_[0]))) {
        return {common::Error{common::ErrorCode::kInvalidState,
                              loaded_model_count_, "npu.preload.full"},
                last_execution_};
    }

    const stai_return_code trace_code = model.SetEpochTraceCallback(
        &NpuDriver::EpochTraceThunk, this);
    last_error_ = static_cast<std::uint32_t>(trace_code);
    if (IsError(trace_code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        return {common::Error{common::ErrorCode::kModel, last_error_,
                              "npu.preload_trace_callback"},
                last_execution_};
    }

    const stai_return_code code = model.Initialize();
    last_error_ = static_cast<std::uint32_t>(code);
    if (IsError(code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        return {common::Error{common::ErrorCode::kModel, last_error_,
                              "npu.preload"},
                last_execution_};
    }
    loaded_models_[loaded_model_count_++] = &model;
    return {common::Error{common::ErrorCode::kOk, 0U, "npu.preload"},
            last_execution_};
}

Status NpuDriver::SelectModel(models::ModelRuntime &model)
{
    if (!initialized_) {
        return InvalidState("npu.select_model");
    }
    for (std::uint32_t i = 0U; i < loaded_model_count_; ++i) {
        if (loaded_models_[i] == &model) {
            model_ = &model;
            last_execution_.state = ExecutionState::kReady;
            last_execution_.stai_status = 0U;
            return {common::Error{common::ErrorCode::kOk, 0U,
                                  "npu.select_model"},
                    last_execution_};
        }
    }
    return {common::Error{common::ErrorCode::kInvalidState, 0U,
                          "npu.select_model.unloaded_model"},
            last_execution_};
}

bool NpuDriver::IsLoaded(const models::ModelRuntime &model) const
{
    for (std::uint32_t i = 0U; i < loaded_model_count_; ++i) {
        if (loaded_models_[i] == &model) {
            return true;
        }
    }
    return false;
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

Status NpuDriver::SetInput(stai_ptr input, stai_size size) const
{
    if (!initialized_ || model_ == nullptr || input == nullptr || size == 0U) {
        return InvalidState("npu.set_input");
    }
    const stai_return_code code = model_->SetInput(input, size);
    return IsError(code)
               ? Status{common::Error{common::ErrorCode::kModel,
                                      static_cast<std::uint32_t>(code),
                                      "npu.set_input"},
                        last_execution_}
               : Status{common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.set_input"},
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

Status NpuDriver::SetOutputs(const stai_ptr *outputs, stai_size count) const
{
    if (!initialized_ || model_ == nullptr || outputs == nullptr) {
        return InvalidState("npu.set_outputs");
    }
    const stai_return_code code = model_->SetOutputs(outputs, count);
    return IsError(code)
               ? Status{common::Error{common::ErrorCode::kModel,
                                      static_cast<std::uint32_t>(code),
                                      "npu.set_outputs"},
                        last_execution_}
               : Status{common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.set_outputs"},
                        last_execution_};
}

Status NpuDriver::StartRun()
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.run");
    }
    if (last_execution_.state == ExecutionState::kSubmitted ||
        last_execution_.state == ExecutionState::kRunning) {
        return {common::Error{common::ErrorCode::kInvalidState, 0U,
                              "npu.run.already_active"},
                last_execution_};
    }

    /* Keep the ref application's asynchronous STAI protocol. The NPU IRQ
     * wakes the waiting task; WaitRun() then consumes the Neural-ART event
     * state and starts the next epoch. */
    const ER clear_event_status = tk_clr_flg(g_npu_irq_event_flag, 0U);
    if (clear_event_status != E_OK) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status =
            static_cast<std::uint32_t>(clear_event_status);
        return {common::Error{common::ErrorCode::kNpu,
                              static_cast<std::uint32_t>(clear_event_status),
                "npu.clear_irq_event"},
                last_execution_};
    }
    ++last_execution_.run_id;
    epoch_trace_active_ = false;
    epoch_trace_last_cycles_ = 0U;
    BeginExecutionTiming(last_execution_);
    const std::uint32_t submit_start_ms = NowMs();
    stai_return_code code = model_->Run(STAI_MODE_ASYNC);
    last_execution_.submit_elapsed_ms = NowMs() - submit_start_ms;
    last_error_ = static_cast<std::uint32_t>(code);
    last_execution_.state = ExecutionState::kSubmitted;
    last_execution_.stai_status = last_error_;
    if (IsError(code)) {
        FinishExecutionTiming(last_execution_);
        last_execution_.state = ExecutionState::kFaulted;
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.run"},
                last_execution_};
    }

    last_execution_.state = ExecutionState::kRunning;
    return {common::Error{common::ErrorCode::kOk, 0U, "npu.start_run"},
            last_execution_};
}

Status NpuDriver::PollRun(RunProgressCallback progress,
                          void *progress_context)
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.poll_run");
    }
    if (last_execution_.state != ExecutionState::kSubmitted &&
        last_execution_.state != ExecutionState::kRunning) {
        return InvalidState("npu.poll_run.not_active");
    }

    ++last_execution_.status_poll_count;
    const stai_return_code code = model_->GetRunStatus();
    last_error_ = static_cast<std::uint32_t>(code);
    if (code == STAI_DONE) {
        FinishExecutionTiming(last_execution_);
        last_execution_.state = ExecutionState::kCompleted;
        last_execution_.stai_status = STAI_DONE;
        return {common::Error{common::ErrorCode::kOk, 0U,
                              "npu.poll_run.done"},
                last_execution_, RunAction::kCompleted};
    }
    if (IsError(code)) {
        FinishExecutionTiming(last_execution_);
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                          "ai: npu status error=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(code), g_aton_irq_count,
                      g_aton_last_irqs);
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.poll_run"},
                last_execution_};
    }

    /* Progress is deliberately called from task context. It may prepare a
     * next frame while this run is active, but it is never called by IRQ. */
    if (progress != nullptr) {
        const std::uint32_t progress_start_ms = NowMs();
        ++last_execution_.progress_count;
        progress(progress_context);
        const std::uint32_t progress_elapsed_ms =
            NowMs() - progress_start_ms;
        last_execution_.progress_elapsed_ms += progress_elapsed_ms;
        if (progress_elapsed_ms > last_execution_.progress_max_elapsed_ms) {
            last_execution_.progress_max_elapsed_ms = progress_elapsed_ms;
        }
    }

    const RunAction action = code == STAI_RUNNING_WFE
                                 ? RunAction::kWaitForIrq
                                 : RunAction::kContinueEpoch;
    return {common::Error{common::ErrorCode::kOk, 0U, "npu.poll_run"},
            last_execution_, action};
}

Status NpuDriver::WaitForIrq()
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.wait_irq");
    }
    if (last_execution_.state != ExecutionState::kSubmitted &&
        last_execution_.state != ExecutionState::kRunning) {
        return InvalidState("npu.wait_irq.not_active");
    }

    UINT pattern = 0U;
    const std::uint32_t wait_start_ms = NowMs();
    ++last_execution_.irq_wait_count;
    const ER wait_status = tk_wai_flg(
        g_npu_irq_event_flag, kNpuIrqEvent, TWF_ANDW | TWF_BITCLR, &pattern,
        static_cast<TMO>(kTimeoutTicks));
    const std::uint32_t elapsed_ms = NowMs() - wait_start_ms;
    last_execution_.irq_wait_elapsed_ms += elapsed_ms;
    if (elapsed_ms > last_execution_.irq_wait_max_elapsed_ms) {
        last_execution_.irq_wait_max_elapsed_ms = elapsed_ms;
    }
    if (wait_status == E_TMOUT) {
        FinishExecutionTiming(last_execution_);
        last_execution_.state = ExecutionState::kTimedOut;
        last_execution_.stai_status = static_cast<std::uint32_t>(wait_status);
        return {common::Error{common::ErrorCode::kTimeout,
                              static_cast<std::uint32_t>(wait_status),
                              "npu.wait_irq"},
                last_execution_};
    }
    if (wait_status != E_OK) {
        FinishExecutionTiming(last_execution_);
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = static_cast<std::uint32_t>(wait_status);
        return {common::Error{common::ErrorCode::kNpu,
                              static_cast<std::uint32_t>(wait_status),
                              "npu.wait_irq"},
                last_execution_};
    }
    return {common::Error{common::ErrorCode::kOk, 0U, "npu.wait_irq"},
            last_execution_, RunAction::kContinueEpoch};
}

Status NpuDriver::ContinueRun()
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.continue_run");
    }
    if (last_execution_.state != ExecutionState::kSubmitted &&
        last_execution_.state != ExecutionState::kRunning) {
        return InvalidState("npu.continue_run.not_active");
    }

    const std::uint32_t continue_start_ms = NowMs();
    ++last_execution_.continue_count;
    const stai_return_code code = model_->ContinueRun();
    const std::uint32_t elapsed_ms = NowMs() - continue_start_ms;
    last_execution_.continue_elapsed_ms += elapsed_ms;
    if (elapsed_ms > last_execution_.continue_max_elapsed_ms) {
        last_execution_.continue_max_elapsed_ms = elapsed_ms;
    }
    if (elapsed_ms >= 10U) {
        ++last_execution_.continue_slow_count;
    }
    last_error_ = static_cast<std::uint32_t>(code);
    if (IsError(code)) {
        FinishExecutionTiming(last_execution_);
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.run_continue"},
                last_execution_};
    }
    last_execution_.state = ExecutionState::kRunning;
    return {common::Error{common::ErrorCode::kOk, 0U,
                          "npu.continue_run"},
            last_execution_};
}

Status NpuDriver::WaitRun(RunProgressCallback progress,
                          void *progress_context)
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.wait_run");
    }
    for (std::uint32_t tick = 0U; tick < kTimeoutTicks; ++tick) {
        Status status = PollRun(progress, progress_context);
        if (!status.Ok()) {
            return status;
        }
        if (status.action == RunAction::kCompleted) {
            return status;
        }
        if (status.action == RunAction::kWaitForIrq) {
            status = WaitForIrq();
            if (!status.Ok()) {
                return status;
            }
        }
        status = ContinueRun();
        if (!status.Ok()) {
            return status;
        }
    }

    FinishExecutionTiming(last_execution_);
    last_execution_.state = ExecutionState::kTimedOut;
    last_execution_.stai_status = last_error_;
    const registers::NpuRegisterSnapshot timeout_hardware =
        registers_.ReadSnapshot();
    UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                      "ai: npu done timeout status=%x irq=%u last=%x epoch=%x/%x bc=%x int=%x bus=%x/%x,%x/%x stream=%x/%x size=%x count=%x/%x/%x/%x\n"),
                  static_cast<unsigned int>(last_error_), g_aton_irq_count,
                  g_aton_last_irqs,
                  static_cast<unsigned int>(timeout_hardware.epoch_control),
                  static_cast<unsigned int>(timeout_hardware.epoch_address),
                  static_cast<unsigned int>(timeout_hardware.epoch_byte_counter),
                  static_cast<unsigned int>(timeout_hardware.interrupt_status),
                  static_cast<unsigned int>(timeout_hardware.busif0_control),
                  static_cast<unsigned int>(timeout_hardware.busif0_error),
                  static_cast<unsigned int>(timeout_hardware.busif1_control),
                  static_cast<unsigned int>(timeout_hardware.busif1_error),
                  static_cast<unsigned int>(timeout_hardware.stream0_control),
                  static_cast<unsigned int>(timeout_hardware.stream0_address),
                  static_cast<unsigned int>(timeout_hardware.stream0_frame_size),
                  static_cast<unsigned int>(timeout_hardware.stream0_depth_count),
                  static_cast<unsigned int>(timeout_hardware.stream0_pixel_count),
                  static_cast<unsigned int>(timeout_hardware.stream0_line_count),
                  static_cast<unsigned int>(timeout_hardware.stream0_frame_count));
    return {common::Error{common::ErrorCode::kTimeout, last_error_, "npu.run"},
            last_execution_};
}

Status NpuDriver::Run()
{
    Status status = StartRun();
    if (!status.Ok()) {
        return status;
    }
    return WaitRun();
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

    stai_return_code code = static_cast<stai_return_code>(0U);
    for (std::uint32_t i = loaded_model_count_; i > 0U; --i) {
        const stai_return_code model_code = loaded_models_[i - 1U]->Shutdown();
        if (IsError(model_code) && !IsError(code)) {
            code = model_code;
        }
    }
    const stai_return_code runtime_code = stai_runtime_deinit();
    if (IsError(runtime_code) && !IsError(code)) {
        code = runtime_code;
    }
    last_error_ = static_cast<std::uint32_t>(code);
    /* A model switch reuses this driver with a different generated context.
     * Tear down the wait object and the IRQ state for every model, not only
     * the old segmentation build that happened to need a restart. */
    HAL_NVIC_DisableIRQ(NPU0_IRQn);
    NVIC_ClearPendingIRQ(NPU0_IRQn);
    if (g_npu_irq_event_flag > 0) {
        (void)tk_del_flg(g_npu_irq_event_flag);
        g_npu_irq_event_flag = 0;
    }
    model_ = nullptr;
    for (auto &loaded_model : loaded_models_) {
        loaded_model = nullptr;
    }
    loaded_model_count_ = 0U;
    epoch_trace_active_ = false;
    epoch_trace_last_cycles_ = 0U;
    initialized_ = false;
    last_execution_.state = ExecutionState::kUninitialized;
    last_execution_.stai_status = last_error_;
    return IsError(code)
               ? Status{common::Error{common::ErrorCode::kModel, last_error_,
                                      "npu.shutdown"},
                        last_execution_}
               : Status{common::Error{common::ErrorCode::kOk, 0U,
                                      "npu.shutdown"},
                        last_execution_};
}

} // namespace uai::ai::npu
