#include "models/face/model.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "models/face/face_decoder.hpp"

extern "C" {
stai_return_code face_model_initialize(void);
stai_return_code face_model_shutdown(void);
stai_return_code face_model_get_info(stai_network_info *info);
stai_return_code face_model_get_inputs(stai_ptr *inputs, stai_size *count);
stai_return_code face_model_set_input(stai_ptr input, stai_size size);
stai_return_code face_model_get_outputs(stai_ptr *outputs, stai_size *count);
stai_return_code face_model_set_outputs(const stai_ptr *outputs,
                                        stai_size count);
stai_return_code face_model_run(stai_run_mode mode);
stai_return_code face_model_continue_run(void);
stai_return_code face_model_wait_for_event(void);
stai_return_code face_model_get_run_status(void);
stai_return_code face_model_new_inference(void);
}

namespace uai::ai::models::face {

namespace {

Decoder &FaceDecoderInstance()
{
    static Decoder decoder;
    return decoder;
}

common::Error ConfigureFaceDecoder(const ModelOutputSpec &spec,
                                   void *user_data)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "face.decoder.context"};
    }
    return static_cast<Decoder *>(user_data)->Initialize(spec);
}

common::Error CompleteFaceInference(const InferenceCompletionContext &context,
                                    ModelResult *result, void *user_data)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "face.decoder.context"};
    }
    return static_cast<const Decoder *>(user_data)->Decode(context, result);
}

} // namespace

const ModelDescriptor &Model::Descriptor()
{
    static constexpr ModelDescriptor kDescriptor{
        ModelKind::kFace, "face", 128U, 128U};
    return kDescriptor;
}

const ModelDescriptor &Model::GetDescriptor() const
{
    return Descriptor();
}

ModelCallbacks Model::GetCallbacks() const
{
    return {ConfigureFaceDecoder, CompleteFaceInference,
            &FaceDecoderInstance()};
}

common::Error Model::PrepareInput(memory_allocator::InferenceFrame &frame,
                                  cache::CacheDriver &cache) const
{
    const ModelDescriptor &descriptor = GetDescriptor();
    return PreparePipe2LetterboxedInput(
        frame, descriptor.input_width, descriptor.input_height, cache);
}

namespace {

std::int16_t ClampCoordinate(float value, std::int32_t limit)
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

common::Error Model::ConvertResult(const ModelResult &source,
                                   memory_allocator::BoxSet *destination) const
{
    if (destination == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "face.model.result_destination"};
    }
    if (!source.detections_valid) {
        return {common::ErrorCode::kModel, 0U, "face.model.result"};
    }

    destination->face = {};
    destination->face.count = source.detection_count <
                                      memory_allocator::kConfig.max_boxes
                                  ? source.detection_count
                                  : memory_allocator::kConfig.max_boxes;
    for (std::uint32_t i = 0U; i < destination->face.count; ++i) {
        const Detection &detection = source.detections[i];
        destination->face.boxes[i].x =
            ClampCoordinate(detection.x, memory_allocator::kConfig.frame_width);
        destination->face.boxes[i].y =
            ClampCoordinate(detection.y,
                            memory_allocator::kConfig.frame_height);
        destination->face.boxes[i].width =
            ClampCoordinate(detection.width,
                            memory_allocator::kConfig.frame_width);
        destination->face.boxes[i].height =
            ClampCoordinate(detection.height,
                            memory_allocator::kConfig.frame_height);
        destination->face.boxes[i].confidence = detection.confidence;
    }
    destination->face_valid = true;
    return {common::ErrorCode::kOk, destination->face.count,
            "face.model.result"};
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

stai_return_code Model::GetInputs(stai_ptr *inputs, stai_size *count)
{
    return face_model_get_inputs(inputs, count);
}

stai_return_code Model::SetInput(stai_ptr input, stai_size size)
{
    return face_model_set_input(input, size);
}

stai_return_code Model::GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return face_model_get_outputs(outputs, count);
}

stai_return_code Model::SetOutputs(const stai_ptr *outputs, stai_size count)
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

} // namespace uai::ai::models::face
