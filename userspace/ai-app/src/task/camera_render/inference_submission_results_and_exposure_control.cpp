#include "task/camera_render/camera_render_context_and_operations.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "memory_manager/memory_manager.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/pipeline/image_diagnostics.hpp"
#include "middleware/pipeline/image_format.hpp"
#include "middleware/task/task.hpp"
#include "task/pipeline_task.hpp"

namespace uai::ai::task::camera_render {

namespace {

void LogPipe2InferenceInputStatistics(
    const CameraRenderContext &context,
    const pipeline::InferenceFrame &frame
)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    if (!frame || !frame.from_pipe2 || frame.buffer.size < memory_manager::kInferenceFrameBytes) {
        UAI_LOG_TEXT(common::LogLevel::kDebug, "ai: input inspect invalid frame\n");
        return;
    }

    const buffer::Buffer input_buffer{
        frame.buffer.address, memory_manager::kInferenceFrameBytes, frame.buffer.index, buffer::Region::kInference
    };
    const common::Error cache_status = context.cache.PrepareForCpuRead(input_buffer);
    if (!cache_status.Ok()) {
        cache_status.LogStatus("ai-input");
        return;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(frame.buffer.address);
    const std::size_t byte_count = memory_manager::kInferenceFrameBytes;
    constexpr std::size_t kPixelCount = pipeline::kInferenceFormat.width * pipeline::kInferenceFormat.height;
    const auto statistics = pipeline::InspectRgb888(bytes, byte_count, kPixelCount, 64U);

    const std::size_t center = ((pipeline::kInferenceFormat.height / 2U) * pipeline::kInferenceFormat.width
                                + pipeline::kInferenceFormat.width / 2U)
        * 3U;
    UAI_LOG_DEBUG(
        "ai: input inspect seq=%u address=%x bytes=%u crc=%x "
        "min=%u max=%u mean_luma=%u p00=%02x/%02x/%02x "
        "pcenter=%02x/%02x/%02x plast=%02x/%02x/%02x\n",
        static_cast<unsigned int>(frame.capture_sequence),
        static_cast<unsigned int>(frame.buffer.address),
        static_cast<unsigned int>(byte_count),
        static_cast<unsigned int>(statistics.crc),
        static_cast<unsigned int>(statistics.minimum),
        static_cast<unsigned int>(statistics.maximum),
        static_cast<unsigned int>(statistics.mean_luminance),
        bytes[0U],
        bytes[1U],
        bytes[2U],
        bytes[center],
        bytes[center + 1U],
        bytes[center + 2U],
        bytes[byte_count - 3U],
        bytes[byte_count - 2U],
        bytes[byte_count - 1U]
    );
}

}

void SubmitPipe2FramesForInferenceOrDisplay(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle,
    CameraRenderWake &wake
)
{
    const auto &features = cycle.features;
    const bool inference_enabled = kCopyInferenceFrames
        && (kInferenceMode == InferenceMode::kCopyOnly || context.external_nor_ready) && !features.input_display;
    const auto release_inference = [&](const pipeline::InferenceFrame &frame) {
        const auto released = context.memory.ReleaseInferenceBuffer(frame);
        if (!released.Ok()) {
            released.LogStatus("memory");
            common::Task::Halt("ai: inference lease release failed\n");
        }
        return released;
    };
    auto &pipe_stats = state.schedule.Stats(RenderOperation::kPipe2);
    auto &pipe2_frame = cycle.pipe2;
    common::Error status{};
    for (std::size_t drained = 0U; drained < memory_manager::kInferenceBufferCount; ++drained) {
        pipe2_frame = {};
        const auto pipe2_status = context.camera.TakeCompletedInference(&pipe2_frame);
        if (!pipe2_status.Ok()) {
            if (pipe2_status.Code() != common::ErrorCode::kNoFrame
                && pipe2_status.Code() != common::ErrorCode::kNoBuffer)
                pipe2_status.LogStatus("camera");
            break;
        }
        cycle.did_work = true;
        state.schedule.StartEvent(RenderOperation::kPipe2);
        if (features.input_display) {
            auto &submit = state.schedule.Stats(RenderOperation::kSubmit);
            ++submit.dropped;
            ++submit.dropped_inactive;
            status = context.memory.ClaimInferenceBuffer(pipe2_frame);
            if (!status.Ok()) {
                status.LogStatus("memory");
                common::Task::Halt("ai: inference input display claim failed\n");
            }
            const bool log_input = context.diagnostics.inference_input
                && (pipe2_frame.capture_sequence <= 3U || (pipe2_frame.capture_sequence % 30U) == 0U);
            if (log_input) {
                UAI_LOG_DEBUG(
                    "ai: input display live sequence=%u buffer=%x\n",
                    static_cast<unsigned int>(pipe2_frame.capture_sequence),
                    static_cast<unsigned int>(pipe2_frame.buffer.address)
                );
                LogPipe2InferenceInputStatistics(context, pipe2_frame);
            }
            status = context.lcd.ComposeInferenceAndPresent(pipe2_frame);
            if (!status.Ok() && (!status.IsRoutine() || log_input)) {
                status.LogStatus("lcd");
            }
            (void)release_inference(pipe2_frame);
        } else if (inference_enabled
                   && state.schedule.TakeInference(pipe2_frame.capture_sequence, state.geometry.fps)) {
            auto &submit = state.schedule.Stats(RenderOperation::kSubmit);
            ++submit.started;
            if (kInferenceMode == InferenceMode::kNpu) {
                status = context.camera.SnapshotInferenceSource(&pipe2_frame);
                if (!status.Ok()) {
                    status.LogStatus("camera");
                    (void)release_inference(pipe2_frame);
                    ++submit.dropped;
                    ++submit.dropped_source;
                } else {
                    status =
                        context.pipeline_task.InferenceFrames().Send(pipe2_frame, context.pipeline_task.Generation());
                }
            } else {
                status = release_inference(pipe2_frame);
            }
            submit.Finish(status.Ok());
        } else {
            status = release_inference(pipe2_frame);
            auto &submit = state.schedule.Stats(RenderOperation::kSubmit);
            ++submit.dropped;
            if (inference_enabled)
                ++submit.dropped_stride;
            else
                ++submit.dropped_inactive;
        }
        pipe_stats.Finish(status.Ok());
        if (drained + 1U == memory_manager::kInferenceBufferCount)
            wake.Notify(CameraRenderWake::kPipe2);
    }
    const auto queue_drops = context.pipeline_task.InferenceFrames().Dropped();
    state.schedule.Stats(RenderOperation::kSubmit).dropped += queue_drops - state.observed_queue_drops;
    state.schedule.Stats(RenderOperation::kSubmit).dropped_queue = queue_drops;
    state.observed_queue_drops = queue_drops;
}

void ReceiveInferenceResultsAndUpdateExposureControl(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle,
    CameraRenderWake &wake
)
{
    const auto now = cycle.now;
    const auto &features = cycle.features;
    const auto &active_boxes = state.results.boxes;
    auto &exposure = state.exposure;
    const auto &exposure_mode = state.exposure_mode;
    auto &screen_ui = state.ui;
    ModelResultSnapshot incoming{};
    const message_channel::DrainResult drain_result = context.pipeline_task.TryGetLatestResult(&incoming);
    if (drain_result.updated) {
        state.schedule.StartEvent(RenderOperation::kResults);
        auto &results = state.schedule.Stats(RenderOperation::kResults);
        if (state.results.Accept(incoming))
            results.Finish(true);
        else
            ++results.skipped;
        cycle.did_work = true;
    }
    if (drain_result.error.Code() == common::ErrorCode::kTimeout)
        wake.Notify(CameraRenderWake::kResult);
    cycle.ui_dirty = state.results.Expire(now) || cycle.ui_dirty;
    if constexpr (kAiExposureControl) {
        const auto exposure_results =
            context.pipeline_task.ConsumeExposureResults([&](const inference::BoxSet &result) {
                if (features.ai_exposure && state.previous_exposure_enabled)
                    exposure.Observe(result, now);
            });
        if (exposure_results.error.Code() == common::ErrorCode::kTimeout)
            wake.Notify(CameraRenderWake::kResult);
        if (!exposure_results.error.Ok() && exposure_results.error.Code() != common::ErrorCode::kNoFrame
            && exposure_results.error.Code() != common::ErrorCode::kTimeout)
            exposure_results.error.LogStatus("exposure-results");
        state.previous_exposure_enabled = features.ai_exposure;
        if (exposure.RemainingWait(now, features.ai_exposure) == 0U) {
            state.schedule.StartEvent(RenderOperation::kExposure);
            const auto exposure_status = exposure.Process(
                context.camera, now, features.ai_exposure, !exposure_mode.manual, !exposure_mode.custom_statistics
            );
            state.schedule.Stats(RenderOperation::kExposure).Finish(exposure_status.Ok());
            cycle.did_work = true;
            if (!exposure_status.Ok()
                && (state.last_exposure_error_tick == 0U || now - state.last_exposure_error_tick >= 1000U)) {
                exposure_status.LogStatus("exposure");
                state.last_exposure_error_tick = now;
            }
        }
    }
    cycle.ui_dirty =
        screen_ui.UpdateStatus(now, kAiExposureControl ? &exposure.DisplayValues() : nullptr) || cycle.ui_dirty;
    if (!drain_result.error.Ok() && drain_result.error.Code() != common::ErrorCode::kNoFrame) {
        drain_result.error.LogStatus("results");
    }
    if (drain_result.updated) {
        if (context.diagnostics.display_trace) {
            UAI_LOG_DEBUG(
                "lcd: box source=ai sequence=%u capture=%u count=%u\n",
                static_cast<unsigned int>(active_boxes.model_sequence),
                static_cast<unsigned int>(active_boxes.capture_sequence),
                static_cast<unsigned int>(active_boxes.person.count + active_boxes.face.count)
            );
            UAI_LOG_DEBUG(
                "lcd: boxes person=%u face=%u mask_px=%u\n",
                static_cast<unsigned int>(active_boxes.person.count),
                static_cast<unsigned int>(active_boxes.face.count),
                static_cast<unsigned int>(active_boxes.segmentation.mask_foreground_pixels)
            );
        }
        /* The latest inference result is authoritative. Until it arrives,
         * keep presenting the previous result so model switching does not
         * create a blank frame. */
    }
}

}