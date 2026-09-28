#include "task/task_context.hpp"

#include <cstring>

#include "driver/npu_driver/debug.h"
#include "common/log.hpp"
#include "image_resizer/image_resizer.hpp"

#include "inference_dispatcher/inference_dispatcher.hpp"
#include "models/face/model.hpp"
#include "models/person/model.hpp"
#include "models/segmentation/model.hpp"
#include "npu_scheduler/npu_scheduler.hpp"
#include "task/inference_task.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include <tm/tmonitor.h>
}

namespace uai::ai::task {

namespace {

common::Error PrepareDynamicModelInput(
    memory_allocator::InferenceFrame *frame, const models::ModelDescriptor &descriptor,
    TaskContext &context)
{
    if (frame == nullptr || !frame->from_pipe2) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.dynamic_input.invalid_frame"};
    }
    if (descriptor.kind == models::ModelKind::kPerson) {
        frame->input_prepared_by_cpu = false;
        return {common::ErrorCode::kOk, 0U, "ai.dynamic_input.direct"};
    }
    if (!frame->scratch ||
        frame->scratch.size < memory_allocator::kInferenceScratchBytes) {
        return {common::ErrorCode::kNoBuffer, 0U, "ai.dynamic_input.no_scratch"};
    }

    /* Pipe2 stays in the person-sized 480x480 layout. Its live image is the
     * 480x288 region at y=96; copy that region before overwriting the input
     * prefix with the selected smaller tensor. */
    common::Error status = context.cache.PrepareForCpuRead(frame->buffer);
    if (!status.Ok()) {
        return status;
    }
    constexpr std::size_t kSourceRowBytes =
        static_cast<std::size_t>(memory_allocator::kInferenceSourceWidth) * 3U;
    const auto *pipe2 = reinterpret_cast<const std::uint8_t *>(
        frame->buffer.address) + 96U * kSourceRowBytes;
    auto *scratch = reinterpret_cast<std::uint8_t *>(frame->scratch.address);
    for (std::uint32_t y = 0U;
         y < memory_allocator::kInferenceSourceHeight; ++y) {
        std::memcpy(scratch + static_cast<std::size_t>(y) * kSourceRowBytes,
                    pipe2 + static_cast<std::size_t>(y) * kSourceRowBytes,
                    kSourceRowBytes);
    }

    const std::uint32_t content_height =
        (descriptor.input_width * memory_allocator::kInferenceSourceHeight +
         memory_allocator::kInferenceSourceWidth - 1U) /
        memory_allocator::kInferenceSourceWidth;
    status = image_resizer::ResizeRgb888Letterbox(
        {scratch, memory_allocator::kInferenceSourceWidth,
         memory_allocator::kInferenceSourceHeight,
         static_cast<std::uint32_t>(kSourceRowBytes)},
        {reinterpret_cast<std::uint8_t *>(frame->buffer.address),
         descriptor.input_width, descriptor.input_height,
         descriptor.input_width * 3U},
        descriptor.input_width, content_height);
    if (!status.Ok()) {
        return status;
    }
    frame->input_prepared_by_cpu = true;
    return {common::ErrorCode::kOk, 0U, "ai.dynamic_input.letterbox"};
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
    npu_scheduler::NpuScheduler scheduler{};
    InferenceDispatcher dispatcher{};
    common::Error model_status{common::ErrorCode::kNotInitialized, 0U,
                               "ai.external_nor_unavailable"};
    if (context.external_nor_ready) {
        model_status = scheduler.RegisterModel(
            {models::ModelKind::kPerson, &person_model,
             &models::person::Runtime(person_model)});
        if (model_status.Ok()) {
            model_status = scheduler.RegisterModel(
                {models::ModelKind::kSegmentation, &segmentation_model,
                 &models::segmentation::Runtime(segmentation_model)});
        }
        if (model_status.Ok()) {
            model_status = scheduler.RegisterModel(
                {models::ModelKind::kFace, &face_model,
                 &models::face::Runtime(face_model)});
        }
        if (model_status.Ok()) {
            model_status = scheduler.Initialize();
        }
        if (model_status.Ok()) {
            model_status = dispatcher.Initialize(scheduler, context.memory,
                                                 context.cache);
        }
        if (!model_status.Ok() && scheduler.Initialized()) {
            (void)scheduler.Shutdown();
        }
    }
    if (model_status.Ok()) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: model loaded; inference execution enabled\n"));
    } else {
        LogStatus("ai", model_status);
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: model load failed; inference disabled\n"));
    }

