#ifndef UAI_AI_NPU_SCHEDULER_HPP
#define UAI_AI_NPU_SCHEDULER_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "model_facade/model_facade.hpp"

namespace uai::ai::npu_scheduler {

/*
 * Owns the NPU resource and the scheduler-owned model facade. Tasks register
 * available models here; they do not choose the next model for each frame.
 */
class NpuScheduler final {
public:
    common::Error RegisterModel(const models::ModelBinding &binding);
    common::Error Initialize();
    /* Selects the next registered model according to scheduler policy. */
    common::Error SelectNext();

    common::Error GetInfo(stai_network_info *info) const;
    common::Error GetOutputs(stai_ptr *outputs, stai_size *count) const;
    common::Error SetInput(stai_ptr input, stai_size size) const;
    common::Error SetOutputs(const stai_ptr *outputs, stai_size count) const;
    common::Error ConfigureActiveModel(const models::ModelOutputSpec &spec);
    common::Error DecodeActive(const models::InferenceCompletionContext &context,
                               models::ModelResult *result) const;
    common::Error Run();
    common::Error NewInference();
    common::Error Shutdown();

    bool Initialized() const { return initialized_; }
    models::ModelKind CurrentModel() const;
    const models::ModelDescriptor *GetDescriptor() const;
    const npu::Status &LastStatus() const { return last_status_; }

private:
    common::Error SelectModel(models::ModelKind kind);
    common::Error InvalidState(const char *operation) const;

    ModelFacade model_facade_{};
    const models::ModelBinding *active_ = nullptr;
    npu::NpuDriver npu_{};
    npu::Status last_status_{};
    bool initialized_ = false;
};

} // namespace uai::ai::npu_scheduler

#endif
