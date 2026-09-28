#include "model_manager/model/segmentation/model_segmentation_adapter.hpp"

extern "C" {
stai_return_code segmentation_model_initialize(void);
stai_return_code segmentation_model_shutdown(void);
stai_return_code segmentation_model_get_info(stai_network_info *info);
stai_return_code segmentation_model_get_inputs(stai_ptr *inputs,
                                                stai_size *count);
stai_return_code segmentation_model_set_input(stai_ptr input, stai_size size);
stai_return_code segmentation_model_get_outputs(stai_ptr *outputs,
                                                 stai_size *count);
stai_return_code segmentation_model_run(stai_run_mode mode);
stai_return_code segmentation_model_continue_run(void);
stai_return_code segmentation_model_wait_for_event(void);
stai_return_code segmentation_model_get_run_status(void);
stai_return_code segmentation_model_new_inference(void);
}

namespace uai::ai::model_manager {

stai_return_code SegmentationModelAdapter::Initialize()
{
    return segmentation_model_initialize();
}

stai_return_code SegmentationModelAdapter::Shutdown()
{
    return segmentation_model_shutdown();
}

stai_return_code SegmentationModelAdapter::GetInfo(stai_network_info *info)
{
    return segmentation_model_get_info(info);
}

stai_return_code SegmentationModelAdapter::GetInputs(stai_ptr *inputs,
                                                      stai_size *count)
{
    return segmentation_model_get_inputs(inputs, count);
}

stai_return_code SegmentationModelAdapter::SetInput(stai_ptr input,
                                                     stai_size size)
{
    return segmentation_model_set_input(input, size);
}

stai_return_code SegmentationModelAdapter::GetOutputs(stai_ptr *outputs,
                                                      stai_size *count)
{
    return segmentation_model_get_outputs(outputs, count);
}

stai_return_code SegmentationModelAdapter::Run(stai_run_mode mode)
{
    return segmentation_model_run(mode);
}

stai_return_code SegmentationModelAdapter::ContinueRun()
{
    return segmentation_model_continue_run();
}

stai_return_code SegmentationModelAdapter::WaitForEvent()
{
    return segmentation_model_wait_for_event();
}

stai_return_code SegmentationModelAdapter::GetRunStatus()
{
    return segmentation_model_get_run_status();
}

stai_return_code SegmentationModelAdapter::NewInference()
{
    return segmentation_model_new_inference();
}

} // namespace uai::ai::model_manager
