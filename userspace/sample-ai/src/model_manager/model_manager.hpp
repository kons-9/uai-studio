#ifndef UAI_AI_MODEL_MANAGER_HPP
#define UAI_AI_MODEL_MANAGER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
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
    common::Error Initialize(memory_allocator::MemoryAllocator &memory,
                             cache::CacheDriver &cache);
    common::Error TryInfer(const memory_allocator::InferenceFrame &frame,
                           memory_allocator::BoxSet *result);
    common::Error Shutdown();

    const npu::Status &LastNpuStatus() const
    {
        return last_npu_status_;
    }

private:
    common::Error RunNetwork();

    memory_allocator::MemoryAllocator *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    npu::NpuDriver npu_{};
#if defined(AI_MODEL_SEGMENTATION)
    model_manager::SegmentationModelAdapter model_{};
#else
    model_manager::PersonModelAdapter model_{};
#endif
    stai_network_info info_{};
    stai_ptr outputs_[3]{};
    bool dynamic_outputs_ = false;
    npu::Status last_npu_status_{};
    bool initialized_ = false;
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
#if defined(AI_MODEL_SEGMENTATION)
    std::uint8_t mask_buffer_index_ = 0U;
#endif
};

} // namespace uai::ai

#endif
