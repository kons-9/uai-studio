#pragma once

#include <atomic>
#include <cstdint>
#include <tk/tkernel.h>

#include "memory_manager/memory_manager.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/message_channel/latest_value_channel.hpp"
#include "middleware/message_channel/utkernel_backend.hpp"
#include "middleware/buffer/stable_aligned_bytes.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "task/model_control.hpp"
#include "task/task_config.hpp"

namespace uai::ai::middleware::cpu_task_monitor {
class CpuTaskMonitor;
}
namespace uai::ai::cache {
class CacheManagement;
}
namespace uai::ai::camera {
class CameraManagement;
}

namespace uai::ai::task {

class PipelineTask;

struct PipelineFrameContext {
    memory_manager::MemoryManager &memory;
    cache::CacheManagement &cache;
    camera::CameraManagement &camera;
    middleware::cpu_task_monitor::CpuTaskMonitor &cpu_task_monitor;
    PipelineTask &pipeline_task;
    const volatile bool &external_nor_ready;
    ID external_memory_ready;
    const DiagnosticsConfig &diagnostics;
};

struct PipelineWorkerContext {
    middleware::cpu_task_monitor::CpuTaskMonitor &cpu_task_monitor;
    ID pipeline_work_ready;
};

class InferenceResultChannel final {
public:
    common::Error Create() { return channel_.Create(); }

    common::Error PublishLatest(const inference::BoxSet &boxes) { return channel_.SendReplacingOldestOnce(boxes); }

    template <typename Consumer>
    message_channel::DrainResult Consume(Consumer consumer)
    {
        inference::BoxSet latest{};
        return channel_.DrainLatest(&latest, [&](const inference::BoxSet &boxes) {
            consumer(boxes);
            return true;
        });
    }

    message_channel::DrainResult DrainLatest(inference::BoxSet *active)
    {
        return channel_.DrainLatest(active, [](const inference::BoxSet &boxes) {
            return boxes.person_valid || boxes.face_valid || boxes.segmentation_valid;
        });
    }

private:
    message_channel::LatestValueChannel<
        inference::BoxSet,
        kResultQueueDepth,
        message_channel::MicroTKernelBackend<inference::BoxSet, kResultQueueDepth>>
        channel_;
};

/* Every frame carries an inference buffer lease, so a dropped frame must be
 * returned to MemoryManager exactly once by whoever drops it. */
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

/* Model pipeline tasks. Each worker runs one ai_runtime lane. */
class PipelineTask final : public ModelControl {
public:
    static PipelineTask &Instance(memory_manager::MemoryManager &memory)
    {
        static PipelineTask task(memory);
        return task;
    }

    static void FrameEntry();
    static void PreprocessEntry();
    static void NpuEntry();
    static void PostprocessEntry();
    void StartFrame(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);
    void StartPreprocess(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);
    void StartNpu(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);
    void StartPostprocess(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);
    InferenceFrameChannel &InferenceFrames() { return inference_frames_; }
    common::Error CreateResultQueue()
    {
        auto status = results_.Create();
        return status.Ok() ? exposure_results_.Create() : status;
    }
    common::Error PublishResult(const inference::BoxSet &boxes) { return results_.PublishLatest(boxes); }
    common::Error PublishExposureResult(const inference::BoxSet &boxes)
    {
        return exposure_results_.PublishLatest(boxes);
    }
    template <typename Consumer>
    message_channel::DrainResult ConsumeExposureResults(Consumer consumer)
    {
        return exposure_results_.Consume(consumer);
    }
    message_channel::DrainResult TryGetLatestResult(inference::BoxSet *active) { return results_.DrainLatest(active); }
    void SetModelMask(std::uint8_t mask) override
    {
        model_mask_.store(mask & kAllModelsMask, std::memory_order_relaxed);
    }
    std::uint8_t ModelMask() const override { return model_mask_.load(std::memory_order_relaxed); }
    PipelineStats Stats() const override;

private:
    explicit PipelineTask(memory_manager::MemoryManager &memory) : inference_frames_(memory) {}
    InferenceFrameChannel inference_frames_;
    InferenceResultChannel results_;
    InferenceResultChannel exposure_results_;
    std::atomic<std::uint8_t> model_mask_{kAllModelsMask};
    common::StableAlignedBytes<kPipelineTaskStackSize> frame_stack_;
    common::StableAlignedBytes<kPipelineTaskStackSize> preprocess_stack_;
    common::StableAlignedBytes<kPipelineTaskStackSize> npu_stack_;
    common::StableAlignedBytes<kPipelinePostprocessTaskStackSize> postprocess_stack_;
};

} // namespace uai::ai::task
