#ifndef UAI_AI_NPU_SCHEDULER_HPP
#define UAI_AI_NPU_SCHEDULER_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "models/model.hpp"

namespace uai::ai::npu_scheduler {

struct ModelBinding {
    models::ModelKind kind{};
    models::Model *model = nullptr;
};

/* Owns the Neural-ART runtime lifecycle and the currently selected generated
 * model. Model-specific decoding and display results stay outside this class. */
class NpuScheduler final {
public:
    common::Error Initialize(const ModelBinding *bindings,
                             std::size_t binding_count,
                             models::ModelKind initial_model);
    common::Error SelectModel(models::ModelKind kind);

    common::Error GetInfo(stai_network_info *info) const;
    common::Error GetOutputs(stai_ptr *outputs, stai_size *count) const;
    common::Error SetInput(stai_ptr input, stai_size size) const;
    common::Error SetOutputs(const stai_ptr *outputs, stai_size count) const;
    common::Error Run();
    common::Error NewInference();
    common::Error Shutdown();

    bool Initialized() const { return initialized_; }
    models::ModelKind CurrentModel() const;
    const models::ModelDescriptor *GetDescriptor() const;
    const npu::Status &LastStatus() const { return last_status_; }

private:
    const ModelBinding *Find(models::ModelKind kind) const;
    common::Error InvalidState(const char *operation) const;

    const ModelBinding *bindings_ = nullptr;
    std::size_t binding_count_ = 0U;
    const ModelBinding *active_ = nullptr;
    npu::NpuDriver npu_{};
    npu::Status last_status_{};
    bool initialized_ = false;
};

} // namespace uai::ai::npu_scheduler

#endif
