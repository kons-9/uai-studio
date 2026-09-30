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
#include "models/segmentation/future.hpp"
#include "models/segmentation/npu_model.hpp"
#include "models/inference_result_types.hpp"
#include "middleware/memory/generated/memory_config.hpp"
#include "task/task_context.hpp"
#include "task/task.hpp"
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
constexpr std::size_t kSegmentationModel = 2U;
constexpr std::size_t kRegisteredModelCount = 3U;
constexpr std::size_t kModelScheduleLength = 4U;
constexpr std::size_t kModelSchedule[kModelScheduleLength]{
    kPersonModel, kFaceModel, kSegmentationModel, kFaceModel};

UINT PipelineWorkBit(ExecutionContext lane)
{
    return 1U << static_cast<UINT>(lane);
}

template <typename Future>
Future *ClaimAvailableFuture(Future *futures)
{
    for (std::size_t i = 0U; i < memory_manager::kInferenceBufferCount;
         ++i) {
        if (futures[i].TryClaim()) return &futures[i];
    }
    return nullptr;
}

void WakePipelineWorker(void *, ExecutionContext lane)
{
    TaskContext &task = GetTaskContext();
    const ER status = tk_set_flg(task.pipeline_work_ready,
                                 PipelineWorkBit(lane));
    if (status != E_OK) {
        UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                          "ai: pipeline worker wake failed lane=%u code=%x\n"),
                      static_cast<unsigned int>(lane),
                      static_cast<unsigned int>(status));
    }
}

common::Error ReadModelInfo(npu::NpuManagement &npu_driver,
                            RegisteredModel &model,
                            std::size_t expected_input_bytes)
{
    npu::Status result = npu_driver.GetInfo(&model.info);
    if (!result.Ok()) return result.error;
    if (model.info.n_inputs != 1U || model.info.inputs == nullptr ||
        model.info.n_outputs == 0U ||
        model.info.n_outputs >
            memory_manager::kMemoryConfig.model_output_bytes.size() ||
        model.info.outputs == nullptr ||
        model.info.inputs[0].size_bytes != expected_input_bytes) {
        return {common::ErrorCode::kModel, 0U, "pipeline.model_info"};
    }

    stai_ptr outputs[memory_manager::kMemoryConfig.model_output_bytes.size()]{};
    stai_size count = 0U;
    result = npu_driver.GetOutputs(outputs, &count);
    if (!result.Ok()) return result.error;
    if (count != model.info.n_outputs) {
        return {common::ErrorCode::kModel, count, "pipeline.output_count"};
    }
    for (std::uint16_t i = 0U; i < count; ++i) {
        if (outputs[i] != nullptr ||
            model.info.outputs[i].size_bytes >
                memory_manager::kMemoryConfig.model_output_bytes[i]) {
            return {common::ErrorCode::kModel, i,
                    "pipeline.output_ownership"};
        }
    }
    return {};
}

