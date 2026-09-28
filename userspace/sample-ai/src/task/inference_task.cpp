#include "task/task_context.hpp"

#include <cstring>

#include "image_resizer/image_resizer.hpp"

#include "model_manager/model_manager.hpp"

#if AI_INFERENCE_DIAGNOSTICS
#define AI_INFERENCE_TRACE(...) tm_printf(__VA_ARGS__)
#else
#define AI_INFERENCE_TRACE(...) ((void)0)
#endif

namespace uai::ai::task {

#if defined(AI_DYNAMIC_MODEL_SWITCHING)
namespace {

Error PrepareDynamicModelInput(
    InferenceFrame *frame, model_manager::ModelKind model_kind)
{
    if (frame == nullptr || !frame->from_pipe2) {
        return {ErrorCode::kInvalidArgument, 0U,
                "ai.dynamic_input.invalid_frame"};
    }
    if (model_kind == model_manager::ModelKind::kPerson) {
        frame->input_prepared_by_cpu = false;
        return {ErrorCode::kOk, 0U, "ai.dynamic_input.direct"};
    }
    if (!frame->scratch ||
        frame->scratch.size < memory_allocator::kInferenceScratchBytes) {
        return {ErrorCode::kNoBuffer, 0U, "ai.dynamic_input.no_scratch"};
    }

    /* Pipe2 stays in the person-sized 480x480 layout. Its live image is the
     * 480x288 region at y=96; copy that region before overwriting the input
     * prefix with the selected smaller tensor. */
    Error status = g_cache.PrepareForCpuRead(frame->buffer);
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

    const auto &descriptor = model_manager::Describe(model_kind);
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
    return {ErrorCode::kOk, 0U, "ai.dynamic_input.letterbox"};
}

} // namespace
#endif

void inference_task(void)
{
    UINT pattern = 0U;
    const ER error = tk_wai_flg(g_external_memory_ready, kExternalMemoryReady,
                                TWF_ANDW, &pattern, TMO_FEVR);
    if (error != E_OK) {
        tm_printf(reinterpret_cast<const UB *>(
                      "error: component=ai operation=wait_memory code=%x detail=0\n"),
                  static_cast<unsigned int>(error));
        return;
    }

    uai::ai::ModelManager model;
    const Error model_status =
        g_external_nor_ready
            ? model.Initialize(g_memory, g_cache)
            : Error{ErrorCode::kNotInitialized, 0U,
                    "ai.external_nor_unavailable"};
    if (model_status.Ok()) {
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: model loaded; inference execution enabled\n")));
    } else {
        LogStatus("ai", model_status);
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: model load failed; inference disabled\n")));
    }

    bool inference_enabled = model_status.Ok();
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    constexpr model_manager::ModelKind kModelSequence[] = {
        model_manager::ModelKind::kPerson,
        model_manager::ModelKind::kSegmentation,
        model_manager::ModelKind::kFace};
    std::size_t next_model_index = 0U;
    BoxSet integrated_boxes{};
#endif
#if AI_INFERENCE_FPS_DIAGNOSTICS
    std::uint32_t fps_window_start = Now();
    std::uint32_t fps_submitted = 0U;
    std::uint32_t fps_completed = 0U;
    std::uint32_t fps_inference_total_ms = 0U;
    std::uint32_t fps_inference_max_ms = 0U;
#endif
    for (;;) {
        InferenceMessage message{};
        const INT size = tk_rcv_mbf(g_frame_queue, &message, TMO_FEVR);
        if (size != static_cast<INT>(sizeof(message))) {
            continue;
        }
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
        const model_manager::ModelKind selected_model =
            kModelSequence[next_model_index];
        next_model_index =
            (next_model_index + 1U) %
            (sizeof(kModelSequence) / sizeof(kModelSequence[0]));
        if (inference_enabled && model.CurrentModel() != selected_model) {
            const Error switch_status = model.SwitchModel(selected_model);
            if (!switch_status.Ok()) {
                tm_printf(reinterpret_cast<const UB *>(
                              "ai: per-frame model switch failed model=%s code=%d detail=%x\n"),
                          reinterpret_cast<const UB *>(
                              model_manager::Describe(selected_model).name),
                          static_cast<int>(switch_status.code),
                          static_cast<unsigned int>(switch_status.detail));
                (void)g_memory.ReleaseInferenceBuffer(message.frame);
                continue;
            }
        }
#endif
#if AI_INFERENCE_FPS_DIAGNOSTICS
        ++fps_submitted;
#endif

        Error status = g_memory.ClaimInferenceBuffer(message.frame);
        if (!status.Ok()) {
            LogStatus("memory", status);
            continue;
        }

        BoxSet boxes{};
        if (inference_enabled) {
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
            const model_manager::ModelKind selected_model = model.CurrentModel();
            status = PrepareDynamicModelInput(&message.frame, selected_model);
            if (!status.Ok()) {
                (void)g_memory.ReleaseInferenceBuffer(message.frame);
                LogStatus("ai.input", status);
                continue;
            }
#endif
            AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                          "ai: inference begin sequence=%u\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence));
#if AI_INFERENCE_DIAGNOSTICS
            /* Inspect the exact Pipe2 buffer immediately before handing it to
             * the NPU. This confirms that the inference path consumes live
             * camera data, rather than only proving that Pipe2 generated a
             * frame event. */
            LogInferenceInput(message.frame);
#endif
            AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                          "ai: inference run begin sequence=%u input=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence),
                      static_cast<unsigned int>(message.frame.buffer.address),
                      g_aton_irq_count, g_aton_last_irqs);
            const std::uint32_t inference_start = Now();
            status = model.TryInfer(message.frame, &boxes);
            const std::uint32_t inference_elapsed = Now() - inference_start;
#if AI_INFERENCE_FPS_DIAGNOSTICS
            fps_inference_total_ms += inference_elapsed;
            if (inference_elapsed > fps_inference_max_ms) {
                fps_inference_max_ms = inference_elapsed;
            }
#endif
#if AI_INFERENCE_DIAGNOSTICS
            if ((message.frame.capture_sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "ai: inference elapsed_ms=%u sequence=%u\n"),
                          static_cast<unsigned int>(inference_elapsed),
                          static_cast<unsigned int>(
                              message.frame.capture_sequence));
            }
#else
            (void)inference_elapsed;
#endif
            AI_INFERENCE_TRACE(reinterpret_cast<const UB *>(
                          "ai: inference run end sequence=%u code=%u detail=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence),
                      static_cast<unsigned int>(status.code),
                      static_cast<unsigned int>(status.detail),
                      g_aton_irq_count, g_aton_last_irqs);
        } else {
            status = {ErrorCode::kNotInitialized, 0U, "ai.infer_disabled"};
        }
        const Error release_status =
            g_memory.ReleaseInferenceBuffer(message.frame);
        LogStatus("memory", release_status);
        if (status.Ok()) {
#if AI_INFERENCE_FPS_DIAGNOSTICS
            ++fps_completed;
#endif
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
            switch (model.CurrentModel()) {
            case model_manager::ModelKind::kPerson:
                if (boxes.person_valid) {
                    integrated_boxes.person = boxes.person;
                    integrated_boxes.person_valid = true;
                }
                break;
            case model_manager::ModelKind::kFace:
                if (boxes.face_valid) {
                    integrated_boxes.face = boxes.face;
                    integrated_boxes.face_valid = true;
                }
                break;
            case model_manager::ModelKind::kSegmentation:
                if (boxes.segmentation_valid) {
                    integrated_boxes.segmentation = boxes.segmentation;
                    integrated_boxes.segmentation_valid = true;
                }
                break;
            }
            integrated_boxes.model_sequence = boxes.model_sequence;
            integrated_boxes.capture_sequence = boxes.capture_sequence;
            SendLatestBoxes(integrated_boxes);
#else
            SendLatestBoxes(boxes);
#endif
#if AI_INFERENCE_DIAGNOSTICS
            if ((boxes.model_sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "ai: inference sequence=%u capture=%u count=%u\n"),
                          static_cast<unsigned int>(boxes.model_sequence),
                          static_cast<unsigned int>(boxes.capture_sequence),
                          static_cast<unsigned int>(
                              boxes.person.count + boxes.face.count));
            }
#endif
        } else if (model_status.Ok() && inference_enabled) {
            LogStatus("ai", status);
            LogNpuStatus(model.LastNpuStatus());
            /* A failed/timed-out NPU execution must not make the camera task
             * wait for a buffer forever. Keep Pipe1 live and leave inference
             * disabled until the next firmware restart. */
            inference_enabled = false;
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "ai: inference disabled after NPU error; camera remains live\n")));
        }
#if AI_INFERENCE_FPS_DIAGNOSTICS
        const std::uint32_t fps_now = Now();
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
                g_camera_pipe2_frame_event_count,
                g_camera_pipe2_drop_count,
                static_cast<unsigned int>(message.frame.capture_sequence));
            tm_putstring(line);
            fps_window_start = fps_now;
            fps_submitted = 0U;
            fps_completed = 0U;
            fps_inference_total_ms = 0U;
            fps_inference_max_ms = 0U;
        }
#endif
#if defined(AI_MODEL_SEGMENTATION) && defined(AI_SEGMENTATION_DIAG)
        /* The diagnostic build is deliberately limited to one NPU attempt. */
        (void)tk_ext_tsk();
#endif
    }
}

} // namespace uai::ai::task
