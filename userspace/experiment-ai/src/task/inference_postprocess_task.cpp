#include "task/inference_postprocess_task.hpp"

#include "common/log.hpp"
#include "task/task_context.hpp"
#include "task/task_diagnostics.hpp"

#include <cstddef>
#include <cstdint>

namespace uai::ai::task {

namespace {

common::Error CompleteInference(TaskContext &context,
                                 const npu_runtime::InferenceCompletion &completion,
                                 memory_allocator::BoxSet *boxes)
{
    if (!completion.valid || completion.model == nullptr || boxes == nullptr ||
        completion.callbacks.on_inference_complete == nullptr ||
        completion.callbacks.user_data == nullptr ||
        completion.output_view.count == 0U ||
        completion.output_view.count > models::kMaxModelOutputs) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.postprocess.completion"};
    }

    for (std::uint16_t i = 0U; i < completion.output_view.count; ++i) {
        const common::Error status =
            context.cache.PrepareForCpuRead(completion.output_buffers[i]);
        if (!status.Ok()) {
            return status;
        }
    }

    models::ModelResult decoded{};
    const models::InferenceCompletionContext decode_context{
        completion.output_view, completion.geometry};
    common::Error status = completion.callbacks.on_inference_complete(
        decode_context, &decoded, completion.callbacks.user_data);
    if (!status.Ok()) {
        return status;
    }

    boxes->person = {};
    boxes->face = {};
    boxes->segmentation = {};
    boxes->person_valid = false;
    boxes->face_valid = false;
    boxes->segmentation_valid = false;
    boxes->capture_sequence = completion.capture_sequence;
    boxes->model_sequence = completion.model_sequence;
    return completion.model->ConvertResult(decoded, boxes);
}

} // namespace

void InferencePostprocessTask::Entry()
{
    InferencePostprocessTask{}.Run();
}

void InferencePostprocessTask::Run()
{
    TaskContext &context = GetTaskContext();
    memory_allocator::BoxSet integrated_boxes{};
    for (;;) {
        npu_runtime::InferenceCompletion completion{};
        const std::uint32_t queue_wait_start = context.Now();
        const INT size = tk_rcv_mbf(context.inference_completion_queue,
                                    &completion, TMO_FEVR);
        const std::uint32_t queue_wait = context.Now() - queue_wait_start;
        context.inference_metrics.postprocess_queue_wait_total_ms += queue_wait;
        if (queue_wait > context.inference_metrics.postprocess_queue_wait_max_ms) {
            context.inference_metrics.postprocess_queue_wait_max_ms = queue_wait;
        }
        if (size != static_cast<INT>(sizeof(completion))) {
            continue;
        }

        memory_allocator::BoxSet boxes{};
        const std::uint32_t postprocess_start = context.Now();
        const common::Error status =
            CompleteInference(context, completion, &boxes);
        const std::uint32_t postprocess_elapsed =
            context.Now() - postprocess_start;
        const std::size_t model_index =
            static_cast<std::size_t>(completion.model_kind);
        if (model_index < kInferenceModelCount) {
            ++context.inference_metrics.postprocess_count[model_index];
            context.inference_metrics.postprocess_total_ms[model_index] +=
                postprocess_elapsed;
            if (postprocess_elapsed >
                context.inference_metrics.postprocess_max_ms[model_index]) {
                context.inference_metrics.postprocess_max_ms[model_index] =
                    postprocess_elapsed;
            }
        }
        if (!status.Ok()) {
            LogStatus("ai.postprocess", status);
        } else {
            if (boxes.person_valid) {
                integrated_boxes.person = boxes.person;
                integrated_boxes.person_valid = true;
            }
            if (boxes.face_valid) {
                integrated_boxes.face = boxes.face;
                integrated_boxes.face_valid = true;
            }
            if (boxes.segmentation_valid) {
                integrated_boxes.segmentation = boxes.segmentation;
                integrated_boxes.segmentation_valid = true;
            }
            integrated_boxes.model_sequence = boxes.model_sequence;
            integrated_boxes.capture_sequence = boxes.capture_sequence;
            context.SendLatestBoxes(integrated_boxes);
        }

        /* Keep ownership with the inference task until it decides whether to
         * release this frame to Pipe2 or reuse it for the next model. */
        context.SendInferencePostprocessDone(completion.frame);
    }
}

} // namespace uai::ai::task