struct PipelineApplication {
    models::person::NpuModel person_model{};
    models::face::NpuModel face_model{};
    models::segmentation::NpuModel segmentation_model{};
    RegisteredModel registered_models[kRegisteredModelCount]{
        {"person", 0U, &person_model, {}},
        {"face", 2U, &face_model, {}},
        {"segmentation", 1U, &segmentation_model, {}},
    };
    npu::NpuManagement &npu = npu::NpuManagement::Instance();
    npu::NpuManagement::Accessor npu_accessor{};
    ai_runtime::PipelineRuntime pipeline{};
    ai_runtime::Scheduler scheduler{pipeline};
    models::person::Future person_futures[memory_manager::kInferenceBufferCount]{};
    models::face::Future face_futures[memory_manager::kInferenceBufferCount]{};
    models::segmentation::Future
        segmentation_futures[memory_manager::kInferenceBufferCount]{};
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
    std::uint32_t report_segmentation_submitted = 0U;
    std::uint32_t report_segmentation_completed = 0U;
    std::uint32_t report_postprocess = 0U;
    /* Keep the requested submission order. If the next model has no free
     * Future, the frame task advances to the next available entry so a busy
     * model cannot stop the other pipelines from making progress. */
    std::size_t next_schedule_index = 0U;
    inference::BoxSet latest_boxes{};
    std::atomic<bool> enabled{false};
    /* The NPU context is shared, but the NPU dispatcher is a single worker.
     * Multiple futures may therefore be queued safely: CPU preprocess can
     * prepare the next frame while the NPU worker is waiting for the current
     * inference, and postprocess can consume an older output concurrently. */

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
                         "seg=%u/%u post=%u boxes=%u capture=%u pipe2=%u "
                         "drops=%u csi=%u\n"),
                     static_cast<unsigned int>(submitted_count[kPersonModel] -
                                               report_submitted),
                     static_cast<unsigned int>(completed_count[kPersonModel] -
                                               report_completed),
                     static_cast<unsigned int>(submitted_count[kFaceModel] -
                                               report_face_submitted),
                     static_cast<unsigned int>(completed_count[kFaceModel] -
                                               report_face_completed),
                     static_cast<unsigned int>(
                         submitted_count[kSegmentationModel] -
                         report_segmentation_submitted),
                     static_cast<unsigned int>(
                         completed_count[kSegmentationModel] -
                         report_segmentation_completed),
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
        report_segmentation_submitted =
            submitted_count[kSegmentationModel];
        report_segmentation_completed =
            completed_count[kSegmentationModel];
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

        result = npu.Preload(*registered_models[kSegmentationModel].network);
        if (!result.Ok()) return result.error;
        result = npu.SelectModel(*registered_models[kSegmentationModel].network);
        if (!result.Ok()) return result.error;
        status = ReadModelInfo(
            npu, registered_models[kSegmentationModel],
            models::segmentation::Future::InputBytes());
        if (!status.Ok()) return status;
        status = models::segmentation::Future::ConfigureDecoder(
            registered_models[kSegmentationModel].info);
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
        pipeline.SetWakeCallback(&WakePipelineWorker, nullptr);
        status = npu.Acquire(&npu_accessor);
        if (!status.Ok()) return status;
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
            const UB *name =
                trace.model_id == static_cast<ai_runtime::AiModelId>(2U)
                    ? reinterpret_cast<const UB *>("face")
                    : trace.model_id == static_cast<ai_runtime::AiModelId>(1U)
                          ? reinterpret_cast<const UB *>("segmentation")
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
        const auto model_id = base.model_id();
        const bool is_face =
            model_id == static_cast<ai_runtime::AiModelId>(2U);
        const bool is_segmentation =
            model_id == static_cast<ai_runtime::AiModelId>(1U);
        const std::size_t model_index =
            is_face ? kFaceModel
                    : is_segmentation ? kSegmentationModel : kPersonModel;
        ++application.completed_count[model_index];
        if (!error.Ok()) {
            LogStatus(is_face ? "face_pipeline"
                              : is_segmentation ? "segmentation_pipeline"
                                                : "person_pipeline",
                      error);
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
        } else if (is_segmentation) {
            auto &future = static_cast<models::segmentation::Future &>(base);
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
        application.Report(task);
    }
};

std::uint32_t PipelineApplication::interrupt_state_ = 0U;
PipelineApplication g_app{};
PipelineApplication &App() { return g_app; }

