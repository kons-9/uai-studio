#include "task/task_context.hpp"

#include "driver/npu_driver/debug.h"
#include "common/log.hpp"

#include "models/face/model.hpp"
#include "models/person/model.hpp"
#include "models/segmentation/model.hpp"
#include "npu_runtime/npu_runtime.hpp"
#include "task/inference_task.hpp"
#include "task/inference_postprocess_task.hpp"
#include "task/input_preparation_task.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include <tm/tmonitor.h>
}

namespace uai::ai::task {

namespace {

common::Error RequestCpuInput(TaskContext &context,
                              npu_runtime::PreparationTarget target,
                              std::uint64_t request_id)
{
    if (!target || request_id == 0U) {
        return {common::ErrorCode::kModel, 0U, "ai.cpu_input.model"};
    }
    const InputPreparationRequest request{
        {pipeline::ExecutionContext::kNpuTask,
         pipeline::ExecutionContext::kCpuInputTask, request_id, 0U},
        target.model, target.kind};
    const ER error = tk_snd_mbf(context.input_preparation_request_queue,
                                 &request, sizeof(request), TMO_POL);
    return error == E_OK
        ? common::Error{common::ErrorCode::kOk, 0U, "ai.cpu_input.request"}
        : common::Error{common::ErrorCode::kInvalidState,
                        static_cast<std::uint32_t>(error),
                        "ai.cpu_input.request_queue"};
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

} // namespace

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
        context.input_preparation_enabled = true;
        context.StartInputPreparationTask(
            reinterpret_cast<FP>(InputPreparationTask::Entry));
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
    std::uint64_t next_request_id = 1U;
    npu_runtime::PreparationTarget requested_target{};
    if (inference_enabled) {
        requested_target = runtime.InputTarget(false);
        model_status = RequestCpuInput(context, requested_target,
                                       next_request_id);
        inference_enabled = model_status.Ok();
        context.input_preparation_enabled = inference_enabled;
    }
    memory_allocator::InferenceFrame reusable_frame{};
    bool reusable_held = false;
    for (;;) {
        DrainPostprocessDone(context, &reusable_frame, &reusable_held);
        ReleaseReusableFrame(context, &reusable_frame, &reusable_held);
        if (!inference_enabled) {
            /* On any NPU fault keep freeing raw and prepared frames; camera
             * DMA cannot progress if all five leases remain occupied. */
            InputPreparationResult orphan{};
            if (tk_rcv_mbf(context.input_preparation_result_queue, &orphan,
                           TMO_POL) == static_cast<INT>(sizeof(orphan)) &&
                orphan.frame) {
                LogStatus("memory", context.memory.ReleaseInferenceBuffer(
                                        orphan.frame));
            }
            InferenceMessage raw{};
            if (tk_rcv_mbf(context.frame_queue, &raw, TMO_POL) ==
                static_cast<INT>(sizeof(raw))) {
                LogStatus("memory", context.memory.ReleaseInferenceBuffer(
                                        raw.frame));
            }
            tk_dly_tsk(1U);
            continue;
        }

        const std::uint32_t frame_queue_wait_start = context.Now();
        InputPreparationResult prepared{};
        INT size = 0;
        do {
            DrainPostprocessDone(context, &reusable_frame, &reusable_held);
            ReleaseReusableFrame(context, &reusable_frame, &reusable_held);
            size = tk_rcv_mbf(context.input_preparation_result_queue,
                              &prepared, TMO_POL);
            if (size != static_cast<INT>(sizeof(prepared))) {
                tk_dly_tsk(1U);
            }
        } while (size != static_cast<INT>(sizeof(prepared)));
        const std::uint32_t frame_queue_wait =
            context.Now() - frame_queue_wait_start;
        ++context.inference_metrics.frame_queue_wait_calls;
        context.inference_metrics.frame_queue_wait_total_ms += frame_queue_wait;
        if (frame_queue_wait > context.inference_metrics.frame_queue_wait_max_ms) {
            context.inference_metrics.frame_queue_wait_max_ms = frame_queue_wait;
        }
        const bool valid_prepared = prepared.status.Ok() &&
            prepared.frame &&
            pipeline::IsPreparedFor(prepared.handoff, next_request_id,
                                    prepared.frame.lease_token) &&
            prepared.frame.input_prepared &&
            prepared.frame.prepared_model_kind_id ==
                static_cast<std::uint8_t>(requested_target.kind);
        if (!valid_prepared) {
            if (prepared.frame) {
                LogStatus("memory", context.memory.ReleaseInferenceBuffer(
                                        prepared.frame));
            }
            LogStatus("ai.cpu_input", prepared.status.Ok()
                ? common::Error{common::ErrorCode::kOwnership, 0U,
                                "ai.cpu_input.invalid_handoff"}
                : prepared.status);
            inference_enabled = false;
            context.input_preparation_enabled = false;
            continue;
        }
        message.frame = prepared.frame;
        if (context.diagnostics.inference_fps) {
            ++fps_submitted;
        }

        common::Error status{};
        npu_runtime::InferenceCompletion completion{};
        bool completion_queued = false;
        const std::uint64_t current_request_id = next_request_id;
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
            status = runtime.Begin(message.frame);
            if (status.Ok()) {
                /* NPU is active: request CPU preparation of the next model
                 * on another task before blocking for the current IRQs. */
                ++next_request_id;
                if (next_request_id == 0U) {
                    ++next_request_id;
                }
                requested_target = runtime.InputTarget(true);
                const common::Error request_status = RequestCpuInput(
                    context, requested_target, next_request_id);
                status = runtime.Wait(&completion);
                if (status.Ok() && !request_status.Ok()) {
                    status = request_status;
                }
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
            completion.handoff = {
                pipeline::ExecutionContext::kNpuTask,
                pipeline::ExecutionContext::kCpuPostprocessTask,
                current_request_id, completion.frame.lease_token};
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
            }
        }
        if (!completion_queued) {
            const common::Error release_status =
                context.memory.ReleaseInferenceBuffer(message.frame);
            LogStatus("memory", release_status);
        }
        if (!status.Ok() && model_status.Ok() && inference_enabled &&
            !context.IsBestEffort(status.code)) {
            LogStatus("ai", status);
            LogNpuStatus(runtime.LastNpuStatus());
            /* A failed/timed-out NPU execution must not make the camera task
             * wait for a buffer forever. Keep Pipe1 live and leave inference
             * disabled until the next firmware restart. */
            inference_enabled = false;
            context.input_preparation_enabled = false;
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
