#ifndef UAI_AI_MODEL_MANAGER_HPP
#define UAI_AI_MODEL_MANAGER_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "models/face/model.hpp"
#include "models/model.hpp"
#include "models/person/model.hpp"
#include "models/segmentation/model.hpp"
#include "npu_scheduler/npu_scheduler.hpp"

namespace uai::ai {

class ModelManager final {
public:
    using ModelKind = models::ModelKind;
    common::Error Initialize(memory_allocator::MemoryAllocator &memory,
                             cache::CacheDriver &cache);
    common::Error SwitchModel(ModelKind kind);
    common::Error TryInfer(const memory_allocator::InferenceFrame &frame,
                           memory_allocator::BoxSet *result);
    common::Error Shutdown();

    const npu::Status &LastNpuStatus() const
    {
        return scheduler_.LastStatus();
    }

    ModelKind CurrentModel() const { return model_kind_; }

private:
    common::Error ConfigureCurrentModel();
    void BuildModelBindings();

    memory_allocator::MemoryAllocator *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    npu_scheduler::NpuScheduler scheduler_{};
    npu_scheduler::ModelBinding bindings_[3]{};
    std::size_t binding_count_ = 0U;
    models::person::Model person_model_{};
    models::segmentation::Model segmentation_model_{};
    models::face::Model face_model_{};
    stai_network_info info_{};
    stai_ptr outputs_[memory_allocator::kMaxModelOutputs]{};
    bool dynamic_outputs_ = false;
    bool initialized_ = false;
    ModelKind model_kind_ = models::ModelKind::kPerson;
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
    std::uint8_t mask_buffer_index_ = 0U;
};

} // namespace uai::ai

#endif
