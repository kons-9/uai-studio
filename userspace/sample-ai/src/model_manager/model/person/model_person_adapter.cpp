#include "model_manager/model/person/model_person_adapter.hpp"

extern "C" {
#include "model_manager/model/person/model_person_c_api.h"
}

namespace uai::ai::model_manager {

stai_return_code PersonModelAdapter::Initialize()
{
    return person_model_c_api.init();
}

stai_return_code PersonModelAdapter::Shutdown()
{
    return person_model_c_api.deinit();
}

stai_return_code PersonModelAdapter::GetInfo(stai_network_info *info)
{
    return person_model_c_api.get_info(info);
}

stai_return_code PersonModelAdapter::GetInputs(stai_ptr *inputs, stai_size *count)
{
    return person_model_c_api.get_inputs(inputs, count);
}

stai_return_code PersonModelAdapter::GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return person_model_c_api.get_outputs(outputs, count);
}

stai_return_code PersonModelAdapter::Run(stai_run_mode mode)
{
    return person_model_c_api.run(mode);
}

stai_return_code PersonModelAdapter::ContinueRun()
{
    return person_model_c_api.run_continue();
}

stai_return_code PersonModelAdapter::WaitForEvent()
{
    return person_model_c_api.wfe();
}

stai_return_code PersonModelAdapter::GetRunStatus()
{
    return person_model_c_api.get_run_status();
}

stai_return_code PersonModelAdapter::NewInference()
{
    return person_model_c_api.new_inference();
}

} // namespace uai::ai::model_manager
