#include "model/person_model.hpp"

#include "model/person_network.h"

namespace uai::ai::mini {

stai_return_code PersonModel::Initialize()
{
    return person_network_init();
}

stai_return_code PersonModel::Shutdown()
{
    return person_network_deinit();
}

stai_return_code PersonModel::GetInfo(stai_network_info *info)
{
    return person_network_get_info(info);
}

stai_return_code PersonModel::SetInput(
    stai_ptr input,
    stai_size size
)
{
    return person_network_set_input(input, size);
}

stai_return_code PersonModel::GetOutputs(
    stai_ptr *outputs,
    stai_size *count
)
{
    return person_network_get_outputs(outputs, count);
}

stai_return_code PersonModel::SetOutputs(
    const stai_ptr *outputs,
    stai_size count
)
{
    return person_network_set_outputs(outputs, count);
}

stai_return_code PersonModel::Run(stai_run_mode mode)
{
    return person_network_run(mode);
}

stai_return_code PersonModel::ContinueRun()
{
    return person_network_run_continue();
}

stai_return_code PersonModel::GetRunStatus()
{
    return person_network_get_run_status();
}

stai_return_code PersonModel::NewInference()
{
    return person_network_new_inference();
}

stai_return_code PersonModel::SetEpochTraceCallback(
    npu::EpochTraceCallback callback,
    void *context
)
{
    return person_network_set_epoch_trace_callback(callback, context);
}

} // namespace uai::ai::mini
