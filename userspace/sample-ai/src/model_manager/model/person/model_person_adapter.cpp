#include "model_manager/model/person/model_person_adapter.hpp"

extern "C" {
stai_return_code person_model_initialize(void);
stai_return_code person_model_shutdown(void);
stai_return_code person_model_get_info(stai_network_info *info);
stai_return_code person_model_get_inputs(stai_ptr *inputs, stai_size *count);
stai_return_code person_model_set_input(stai_ptr input, stai_size size);
stai_return_code person_model_get_outputs(stai_ptr *outputs, stai_size *count);
stai_return_code person_model_run(stai_run_mode mode);
stai_return_code person_model_continue_run(void);
stai_return_code person_model_wait_for_event(void);
stai_return_code person_model_get_run_status(void);
stai_return_code person_model_new_inference(void);
}

namespace uai::ai::model_manager {

stai_return_code PersonModelAdapter::Initialize()
{
    return person_model_initialize();
}

stai_return_code PersonModelAdapter::Shutdown()
{
    return person_model_shutdown();
}

stai_return_code PersonModelAdapter::GetInfo(stai_network_info *info)
{
    return person_model_get_info(info);
}

stai_return_code PersonModelAdapter::GetInputs(stai_ptr *inputs, stai_size *count)
{
    return person_model_get_inputs(inputs, count);
}

stai_return_code PersonModelAdapter::SetInput(stai_ptr input, stai_size size)
{
    return person_model_set_input(input, size);
}

stai_return_code PersonModelAdapter::GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return person_model_get_outputs(outputs, count);
}

stai_return_code PersonModelAdapter::Run(stai_run_mode mode)
{
    return person_model_run(mode);
}

stai_return_code PersonModelAdapter::ContinueRun()
{
    return person_model_continue_run();
}

stai_return_code PersonModelAdapter::WaitForEvent()
{
    return person_model_wait_for_event();
}

stai_return_code PersonModelAdapter::GetRunStatus()
{
    return person_model_get_run_status();
}

stai_return_code PersonModelAdapter::NewInference()
{
    return person_model_new_inference();
}

} // namespace uai::ai::model_manager
