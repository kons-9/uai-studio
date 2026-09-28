#include "models/segmentation/model.hpp"

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

const ModelDescriptor &Model::Descriptor()
{
    static constexpr ModelDescriptor kDescriptor{
        ModelKind::kSegmentation, "segmentation", 320U, 320U, 1U,
        {320U * 320U * 2U, 0U, 0U, 0U}, true};
    return kDescriptor;
}

const ModelDescriptor &Model::GetDescriptor() const
{
    return Descriptor();
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
