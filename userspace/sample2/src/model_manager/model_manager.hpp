#ifndef UAI_SAMPLE2_MODEL_MANAGER_HPP
#define UAI_SAMPLE2_MODEL_MANAGER_HPP

#include "common/error.hpp"
#include "memory_manager/memory_manager.hpp"
#include "model_manager/model_api.h"
#include "npu_driver/npu_driver.hpp"

namespace uai::sample2 {

class ModelManager final {
public:
    common::Error Initialize(memory_manager::MemoryManager &memory);
    common::Error TryInfer(const memory_manager::InferenceFrame &frame,
                           memory_manager::BoxSet *result);
    common::Error Shutdown();

    const npu_driver::Status &LastNpuStatus() const
    {
        return last_npu_status_;
    }

private:
    common::Error RunNetwork();

    memory_manager::MemoryManager *memory_ = nullptr;
    npu_driver::NpuDriver npu_{};
    const sample2_model_api *model_ = &person_model;
    stai_network_info info_{};
    stai_ptr input_ = nullptr;
    stai_ptr outputs_[3]{};
    npu_driver::Status last_npu_status_{};
    bool initialized_ = false;
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
};

} // namespace uai::sample2

#endif
