#include "task/person_pipeline_task.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "common/log.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "middleware/ai_runtime/pipeline.hpp"
#include "models/person/model.hpp"
#include "task/task_context.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task {
namespace {

using ai_runtime::ExecutionContext;
using ai_runtime::DispatchResult;
using ai_runtime::NextStep;

struct PersonApplication;
PersonApplication &App();

class PersonFuture final : public ai_runtime::AiFuture {
public:
    enum class Phase : std::uint32_t { kPreprocess, kNpu, kPostprocess };

    void Reset(PersonApplication &owner, const memory_allocator::InferenceFrame &frame)
    {
        owner_ = &owner;
        frame_ = frame;
        phase_ = Phase::kPreprocess;
    }
    ai_runtime::AiModelId model_id() const override
    {
        return static_cast<ai_runtime::AiModelId>(models::ModelKind::kPerson);
    }
    std::uint32_t step_id() const override { return static_cast<std::uint32_t>(phase_); }
    bool is_ready() const override { return true; }
    ai_runtime::AiRuntimeResult Evaluate() override;

    memory_allocator::InferenceFrame frame_{};
    std::atomic<bool> occupied_{false};

private:
    PersonApplication *owner_ = nullptr;
    Phase phase_ = Phase::kPreprocess;
};

struct PersonApplication {
    models::person::Model model{};
    npu::NpuDriver npu{};
    ai_runtime::PipelineRuntime pipeline{};
    ai_runtime::Scheduler scheduler{pipeline};
    PersonFuture futures[memory_allocator::kInferenceBufferCount]{};
    stai_network_info info{};
    std::uint32_t model_sequence = 0U;
    std::uint32_t submitted_count = 0U;
    std::uint32_t completed_count = 0U;
    std::uint32_t postprocess_count = 0U;
    std::uint32_t last_detection_count = 0U;
    std::uint32_t last_capture_sequence = 0U;
    std::uint32_t report_tick = 0U;
    std::uint32_t report_submitted = 0U;
    std::uint32_t report_completed = 0U;
    std::uint32_t report_postprocess = 0U;
    bool submit_stage_logged = false;
    bool preprocess_stage_logged = false;
    bool infer_stage_logged = false;
    bool postprocess_stage_logged = false;

