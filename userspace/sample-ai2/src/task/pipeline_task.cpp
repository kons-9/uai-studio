#include "task/pipeline_task.hpp"

#include <cstddef>
#include <cstdint>

#include "common/log.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "driver/npu_driver/npu_network.hpp"
#include "middleware/ai_runtime/pipeline_dispatcher.hpp"
#include "models/face/future.hpp"
#include "models/face/npu_model.hpp"
#include "models/person/future.hpp"
#include "models/person/npu_model.hpp"
#include "task/task_context.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task {
namespace {

using ai_runtime::ExecutionContext;
using ai_runtime::DispatchResult;

struct PipelineApplication;
PipelineApplication &App();

struct RegisteredModel {
    const char *name = "";
    std::uint32_t kind_id = 0U;
    npu::NpuNetwork *network = nullptr;
    stai_network_info info{};
};

constexpr std::size_t kPersonModel = 0U;
constexpr std::size_t kFaceModel = 1U;
constexpr std::size_t kRegisteredModelCount = 2U;

common::Error ReadModelInfo(npu::NpuDriver &npu_driver,
                            RegisteredModel &model,
                            std::size_t expected_input_bytes)
{
    npu::Status result = npu_driver.GetInfo(&model.info);
    if (!result.Ok()) return result.error;
    if (model.info.n_inputs != 1U || model.info.inputs == nullptr ||
        model.info.n_outputs == 0U ||
        model.info.n_outputs >
            memory_allocator::kConfig.model_output_bytes.size() ||
        model.info.outputs == nullptr ||
        model.info.inputs[0].size_bytes != expected_input_bytes) {
        return {common::ErrorCode::kModel, 0U, "pipeline.model_info"};
    }

    stai_ptr outputs[memory_allocator::kConfig.model_output_bytes.size()]{};
    stai_size count = 0U;
    result = npu_driver.GetOutputs(outputs, &count);
    if (!result.Ok()) return result.error;
    if (count != model.info.n_outputs) {
        return {common::ErrorCode::kModel, count, "pipeline.output_count"};
    }
    for (std::uint16_t i = 0U; i < count; ++i) {
        if (outputs[i] != nullptr ||
            model.info.outputs[i].size_bytes >
                memory_allocator::kConfig.model_output_bytes[i]) {
            return {common::ErrorCode::kModel, i,
                    "pipeline.output_ownership"};
        }
    }
    return {};
}

struct PipelineApplication {
    models::person::NpuModel person_model{};
    models::face::NpuModel face_model{};
    RegisteredModel registered_models[kRegisteredModelCount]{
        {"person", 0U, &person_model, {}},
        {"face", 2U, &face_model, {}},
    };
    npu::NpuDriver npu{};
    ai_runtime::PipelineRuntime pipeline{};
    ai_runtime::Scheduler scheduler{pipeline};
    models::person::Future person_futures[memory_allocator::kInferenceBufferCount]{};
    models::face::Future face_futures[memory_allocator::kInferenceBufferCount]{};
    std::uint32_t model_sequence = 0U;
    std::uint32_t submitted_count[kRegisteredModelCount]{};
    std::uint32_t completed_count[kRegisteredModelCount]{};
    std::uint32_t postprocess_count = 0U;
    std::uint32_t last_detection_count = 0U;
    std::uint32_t last_capture_sequence = 0U;
    std::uint32_t report_tick = 0U;
    std::uint32_t report_submitted = 0U;
    std::uint32_t report_completed = 0U;
    std::uint32_t report_face_submitted = 0U;
    std::uint32_t report_face_completed = 0U;
    std::uint32_t report_postprocess = 0U;
    // Start with face so the first visible result is available immediately;
    // the scheduler alternates to person after the first completed run.
    bool next_face = true;
    memory_allocator::BoxSet latest_boxes{};
    std::atomic<bool> enabled{false};
    // The two models share one NPU context.  Keep only one inference in the
    // pipeline at a time so that a model switch cannot race an outstanding
    // NPU wait or overwrite the selected model's buffers.
    std::atomic<bool> inference_in_flight{false};

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
                         "ai: model stats person=%u/%u face=%u/%u "
                         "post=%u boxes=%u capture=%u pipe2=%u drops=%u csi=%u\n"),
                     static_cast<unsigned int>(submitted_count[kPersonModel] -
                                               report_submitted),
                     static_cast<unsigned int>(completed_count[kPersonModel] -
                                               report_completed),
                     static_cast<unsigned int>(submitted_count[kFaceModel] -
                                               report_face_submitted),
                     static_cast<unsigned int>(completed_count[kFaceModel] -
                                               report_face_completed),
                     static_cast<unsigned int>(postprocess_count -
                                               report_postprocess),
                     static_cast<unsigned int>(last_detection_count),
                     static_cast<unsigned int>(last_capture_sequence),
                     static_cast<unsigned int>(camera.pipe2_frame_event_count),
                     static_cast<unsigned int>(camera.pipe2_drop_count),
                     static_cast<unsigned int>(camera.csi_error_count));
        report_tick = now;
        report_submitted = submitted_count[kPersonModel];
        report_completed = completed_count[kPersonModel];
        report_face_submitted = submitted_count[kFaceModel];
        report_face_completed = completed_count[kFaceModel];
        report_postprocess = postprocess_count;
    }

    common::Error Initialize(TaskContext &context)
    {
        npu::Status result =
            npu.Initialize(*registered_models[kPersonModel].network);
        if (!result.Ok()) return result.error;
        common::Error status = ReadModelInfo(
            npu, registered_models[kPersonModel],
            models::person::Future::InputBytes());
        if (!status.Ok()) return status;
        status = models::person::Future::ConfigureDecoder(
            registered_models[kPersonModel].info);
        if (!status.Ok()) return status;

        result = npu.Preload(*registered_models[kFaceModel].network);
        if (!result.Ok()) return result.error;
        result = npu.SelectModel(*registered_models[kFaceModel].network);
        if (!result.Ok()) return result.error;
        status = ReadModelInfo(npu, registered_models[kFaceModel],
                               models::face::Future::InputBytes());
        if (!status.Ok()) return status;
        status = models::face::Future::ConfigureDecoder(
            registered_models[kFaceModel].info);
        if (!status.Ok()) return status;

        result = npu.SelectModel(*registered_models[kPersonModel].network);
        if (!result.Ok()) return result.error;
        npu.SetEpochTraceModelKindId(registered_models[kPersonModel].kind_id);

        for (const RegisteredModel &model : registered_models) {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                             "ai: model registered=%s\n"),
                         reinterpret_cast<const UB *>(model.name));
        }
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
            const UB *name = trace.model_id ==
                                     static_cast<ai_runtime::AiModelId>(2U)
                                 ? reinterpret_cast<const UB *>("face")
                                 : reinterpret_cast<const UB *>("person");
            UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                "ai: %s inference=%u model=%u step=%u lane=%u time=%u begin=%u\n"),
                name,
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
        auto &application = App();
        const bool is_face =
            base.model_id() == static_cast<ai_runtime::AiModelId>(2U);
        const std::size_t model_index = is_face ? kFaceModel : kPersonModel;
        ++application.completed_count[model_index];
        if (!error.Ok()) {
            LogStatus(is_face ? "face_pipeline" : "person_pipeline", error);
            application.enabled.store(false);
            UAI_LOG_WARN(reinterpret_cast<const UB *>(
                "ai: model pipeline disabled; camera remains live\n"));
        }
        if (is_face) {
            auto &future = static_cast<models::face::Future &>(base);
            const common::Error released =
                task.memory.ReleaseInferenceBuffer(future.frame());
            LogStatus("memory", released);
            future.ReleaseClaim();
        } else {
            auto &future = static_cast<models::person::Future &>(base);
            const common::Error released =
                task.memory.ReleaseInferenceBuffer(future.frame());
            LogStatus("memory", released);
            future.ReleaseClaim();
        }
        application.inference_in_flight.store(false, std::memory_order_release);
        application.Report(task);
    }
};

