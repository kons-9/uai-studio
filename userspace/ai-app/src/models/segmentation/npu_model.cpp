#include "models/segmentation/npu_model.hpp"
#include "models/segmentation/c_wrapper.h"

namespace uai::ai::models::segmentation {

stai_return_code NpuModel::Initialize()
{
    return c_wrapper::Initialize();
}

stai_return_code NpuModel::Shutdown()
{
    return c_wrapper::Shutdown();
}

stai_return_code NpuModel::GetInfo(stai_network_info *info)
{
    return c_wrapper::GetInfo(info);
}

stai_return_code NpuModel::SetInput(
    stai_ptr input,
    stai_size size
)
{
    return c_wrapper::SetInput(input, size);
}

stai_return_code NpuModel::GetOutputs(
    stai_ptr *outputs,
    stai_size *count
)
{
    return c_wrapper::GetOutputs(outputs, count);
}

stai_return_code NpuModel::SetOutputs(
    const stai_ptr *outputs,
    stai_size count
)
{
    return c_wrapper::SetOutputs(outputs, count);
}

stai_return_code NpuModel::Run(stai_run_mode mode)
{
    return c_wrapper::Run(mode);
}

stai_return_code NpuModel::ContinueRun()
{
    return c_wrapper::ContinueRun();
}

stai_return_code NpuModel::GetRunStatus()
{
    return c_wrapper::GetRunStatus();
}

stai_return_code NpuModel::NewInference()
{
    return c_wrapper::NewInference();
}

stai_return_code NpuModel::SetEpochTraceCallback(
    ::uai::ai::npu::EpochTraceCallback callback,
    void *context
)
{
    return c_wrapper::SetEpochTraceCallback(callback, context);
}

} // namespace uai::ai::models::segmentation
