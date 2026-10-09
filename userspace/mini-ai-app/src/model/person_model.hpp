#pragma once

#include "driver/npu_driver/npu_network.hpp"

namespace uai::ai::mini {

/* NpuNetwork adapter for the generated person network. The NPU driver only
 * sees this interface; the generated symbols stay behind person_network.c. */
class PersonModel final : public npu::NpuNetwork {
public:
    stai_return_code Initialize() override;
    stai_return_code Shutdown() override;
    stai_return_code GetInfo(stai_network_info *info) override;
    stai_return_code SetInput(
        stai_ptr input,
        stai_size size
    ) override;
    stai_return_code GetOutputs(
        stai_ptr *outputs,
        stai_size *count
    ) override;
    stai_return_code SetOutputs(
        const stai_ptr *outputs,
        stai_size count
    ) override;
    stai_return_code Run(stai_run_mode mode) override;
    stai_return_code ContinueRun() override;
    stai_return_code GetRunStatus() override;
    stai_return_code NewInference() override;
    stai_return_code SetEpochTraceCallback(
        npu::EpochTraceCallback callback,
        void *context
    ) override;
};

} // namespace uai::ai::mini
