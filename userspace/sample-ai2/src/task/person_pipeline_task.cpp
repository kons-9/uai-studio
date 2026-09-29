#include "task/person_pipeline_task.hpp"

#include <cstdint>

#include "common/log.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "middleware/ai_runtime/pipeline.hpp"
#include "models/person/future.hpp"
#include "models/person/model.hpp"
#include "task/task_context.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task {
namespace {

using ai_runtime::ExecutionContext;
using ai_runtime::DispatchResult;

struct PersonApplication;
PersonApplication &App();

struct PersonApplication {
    models::person::Model model{};
    npu::NpuDriver npu{};
    ai_runtime::PipelineRuntime pipeline{};
    ai_runtime::Scheduler scheduler{pipeline};
    models::person::Future futures[memory_allocator::kInferenceBufferCount]{};
    stai_network_info info{};
    std::uint32_t model_sequence = 0U;
    std::uint32_t submitted_count = 0U;
    std::uint32_t completed_count = 0U;
    std::uint32_t postprocess_count = 0U;
    std::uint32_t last_detection_count = 0U;
    std::uint32_t last_capture_sequence = 0U;
    std::uint32_t report_tick = 0U;
    std::uint32_t report_submitted = 0U;
    std::uint32_t report_completed = 0U;
    std::uint32_t report_postprocess = 0U;

    void Report(TaskContext &context)
    {
        if (!context.diagnostics.inference_fps) return;
        const std::uint32_t now = context.Now();
        if (report_tick == 0U) {
            report_tick = now;
            return;
        }
        if (now - report_tick < 1000U) return;
        const auto camera = context.camera.GetDiagnostics();
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: person stats submitted=%u completed=%u "
                         "post=%u boxes=%u capture=%u pipe2=%u drops=%u csi=%u\n"),
                     static_cast<unsigned int>(submitted_count - report_submitted),
                     static_cast<unsigned int>(completed_count - report_completed),
                     static_cast<unsigned int>(postprocess_count - report_postprocess),
                     static_cast<unsigned int>(last_detection_count),
                     static_cast<unsigned int>(last_capture_sequence),
                     static_cast<unsigned int>(camera.pipe2_frame_event_count),
                     static_cast<unsigned int>(camera.pipe2_drop_count),
                     static_cast<unsigned int>(camera.csi_error_count));
        report_tick = now;
        report_submitted = submitted_count;
        report_completed = completed_count;
        report_postprocess = postprocess_count;
    }
    std::atomic<bool> enabled{false};

    common::Error Initialize(TaskContext &context)
    {
        npu::Status result = npu.Initialize(models::person::Runtime(model));
        if (!result.Ok()) return result.error;
        result = npu.GetInfo(&info);
        if (!result.Ok()) return result.error;
        if (info.n_inputs != 1U || info.inputs == nullptr ||
            info.n_outputs == 0U || info.n_outputs > models::kMaxModelOutputs ||
            info.outputs == nullptr ||
            info.inputs[0].size_bytes !=
                static_cast<std::size_t>(model.GetDescriptor().input_width) *
                    model.GetDescriptor().input_height * 3U) {
            return {common::ErrorCode::kModel, 0U, "person_pipeline.model_info"};
        }
        stai_ptr outputs[models::kMaxModelOutputs]{};
        stai_size count = 0U;
        result = npu.GetOutputs(outputs, &count);
        if (!result.Ok()) return result.error;
        if (count != info.n_outputs) {
            return {common::ErrorCode::kModel, count,
                    "person_pipeline.output_count"};
        }
        /* Concurrent postprocessing requires a distinct output per frame. */
        for (std::uint16_t i = 0U; i < count; ++i) {
            if (outputs[i] != nullptr ||
                info.outputs[i].size_bytes >
                    memory_allocator::kConfig.model_output_bytes[i]) {
                return {common::ErrorCode::kModel, i,
                        "person_pipeline.output_ownership"};
            }
        }
        const models::ModelCallbacks callbacks = model.GetCallbacks();
        if (callbacks.configure == nullptr ||
            callbacks.on_inference_complete == nullptr ||
            callbacks.user_data == nullptr) {
            return {common::ErrorCode::kModel, 0U,
                    "person_pipeline.decoder_callbacks"};
        }
        models::ModelOutputSpec spec{};
        spec.count = info.n_outputs;
        for (std::uint16_t i = 0U; i < spec.count; ++i) {
            spec.tensors[i] = {info.outputs[i].size_bytes,
                               info.outputs[i].scale.data[0],
                               info.outputs[i].zeropoint.data[0]};
        }
        const common::Error status = callbacks.configure(spec, callbacks.user_data);
        if (!status.Ok()) return status;
        pipeline.SetCriticalSection(&EnterCritical, &LeaveCritical, nullptr);
        pipeline.SetObserver(&OnDone, &context);
        pipeline.SetTrace(&OnTrace, &context, &Now, &context);
        enabled.store(true);
        return {};
    }

    static std::uint32_t Now(void *context)
    {
        return static_cast<TaskContext *>(context)->Now();
    }
    static void EnterCritical(void *) { interrupt_state_ = __get_PRIMASK(); __disable_irq(); }
    static void LeaveCritical(void *) { __set_PRIMASK(interrupt_state_); }
    static std::uint32_t interrupt_state_;

    static void OnTrace(void *context, const ai_runtime::StepTrace &trace)
    {
        auto &task = *static_cast<TaskContext *>(context);
        if (task.diagnostics.inference_trace) {
            UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                "ai: person inference=%u model=%u step=%u lane=%u time=%u begin=%u\n"),
                static_cast<unsigned int>(trace.inference_id),
                static_cast<unsigned int>(trace.model_id),
                static_cast<unsigned int>(trace.step_id),
                static_cast<unsigned int>(trace.context),
                static_cast<unsigned int>(trace.timestamp),
                static_cast<unsigned int>(trace.begin));
        }
    }
    static void OnDone(void *context, ai_runtime::AiFuture &base,
                       common::Error error)
    {
        auto &task = *static_cast<TaskContext *>(context);
        auto &future = static_cast<models::person::Future &>(base);
        ++App().completed_count;
        if (!error.Ok()) {
            LogStatus("person_pipeline", error);
            App().enabled.store(false);
            UAI_LOG_WARN(reinterpret_cast<const UB *>(
                "ai: person pipeline disabled; camera remains live\n"));
        }
        const common::Error released =
            task.memory.ReleaseInferenceBuffer(future.frame());
        LogStatus("memory", released);
        future.ReleaseClaim();
        App().Report(task);
    }
};