void PublishBoxes(void *context, const inference::BoxSet &source)
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
    if (source.segmentation_valid) {
        boxes.segmentation = source.segmentation;
        boxes.segmentation_valid = true;
    }
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
    if (lane == ExecutionContext::kNpu) {
        for (const RegisteredModel &model : g_app.registered_models) {
            const common::Error name_status =
                g_app.pipeline.RegisterModelName(
                    static_cast<ai_runtime::AiModelId>(model.kind_id),
                    model.name);
            if (!name_status.Ok()) {
                LogStatus("ai_model_monitor.register_model_name", name_status);
            }
        }
        const common::Error monitor_status =
            g_app.pipeline.StartAiModelMonitor();
        if (!monitor_status.Ok()) {
            LogStatus("ai_model_monitor.start", monitor_status);
        } else {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                             "ai: ai_model_monitor started for ai_runtime npu lane\n"));
        }
    }
    ai_runtime::Dispatcher dispatcher(g_app.pipeline, lane);
    TaskContext &task = GetTaskContext();
    const UINT wake_bit = PipelineWorkBit(lane);
    const char *task_name =
        lane == ExecutionContext::kPreprocessCpu
            ? "pipeline_preprocess"
            : lane == ExecutionContext::kNpu ? "pipeline_npu"
                                               : "pipeline_postprocess";
    bool dispatch_ready = true;
    Task::RunForever(task.cpu_task_monitor, task_name, [&] {
        if (dispatch_ready) return;
        UINT pattern = 0U;
        const ER wait_status = tk_wai_flg(
            task.pipeline_work_ready, wake_bit, TWF_ANDW | TWF_BITCLR,
            &pattern, TMO_FEVR);
        if (wait_status != E_OK) {
            UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                              "ai: pipeline worker wait failed lane=%u code=%x\n"),
                          static_cast<unsigned int>(lane),
                          static_cast<unsigned int>(wait_status));
            task.Halt("ai: pipeline worker event wait failed\n");
        }
        dispatch_ready = true;
    }, [&] {
        const DispatchResult result = dispatcher.RunOnce();
        dispatch_ready = result == DispatchResult::kRan;
        if (dispatch_ready) {
            /* Keep the three same-priority pipeline workers fair. This is
             * especially important after a pre step queues NPU work: the
             * next worker must get CPU time while the NPU task is waiting for
             * accelerator progress. */
            (void)tk_rot_rdq(TPRI_RUN);
        }
    });
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
                "ai: 3-model pipeline enabled (person/face/segmentation)\n"));
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
            std::size_t model_index = kRegisteredModelCount;
            std::size_t selected_schedule_index = kModelScheduleLength;
            models::face::Future *face_available = nullptr;
            models::segmentation::Future *segmentation_available = nullptr;
            models::person::Future *person_available = nullptr;
            for (std::size_t offset = 0U; offset < kModelScheduleLength;
                 ++offset) {
                const std::size_t schedule_index =
                    (g_app.next_schedule_index + offset) %
                    kModelScheduleLength;
                const std::size_t candidate =
                    kModelSchedule[schedule_index];
                if (candidate == kFaceModel) {
                    face_available =
                        ClaimAvailableFuture(g_app.face_futures);
                    if (face_available != nullptr) {
                        model_index = candidate;
                        selected_schedule_index = schedule_index;
                    }
                } else if (candidate == kSegmentationModel) {
                    segmentation_available = ClaimAvailableFuture(
                        g_app.segmentation_futures);
                    if (segmentation_available != nullptr) {
                        model_index = candidate;
                        selected_schedule_index = schedule_index;
                    }
                } else {
                    person_available =
                        ClaimAvailableFuture(g_app.person_futures);
                    if (person_available != nullptr) {
                        model_index = candidate;
                        selected_schedule_index = schedule_index;
                    }
                }
                if (model_index != kRegisteredModelCount) break;
            }
            if (model_index == kRegisteredModelCount) {
                const common::Error released =
                    task.memory.ReleaseInferenceBuffer(message.frame);
                LogStatus("memory", released);
                continue;
            }

            if (model_index == kFaceModel) {
                models::face::FutureContext future_context{};
                future_context.npu = g_app.npu_accessor.Get();
                future_context.npu_writer = &g_app.npu_accessor.Ownership();
                future_context.model =
                    g_app.registered_models[kFaceModel].network;
                future_context.model_kind_id =
                    g_app.registered_models[kFaceModel].kind_id;
                future_context.cache = &task.cache;
                future_context.info = &g_app.registered_models[kFaceModel].info;
                future_context.publish = &PublishBoxes;
                future_context.publish_context = &task;
                face_available->Reset(future_context, message.frame);
                status = g_app.scheduler.Submit(*face_available);
            } else if (model_index == kSegmentationModel) {
                models::segmentation::FutureContext future_context{};
                future_context.npu = g_app.npu_accessor.Get();
                future_context.npu_writer = &g_app.npu_accessor.Ownership();
                future_context.model =
                    g_app.registered_models[kSegmentationModel].network;
                future_context.model_kind_id =
                    g_app.registered_models[kSegmentationModel].kind_id;
                future_context.cache = &task.cache;
                future_context.info =
                    &g_app.registered_models[kSegmentationModel].info;
                future_context.publish = &PublishBoxes;
                future_context.publish_context = &task;
                segmentation_available->Reset(future_context, message.frame);
                status = g_app.scheduler.Submit(*segmentation_available);
            } else {
                models::person::FutureContext future_context{};
                future_context.npu = g_app.npu_accessor.Get();
                future_context.npu_writer = &g_app.npu_accessor.Ownership();
                future_context.model =
                    g_app.registered_models[kPersonModel].network;
                future_context.model_kind_id =
                    g_app.registered_models[kPersonModel].kind_id;
                future_context.cache = &task.cache;
                future_context.info = &g_app.registered_models[kPersonModel].info;
                future_context.publish = &PublishBoxes;
                future_context.publish_context = &task;
                person_available->Reset(future_context, message.frame);
                status = g_app.scheduler.Submit(*person_available);
            }
            if (status.Ok()) {
                ++g_app.submitted_count[model_index];
                if (g_app.submitted_count[model_index] == 1U) {
                    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                                     "ai: %s submit ok seq=%u\n"),
                                 reinterpret_cast<const UB *>(
                                     model_index == kFaceModel
                                         ? "face"
                                         : model_index == kSegmentationModel
                                               ? "segmentation"
                                               : "person"),
                                 static_cast<unsigned int>(
                                     message.frame.capture_sequence));
                }
                g_app.Report(task);
                g_app.next_schedule_index =
                    (selected_schedule_index + 1U) % kModelScheduleLength;
                continue;
            }
            if (model_index == kFaceModel) {
                face_available->ReleaseClaim();
            } else if (model_index == kSegmentationModel) {
                segmentation_available->ReleaseClaim();
            } else {
                person_available->ReleaseClaim();
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
