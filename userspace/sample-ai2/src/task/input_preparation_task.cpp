#include "task/input_preparation_task.hpp"

#include "task/task_context.hpp"
#include "task/task_diagnostics.hpp"

namespace uai::ai::task {

void InputPreparationTask::Entry()
{
    InputPreparationTask{}.Run();
}

void InputPreparationTask::Run()
{
    TaskContext &context = GetTaskContext();
    for (;;) {
        InputPreparationRequest request{};
        if (tk_rcv_mbf(context.input_preparation_request_queue, &request,
                       TMO_FEVR) != static_cast<INT>(sizeof(request))) {
            continue;
        }
        if (!context.input_preparation_enabled) {
            return;
        }
        InputPreparationResult result{};
        result.handoff = {pipeline::ExecutionContext::kCpuInputTask,
                          pipeline::ExecutionContext::kNpuTask,
                          request.handoff.request_id, 0U};
        if (!pipeline::ValidHandoff(request.handoff) ||
            request.handoff.to != pipeline::ExecutionContext::kCpuInputTask ||
            request.model == nullptr ||
            request.model->GetDescriptor().kind != request.kind) {
            result.status = {common::ErrorCode::kInvalidArgument, 0U,
                             "ai.input_prepare.request"};
        } else {
            ++context.inference_metrics.prefetch_attempts;
            bool saw_empty_queue = false;
            while (context.input_preparation_enabled) {
                InferenceMessage message{};
                const INT size = tk_rcv_mbf(context.frame_queue, &message,
                                             TMO_POL);
                if (size != static_cast<INT>(sizeof(message))) {
                    if (!saw_empty_queue) {
                        ++context.inference_metrics.prefetch_queue_empty;
                        saw_empty_queue = true;
                    }
                    tk_dly_tsk(1U);
                    continue;
                }
                result.status = context.memory.ClaimInferenceBuffer(message.frame);
                if (!result.status.Ok()) {
                    if (result.status.code == common::ErrorCode::kNoBuffer) {
                        ++context.inference_metrics.prefetch_buffer_busy;
                    } else {
                        ++context.inference_metrics.prefetch_claim_errors;
                    }
                    LogStatus("memory", result.status);
                    /* Release a still-Ready frame when possible; a stale
                     * lease is rejected by the allocator and stays untouched. */
                    (void)context.memory.ReleaseInferenceBuffer(message.frame);
                    if (context.IsBestEffort(result.status.code)) {
                        continue;
                    }
                    break;
                }

                const std::uint32_t start_ms = context.Now();
                result.status = request.model->PrepareInput(message.frame,
                                                             context.cache);
                const std::uint32_t end_ms = context.Now();
                if (!result.status.Ok() || !context.input_preparation_enabled) {
                    const common::Error release =
                        context.memory.ReleaseInferenceBuffer(message.frame);
                    LogStatus("memory", release);
                    break;
                }
                message.frame.input_prepared = true;
                message.frame.prepared_model_kind_id =
                    static_cast<std::uint8_t>(request.kind);
                message.frame.input_preparation_start_ms = start_ms;
                message.frame.input_preparation_end_ms = end_ms;
                message.frame.input_preparation_elapsed_ms = end_ms - start_ms;
                result.frame = message.frame;
                result.handoff.lease_token = result.frame.lease_token;
                ++context.inference_metrics.prefetch_successes;
                break;
            }
        }
        if (!context.input_preparation_enabled) {
            if (result.frame) {
                const common::Error release =
                    context.memory.ReleaseInferenceBuffer(result.frame);
                LogStatus("memory", release);
            }
            return;
        }
        /* Exactly one request is outstanding. A failed send is a protocol
         * fault: do not leave the claimed frame in an unowned queue slot. */
        if (tk_snd_mbf(context.input_preparation_result_queue, &result,
                       sizeof(result), TMO_POL) != E_OK) {
            if (result.frame) {
                const common::Error release =
                    context.memory.ReleaseInferenceBuffer(result.frame);
                LogStatus("memory", release);
            }
            context.Halt("ai: input preparation result queue full\n");
        }
    }
}

} // namespace uai::ai::task