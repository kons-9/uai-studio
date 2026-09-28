#include "driver/npu_driver/npu_driver.hpp"
#include "driver/npu_driver/debug.h"

#include <tk/tkernel.h>

/* C ABI のST AIランタイム/HAL関数とリンクする宣言。 */
extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
void LL_ATON_NPU0_IRQHandler(void);
#if defined(AI_MODEL_SEGMENTATION) && defined(AI_SEGMENTATION_DIAG)
#include "model_manager/model/segmentation/model_segmentation_diagnostics.h"
#endif

stai_return_code stai_runtime_init(void);
stai_return_code stai_runtime_deinit(void);
}

namespace {

constexpr UINT kNpuIrqEvent = 0x01U;
ID g_npu_irq_event_flag = 0;

} // namespace
extern "C" void NPU0_IRQHandler(UINT intno)
{
#if defined(AI_MODEL_SEGMENTATION) && defined(AI_SEGMENTATION_DIAG)
    ai_segmentation_diag_irq(0U);
#endif
    LL_ATON_NPU0_IRQHandler();
#if defined(AI_MODEL_SEGMENTATION) && defined(AI_SEGMENTATION_DIAG)
    ai_segmentation_diag_irq(1U);
#endif
    if (g_npu_irq_event_flag > 0) {
        (void)tk_set_flg(g_npu_irq_event_flag, kNpuIrqEvent);
    }
    (void)intno;
}

namespace uai::ai::npu {

namespace {

constexpr std::uint32_t kTimeoutTicks = 5000U;

#if AI_INFERENCE_DIAGNOSTICS
#define AI_INFERENCE_TRACE(...) tm_printf(__VA_ARGS__)
#else
#define AI_INFERENCE_TRACE(...) ((void)0)
#endif

#if AI_INFERENCE_DIAGNOSTICS
void EnableCycleCounter()
{
    /* DWT gives a sub-millisecond wall-clock measurement for the asynchronous
     * NPU run. It is enabled only for instrumentation; the counter does not
     * affect the NPU protocol or the task scheduler. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
#endif

#if AI_INFERENCE_DIAGNOSTICS
std::uint32_t CyclesToMicroseconds(std::uint32_t cycles)
{
    if (SystemCoreClock == 0U) {
        return 0U;
    }
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(cycles) * 1000000ULL) /
        static_cast<std::uint32_t>(SystemCoreClock));
}
#endif

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

#if defined(AI_MODEL_SEGMENTATION) && defined(AI_SEGMENTATION_DIAG)
class SegmentationDiagnosticRunScope final {
public:
    SegmentationDiagnosticRunScope()
        : active_(ai_segmentation_diag_begin() != 0)
    {
    }

    ~SegmentationDiagnosticRunScope()
    {
        if (active_) {
            ai_segmentation_diag_dump();
        }
    }

private:
    bool active_;
};
#endif

} // namespace

common::Error NpuDriver::InitializeMemory()
{
    static bool initialized = false;
    if (initialized) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "npu.memory_initialize"};
    }
    EnableNpuMemory();
#if AI_INFERENCE_DIAGNOSTICS
    EnableCycleCounter();
#endif
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

Status NpuDriver::InvalidState(const char *operation) const
{
    return {common::Error{common::ErrorCode::kNotInitialized, 0U, operation},
            last_execution_};
}

Status NpuDriver::Initialize(model_manager::Model &model)
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

    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: npu init begin irq_en=%u irq_pending=%u\n"),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)));
    HAL_NVIC_SetPriority(NPU0_IRQn, 8U, 0U);
    HAL_NVIC_EnableIRQ(NPU0_IRQn);

    const stai_return_code runtime_code = stai_runtime_init();
#if AI_INFERENCE_DIAGNOSTICS
    const registers::NpuRegisterSnapshot runtime_hardware =
        registers_.ReadSnapshot();
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: npu runtime init code=%x irq_en=%u pending=%u epoch=%x int=%x bus=%x\n"),
              static_cast<unsigned int>(runtime_code),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(runtime_hardware.epoch_control),
              static_cast<unsigned int>(runtime_hardware.interrupt_status),
              static_cast<unsigned int>(runtime_hardware.busif0_error));
#endif
    last_error_ = static_cast<std::uint32_t>(runtime_code);
    if (IsError(runtime_code)) {
        last_execution_.state = ExecutionState::kFaulted;
        last_execution_.stai_status = last_error_;
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.runtime_initialize"},
                last_execution_};
    }

    const stai_return_code model_code = model_->Initialize();
#if AI_INFERENCE_DIAGNOSTICS
    const registers::NpuRegisterSnapshot model_hardware =
        registers_.ReadSnapshot();
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: npu model init code=%x irq_en=%u pending=%u epoch=%x int=%x bus=%x\n"),
              static_cast<unsigned int>(model_code),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(model_hardware.epoch_control),
              static_cast<unsigned int>(model_hardware.interrupt_status),
              static_cast<unsigned int>(model_hardware.busif0_error));
#endif
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

Status NpuDriver::Preload(model_manager::Model &model)
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

Status NpuDriver::SelectModel(model_manager::Model &model)
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

bool NpuDriver::IsLoaded(const model_manager::Model &model) const
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

Status NpuDriver::Run()
{
    if (!initialized_ || model_ == nullptr) {
        return InvalidState("npu.run");
    }

#if defined(AI_MODEL_SEGMENTATION) && defined(AI_SEGMENTATION_DIAG)
    SegmentationDiagnosticRunScope segmentation_diagnostics;
#endif

    /* Keep the ref application's asynchronous STAI protocol. The NPU IRQ
     * wakes this task through a kernel event flag; run_continue() then
     * consumes the Neural-ART event state and starts the next epoch. */
#if AI_INFERENCE_DIAGNOSTICS
    const registers::NpuRegisterSnapshot before_run =
        registers_.ReadSnapshot();
#endif
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
    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: npu run begin irq_en=%u pending=%u priority=%u count=%u last=%x csi=%x/%x epoch=%x int=%x bus=%x\n"),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(NVIC_GetPriority(NPU0_IRQn)),
              g_aton_irq_count, g_aton_last_irqs,
              static_cast<unsigned int>(CSI->SR0),
              static_cast<unsigned int>(CSI->SR1),
              static_cast<unsigned int>(before_run.epoch_control),
              static_cast<unsigned int>(before_run.interrupt_status),
              static_cast<unsigned int>(before_run.busif0_error));
#if AI_INFERENCE_DIAGNOSTICS
    const std::uint32_t npu_start_cycles = DWT->CYCCNT;
#endif
    stai_return_code code = model_->Run(STAI_MODE_ASYNC);
    last_error_ = static_cast<std::uint32_t>(code);
    last_execution_.state = ExecutionState::kSubmitted;
    last_execution_.stai_status = last_error_;
    if (IsError(code)) {
        last_execution_.state = ExecutionState::kFaulted;
        return {common::Error{common::ErrorCode::kNpu, last_error_,
                              "npu.run"},
                last_execution_};
    }

