#ifndef UAI_AI_TASK_PIPELINE_TASK_HPP
#define UAI_AI_TASK_PIPELINE_TASK_HPP

#include <cstdint>
#include <tk/tkernel.h>

#include "memory_manager/memory_manager.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/foundation/message_channel.hpp"
#include "middleware/foundation/stable_aligned_bytes.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "task/task_config.hpp"

namespace uai::ai::middleware::cpu_task_monitor { class CpuTaskMonitor; }
namespace uai::ai::cache { class CacheManagement; }
namespace uai::ai::camera { class CameraManagement; }

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
    ID Create() { return channel_.Create(); }

    common::Error PublishLatest(const inference::BoxSet &boxes)
    {
        common::Error status = Send(boxes);
        if (status.Code() == common::ErrorCode::kBufferOverflow) {
            DiscardOldest();
            status = Send(boxes);
        }
        return status;
    }

    bool DrainLatest(inference::BoxSet *active)
    {
        if (active == nullptr) return false;
        inference::BoxSet message{};
        bool received = false;
        for (;;) {
            const INT size = channel_.Receive(&message, TMO_POL);
            if (size < 0) break;
            if (size == static_cast<INT>(sizeof(message)) &&
                (message.person_valid || message.face_valid ||
                 message.segmentation_valid)) {
                *active = message;
                received = true;
            }
        }
        return received;
    }

private:
    common::Error Send(const inference::BoxSet &boxes)
    {
        if (channel_.id() < E_OK) {
            return {common::ErrorCode::kNotInitialized};
        }
        const ER error = channel_.Send(boxes, TMO_POL);
        if (error == E_TMOUT) {
            return {common::ErrorCode::kBufferOverflow};
        }
        if (error != E_OK) {
            return {common::ErrorCode::kHardware};
        }
        return {};
    }

    bool DiscardOldest()
    {
        inference::BoxSet discarded{};
        return channel_.Receive(&discarded, TMO_POL) ==
               static_cast<INT>(sizeof(discarded));
    }

    common::MessageChannel<inference::BoxSet, kResultQueueDepth> channel_;
};

class InferenceFrameChannel final {
public:
    explicit InferenceFrameChannel(memory_manager::MemoryManager &memory)
        : memory_(memory) {}

    ID Create() { return channel_.Create(); }

    void Send(const pipeline::InferenceFrame &frame)
    {
        for (;;) {
            const ER error = channel_.Send(frame, TMO_POL);
            if (error == E_OK) return;

            pipeline::InferenceFrame discarded{};
            const INT size = channel_.Receive(&discarded, TMO_POL);
            if (size == static_cast<INT>(sizeof(discarded))) {
                memory_.ReleaseInferenceBuffer(discarded).LogStatus("memory");
                continue;
            }

            memory_.ReleaseInferenceBuffer(frame).LogStatus("memory");
            UAI_LOG_DEBUG("ai: frame dropped reason=queue_full sequence=%u\n",
                          static_cast<unsigned int>(frame.capture_sequence));
            return;
        }
    }

    bool Receive(pipeline::InferenceFrame *frame)
    {
        if (frame == nullptr) return false;
        return channel_.Receive(frame, TMO_FEVR) ==
               static_cast<INT>(sizeof(*frame));
    }

private:
    memory_manager::MemoryManager &memory_;
    common::MessageChannel<pipeline::InferenceFrame, kFrameQueueDepth> channel_;
};

/* Model pipeline tasks. Each worker runs one ai_runtime lane. */
class PipelineTask final {
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
    ID CreateResultQueue() { return results_.Create(); }
    common::Error PublishResult(const inference::BoxSet &boxes)
    {
        return results_.PublishLatest(boxes);
    }
    bool TryGetLatestResult(inference::BoxSet *active)
    {
        return results_.DrainLatest(active);
    }

private:
    explicit PipelineTask(memory_manager::MemoryManager &memory)
        : inference_frames_(memory) {}
    InferenceFrameChannel inference_frames_;
    InferenceResultChannel results_;
    common::StableAlignedBytes<kPipelineTaskStackSize> frame_stack_;
    common::StableAlignedBytes<kPipelineTaskStackSize> preprocess_stack_;
    common::StableAlignedBytes<kPipelineTaskStackSize> npu_stack_;
    common::StableAlignedBytes<kPipelinePostprocessTaskStackSize> postprocess_stack_;
};

} // namespace uai::ai::task

#endif
