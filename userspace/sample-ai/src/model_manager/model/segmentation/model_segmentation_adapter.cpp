#include "model_manager/model/segmentation/model_segmentation_adapter.hpp"

extern "C" {
#include "model_manager/model/segmentation/model_segmentation_c_api.h"
}

namespace uai::ai::model_manager {

stai_return_code SegmentationModelAdapter::Initialize()
{
    return segmentation_model_c_api.init();
}

stai_return_code SegmentationModelAdapter::Shutdown()
{
    return segmentation_model_c_api.deinit();
}

stai_return_code SegmentationModelAdapter::GetInfo(stai_network_info *info)
{
    return segmentation_model_c_api.get_info(info);
}

stai_return_code SegmentationModelAdapter::GetInputs(stai_ptr *inputs,
                                                      stai_size *count)
{
    return segmentation_model_c_api.get_inputs(inputs, count);
}

stai_return_code SegmentationModelAdapter::SetInput(stai_ptr input,
                                                     stai_size size)
{
    return segmentation_model_c_api.set_input(input, size);
}

stai_return_code SegmentationModelAdapter::GetOutputs(stai_ptr *outputs,
                                                      stai_size *count)
{
    return segmentation_model_c_api.get_outputs(outputs, count);
}

stai_return_code SegmentationModelAdapter::Run(stai_run_mode mode)
{
    return segmentation_model_c_api.run(mode);
}

stai_return_code SegmentationModelAdapter::ContinueRun()
{
    return segmentation_model_c_api.run_continue();
}

stai_return_code SegmentationModelAdapter::WaitForEvent()
{
    return segmentation_model_c_api.wfe();
}

stai_return_code SegmentationModelAdapter::GetRunStatus()
{
    return segmentation_model_c_api.get_run_status();
}

stai_return_code SegmentationModelAdapter::NewInference()
{
    return segmentation_model_c_api.new_inference();
}

} // namespace uai::ai::model_manager
