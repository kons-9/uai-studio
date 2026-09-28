#ifndef UAI_AI_MODELS_SEGMENTATION_MODEL_HPP
#define UAI_AI_MODELS_SEGMENTATION_MODEL_HPP

#include "models/model.hpp"

namespace uai::ai::models::segmentation {

class Model final : public ::uai::ai::models::Model,
                    private ::uai::ai::models::ModelRuntime {
public:
    static const ModelDescriptor &Descriptor();
    const ModelDescriptor &GetDescriptor() const override;

private:
    stai_return_code Initialize() override;
    stai_return_code Shutdown() override;
    stai_return_code GetInfo(stai_network_info *info) override;
    stai_return_code GetInputs(stai_ptr *inputs, stai_size *count) override;
    stai_return_code SetInput(stai_ptr input, stai_size size) override;
    stai_return_code GetOutputs(stai_ptr *outputs, stai_size *count) override;
    stai_return_code SetOutputs(const stai_ptr *outputs,
                                stai_size count) override;
    stai_return_code Run(stai_run_mode mode) override;
    stai_return_code ContinueRun() override;
    stai_return_code WaitForEvent() override;
    stai_return_code GetRunStatus() override;
    stai_return_code NewInference() override;

    friend ::uai::ai::models::ModelRuntime &Runtime(Model &model);
};

/* Internal bridge for NpuScheduler; application code should use Model. */
::uai::ai::models::ModelRuntime &Runtime(Model &model);

} // namespace uai::ai::models::segmentation

#endif
