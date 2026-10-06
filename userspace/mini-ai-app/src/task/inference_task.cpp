#include "task/inference_task.hpp"

#include <tk/tkernel.h>

#include "app_context.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/buffer/buffer_types.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/memory/generated/memory_config.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "middleware/task/task.hpp"
#include "model/person_decoder.hpp"
#include "model/person_model.hpp"

namespace uai::ai::mini {

namespace {

constexpr std::size_t kMaxOutputs =
    memory_manager::kMemoryConfig.model_output_bytes.size();

/* The network must take one 480x480 RGB888 input and produce outputs that
 * fit the per-slot output areas reserved by the memory layout. */
common::Error ValidateModelInfo(const stai_network_info &info,
                                npu::NpuManagement &npu)
{
    if (info.n_inputs != 1U || info.inputs == nullptr ||
        info.inputs[0].size_bytes != PersonDecoder::InputBytes() ||
        info.n_outputs != PersonDecoder::kOutputCount ||
        info.n_outputs > kMaxOutputs || info.outputs == nullptr) {
        return {common::ErrorCode::kModel};
    }
    for (std::uint16_t i = 0U; i < info.n_outputs; ++i) {
        if (info.outputs[i].size_bytes >
            memory_manager::kMemoryConfig.model_output_bytes[i]) {
            return {common::ErrorCode::kModel};
        }
    }
    /* Generated with --no-outputs-allocation: the application supplies the
     * output buffers, so the network must not report its own. */
    stai_ptr outputs[kMaxOutputs]{};
    stai_size count = 0U;
    const npu::Status status = npu.GetOutputs(outputs, &count);
    if (!status.Ok()) return status.error;
    if (count != info.n_outputs) return {common::ErrorCode::kModel};
    for (std::uint16_t i = 0U; i < count; ++i) {
        if (outputs[i] != nullptr) return {common::ErrorCode::kModel};
    }
    return {};
}

struct Inference {
    AppContext &app;
    npu::NpuManagement::Accessor &npu;
    const stai_network_info &info;
    PersonDecoder &decoder;
    std::uint32_t sequence = 0U;
    std::uint32_t completed = 0U;
    std::uint32_t report_tick = 0U;
    std::uint32_t report_completed = 0U;

    common::Error RunOne(const pipeline::InferenceFrame &frame)
    {
        /* Pipe2 wrote the input with DMA; drop any stale CPU cache lines
         * before the NPU (and later the CPU) read the range. */
        const buffer::Buffer input{frame.buffer.address,
                                   info.inputs[0].size_bytes,
                                   frame.buffer.index,
                                   buffer::Region::kInference};
        common::Error status = app.cache.PrepareForCpuRead(input);
        if (!status.Ok()) return status;

        npu::NpuDriver &driver = *npu.Get();
        const auto &writer = npu.Ownership();
        npu::Status result = driver.SetInput(
            reinterpret_cast<stai_ptr>(input.address), input.size, writer);
        if (!result.Ok()) return result.error;

        stai_ptr outputs[kMaxOutputs]{};
        for (std::uint16_t i = 0U; i < info.n_outputs; ++i) {
            const buffer::Buffer &output = frame.outputs[i];
            if (!output || output.size < info.outputs[i].size_bytes) {
                return {common::ErrorCode::kInvalidArgument};
            }
            outputs[i] = reinterpret_cast<stai_ptr>(output.address);
        }
        result = driver.SetOutputs(outputs, info.n_outputs, writer);
        if (!result.Ok()) return result.error;

        /* Blocks until the Neural-ART interrupt reports completion. */
        result = driver.Run(writer);
        if (!result.Ok()) return result.error;
        result = driver.NewInference(writer);
        if (!result.Ok()) return result.error;

        const void *views[kMaxOutputs]{};
        for (std::uint16_t i = 0U; i < info.n_outputs; ++i) {
            const buffer::Buffer range{frame.outputs[i].address,
                                       info.outputs[i].size_bytes,
                                       frame.outputs[i].index,
                                       buffer::Region::kInference};
            status = app.cache.PrepareForCpuRead(range);
            if (!status.Ok()) return status;
            views[i] = reinterpret_cast<const void *>(range.address);
        }

        inference::BoxSet boxes{};
        status = decoder.Decode(views, info.n_outputs, &boxes);
        if (!status.Ok()) return status;
        boxes.model_sequence = ++sequence;
        boxes.capture_sequence = frame.capture_sequence;
        ++completed;
        if (completed == 1U) {
            UAI_LOG_INFO("mini: first inference seq=%u boxes=%u\n",
                         static_cast<unsigned int>(frame.capture_sequence),
                         static_cast<unsigned int>(boxes.person.count));
        }
        return app.results.Publish(boxes);
    }

