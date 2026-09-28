#ifndef UAI_AI_MODEL_FACE_ADAPTER_HPP
#define UAI_AI_MODEL_FACE_ADAPTER_HPP

#include "model_manager/model_api.hpp"

namespace uai::ai::model_manager {

class FaceModelAdapter final : public Model {
public:
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
};

} // namespace uai::ai::model_manager

#endif