    bool inference_enabled = model_status.Ok();
    bool first_model = true;
    memory_allocator::BoxSet integrated_boxes{};
    std::uint32_t fps_window_start = 0U;
    std::uint32_t fps_submitted = 0U;
    std::uint32_t fps_completed = 0U;
    std::uint32_t fps_inference_total_ms = 0U;
    std::uint32_t fps_inference_max_ms = 0U;
    if (context.diagnostics.inference_fps) {
        fps_window_start = context.Now();
    }
    for (;;) {
        InferenceMessage message{};
        const INT size = tk_rcv_mbf(context.frame_queue, &message, TMO_FEVR);
        if (size != static_cast<INT>(sizeof(message))) {
            continue;
        }
        if (inference_enabled && !first_model) {
            const common::Error switch_status = dispatcher.SelectNextModel();
            if (!switch_status.Ok()) {
                UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                                  "ai: per-frame model switch failed code=%d detail=%x\n"),
                              static_cast<int>(switch_status.code),
                              static_cast<unsigned int>(switch_status.detail));
                (void)context.memory.ReleaseInferenceBuffer(message.frame);
                continue;
            }
        } else if (inference_enabled) {
            first_model = false;
        }
        if (context.diagnostics.inference_fps) {
            ++fps_submitted;
        }

        common::Error status = context.memory.ClaimInferenceBuffer(message.frame);
        if (!status.Ok()) {
            LogStatus("memory", status);
            continue;
        }

        memory_allocator::BoxSet boxes{};
        if (inference_enabled) {
            const models::ModelDescriptor *descriptor =
                dispatcher.CurrentDescriptor();
            if (descriptor == nullptr) {
                (void)context.memory.ReleaseInferenceBuffer(message.frame);
                LogStatus("ai.descriptor",
                          {common::ErrorCode::kModel, 0U, "ai.current_descriptor"});
                continue;
            }
            status = PrepareDynamicModelInput(&message.frame, *descriptor,
                                              context);
            if (!status.Ok()) {
                (void)context.memory.ReleaseInferenceBuffer(message.frame);
                LogStatus("ai.input", status);
                continue;
            }
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
            status = dispatcher.TryInfer(message.frame, &boxes);
            const std::uint32_t inference_elapsed = measure_inference
                                                        ? context.Now() -
                                                              inference_start
                                                        : 0U;
            if (context.diagnostics.inference_fps) {
                fps_inference_total_ms += inference_elapsed;
                if (inference_elapsed > fps_inference_max_ms) {
                    fps_inference_max_ms = inference_elapsed;
                }
            }
            if (context.diagnostics.inference_trace &&
                (message.frame.capture_sequence % 10U) == 0U) {
                UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                                  "ai: inference elapsed_ms=%u sequence=%u\n"),
                              static_cast<unsigned int>(inference_elapsed),
                              static_cast<unsigned int>(
                                  message.frame.capture_sequence));
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
        const common::Error release_status =
            context.memory.ReleaseInferenceBuffer(message.frame);
        LogStatus("memory", release_status);
        if (status.Ok()) {
            if (context.diagnostics.inference_fps) {
                ++fps_completed;
            }
            switch (dispatcher.CurrentModel()) {
            case models::ModelKind::kPerson:
                if (boxes.person_valid) {
                    integrated_boxes.person = boxes.person;
                    integrated_boxes.person_valid = true;
                }
                break;
            case models::ModelKind::kFace:
                if (boxes.face_valid) {
                    integrated_boxes.face = boxes.face;
                    integrated_boxes.face_valid = true;
                }
                break;
            case models::ModelKind::kSegmentation:
                if (boxes.segmentation_valid) {
                    integrated_boxes.segmentation = boxes.segmentation;
                    integrated_boxes.segmentation_valid = true;
                }
                break;
            }
            integrated_boxes.model_sequence = boxes.model_sequence;
            integrated_boxes.capture_sequence = boxes.capture_sequence;
            context.SendLatestBoxes(integrated_boxes);
            if (context.diagnostics.inference_trace &&
                (boxes.model_sequence % 10U) == 0U) {
                UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                                  "ai: inference sequence=%u capture=%u count=%u\n"),
                              static_cast<unsigned int>(boxes.model_sequence),
                              static_cast<unsigned int>(boxes.capture_sequence),
                              static_cast<unsigned int>(
                                  boxes.person.count + boxes.face.count));
            }
        } else if (model_status.Ok() && inference_enabled) {
            LogStatus("ai", status);
            LogNpuStatus(dispatcher.LastNpuStatus());
            /* A failed/timed-out NPU execution must not make the camera task
             * wait for a buffer forever. Keep Pipe1 live and leave inference
             * disabled until the next firmware restart. */
            inference_enabled = false;
            UAI_LOG_WARN(reinterpret_cast<const UB *>(
                             "ai: inference disabled after NPU error; camera remains live\n"));
        }
        if (context.diagnostics.inference_fps) {
            camera_diag = context.camera.GetDiagnostics();
            const std::uint32_t fps_now = context.Now();
            const std::uint32_t fps_window_ms = fps_now - fps_window_start;
            if (fps_window_ms >= 1000U) {
                UB line[192] = {};
                (void)tm_sprintf(
                    line,
                    reinterpret_cast<const UB *>(
                        "ai: inference fps submitted=%u completed=%u "
                        "window_ms=%u infer_total_ms=%u infer_max_ms=%u "
                        "pipe2=%u drops=%u capture=%u\n"),
                    static_cast<unsigned int>(fps_submitted),
                    static_cast<unsigned int>(fps_completed),
                    static_cast<unsigned int>(fps_window_ms),
                    static_cast<unsigned int>(fps_inference_total_ms),
                    static_cast<unsigned int>(fps_inference_max_ms),
                    camera_diag.pipe2_frame_event_count,
                    camera_diag.pipe2_drop_count,
                    static_cast<unsigned int>(message.frame.capture_sequence));
                UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, line);
                fps_window_start = fps_now;
                fps_submitted = 0U;
                fps_completed = 0U;
                fps_inference_total_ms = 0U;
                fps_inference_max_ms = 0U;
            }
        }
    }
}

} // namespace uai::ai::task