    void Report()
    {
        const std::uint32_t now = common::Task::Now();
        if (report_tick == 0U) {
            report_tick = now;
            return;
        }
        if (now - report_tick < 1000U) return;
        const camera::Diagnostics camera = app.camera.GetDiagnostics();
        UAI_LOG_INFO("mini: inference/s=%u pipe2=%u drops=%u csi_errors=%u\n",
                     static_cast<unsigned int>(completed - report_completed),
                     static_cast<unsigned int>(camera.pipe2_frame_event_count),
                     static_cast<unsigned int>(camera.pipe2_drop_count),
                     static_cast<unsigned int>(camera.csi_error_count));
        report_tick = now;
        report_completed = completed;
    }
};

} // namespace

void InferenceTask::Start(
    middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_,
                        kInferenceTaskPriority, "inference");
}

void InferenceTask::Entry() { Instance().Run(); }

void InferenceTask::Run()
{
    AppContext &app = App();

    UINT pattern = 0U;
    if (tk_wai_flg(app.external_memory_ready, kExternalMemoryReady, TWF_ANDW,
                   &pattern, TMO_FEVR) != E_OK) {
        common::Task::Halt("mini: external memory wait failed\n");
    }
    if (!app.external_nor_ready) {
        UAI_LOG_WARN("mini: model unavailable; camera remains live\n");
        for (;;) tk_slp_tsk(TMO_FEVR);
    }

    static PersonModel model;
    static PersonDecoder decoder;
    static stai_network_info info{};
    npu::NpuManagement &npu = npu::NpuManagement::Instance();

    npu::Status npu_status = npu.Initialize(model);
    if (!npu_status.Ok()) {
        npu_status.error.LogStatus("npu.init");
        common::Task::Halt("mini: NPU initialization failed\n");
    }
    npu_status = npu.GetInfo(&info);
    if (!npu_status.Ok()) {
        npu_status.error.LogStatus("npu.info");
        common::Task::Halt("mini: model info unavailable\n");
    }
    common::Error status = ValidateModelInfo(info, npu);
    if (!status.Ok()) {
        status.LogStatus("model");
        common::Task::Halt("mini: model does not match this application\n");
    }
    status = decoder.Configure(info);
    if (!status.Ok()) {
        status.LogStatus("decoder");
        common::Task::Halt("mini: decoder configuration failed\n");
    }
    UAI_LOG_INFO("mini: model ready input=%u bytes outputs=%u\n",
                 static_cast<unsigned int>(info.inputs[0].size_bytes),
                 static_cast<unsigned int>(info.n_outputs));

    /* This task is the only NPU user, so it keeps the ownership token for
     * its whole lifetime instead of taking it per call. */
    static npu::NpuManagement::Accessor npu_accessor;
    status = npu.Acquire(&npu_accessor);
    if (!status.Ok()) {
        status.LogStatus("npu.acquire");
        common::Task::Halt("mini: NPU ownership failed\n");
    }

    static Inference inference{app, npu_accessor, info, decoder};
    static pipeline::InferenceFrame frame{};

    common::Task::RunForever(app.cpu_task_monitor, "inference", [&] {
        for (;;) {
            frame = {};
            const common::Error receive = app.frames.Receive(&frame);
            if (receive.Ok()) return;
            if (receive.Code() != common::ErrorCode::kNoFrame) {
                receive.LogStatus("frames");
                common::Task::Halt("mini: frame receive failed\n");
            }
        }
    }, [&] {
        /* Claim fails when the camera already recycled this slot; the frame
         * is then stale and nothing is owed to the memory manager. */
        status = app.memory.ClaimInferenceBuffer(frame);
        if (!status.Ok()) {
            status.LogStatus("memory");
            return;
        }
        status = inference.RunOne(frame);
        if (!status.Ok()) status.LogStatus("inference");
        app.memory.ReleaseInferenceBuffer(frame).LogStatus("memory");
        inference.Report();
    });
}

} // namespace uai::ai::mini
