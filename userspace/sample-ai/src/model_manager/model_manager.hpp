#ifndef UAI_AI_MODEL_MANAGER_HPP
#define UAI_AI_MODEL_MANAGER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "memory_manager/memory_hardware.hpp"
#include "memory_manager/memory_manager.hpp"
#include "model_manager/model_api.hpp"
#if defined(AI_MODEL_SEGMENTATION)
#include "model_manager/model/segmentation/model_segmentation_adapter.hpp"
#else
#include "model_manager/model/person/model_person_adapter.hpp"
#endif
#include "driver/npu_driver/npu_driver.hpp"

namespace uai::ai {

class ModelManager final {
public:
    common::Error Initialize(memory_manager::MemoryManager &memory,
                             memory_manager::MemoryHardware &memory_hardware);
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
    memory_manager::MemoryHardware *memory_hardware_ = nullptr;
    npu_driver::NpuDriver npu_{};
#if defined(AI_MODEL_SEGMENTATION)
    model_manager::SegmentationModelAdapter model_{};
#else
    model_manager::PersonModelAdapter model_{};
#endif
    stai_network_info info_{};
    stai_ptr input_ = nullptr;
    stai_ptr outputs_[3]{};
    npu_driver::Status last_npu_status_{};
    bool initialized_ = false;
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
#if defined(AI_MODEL_SEGMENTATION)
    std::uint8_t mask_buffer_index_ = 0U;
#endif
};

} // namespace uai::ai

#endif