    void Report(TaskContext &context)
    {
        if (!context.diagnostics.inference_fps) return;
        const std::uint32_t now = context.Now();
        if (report_tick == 0U) {
            report_tick = now;
            return;
        }
        if (now - report_tick < 1000U) return;
        const auto camera = context.camera.GetDiagnostics();
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: person stats submitted=%u completed=%u "
                         "post=%u boxes=%u capture=%u pipe2=%u drops=%u csi=%u\n"),
                     static_cast<unsigned int>(submitted_count - report_submitted),
                     static_cast<unsigned int>(completed_count - report_completed),
                     static_cast<unsigned int>(postprocess_count - report_postprocess),
                     static_cast<unsigned int>(last_detection_count),
                     static_cast<unsigned int>(last_capture_sequence),
                     static_cast<unsigned int>(camera.pipe2_frame_event_count),
                     static_cast<unsigned int>(camera.pipe2_drop_count),
                     static_cast<unsigned int>(camera.csi_error_count));
        report_tick = now;
        report_submitted = submitted_count;
        report_completed = completed_count;
        report_postprocess = postprocess_count;
    }
    std::atomic<bool> enabled{false};

    common::Error Initialize(TaskContext &context)
    {
        npu::Status result = npu.Initialize(models::person::Runtime(model));
        if (!result.Ok()) return result.error;
        result = npu.GetInfo(&info);
        if (!result.Ok()) return result.error;
        if (info.n_inputs != 1U || info.inputs == nullptr ||
            info.n_outputs == 0U || info.n_outputs > models::kMaxModelOutputs ||
            info.outputs == nullptr ||
            info.inputs[0].size_bytes !=
                static_cast<std::size_t>(model.GetDescriptor().input_width) *
                    model.GetDescriptor().input_height * 3U) {
            return {common::ErrorCode::kModel, 0U, "person_pipeline.model_info"};
        }
        stai_ptr outputs[models::kMaxModelOutputs]{};
        stai_size count = 0U;
        result = npu.GetOutputs(outputs, &count);
        if (!result.Ok()) return result.error;
        if (count != info.n_outputs) {
            return {common::ErrorCode::kModel, count,
                    "person_pipeline.output_count"};
        }
        /* Concurrent postprocessing requires a distinct output per frame. */
        for (std::uint16_t i = 0U; i < count; ++i) {
            if (outputs[i] != nullptr ||
                info.outputs[i].size_bytes >
                    memory_allocator::kConfig.model_output_bytes[i]) {
                return {common::ErrorCode::kModel, i,
                        "person_pipeline.output_ownership"};
            }
        }
        const models::ModelCallbacks callbacks = model.GetCallbacks();
        if (callbacks.configure == nullptr ||
            callbacks.on_inference_complete == nullptr ||
            callbacks.user_data == nullptr) {
            return {common::ErrorCode::kModel, 0U,
                    "person_pipeline.decoder_callbacks"};
        }
        models::ModelOutputSpec spec{};
        spec.count = info.n_outputs;
        for (std::uint16_t i = 0U; i < spec.count; ++i) {
            spec.tensors[i] = {info.outputs[i].size_bytes,
                               info.outputs[i].scale.data[0],
                               info.outputs[i].zeropoint.data[0]};
        }
        const common::Error status = callbacks.configure(spec, callbacks.user_data);
        if (!status.Ok()) return status;
        pipeline.SetCriticalSection(&EnterCritical, &LeaveCritical, nullptr);
        pipeline.SetObserver(&OnDone, &context);
        pipeline.SetTrace(&OnTrace, &context, &Now, &context);
        enabled.store(true);
        return {};
    }

    static std::uint32_t Now(void *context)
    {
        return static_cast<TaskContext *>(context)->Now();
    }
    static void EnterCritical(void *) { interrupt_state_ = __get_PRIMASK(); __disable_irq(); }
    static void LeaveCritical(void *) { __set_PRIMASK(interrupt_state_); }
    static std::uint32_t interrupt_state_;

    static void OnTrace(void *context, const ai_runtime::StepTrace &trace)
    {
        auto &task = *static_cast<TaskContext *>(context);
        if (task.diagnostics.inference_trace) {
            UAI_LOG_TRACE(reinterpret_cast<const UB *>(
                "ai: person inference=%u model=%u step=%u lane=%u time=%u begin=%u\n"),
                static_cast<unsigned int>(trace.inference_id),
                static_cast<unsigned int>(trace.model_id),
                static_cast<unsigned int>(trace.step_id),
                static_cast<unsigned int>(trace.context),
                static_cast<unsigned int>(trace.timestamp),
                static_cast<unsigned int>(trace.begin));
        }
    }
    static void OnDone(void *context, ai_runtime::AiFuture &base,
                       common::Error error)
    {
        auto &task = *static_cast<TaskContext *>(context);
        auto &future = static_cast<PersonFuture &>(base);
        ++App().completed_count;
        if (!error.Ok()) {
            LogStatus("person_pipeline", error);
            App().enabled.store(false);
            UAI_LOG_WARN(reinterpret_cast<const UB *>(
                "ai: person pipeline disabled; camera remains live\n"));
        }
        const common::Error released = task.memory.ReleaseInferenceBuffer(future.frame_);
        LogStatus("memory", released);
        future.occupied_.store(false, std::memory_order_release);
        App().Report(task);
    }

    common::Error Preprocess(TaskContext &task, PersonFuture &future)
    {
        auto &frame = future.frame_;
        if (!preprocess_stage_logged) {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                             "ai: person preprocess begin seq=%u buffer=%x\n"),
                         static_cast<unsigned int>(frame.capture_sequence),
                         static_cast<unsigned int>(frame.buffer.address));
        }
        if (!frame || !frame.from_pipe2 || frame.output_count < info.n_outputs ||
            frame.buffer.size < info.inputs[0].size_bytes) {
            return {common::ErrorCode::kInvalidArgument, 0U,
                    "person_pipeline.frame"};
        }
        common::Error status = model.PrepareInput(frame, task.cache);
        if (!status.Ok()) return status;
        const memory_allocator::Buffer &input = frame.source_valid
                                                    ? frame.source : frame.buffer;
        if (!input || input.size < info.inputs[0].size_bytes) {
            return {common::ErrorCode::kInvalidArgument, 0U,
                    "person_pipeline.input"};
        }
        /* Pipe2 wrote this buffer using DMA. A CPU read/invalidate here
         * must not overwrite the DMA image with dirty cache lines. */
        const memory_allocator::Buffer range{input.address,
                                              info.inputs[0].size_bytes,
                                              input.index,
                                              memory_allocator::Region::kInference};
        status = frame.source_valid ? task.cache.PrepareForPeripheralRead(range)
                                    : task.cache.PrepareForCpuRead(range);
        if (status.Ok() && !preprocess_stage_logged) {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                             "ai: person preprocess done seq=%u\n"),
                         static_cast<unsigned int>(frame.capture_sequence));
            preprocess_stage_logged = true;
        }
        return status;
    }

    common::Error Infer(PersonFuture &future)
    {
        const auto &frame = future.frame_;
        if (!infer_stage_logged) {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                             "ai: person infer begin seq=%u\n"),
                         static_cast<unsigned int>(frame.capture_sequence));
        }
        const memory_allocator::Buffer &input = frame.source_valid
                                                    ? frame.source : frame.buffer;
        npu::Status result = npu.SetInput(
            reinterpret_cast<stai_ptr>(input.address), info.inputs[0].size_bytes);
        if (!result.Ok()) return result.error;
        stai_ptr outputs[models::kMaxModelOutputs]{};
        for (std::uint16_t i = 0U; i < info.n_outputs; ++i) {
            const auto &output = frame.outputs[i];
            if (!output || output.size < info.outputs[i].size_bytes ||
                output.alignment == 0U ||
                output.address % output.alignment != 0U) {
                return {common::ErrorCode::kInvalidArgument, i,
                        "person_pipeline.output_buffer"};
            }
            outputs[i] = reinterpret_cast<stai_ptr>(output.address);
        }
        result = npu.SetOutputs(outputs, info.n_outputs);
        if (!result.Ok()) return result.error;
        result = npu.Run(); // IRQ waits and epoch continuation happen on NPU task.
        if (!result.Ok()) {
            UAI_LOG_WARN(reinterpret_cast<const UB *>(
                             "ai: person infer failed code=%u detail=%u op=%s\n"),
                         static_cast<unsigned int>(result.error.code),
                         static_cast<unsigned int>(result.error.detail),
                         result.error.operation);
            return result.error;
        }
        if (!infer_stage_logged) {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                             "ai: person infer done seq=%u\n"),
                         static_cast<unsigned int>(frame.capture_sequence));
            infer_stage_logged = true;
        }
        result = npu.NewInference();
        return result.error;
    }

    common::Error Postprocess(TaskContext &task, PersonFuture &future)
    {
        const auto &frame = future.frame_;
        models::ModelOutputView view{};
        view.count = info.n_outputs;
        for (std::uint16_t i = 0U; i < info.n_outputs; ++i) {
            const auto &output = frame.outputs[i];
            const memory_allocator::Buffer range{output.address,
                                                  info.outputs[i].size_bytes,
                                                  output.index,
                                                  memory_allocator::Region::kInference};
            common::Error status = task.cache.PrepareForCpuRead(range);
            if (!status.Ok()) return status;
            view.tensors[i] = {reinterpret_cast<const void *>(output.address),
                               {info.outputs[i].size_bytes,
                                info.outputs[i].scale.data[0],
                                info.outputs[i].zeropoint.data[0]}};
        }
        const auto &descriptor = model.GetDescriptor();
        models::InferenceGeometry geometry{};
        geometry.projection = models::InputProjection::kLetterboxed;
        geometry.frame_width = memory_allocator::kConfig.frame_width;
        geometry.frame_height = memory_allocator::kConfig.frame_height;
        geometry.model_width = descriptor.input_width;
        geometry.model_height = descriptor.input_height;
        geometry.content_height =
            (descriptor.input_width * memory_allocator::kConfig.inference_source_height +
             memory_allocator::kConfig.inference_source_width - 1U) /
            memory_allocator::kConfig.inference_source_width;
        geometry.pad_top = (geometry.model_height - geometry.content_height) / 2U;
        const models::InferenceCompletionContext decode_context{view, geometry};
        const models::ModelCallbacks callbacks = model.GetCallbacks();
        models::ModelResult decoded{};
        common::Error status = callbacks.on_inference_complete(
            decode_context, &decoded, callbacks.user_data);
        if (!status.Ok()) return status;
        memory_allocator::BoxSet boxes{};
        boxes.capture_sequence = frame.capture_sequence;
        boxes.model_sequence = ++model_sequence;
        status = model.ConvertResult(decoded, &boxes);
        if (status.Ok()) {
            ++postprocess_count;
            last_detection_count = boxes.person.count;
            last_capture_sequence = boxes.capture_sequence;
            task.SendLatestBoxes(boxes);
            if (!postprocess_stage_logged) {
                UAI_LOG_INFO(reinterpret_cast<const UB *>(
                                 "ai: person postprocess done seq=%u boxes=%u\n"),
                             static_cast<unsigned int>(boxes.capture_sequence),
                             static_cast<unsigned int>(boxes.person.count));
                postprocess_stage_logged = true;
            }
        }
        Report(task);
        return status;
    }
};

