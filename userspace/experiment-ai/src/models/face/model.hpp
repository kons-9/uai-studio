#pragma once

#include "models/model.hpp"

namespace uai::ai::models::face {

class Model final : public ::uai::ai::models::Model,
                    private ::uai::ai::models::ModelRuntime {
public:
    static const ModelDescriptor &Descriptor();
    const ModelDescriptor &GetDescriptor() const override;
    const ModelPipeline &GetPipeline() const override;
    common::Error ExecuteStage(ModelStageId stage,
                               ModelStageContext &context) const override;
    ModelCallbacks GetCallbacks() const override;
    common::Error PrepareInput(memory_allocator::InferenceFrame &frame,
                               cache::CacheDriver &cache) const override;
    common::Error ConvertResult(
        const ModelResult &source,
        memory_allocator::BoxSet *destination) const override;

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
    stai_return_code SetEpochTraceCallback(EpochTraceCallback callback,
                                            void *context) override;

    friend ::uai::ai::models::ModelRuntime &Runtime(Model &model);
};

/* Internal bridge for NpuRuntime; application code should use Model. */
::uai::ai::models::ModelRuntime &Runtime(Model &model);

} // namespace uai::ai::models::face