    bool completed = false;
    for (std::uint32_t tick = 0U; tick < kTimeoutTicks; ++tick) {
        code = model_->GetRunStatus();
        last_error_ = static_cast<std::uint32_t>(code);
#if defined(AI_MODEL_SEGMENTATION) && defined(AI_SEGMENTATION_DIAG)
        ai_segmentation_diag_poll(tick, static_cast<std::uint32_t>(code));
#endif
        if (code == STAI_DONE) {
            completed = true;
            break;
        }
        if (IsError(code)) {
            last_execution_.state = ExecutionState::kFaulted;
            last_execution_.stai_status = last_error_;
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: npu done error=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(code), g_aton_irq_count,
                      g_aton_last_irqs);
            return {common::Error{common::ErrorCode::kNpu, last_error_,
                                  "npu.run"},
                    last_execution_};
        }

 #if AI_INFERENCE_DIAGNOSTICS
        if ((tick % 100U) == 0U) {
            const registers::NpuRegisterSnapshot waiting =
                registers_.ReadSnapshot();
            AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                          "ai: npu wait tick=%u status=%x irq=%u last=%x epoch=%x int=%x bus=%x\n"),
                      static_cast<unsigned int>(tick),
                      static_cast<unsigned int>(code), g_aton_irq_count,
                      g_aton_last_irqs,
                      static_cast<unsigned int>(waiting.epoch_control),
                      static_cast<unsigned int>(waiting.interrupt_status),
                      static_cast<unsigned int>(waiting.busif0_error));
        }
 #endif

        /* The generated ST.AI runtime distinguishes two kinds of wait:
         * STAI_RUNNING_NO_WFE means that the next epoch can be continued
         * immediately, while STAI_RUNNING_WFE means that an NPU IRQ must
         * arrive first.  Do not add a fixed 1-tick delay here.  A face model
         * has many epoch blocks, so that delay accumulates into seconds. */
        if (code == STAI_RUNNING_WFE) {
            UINT pattern = 0U;
            const ER wait_status = tk_wai_flg(
                g_npu_irq_event_flag, kNpuIrqEvent,
                TWF_ANDW | TWF_BITCLR, &pattern,
                static_cast<TMO>(kTimeoutTicks));
            if (wait_status == E_TMOUT) {
                break;
            }
            if (wait_status != E_OK) {
                last_execution_.state = ExecutionState::kFaulted;
                last_execution_.stai_status =
                    static_cast<std::uint32_t>(wait_status);
                return {common::Error{common::ErrorCode::kNpu,
                                      static_cast<std::uint32_t>(wait_status),
                                      "npu.wait_irq"},
                        last_execution_};
            }
        }
        code = model_->ContinueRun();
        last_error_ = static_cast<std::uint32_t>(code);
        if (IsError(code)) {
            last_execution_.state = ExecutionState::kFaulted;
            last_execution_.stai_status = last_error_;
            return {common::Error{common::ErrorCode::kNpu, last_error_,
                                  "npu.run_continue"},
                    last_execution_};
        }
    }

    if (!completed) {
        last_execution_.state = ExecutionState::kTimedOut;
        last_execution_.stai_status = last_error_;
        const registers::NpuRegisterSnapshot timeout_hardware =
            registers_.ReadSnapshot();
        tm_printf(reinterpret_cast<const UB *>(
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
        return {common::Error{common::ErrorCode::kTimeout, last_error_,
                              "npu.run"},
                last_execution_};
    }

    AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                  "ai: npu done code=%x irq=%u last=%x pending=%u csi=%x/%x\n"),
              static_cast<unsigned int>(STAI_SUCCESS), g_aton_irq_count,
              g_aton_last_irqs,
              static_cast<unsigned int>(NVIC_GetPendingIRQ(NPU0_IRQn)),
              static_cast<unsigned int>(CSI->SR0),
              static_cast<unsigned int>(CSI->SR1));
#if AI_INFERENCE_DIAGNOSTICS
    const std::uint32_t npu_cycles = DWT->CYCCNT - npu_start_cycles;
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu timing cycles=%u us=%u core_hz=%u\n"),
              static_cast<unsigned int>(npu_cycles),
              static_cast<unsigned int>(CyclesToMicroseconds(npu_cycles)),
              static_cast<unsigned int>(SystemCoreClock));
#endif
    last_execution_.state = ExecutionState::kCompleted;
    last_execution_.stai_status = STAI_DONE;
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
