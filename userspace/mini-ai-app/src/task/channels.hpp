#pragma once

#include "app_config.hpp"
#include "memory_manager/memory_manager.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/message_channel/latest_value_channel.hpp"
#include "middleware/message_channel/utkernel_backend.hpp"
#include "middleware/pipeline/frame_types.hpp"

namespace uai::ai::mini {

/* Pipe2 frames from the camera task to the inference task. Every frame
 * carries a buffer lease, so whoever drops a frame returns it to the memory
 * manager exactly once. */
class InferenceFrameChannel final {
public:
    explicit InferenceFrameChannel(memory_manager::MemoryManager &memory) : memory_(memory) {}

    common::Error Create() { return channel_.Create(); }

    void Send(const pipeline::InferenceFrame &frame)
    {
        const common::Error status =
            channel_.SendReplacingOldest(frame, [this](const pipeline::InferenceFrame &discarded) {
                memory_.ReleaseInferenceBuffer(discarded).LogStatus("memory");
            });
        if (status.Ok())
            return;
        memory_.ReleaseInferenceBuffer(frame).LogStatus("memory");
        UAI_LOG_DEBUG(
            "ai: frame dropped reason=%s sequence=%u\n",
            common::ErrorCodeName(status.Code()),
            static_cast<unsigned int>(frame.capture_sequence)
        );
    }

    common::Error Receive(pipeline::InferenceFrame *frame) { return channel_.ReceiveBlocking(frame); }

private:
    memory_manager::MemoryManager &memory_;
    message_channel::LatestValueChannel<
        pipeline::InferenceFrame,
        kFrameQueueDepth,
        message_channel::MicroTKernelBackend<pipeline::InferenceFrame, kFrameQueueDepth>>
        channel_;
};

/* Detection results from the inference task to the camera task. The camera
 * task only ever needs the newest result. */
class InferenceResultChannel final {
public:
    common::Error Create() { return channel_.Create(); }

    common::Error Publish(const inference::BoxSet &boxes) { return channel_.SendReplacingOldestOnce(boxes); }

    message_channel::DrainResult DrainLatest(inference::BoxSet *latest)
    {
        return channel_.DrainLatest(latest, [](const inference::BoxSet &boxes) {
            return boxes.person_valid;
        });
    }

private:
    message_channel::LatestValueChannel<
        inference::BoxSet,
        kResultQueueDepth,
        message_channel::MicroTKernelBackend<inference::BoxSet, kResultQueueDepth>>
        channel_;
};

} // namespace uai::ai::mini