std::uint32_t PipelineApplication::interrupt_state_ = 0U;
PipelineApplication g_app{};
PipelineApplication &App() { return g_app; }

void PublishBoxes(void *context, const memory_allocator::BoxSet &source)
{
    auto &task = *static_cast<TaskContext *>(context);
    auto &application = App();
    auto &boxes = application.latest_boxes;
    if (source.person_valid) {
        boxes.person = source.person;
        boxes.person_valid = true;
    }
    if (source.face_valid) {
        boxes.face = source.face;
        boxes.face_valid = true;
    }
    boxes.segmentation_valid = false;
    boxes.model_sequence = ++application.model_sequence;
    boxes.capture_sequence = source.capture_sequence;
    ++application.postprocess_count;
    application.last_detection_count = boxes.person.count + boxes.face.count;
    application.last_capture_sequence = boxes.capture_sequence;
    task.SendLatestBoxes(boxes);
    application.Report(task);
}

void RunWorker(ExecutionContext lane)
{
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "ai: model worker started lane=%u\n"),
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

void PipelineTask::FrameEntry()
{
    TaskContext &task = GetTaskContext();
    UINT pattern = 0U;
    if (tk_wai_flg(task.external_memory_ready, kExternalMemoryReady,
                   TWF_ANDW, &pattern, TMO_FEVR) != E_OK) {
        task.Halt("ai: model pipeline memory wait failed\n");
    }
    if (!task.external_nor_ready) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
            "ai: registered models unavailable; camera remains live\n"));
    } else {
        const common::Error status = g_app.Initialize(task);
        if (!status.Ok()) {
            LogStatus("model_pipeline.init", status);
        } else {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                "ai: 2-model pipeline enabled (person/face)\n"));
            task.StartPreprocessTask(
                reinterpret_cast<FP>(PipelineTask::PreprocessEntry));
            task.StartNpuTask(reinterpret_cast<FP>(PipelineTask::NpuEntry));
            task.StartPostprocessTask(
                reinterpret_cast<FP>(PipelineTask::PostprocessEntry));
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
        if (g_app.enabled.load(std::memory_order_acquire)) {
            bool expected = false;
            if (g_app.inference_in_flight.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                const bool is_face = g_app.next_face;
                auto *person_available = &g_app.person_futures[0];
                auto *face_available = &g_app.face_futures[0];
                if (is_face) {
                    if (!face_available->TryClaim()) {
                        g_app.inference_in_flight.store(
                            false, std::memory_order_release);
                        const common::Error released =
                            task.memory.ReleaseInferenceBuffer(message.frame);
                        LogStatus("memory", released);
                        continue;
                    }
                } else if (!person_available->TryClaim()) {
                    g_app.inference_in_flight.store(
                        false, std::memory_order_release);
                    const common::Error released =
                        task.memory.ReleaseInferenceBuffer(message.frame);
                    LogStatus("memory", released);
                    continue;
                }
                g_app.next_face = !is_face;
                if (is_face) {
                    models::face::FutureContext future_context{};
                    future_context.npu = &g_app.npu;
                    future_context.model =
                        g_app.registered_models[kFaceModel].network;
                    future_context.model_kind_id =
                        g_app.registered_models[kFaceModel].kind_id;
                    future_context.cache = &task.cache;
                    future_context.info =
                        &g_app.registered_models[kFaceModel].info;
                    future_context.publish = &PublishBoxes;
                    future_context.publish_context = &task;
                    face_available->Reset(future_context, message.frame);
                    status = g_app.scheduler.Submit(*face_available);
                } else {
                    models::person::FutureContext future_context{};
                    future_context.npu = &g_app.npu;
                    future_context.model =
                        g_app.registered_models[kPersonModel].network;
                    future_context.model_kind_id =
                        g_app.registered_models[kPersonModel].kind_id;
                    future_context.cache = &task.cache;
                    future_context.info =
                        &g_app.registered_models[kPersonModel].info;
                    future_context.publish = &PublishBoxes;
                    future_context.publish_context = &task;
                    person_available->Reset(future_context, message.frame);
                    status = g_app.scheduler.Submit(*person_available);
                }
                if (status.Ok()) {
                    const std::size_t model_index =
                        is_face ? kFaceModel : kPersonModel;
                    ++g_app.submitted_count[model_index];
                    if (g_app.submitted_count[model_index] == 1U) {
                        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                                         "ai: %s submit ok seq=%u\n"),
                                     reinterpret_cast<const UB *>(
                                         is_face ? "face" : "person"),
                                     static_cast<unsigned int>(
                                         message.frame.capture_sequence));
                    }
                    g_app.Report(task);
                    continue;
                }
                if (is_face) {
                    face_available->ReleaseClaim();
                } else {
                    person_available->ReleaseClaim();
                }
                g_app.inference_in_flight.store(false, std::memory_order_release);
            }
        }
        const common::Error released = task.memory.ReleaseInferenceBuffer(message.frame);
        LogStatus("memory", released);
    }
}

void PipelineTask::PreprocessEntry()
{
    RunWorker(ExecutionContext::kPreprocessCpu);
}

void PipelineTask::NpuEntry() { RunWorker(ExecutionContext::kNpu); }
void PipelineTask::PostprocessEntry()
{
    RunWorker(ExecutionContext::kPostprocessCpu);
}

} // namespace uai::ai::task
