#include "task/task_context.hpp"

#include "driver/npu_driver/debug.h"
#include "common/log.hpp"

#include "models/face/model.hpp"
#include "models/person/model.hpp"
#include "models/segmentation/model.hpp"
#include "npu_runtime/npu_runtime.hpp"
#include "task/inference_task.hpp"
#include "task/inference_postprocess_task.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include <tm/tmonitor.h>
}

namespace uai::ai::task {

struct PrefetchContext {
    TaskContext *task = nullptr;
    InferenceMessage message{};
    bool claimed = false;
};

memory_allocator::InferenceFrame *ProvidePrefetch(void *context)
{
    auto *prefetch = static_cast<PrefetchContext *>(context);
    if (prefetch == nullptr || prefetch->task == nullptr ||
        prefetch->claimed) {
        return nullptr;
    }

    ++prefetch->task->inference_metrics.prefetch_attempts;

    const INT size = tk_rcv_mbf(prefetch->task->frame_queue,
                                &prefetch->message, TMO_POL);
    if (size != static_cast<INT>(sizeof(prefetch->message))) {
        ++prefetch->task->inference_metrics.prefetch_queue_empty;
        return nullptr;
    }
    const common::Error status =
        prefetch->task->memory.ClaimInferenceBuffer(prefetch->message.frame);
    if (!status.Ok()) {
        if (status.code == common::ErrorCode::kNoBuffer) {
            ++prefetch->task->inference_metrics.prefetch_buffer_busy;
        } else {
            ++prefetch->task->inference_metrics.prefetch_claim_errors;
        }
        LogStatus("memory", status);
        return nullptr;
    }
    prefetch->claimed = true;
    ++prefetch->task->inference_metrics.prefetch_successes;
    return &prefetch->message.frame;
}

void ReleaseReusableFrame(TaskContext &context,
                          memory_allocator::InferenceFrame *frame,
                          bool *held)
{
    if (frame == nullptr || held == nullptr || !*held) {
        return;
    }
    const common::Error status = context.memory.ReleaseInferenceBuffer(*frame);
    LogStatus("memory", status);
    *held = false;
    *frame = {};
}

void DrainPostprocessDone(TaskContext &context,
                          memory_allocator::InferenceFrame *reusable,
                          bool *held)
{
    if (reusable == nullptr || held == nullptr) {
        return;
    }
    for (;;) {
        InferencePostprocessDoneMessage done{};
        const INT size = tk_rcv_mbf(context.inference_postprocess_done_queue,
                                    &done, TMO_POL);
        if (size != static_cast<INT>(sizeof(done))) {
            return;
        }
        /* Keep only the newest completed frame. An older completed frame is
         * no longer useful once a newer one is available for reuse. */
        ReleaseReusableFrame(context, reusable, held);
        *reusable = done.frame;
        *held = true;
    }
}

void InferenceTask::Entry()
{
    InferenceTask{}.Run();
}

void InferenceTask::Run()
{
    TaskContext &context = GetTaskContext();
    uai::ai::camera::Diagnostics camera_diag{};
    UINT pattern = 0U;
    const ER error = tk_wai_flg(context.external_memory_ready,
                                kExternalMemoryReady,
                                TWF_ANDW, &pattern, TMO_FEVR);
    if (error != E_OK) {
        UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                          "error: component=ai operation=wait_memory code=%x detail=0\n"),
                      static_cast<unsigned int>(error));
        return;
    }

    models::person::Model person_model{};
    models::segmentation::Model segmentation_model{};
    models::face::Model face_model{};
    npu_runtime::NpuRuntime runtime{};
    common::Error model_status{common::ErrorCode::kNotInitialized, 0U,
                               "ai.external_nor_unavailable"};
    if (context.external_nor_ready) {
        model_status = runtime.RegisterModel(
            {models::ModelKind::kPerson, &person_model,
             &models::person::Runtime(person_model)});
        if (model_status.Ok()) {
            model_status = runtime.RegisterModel(
                {models::ModelKind::kSegmentation, &segmentation_model,
                 &models::segmentation::Runtime(segmentation_model)});
        }
        if (model_status.Ok()) {
            model_status = runtime.RegisterModel(
                {models::ModelKind::kFace, &face_model,
                 &models::face::Runtime(face_model)});
        }
        if (model_status.Ok()) {
            model_status = runtime.Initialize(context.cache);
        }
        if (!model_status.Ok() && runtime.Initialized()) {
            (void)runtime.Shutdown();
        }
    }
    if (model_status.Ok()) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: model loaded; inference execution enabled\n"));
        context.StartInferencePostprocessTask(
            reinterpret_cast<FP>(InferencePostprocessTask::Entry));
    } else {
        LogStatus("ai", model_status);
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: model load failed; inference disabled\n"));
    }

    bool inference_enabled = model_status.Ok();
    std::uint32_t fps_window_start = 0U;
    std::uint32_t fps_submitted = 0U;
    std::uint32_t fps_completed = 0U;
    std::uint32_t fps_inference_total_ms = 0U;
    std::uint32_t fps_inference_max_ms = 0U;
    std::uint32_t fps_capture_lag_start_total = 0U;
    std::uint32_t fps_capture_lag_start_max = 0U;
    std::uint32_t fps_capture_lag_end_total = 0U;
    std::uint32_t fps_capture_lag_end_max = 0U;
    std::uint32_t last_frame_queue_wait_total_ms = 0U;
    std::uint32_t last_reused_frame_count = 0U;
    std::uint32_t last_frame_queue_drop_count = 0U;
    std::uint32_t last_prefetch_attempts = 0U;
    std::uint32_t last_prefetch_successes = 0U;
    std::uint32_t last_prefetch_queue_empty = 0U;
    std::uint32_t last_prefetch_buffer_busy = 0U;
    std::uint32_t last_prefetch_claim_errors = 0U;
    std::uint32_t last_postprocess_queue_wait_total_ms = 0U;
    std::uint32_t last_postprocess_count[kInferenceModelCount]{};
    std::uint32_t last_postprocess_total_ms[kInferenceModelCount]{};
    if (context.diagnostics.inference_fps) {
        fps_window_start = context.Now();
    }
    InferenceMessage message{};
    bool have_message = false;
    memory_allocator::InferenceFrame reusable_frame{};
    bool reusable_held = false;
    for (;;) {
        bool message_already_claimed = false;
        if (!have_message) {
            const std::uint32_t frame_queue_wait_start = context.Now();
            INT size = 0;
            for (;;) {
                DrainPostprocessDone(context, &reusable_frame, &reusable_held);
                size = tk_rcv_mbf(context.frame_queue, &message, TMO_POL);
                if (size == static_cast<INT>(sizeof(message))) {
                    ReleaseReusableFrame(context, &reusable_frame,
                                         &reusable_held);
                    break;
                }
                /* A completed frame is no longer reused as a new input.  It
                 * is already older than the live Pipe2 stream, so reusing it
                 * keeps the NPU busy but makes the boxes belong to a past
                 * image.  Release it and wait for the next fresh frame;
                 * SendInferenceFrame() keeps that queue latest-wins. */
                if (reusable_held) {
                    ReleaseReusableFrame(context, &reusable_frame,
                                         &reusable_held);
                }
                tk_dly_tsk(1U);
            }
            const std::uint32_t frame_queue_wait =
                context.Now() - frame_queue_wait_start;
            ++context.inference_metrics.frame_queue_wait_calls;
            context.inference_metrics.frame_queue_wait_total_ms +=
                frame_queue_wait;
            if (frame_queue_wait > context.inference_metrics.frame_queue_wait_max_ms) {
                context.inference_metrics.frame_queue_wait_max_ms =
                    frame_queue_wait;
            }
            if (size != static_cast<INT>(sizeof(message)) &&
                !message_already_claimed) {
                continue;
            }
        } else {
            have_message = false;
            message_already_claimed = true;
            DrainPostprocessDone(context, &reusable_frame, &reusable_held);
            ReleaseReusableFrame(context, &reusable_frame, &reusable_held);
        }
        if (context.diagnostics.inference_fps) {
            ++fps_submitted;
        }

        common::Error status{};
        if (!message_already_claimed) {
            status = context.memory.ClaimInferenceBuffer(message.frame);
        }
        if (!status.Ok()) {
            LogStatus("memory", status);
            continue;
        }

        PrefetchContext prefetch{&context};
        npu_runtime::InferenceCompletion completion{};
        bool completion_queued = false;
        if (inference_enabled) {
            if (context.diagnostics.inference_trace) {
                UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                                  "ai: inference begin sequence=%u\n"),
                              static_cast<unsigned int>(message.frame.capture_sequence));
            }
            /* Inspect the exact Pipe2 buffer immediately before handing it to
             * the NPU. This confirms that the inference path consumes live
             * camera data, rather than only proving that Pipe2 generated a
             * frame event. */
            if (context.diagnostics.inference_input) {
                LogInferenceInput(message.frame);
            }
            if (context.diagnostics.inference_trace) {
                UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                                  "ai: inference run begin sequence=%u input=%x irq=%u last=%x\n"),
                              static_cast<unsigned int>(message.frame.capture_sequence),
                              static_cast<unsigned int>(message.frame.buffer.address),
                              g_aton_irq_count, g_aton_last_irqs);
            }
            const bool measure_inference = context.diagnostics.inference_fps ||
                                           context.diagnostics.inference_trace;
            const std::uint32_t inference_start =
                measure_inference ? context.Now() : 0U;
            const auto camera_before = context.camera.GetDiagnostics();
            const std::uint32_t capture_lag_start =
                camera_before.pipe2_latest_capture_sequence >=
                        message.frame.capture_sequence
                    ? camera_before.pipe2_latest_capture_sequence -
                          message.frame.capture_sequence
                    : 0U;
            status = runtime.Begin(message.frame, ProvidePrefetch, &prefetch);
            if (status.Ok()) {
                status = runtime.Wait(&completion);
            }
            const auto camera_after = context.camera.GetDiagnostics();
            const std::uint32_t capture_lag_end =
                camera_after.pipe2_latest_capture_sequence >=
                        message.frame.capture_sequence
                    ? camera_after.pipe2_latest_capture_sequence -
                          message.frame.capture_sequence
                    : 0U;
            const std::uint32_t inference_elapsed = measure_inference
                                                        ? context.Now() -
                                                              inference_start
                                                        : 0U;
            if (context.diagnostics.inference_fps) {
                fps_inference_total_ms += inference_elapsed;
                if (inference_elapsed > fps_inference_max_ms) {
                    fps_inference_max_ms = inference_elapsed;
                }
                fps_capture_lag_start_total += capture_lag_start;
                if (capture_lag_start > fps_capture_lag_start_max) {
                    fps_capture_lag_start_max = capture_lag_start;
                }
                fps_capture_lag_end_total += capture_lag_end;
                if (capture_lag_end > fps_capture_lag_end_max) {
                    fps_capture_lag_end_max = capture_lag_end;
                }
            }
            if (context.diagnostics.inference_trace &&
                (message.frame.capture_sequence % 10U) == 0U) {
                const auto &npu_execution = completion.execution;
                UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                        "ai: inference elapsed_ms=%u npu_elapsed_ms=%u "
                                  "sequence=%u latest=%u lag_start=%u "
                                  "lag_end=%u\n"),
                              static_cast<unsigned int>(inference_elapsed),
                              static_cast<unsigned int>(
                                  npu_execution.elapsed_ms),
                              static_cast<unsigned int>(
                                  message.frame.capture_sequence),
                              static_cast<unsigned int>(
                                  camera_after.pipe2_latest_capture_sequence),
                              static_cast<unsigned int>(capture_lag_start),
                              static_cast<unsigned int>(capture_lag_end));
            }
            if (context.diagnostics.inference_trace) {
                UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                                  "ai: inference run end sequence=%u code=%u detail=%x irq=%u last=%x\n"),
                              static_cast<unsigned int>(message.frame.capture_sequence),
                              static_cast<unsigned int>(status.code),
                              static_cast<unsigned int>(status.detail),
                              g_aton_irq_count, g_aton_last_irqs);
            }
        } else {
            status = {common::ErrorCode::kNotInitialized, 0U, "ai.infer_disabled"};
        }
        if (status.Ok()) {
            const ER queue_status = tk_snd_mbf(
                context.inference_completion_queue, &completion,
                sizeof(completion), TMO_POL);
            if (queue_status != E_OK) {
                status = {common::ErrorCode::kQueueFull,
                          static_cast<std::uint32_t>(queue_status),
                          "ai.inference_completion_queue"};
            } else {
                completion_queued = true;
                if (context.diagnostics.inference_fps) {
                    ++fps_completed;
                }
                if (prefetch.claimed) {
                    message = prefetch.message;
                    have_message = true;
                }
            }
        }
        if (!completion_queued) {
            const common::Error release_status =
                context.memory.ReleaseInferenceBuffer(message.frame);
            LogStatus("memory", release_status);
        }
        if (prefetch.claimed && !completion_queued) {
            const common::Error prefetch_release =
                context.memory.ReleaseInferenceBuffer(prefetch.message.frame);
            LogStatus("memory", prefetch_release);
        }
        if (!status.Ok() && model_status.Ok() && inference_enabled &&
            !context.IsBestEffort(status.code)) {
            LogStatus("ai", status);
            LogNpuStatus(runtime.LastNpuStatus());
            /* A failed/timed-out NPU execution must not make the camera task
             * wait for a buffer forever. Keep Pipe1 live and leave inference
             * disabled until the next firmware restart. */
            inference_enabled = false;
            UAI_LOG_WARN(reinterpret_cast<const UB *>(
                             "ai: inference disabled after NPU error; camera remains live\n"));
            ReleaseReusableFrame(context, &reusable_frame, &reusable_held);
        }
        if (context.diagnostics.inference_fps) {
            camera_diag = context.camera.GetDiagnostics();
            const std::uint32_t fps_now = context.Now();
            const std::uint32_t fps_window_ms = fps_now - fps_window_start;
            if (fps_window_ms >= 1000U) {
                UB line[256] = {};
                (void)tm_sprintf(
                    line,
                    reinterpret_cast<const UB *>(
                        "ai: inference fps submitted=%u completed=%u "
                        "window_ms=%u infer_total_ms=%u infer_max_ms=%u "
                        "pipe2=%u drops=%u latest=%u capture=%u "
                        "lag_start_sum=%u/max=%u lag_end_sum=%u/max=%u\n"),
                    static_cast<unsigned int>(fps_submitted),
                    static_cast<unsigned int>(fps_completed),
                    static_cast<unsigned int>(fps_window_ms),
                    static_cast<unsigned int>(fps_inference_total_ms),
                    static_cast<unsigned int>(fps_inference_max_ms),
                    camera_diag.pipe2_frame_event_count,
                    camera_diag.pipe2_drop_count,
                    static_cast<unsigned int>(
                        camera_diag.pipe2_latest_capture_sequence),
                    static_cast<unsigned int>(message.frame.capture_sequence),
                    static_cast<unsigned int>(fps_capture_lag_start_total),
                    static_cast<unsigned int>(fps_capture_lag_start_max),
                    static_cast<unsigned int>(fps_capture_lag_end_total),
                    static_cast<unsigned int>(fps_capture_lag_end_max));
                UAI_LOG_TEXT(uai::ai::common::LogLevel::kInfo, line);

                const auto &metrics = context.inference_metrics;
                UB pipeline_line[512] = {};
                const std::uint32_t frame_queue_wait_total_ms =
                    metrics.frame_queue_wait_total_ms -
                    last_frame_queue_wait_total_ms;
                const std::uint32_t frame_queue_wait_max_ms =
                    metrics.frame_queue_wait_max_ms;
                const std::uint32_t frame_queue_drop_count =
                    metrics.frame_queue_drop_count - last_frame_queue_drop_count;
                const std::uint32_t reused_frame_count =
                    metrics.reused_frame_count - last_reused_frame_count;
                const std::uint32_t prefetch_attempts =
                    metrics.prefetch_attempts - last_prefetch_attempts;
                const std::uint32_t prefetch_successes =
                    metrics.prefetch_successes - last_prefetch_successes;
                const std::uint32_t prefetch_queue_empty =
                    metrics.prefetch_queue_empty - last_prefetch_queue_empty;
                const std::uint32_t prefetch_buffer_busy =
                    metrics.prefetch_buffer_busy - last_prefetch_buffer_busy;
                const std::uint32_t prefetch_claim_errors =
                    metrics.prefetch_claim_errors - last_prefetch_claim_errors;
                const std::uint32_t postprocess_queue_wait_total_ms =
                    metrics.postprocess_queue_wait_total_ms -
                    last_postprocess_queue_wait_total_ms;
                const std::uint32_t postprocess_queue_wait_max_ms =
                    metrics.postprocess_queue_wait_max_ms;
                const std::uint32_t postprocess_person_count =
                    metrics.postprocess_count[0] - last_postprocess_count[0];
                const std::uint32_t postprocess_segmentation_count =
                    metrics.postprocess_count[1] - last_postprocess_count[1];
                const std::uint32_t postprocess_face_count =
                    metrics.postprocess_count[2] - last_postprocess_count[2];
                const std::uint32_t postprocess_person_total_ms =
                    metrics.postprocess_total_ms[0] - last_postprocess_total_ms[0];
                const std::uint32_t postprocess_segmentation_total_ms =
                    metrics.postprocess_total_ms[1] - last_postprocess_total_ms[1];
                const std::uint32_t postprocess_face_total_ms =
                    metrics.postprocess_total_ms[2] - last_postprocess_total_ms[2];
                const std::uint32_t postprocess_person_max_ms =
                    metrics.postprocess_max_ms[0];
                const std::uint32_t postprocess_segmentation_max_ms =
                    metrics.postprocess_max_ms[1];
                const std::uint32_t postprocess_face_max_ms =
                    metrics.postprocess_max_ms[2];
                (void)tm_sprintf(
                    pipeline_line,
                    reinterpret_cast<const UB *>(
                        "ai: pipeline frame_wait=%u/%u qdrop=%u "
                        "reuse=%u "
                        "prefetch=%u/%u empty=%u busy=%u err=%u "
                        "postwait=%u/%u "
                        "post_ms person=%u/%u/%u seg=%u/%u/%u face=%u/%u/%u\n"),
                    static_cast<unsigned int>(frame_queue_wait_total_ms),
                    static_cast<unsigned int>(frame_queue_wait_max_ms),
                    static_cast<unsigned int>(frame_queue_drop_count),
                    static_cast<unsigned int>(reused_frame_count),
                    static_cast<unsigned int>(prefetch_attempts),
                    static_cast<unsigned int>(prefetch_successes),
                    static_cast<unsigned int>(prefetch_queue_empty),
                    static_cast<unsigned int>(prefetch_buffer_busy),
                    static_cast<unsigned int>(prefetch_claim_errors),
                    static_cast<unsigned int>(postprocess_queue_wait_total_ms),
                    static_cast<unsigned int>(postprocess_queue_wait_max_ms),
                    static_cast<unsigned int>(postprocess_person_total_ms),
                    static_cast<unsigned int>(postprocess_person_count),
                    static_cast<unsigned int>(postprocess_person_max_ms),
                    static_cast<unsigned int>(postprocess_segmentation_total_ms),
                    static_cast<unsigned int>(postprocess_segmentation_count),
                    static_cast<unsigned int>(postprocess_segmentation_max_ms),
                    static_cast<unsigned int>(postprocess_face_total_ms),
                    static_cast<unsigned int>(postprocess_face_count),
                    static_cast<unsigned int>(postprocess_face_max_ms));
                UAI_LOG_TEXT(uai::ai::common::LogLevel::kInfo, pipeline_line);

                last_frame_queue_wait_total_ms =
                    metrics.frame_queue_wait_total_ms;
                last_reused_frame_count = metrics.reused_frame_count;
                last_frame_queue_drop_count = metrics.frame_queue_drop_count;
                last_prefetch_attempts = metrics.prefetch_attempts;
                last_prefetch_successes = metrics.prefetch_successes;
                last_prefetch_queue_empty = metrics.prefetch_queue_empty;
                last_prefetch_buffer_busy = metrics.prefetch_buffer_busy;
                last_prefetch_claim_errors = metrics.prefetch_claim_errors;
                last_postprocess_queue_wait_total_ms =
                    metrics.postprocess_queue_wait_total_ms;
                for (std::size_t i = 0U; i < kInferenceModelCount; ++i) {
                    last_postprocess_count[i] = metrics.postprocess_count[i];
                    last_postprocess_total_ms[i] =
                        metrics.postprocess_total_ms[i];
                }
                fps_window_start = fps_now;
                fps_submitted = 0U;
                fps_completed = 0U;
                fps_inference_total_ms = 0U;
                fps_inference_max_ms = 0U;
                fps_capture_lag_start_total = 0U;
                fps_capture_lag_start_max = 0U;
                fps_capture_lag_end_total = 0U;
                fps_capture_lag_end_max = 0U;
            }
        }
    }
}

} // namespace uai::ai::task
