#include "models/face/model.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "models/face/face_decoder.hpp"

extern "C" {
stai_return_code face_model_initialize(void);
stai_return_code face_model_shutdown(void);
stai_return_code face_model_get_info(stai_network_info *info);
stai_return_code face_model_get_inputs(
    stai_ptr *inputs,
    stai_size *count
);
stai_return_code face_model_set_input(
    stai_ptr input,
    stai_size size
);
stai_return_code face_model_get_outputs(
    stai_ptr *outputs,
    stai_size *count
);
stai_return_code face_model_set_outputs(
    const stai_ptr *outputs,
    stai_size count
);
stai_return_code face_model_run(stai_run_mode mode);
stai_return_code face_model_continue_run(void);
stai_return_code face_model_wait_for_event(void);
stai_return_code face_model_get_run_status(void);
stai_return_code face_model_new_inference(void);
stai_return_code face_model_set_epoch_trace_callback(
    ::uai::ai::models::EpochTraceCallback callback,
    void *context
);
}

namespace uai::ai::models::face {

namespace {

constexpr ModelStageDescriptor kFaceStages[] = {
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
     "decode face tensors"},
    {ModelStageId::kConvert,
     "convert",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "convert detections to application boxes"},
    {ModelStageId::kFinalize,
     "finalize",
     ModelStageLocation::kInferenceTaskCpu,
     ModelStageOverlap::kSerial,
     "reset the generated runtime for the next inference"},
};

Decoder &FaceDecoderInstance()
{
    static Decoder decoder;
    return decoder;
}

common::Error ConfigureFaceDecoder(
    const ModelOutputSpec &spec,
    void *user_data
)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "face.decoder.context"};
    }
    return static_cast<Decoder *>(user_data)->Initialize(spec);
}

common::Error CompleteFaceInference(
    const InferenceCompletionContext &context,
    ModelResult *result,
    void *user_data
)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "face.decoder.context"};
    }
    return static_cast<const Decoder *>(user_data)->Decode(context, result);
}

} // namespace

const ModelDescriptor &Model::Descriptor()
{
    static constexpr ModelDescriptor kDescriptor{ModelKind::kFace, "face", 128U, 128U};
    return kDescriptor;
}

const ModelDescriptor &Model::GetDescriptor() const
{
    return Descriptor();
}

const ModelPipeline &Model::GetPipeline() const
{
    static constexpr ModelPipeline kPipeline{kFaceStages, sizeof(kFaceStages) / sizeof(kFaceStages[0])};
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
    return {ConfigureFaceDecoder, CompleteFaceInference, &FaceDecoderInstance()};
}

common::Error Model::PrepareInput(
    memory_allocator::InferenceFrame &frame,
    cache::CacheDriver &cache
) const
{
    const ModelDescriptor &descriptor = GetDescriptor();
    return PreparePipe2LetterboxedInput(frame, descriptor.input_width, descriptor.input_height, cache);
}

namespace {

std::int16_t ClampCoordinate(
    float value,
    std::int32_t limit
)
{
    if (value <= 0.0F) {
        return 0;
    }
    if (value >= static_cast<float>(limit)) {
        return static_cast<std::int16_t>(limit);
    }
    return static_cast<std::int16_t>(value);
}

} // namespace

common::Error Model::ConvertResult(
    const ModelResult &source,
    memory_allocator::BoxSet *destination
) const
{
    if (destination == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "face.model.result_destination"};
    }
    if (!source.detections_valid) {
        return {common::ErrorCode::kModel, 0U, "face.model.result"};
    }

    destination->face = {};
    destination->face.count = source.detection_count < memory_allocator::kConfig.max_boxes
        ? source.detection_count
        : memory_allocator::kConfig.max_boxes;
    for (std::uint32_t i = 0U; i < destination->face.count; ++i) {
        const Detection &detection = source.detections[i];
        destination->face.boxes[i].x = ClampCoordinate(detection.x, memory_allocator::kConfig.frame_width);
        destination->face.boxes[i].y = ClampCoordinate(detection.y, memory_allocator::kConfig.frame_height);
        destination->face.boxes[i].width = ClampCoordinate(detection.width, memory_allocator::kConfig.frame_width);
        destination->face.boxes[i].height = ClampCoordinate(detection.height, memory_allocator::kConfig.frame_height);
        destination->face.boxes[i].confidence = detection.confidence;
    }
    destination->face_valid = true;
    return {common::ErrorCode::kOk, destination->face.count, "face.model.result"};
}

::uai::ai::models::ModelRuntime &Runtime(Model &model)
{
    return model;
}

stai_return_code Model::Initialize()
{
    return face_model_initialize();
}

stai_return_code Model::Shutdown()
{
    return face_model_shutdown();
}

stai_return_code Model::GetInfo(stai_network_info *info)
{
    return face_model_get_info(info);
}

stai_return_code Model::GetInputs(
    stai_ptr *inputs,
    stai_size *count
)
{
    return face_model_get_inputs(inputs, count);
}

stai_return_code Model::SetInput(
    stai_ptr input,
    stai_size size
)
{
    return face_model_set_input(input, size);
}

stai_return_code Model::GetOutputs(
    stai_ptr *outputs,
    stai_size *count
)
{
    return face_model_get_outputs(outputs, count);
}

stai_return_code Model::SetOutputs(
    const stai_ptr *outputs,
    stai_size count
)
{
    return face_model_set_outputs(outputs, count);
}

stai_return_code Model::Run(stai_run_mode mode)
{
    return face_model_run(mode);
}

stai_return_code Model::ContinueRun()
{
    return face_model_continue_run();
}

stai_return_code Model::WaitForEvent()
{
    return face_model_wait_for_event();
}

stai_return_code Model::GetRunStatus()
{
    return face_model_get_run_status();
}

stai_return_code Model::NewInference()
{
    return face_model_new_inference();
}

stai_return_code Model::SetEpochTraceCallback(
    EpochTraceCallback callback,
    void *context
)
{
    return face_model_set_epoch_trace_callback(callback, context);
}

} // namespace uai::ai::models::face
