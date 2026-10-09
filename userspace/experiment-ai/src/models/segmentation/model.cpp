#include "models/segmentation/model.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "models/segmentation/segmentation_decoder.hpp"

extern "C" {
stai_return_code segmentation_model_initialize(void);
stai_return_code segmentation_model_shutdown(void);
stai_return_code segmentation_model_get_info(stai_network_info *info);
stai_return_code segmentation_model_get_inputs(
    stai_ptr *inputs,
    stai_size *count
);
stai_return_code segmentation_model_set_input(
    stai_ptr input,
    stai_size size
);
stai_return_code segmentation_model_get_outputs(
    stai_ptr *outputs,
    stai_size *count
);
stai_return_code segmentation_model_set_outputs(
    const stai_ptr *outputs,
    stai_size count
);
stai_return_code segmentation_model_run(stai_run_mode mode);
stai_return_code segmentation_model_continue_run(void);
stai_return_code segmentation_model_wait_for_event(void);
stai_return_code segmentation_model_get_run_status(void);
stai_return_code segmentation_model_new_inference(void);
stai_return_code segmentation_model_set_epoch_trace_callback(
    ::uai::ai::models::EpochTraceCallback callback,
    void *context
);
}

namespace uai::ai::models::segmentation {

namespace {

constexpr ModelStageDescriptor kSegmentationStages[] = {
    {ModelStageId::kCopy,
     "copy",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kMayOverlap,
     "copy live Pipe2 rows to scratch"},
    {ModelStageId::kResize,
     "resize",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kMayOverlap,
     "resize RGB888 content to model size"},
    {ModelStageId::kLetterbox,
     "letterbox",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kMayOverlap,
     "fill model input padding"},
    {ModelStageId::kInputCache,
     "input_cache",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "clean the CPU-produced input for the NPU"},
    {ModelStageId::kSubmit,
     "submit",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "start the asynchronous ST.AI run"},
    {ModelStageId::kIrqWait,
     "irq_wait",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "wait for the NPU event and inspect ST.AI status"},
    {ModelStageId::kEpochContinue,
     "epoch_continue",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "continue the next ST.AI epoch after an IRQ"},
    {ModelStageId::kOutputCache,
     "output_cache",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "make NPU output tensors visible to the CPU"},
    {ModelStageId::kDecode,
     "decode",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "decode segmentation tensors"},
    {ModelStageId::kConvert,
     "convert",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "convert the decoded mask"},
    {ModelStageId::kFinalize,
     "finalize",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "reset the generated runtime for the next inference"},
};

Decoder &SegmentationDecoderInstance()
{
    static Decoder decoder;
    return decoder;
}

common::Error ConfigureSegmentationDecoder(
    const ModelOutputSpec &spec,
    void *user_data
)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "segmentation.decoder.context"};
    }
    return static_cast<Decoder *>(user_data)->Initialize(spec);
}

common::Error CompleteSegmentationInference(
    const InferenceCompletionContext &context,
    ModelResult *result,
    void *user_data
)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "segmentation.decoder.context"};
    }
    return static_cast<Decoder *>(user_data)->Decode(context, result);
}

} // namespace

const ModelDescriptor &Model::Descriptor()
{
    static constexpr ModelDescriptor kDescriptor{ModelKind::kSegmentation, "segmentation", 320U, 320U};
    return kDescriptor;
}

const ModelDescriptor &Model::GetDescriptor() const
{
    return Descriptor();
}

const ModelPipeline &Model::GetPipeline() const
{
    static constexpr ModelPipeline kPipeline{
        kSegmentationStages, sizeof(kSegmentationStages) / sizeof(kSegmentationStages[0])
    };
    return kPipeline;
}

common::Error Model::ExecuteStage(
    ModelStageId stage,
    ModelStageContext &context
) const
{
    return ExecutePipe2InputStage(stage, context, Descriptor().input_width, Descriptor().input_height);
}

ModelCallbacks Model::GetCallbacks() const
{
    return {ConfigureSegmentationDecoder, CompleteSegmentationInference, &SegmentationDecoderInstance()};
}

common::Error Model::PrepareInput(
    memory_allocator::InferenceFrame &frame,
    cache::CacheDriver &cache
) const
{
    const ModelDescriptor &descriptor = GetDescriptor();
    return PreparePipe2LetterboxedInput(frame, descriptor.input_width, descriptor.input_height, cache);
}

common::Error Model::ConvertResult(
    const ModelResult &source,
    memory_allocator::BoxSet *destination
) const
{
    if (destination == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "segmentation.model.result_destination"};
    }
    if (!source.segmentation_valid) {
        return {common::ErrorCode::kModel, 0U, "segmentation.model.result"};
    }

    destination->segmentation = {};
    destination->segmentation.mask_address = source.segmentation.mask_address;
    destination->segmentation.mask_width = source.segmentation.mask_width;
    destination->segmentation.mask_height = source.segmentation.mask_height;
    destination->segmentation.mask_foreground_pixels = source.segmentation.mask_foreground_pixels;
    destination->segmentation_valid = true;
    return {common::ErrorCode::kOk, 0U, "segmentation.model.result"};
}

::uai::ai::models::ModelRuntime &Runtime(Model &model)
{
    return model;
}

stai_return_code Model::Initialize()
{
    return segmentation_model_initialize();
}

stai_return_code Model::Shutdown()
{
    return segmentation_model_shutdown();
}

stai_return_code Model::GetInfo(stai_network_info *info)
{
    return segmentation_model_get_info(info);
}

stai_return_code Model::GetInputs(
    stai_ptr *inputs,
    stai_size *count
)
{
    return segmentation_model_get_inputs(inputs, count);
}

stai_return_code Model::SetInput(
    stai_ptr input,
    stai_size size
)
{
    return segmentation_model_set_input(input, size);
}

stai_return_code Model::GetOutputs(
    stai_ptr *outputs,
    stai_size *count
)
{
    return segmentation_model_get_outputs(outputs, count);
}

stai_return_code Model::SetOutputs(
    const stai_ptr *outputs,
    stai_size count
)
{
    return segmentation_model_set_outputs(outputs, count);
}

stai_return_code Model::Run(stai_run_mode mode)
{
    return segmentation_model_run(mode);
}

stai_return_code Model::ContinueRun()
{
    return segmentation_model_continue_run();
}

stai_return_code Model::WaitForEvent()
{
    return segmentation_model_wait_for_event();
}

stai_return_code Model::GetRunStatus()
{
    return segmentation_model_get_run_status();
}

stai_return_code Model::NewInference()
{
    return segmentation_model_new_inference();
}

stai_return_code Model::SetEpochTraceCallback(
    EpochTraceCallback callback,
    void *context
)
{
    return segmentation_model_set_epoch_trace_callback(callback, context);
}

} // namespace uai::ai::models::segmentation
