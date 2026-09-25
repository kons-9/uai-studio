#include "task/task_context.hpp"

#include "model_manager/model_manager.hpp"

namespace uai::ai::task {

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
            ? model.Initialize(g_memory, g_memory_hardware)
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

    for (;;) {
        InferenceMessage message{};
        const INT size = tk_rcv_mbf(g_frame_queue, &message, TMO_FEVR);
        if (size != static_cast<INT>(sizeof(message))) {
            continue;
        }

        Error status = g_memory.ClaimInferenceBuffer(message.frame);
        if (!status.Ok()) {
            LogStatus("memory", status);
            continue;
        }

        BoxSet boxes{};
        if (model_status.Ok()) {
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: inference begin sequence=%u\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence));
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: inference run begin sequence=%u input=%x irq=%u last=%x\n"),
                      static_cast<unsigned int>(message.frame.capture_sequence),
                      static_cast<unsigned int>(message.frame.buffer.address),
                      g_aton_irq_count, g_aton_last_irqs);
            status = model.TryInfer(message.frame, &boxes);
            tm_printf(reinterpret_cast<const UB *>(
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
            SendLatestBoxes(boxes);
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: inference sequence=%u capture=%u count=%u\n"),
                      static_cast<unsigned int>(boxes.model_sequence),
                      static_cast<unsigned int>(boxes.capture_sequence),
                      static_cast<unsigned int>(boxes.count));
        } else if (model_status.Ok()) {
            LogStatus("ai", status);
            LogNpuStatus(model.LastNpuStatus());
        }
    }
}

} // namespace uai::ai::task