std::uint32_t PersonApplication::interrupt_state_ = 0U;
PersonApplication g_app{};
PersonApplication &App() { return g_app; }

ai_runtime::AiRuntimeResult PersonFuture::Evaluate()
{
    TaskContext &task = GetTaskContext();
    if (!owner_->enabled.load()) {
        return {{common::ErrorCode::kNotInitialized, 0U,
                 "person_pipeline.disabled"}, {}, false};
    }
    common::Error status{};
    switch (phase_) {
    case Phase::kPreprocess:
        status = owner_->Preprocess(task, *this);
        if (status.Ok()) {
            phase_ = Phase::kNpu;
            return {{}, {ExecutionContext::kNpu}, false};
        }
        break;
    case Phase::kNpu:
        status = owner_->Infer(*this);
        if (status.Ok()) {
            phase_ = Phase::kPostprocess;
            return {{}, {ExecutionContext::kPostprocessCpu}, false};
        }
        break;
    case Phase::kPostprocess:
        status = owner_->Postprocess(task, *this);
        return {status, {}, true};
    }
    return {status, {}, false};
}

void RunWorker(ExecutionContext lane)
{
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "ai: person worker started lane=%u\n"),
                 static_cast<unsigned int>(lane));
    ai_runtime::Dispatcher dispatcher(g_app.pipeline, lane);
    for (;;) {
        const DispatchResult result = dispatcher.RunOnce();
        if (result == DispatchResult::kIdle ||
            result == DispatchResult::kNotReady) {
            tk_dly_tsk(1U);
        }
    }
}

} // namespace

