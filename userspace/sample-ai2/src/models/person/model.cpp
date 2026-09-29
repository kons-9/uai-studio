#include "models/person/model.hpp"
#include "models/person/person_decoder.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"

extern "C" {
stai_return_code person_model_initialize(void);
stai_return_code person_model_shutdown(void);
stai_return_code person_model_get_info(stai_network_info *info);
stai_return_code person_model_get_inputs(stai_ptr *inputs, stai_size *count);
stai_return_code person_model_set_input(stai_ptr input, stai_size size);
stai_return_code person_model_get_outputs(stai_ptr *outputs, stai_size *count);
stai_return_code person_model_set_outputs(const stai_ptr *outputs,
                                          stai_size count);
stai_return_code person_model_run(stai_run_mode mode);
stai_return_code person_model_continue_run(void);
stai_return_code person_model_wait_for_event(void);
stai_return_code person_model_get_run_status(void);
stai_return_code person_model_new_inference(void);
stai_return_code person_model_set_epoch_trace_callback(
    ::uai::ai::models::EpochTraceCallback callback, void *context);
}

namespace uai::ai::models::person {

namespace {

Decoder &PersonDecoderInstance()
{
    static Decoder decoder;
    return decoder;
}

common::Error ConfigurePersonDecoder(const ModelOutputSpec &spec,
                                     void *user_data)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "person.decoder.context"};
    }
    return static_cast<Decoder *>(user_data)->Initialize(spec);
}

common::Error CompletePersonInference(const InferenceCompletionContext &context,
                                      ModelResult *result, void *user_data)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "person.decoder.context"};
    }
    return static_cast<const Decoder *>(user_data)->Decode(context, result);
}

} // namespace

const ModelDescriptor &Model::Descriptor()
{
    static constexpr ModelDescriptor kDescriptor{
        ModelKind::kPerson, "person", 480U, 480U};
    return kDescriptor;
}

const ModelDescriptor &Model::GetDescriptor() const
{
    return Descriptor();
}

ModelCallbacks Model::GetCallbacks() const
{
    return {ConfigurePersonDecoder, CompletePersonInference,
            &PersonDecoderInstance()};
}

common::Error Model::PrepareInput(memory_allocator::InferenceFrame &frame,
                                  cache::CacheDriver &cache) const
{
    (void)cache;
    if (!frame.from_pipe2) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.model_input.invalid_pipe2_frame"};
    }
    frame.input_prepared_by_cpu = false;
    return {common::ErrorCode::kOk, 0U, "ai.model_input.direct_pipe2"};
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
                "person.model.result_destination"};
    }
    if (!source.detections_valid) {
        return {common::ErrorCode::kModel, 0U, "person.model.result"};
    }

    destination->person = {};
    destination->person.count = source.detection_count <
                                        memory_allocator::kConfig.max_boxes
                                    ? source.detection_count
                                    : memory_allocator::kConfig.max_boxes;
    for (std::uint32_t i = 0U; i < destination->person.count; ++i) {
        const Detection &detection = source.detections[i];
        destination->person.boxes[i].x =
            ClampCoordinate(detection.x, memory_allocator::kConfig.frame_width);
        destination->person.boxes[i].y =
            ClampCoordinate(detection.y,
                            memory_allocator::kConfig.frame_height);
        destination->person.boxes[i].width =
            ClampCoordinate(detection.width,
                            memory_allocator::kConfig.frame_width);
        destination->person.boxes[i].height =
            ClampCoordinate(detection.height,
                            memory_allocator::kConfig.frame_height);
        destination->person.boxes[i].confidence = detection.confidence;
    }
    destination->person_valid = true;
    return {common::ErrorCode::kOk, destination->person.count,
            "person.model.result"};
}

::uai::ai::models::ModelRuntime &Runtime(Model &model)
{
    return model;
}

stai_return_code Model::Initialize()
{
    return person_model_initialize();
}

stai_return_code Model::Shutdown()
{
    return person_model_shutdown();
}

stai_return_code Model::GetInfo(stai_network_info *info)
{
    return person_model_get_info(info);
}

stai_return_code Model::GetInputs(stai_ptr *inputs, stai_size *count)
{
    return person_model_get_inputs(inputs, count);
}

stai_return_code Model::SetInput(stai_ptr input, stai_size size)
{
    return person_model_set_input(input, size);
}

stai_return_code Model::GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return person_model_get_outputs(outputs, count);
}

stai_return_code Model::SetOutputs(const stai_ptr *outputs, stai_size count)
{
    return person_model_set_outputs(outputs, count);
}

stai_return_code Model::Run(stai_run_mode mode)
{
    return person_model_run(mode);
}

stai_return_code Model::ContinueRun()
{
    return person_model_continue_run();
}

stai_return_code Model::WaitForEvent()
{
    return person_model_wait_for_event();
}

stai_return_code Model::GetRunStatus()
{
    return person_model_get_run_status();
}

stai_return_code Model::NewInference()
{
    return person_model_new_inference();
}

stai_return_code Model::SetEpochTraceCallback(EpochTraceCallback callback,
                                              void *context)
{
    return person_model_set_epoch_trace_callback(callback, context);
}

} // namespace uai::ai::models::person