std::uint32_t PersonApplication::interrupt_state_ = 0U;
PersonApplication g_app{};
PersonApplication &App() { return g_app; }

void PublishBoxes(void *context, const memory_allocator::BoxSet &source)
{
    auto &task = *static_cast<TaskContext *>(context);
    auto &application = App();
    memory_allocator::BoxSet boxes = source;
    boxes.model_sequence = ++application.model_sequence;
    ++application.postprocess_count;
    application.last_detection_count = boxes.person.count;
    application.last_capture_sequence = boxes.capture_sequence;
    task.SendLatestBoxes(boxes);
    application.Report(task);
}

void RunWorker(ExecutionContext lane)
{
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "ai: person worker started lane=%u\n"),
                 static_cast<unsigned int>(lane));
    ai_runtime::Dispatcher dispatcher(g_app.pipeline, lane);
    for (;;) {
        const DispatchResult result = dispatcher.RunOnce();
        if (result == DispatchResult::kIdle ||
            result == DispatchResult::kNotReady) {
            tk_dly_tsk(1U);
        }
    }
}

} // namespace

void PersonPipelineTask::FrameEntry()
{
    TaskContext &task = GetTaskContext();
    UINT pattern = 0U;
    if (tk_wai_flg(task.external_memory_ready, kExternalMemoryReady,
                   TWF_ANDW, &pattern, TMO_FEVR) != E_OK) {
        task.Halt("ai: person pipeline memory wait failed\n");
    }
    if (!task.external_nor_ready) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
            "ai: person model unavailable; camera remains live\n"));
    } else {
        const common::Error status = g_app.Initialize(task);
        if (!status.Ok()) {
            LogStatus("person_pipeline.init", status);
        } else {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                "ai: person pipeline enabled (pre/npu/post)\n"));
            task.StartPersonPreprocessTask(
                reinterpret_cast<FP>(PersonPipelineTask::PreprocessEntry));
            task.StartPersonNpuTask(reinterpret_cast<FP>(PersonPipelineTask::NpuEntry));
            task.StartPersonPostprocessTask(
                reinterpret_cast<FP>(PersonPipelineTask::PostprocessEntry));
        }
    }
    for (;;) {
        InferenceMessage message{};
        if (tk_rcv_mbf(task.frame_queue, &message, TMO_FEVR) !=
            static_cast<INT>(sizeof(message))) continue;
        common::Error status = task.memory.ClaimInferenceBuffer(message.frame);
        if (!status.Ok()) {
            LogStatus("memory", status);
            continue;
        }
        models::person::Future *available = nullptr;
        if (g_app.enabled.load()) {
            for (models::person::Future &future : g_app.futures) {
                if (future.TryClaim()) {
                    available = &future;
                    break;
                }
            }
        }
        if (available != nullptr) {
            const models::person::FutureContext future_context{
                &g_app.model,
                &g_app.npu,
                &task.cache,
                &g_app.info,
                &PublishBoxes,
                &task};
            available->Reset(future_context, message.frame);
            status = g_app.scheduler.Submit(*available);
            if (status.Ok()) {
                ++g_app.submitted_count;
                if (g_app.submitted_count == 1U) {
                    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                                     "ai: person submit ok seq=%u\n"),
                                 static_cast<unsigned int>(message.frame.capture_sequence));
                }
                g_app.Report(task);
                continue;
            }
            available->ReleaseClaim();
        }
        const common::Error released = task.memory.ReleaseInferenceBuffer(message.frame);
        LogStatus("memory", released);
    }
}

void PersonPipelineTask::PreprocessEntry()
{
    RunWorker(ExecutionContext::kPreprocessCpu);
}

void PersonPipelineTask::NpuEntry() { RunWorker(ExecutionContext::kNpu); }
void PersonPipelineTask::PostprocessEntry()
{
    RunWorker(ExecutionContext::kPostprocessCpu);
}

} // namespace uai::ai::task
