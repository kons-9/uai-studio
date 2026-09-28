#include "models/segmentation/model.hpp"
#include "models/segmentation/segmentation_decoder.hpp"

extern "C" {
stai_return_code segmentation_model_initialize(void);
stai_return_code segmentation_model_shutdown(void);
stai_return_code segmentation_model_get_info(stai_network_info *info);
stai_return_code segmentation_model_get_inputs(stai_ptr *inputs,
                                                stai_size *count);
stai_return_code segmentation_model_set_input(stai_ptr input, stai_size size);
stai_return_code segmentation_model_get_outputs(stai_ptr *outputs,
                                                 stai_size *count);
stai_return_code segmentation_model_set_outputs(const stai_ptr *outputs,
                                                 stai_size count);
stai_return_code segmentation_model_run(stai_run_mode mode);
stai_return_code segmentation_model_continue_run(void);
stai_return_code segmentation_model_wait_for_event(void);
stai_return_code segmentation_model_get_run_status(void);
stai_return_code segmentation_model_new_inference(void);
}

namespace uai::ai::models::segmentation {

namespace {

Decoder &SegmentationDecoderInstance()
{
    static Decoder decoder;
    return decoder;
}

common::Error ConfigureSegmentationDecoder(const ModelOutputSpec &spec,
                                           void *user_data)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "segmentation.decoder.context"};
    }
    return static_cast<Decoder *>(user_data)->Initialize(spec);
}

common::Error CompleteSegmentationInference(
    const InferenceCompletionContext &context, ModelResult *result,
    void *user_data)
{
    if (user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "segmentation.decoder.context"};
    }
    return static_cast<Decoder *>(user_data)->Decode(context, result);
}

} // namespace

const ModelDescriptor &Model::Descriptor()
{
    static constexpr ModelDescriptor kDescriptor{
        ModelKind::kSegmentation, "segmentation", 320U, 320U};
    return kDescriptor;
}

const ModelDescriptor &Model::GetDescriptor() const
{
    return Descriptor();
}

ModelCallbacks Model::GetCallbacks() const
{
    return {ConfigureSegmentationDecoder, CompleteSegmentationInference,
            &SegmentationDecoderInstance()};
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

stai_return_code Model::GetInputs(stai_ptr *inputs, stai_size *count)
{
    return segmentation_model_get_inputs(inputs, count);
}

stai_return_code Model::SetInput(stai_ptr input, stai_size size)
{
    return segmentation_model_set_input(input, size);
}

stai_return_code Model::GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return segmentation_model_get_outputs(outputs, count);
}

stai_return_code Model::SetOutputs(const stai_ptr *outputs, stai_size count)
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

} // namespace uai::ai::models::segmentation