void PersonPipelineTask::FrameEntry()
{
    TaskContext &task = GetTaskContext();
    UINT pattern = 0U;
    if (tk_wai_flg(task.external_memory_ready, kExternalMemoryReady,
                   TWF_ANDW, &pattern, TMO_FEVR) != E_OK) {
        task.Halt("ai: person pipeline memory wait failed\n");
    }
    if (!task.external_nor_ready) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
            "ai: person model unavailable; camera remains live\n"));
    } else {
        const common::Error status = g_app.Initialize(task);
        if (!status.Ok()) {
            LogStatus("person_pipeline.init", status);
        } else {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                "ai: person pipeline enabled (pre/npu/post)\n"));
            task.StartPersonPreprocessTask(
                reinterpret_cast<FP>(PersonPipelineTask::PreprocessEntry));
            task.StartPersonNpuTask(reinterpret_cast<FP>(PersonPipelineTask::NpuEntry));
            task.StartPersonPostprocessTask(
                reinterpret_cast<FP>(PersonPipelineTask::PostprocessEntry));
        }
    }
    for (;;) {
        InferenceMessage message{};
        if (tk_rcv_mbf(task.frame_queue, &message, TMO_FEVR) !=
            static_cast<INT>(sizeof(message))) continue;
        common::Error status = task.memory.ClaimInferenceBuffer(message.frame);
        if (!status.Ok()) {
            LogStatus("memory", status);
            continue;
        }
        PersonFuture *available = nullptr;
        if (g_app.enabled.load()) {
            for (PersonFuture &future : g_app.futures) {
                bool expected = false;
                if (future.occupied_.compare_exchange_strong(expected, true)) {
                    available = &future;
                    break;
                }
            }
        }
        if (available != nullptr) {
            available->Reset(g_app, message.frame);
            status = g_app.scheduler.Submit(*available);
            if (status.Ok()) {
                ++g_app.submitted_count;
                if (!g_app.submit_stage_logged) {
                    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                                     "ai: person submit ok seq=%u\n"),
                                 static_cast<unsigned int>(message.frame.capture_sequence));
                    g_app.submit_stage_logged = true;
                }
                g_app.Report(task);
                continue;
            }
            available->occupied_.store(false, std::memory_order_release);
        }
        const common::Error released = task.memory.ReleaseInferenceBuffer(message.frame);
        LogStatus("memory", released);
    }
}

void PersonPipelineTask::PreprocessEntry()
{
    RunWorker(ExecutionContext::kPreprocessCpu);
}

void PersonPipelineTask::NpuEntry() { RunWorker(ExecutionContext::kNpu); }
void PersonPipelineTask::PostprocessEntry()
{
    RunWorker(ExecutionContext::kPostprocessCpu);
}

} // namespace uai::ai::task
