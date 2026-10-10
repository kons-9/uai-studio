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
#include "task/model_result_snapshot.hpp"
#include "middleware/task/event_notification.hpp"

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

class ExposureResultChannel final {
public:
    common::Error Create() { return channel_.Create(); }
    common::Error PublishLatest(
        const inference::BoxSet &boxes,
        std::uint32_t generation
    )
    {
        return channel_.SendReplacingOldestOnce({boxes, generation});
    }
    template <typename Consumer>
    message_channel::DrainResult Consume(
        Consumer consumer,
        std::uint32_t generation
    )
    {
        Result latest{};
        return channel_.DrainLatest(&latest, [&](const Result &result) {
            if (result.generation != generation)
                return false;
            consumer(result.boxes);
            return true;
        });
    }

private:
    struct Result {
        inference::BoxSet boxes{};
        std::uint32_t generation = 0U;
    };
    message_channel::
        LatestValueChannel<Result, kResultQueueDepth, message_channel::MicroTKernelBackend<Result, kResultQueueDepth>>
            channel_;
};

class ModelResultChannel final {
public:
    common::Error Create() { return channel_.Create(); }
    void SetNotification(common::EventNotification notification) { notification_ = notification; }
    common::Error PublishLatest(const ModelResultSnapshot &snapshot)
    {
        const auto status = channel_.SendReplacingOldestOnce(snapshot);
        if (status.Ok())
            notification_.Notify();
        return status;
    }
    message_channel::DrainResult DrainLatest(ModelResultSnapshot *snapshot)
    {
        return channel_.DrainLatest(snapshot, [](const ModelResultSnapshot &) {
            return true;
        });
    }

private:
    message_channel::LatestValueChannel<
        ModelResultSnapshot,
        kResultQueueDepth,
        message_channel::MicroTKernelBackend<ModelResultSnapshot, kResultQueueDepth>>
        channel_;
    common::EventNotification notification_;
};

/* Every frame carries an inference buffer lease, so a dropped frame must be
 * returned to MemoryManager exactly once by whoever drops it. */
class InferenceFrameChannel final {
public:
    explicit InferenceFrameChannel(memory_manager::MemoryManager &memory) : memory_(memory) {}

    common::Error Create() { return channel_.Create(); }

    common::Error Send(
        const pipeline::InferenceFrame &frame,
        std::uint32_t generation = 0U
    )
    {
        const common::Error status = channel_.SendReplacingOldest({frame, generation}, [this](const Job &discarded) {
            memory_.ReleaseInferenceBuffer(discarded.frame).LogStatus("memory");
            dropped_.fetch_add(1U, std::memory_order_relaxed);
        });
        if (status.Ok())
            return status;
        memory_.ReleaseInferenceBuffer(frame).LogStatus("memory");
        dropped_.fetch_add(1U, std::memory_order_relaxed);
        UAI_LOG_DEBUG(
            "ai: frame dropped reason=%s sequence=%u\n",
            common::ErrorCodeName(status.Code()),
            static_cast<unsigned int>(frame.capture_sequence)
        );
        return status;
    }
    std::uint32_t Dropped() const { return dropped_.load(std::memory_order_relaxed); }

    common::Error Receive(
        pipeline::InferenceFrame *frame,
        std::uint32_t *generation = nullptr
    )
    {
        if (frame == nullptr)
            return {common::ErrorCode::kInvalidArgument};
        Job job{};
        const auto status = channel_.ReceiveBlocking(&job);
        if (status.Ok()) {
            *frame = job.frame;
            if (generation != nullptr)
                *generation = job.generation;
        }
        return status;
    }

private:
    struct Job {
        pipeline::InferenceFrame frame{};
        std::uint32_t generation = 0U;
    };
    memory_manager::MemoryManager &memory_;
    std::atomic<std::uint32_t> dropped_{0U};
    message_channel::
        LatestValueChannel<Job, kFrameQueueDepth, message_channel::MicroTKernelBackend<Job, kFrameQueueDepth>>
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
    common::Error PublishResult(const ModelResultSnapshot &snapshot) { return results_.PublishLatest(snapshot); }
    void SetResultNotification(common::EventNotification notification)
    {
        results_.SetNotification(notification);
        result_notification_ = notification;
    }
    std::uint32_t Generation() const { return generation_.load(std::memory_order_acquire); }
    std::uint32_t AdvanceGeneration() { return generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U; }
    common::Error PublishExposureResult(
        const inference::BoxSet &boxes,
        std::uint32_t generation
    )
    {
        const auto status = exposure_results_.PublishLatest(boxes, generation);
        if (status.Ok())
            result_notification_.Notify();
        return status;
    }
    template <typename Consumer>
    message_channel::DrainResult ConsumeExposureResults(Consumer consumer)
    {
        return exposure_results_.Consume(consumer, Generation());
    }
    message_channel::DrainResult TryGetLatestResult(ModelResultSnapshot *active)
    {
        return results_.DrainLatest(active);
    }
    void SetModelMask(std::uint8_t mask) override
    {
        model_mask_.store(mask & kAllModelsMask, std::memory_order_relaxed);
    }
    std::uint8_t ModelMask() const override { return model_mask_.load(std::memory_order_relaxed); }
    PipelineStats Stats() const override;
    common::Error PauseAiTrace();
    void ResumeAiTrace();

private:
    explicit PipelineTask(memory_manager::MemoryManager &memory) : inference_frames_(memory) {}
    InferenceFrameChannel inference_frames_;
    ModelResultChannel results_;
    ExposureResultChannel exposure_results_;
    common::EventNotification result_notification_;
    std::atomic<std::uint32_t> generation_{0U};
    std::atomic<std::uint8_t> model_mask_{kAllModelsMask};
    common::StableAlignedBytes<kPipelineTaskStackSize> frame_stack_;
    common::StableAlignedBytes<kPipelineTaskStackSize> preprocess_stack_;
    common::StableAlignedBytes<kPipelineTaskStackSize> npu_stack_;
    common::StableAlignedBytes<kPipelinePostprocessTaskStackSize> postprocess_stack_;
};

} // namespace uai::ai::task
