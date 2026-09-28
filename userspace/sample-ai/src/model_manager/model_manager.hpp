#ifndef UAI_AI_MODEL_MANAGER_HPP
#define UAI_AI_MODEL_MANAGER_HPP

#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "model_manager/model_api.hpp"
#include "model_manager/model_descriptor.hpp"
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
#include "model_manager/model/face/model_face_adapter.hpp"
#include "model_manager/model/person/model_person_adapter.hpp"
#include "model_manager/model/segmentation/model_segmentation_adapter.hpp"
#elif defined(AI_MODEL_SEGMENTATION)
#include "model_manager/model/segmentation/model_segmentation_adapter.hpp"
#elif defined(AI_MODEL_FACE)
#include "model_manager/model/face/model_face_adapter.hpp"
#else
#include "model_manager/model/person/model_person_adapter.hpp"
#endif
#include "driver/npu_driver/npu_driver.hpp"

namespace uai::ai {

class ModelManager final {
public:
    using ModelKind = model_manager::ModelKind;
    common::Error Initialize(memory_allocator::MemoryAllocator &memory,
                             cache::CacheDriver &cache);
    common::Error SwitchModel(ModelKind kind);
    common::Error TryInfer(const memory_allocator::InferenceFrame &frame,
                           memory_allocator::BoxSet *result);
    common::Error Shutdown();

    const npu::Status &LastNpuStatus() const
    {
        return last_npu_status_;
    }

    ModelKind CurrentModel() const { return model_kind_; }

private:
    common::Error RunNetwork();

    memory_allocator::MemoryAllocator *memory_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    npu::NpuDriver npu_{};
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
    model_manager::Model *active_model_ = nullptr;
    model_manager::PersonModelAdapter person_model_{};
    model_manager::SegmentationModelAdapter segmentation_model_{};
    model_manager::FaceModelAdapter face_model_{};
#elif defined(AI_MODEL_SEGMENTATION)
    model_manager::SegmentationModelAdapter model_{};
#elif defined(AI_MODEL_FACE)
    model_manager::FaceModelAdapter model_{};
#else
    model_manager::PersonModelAdapter model_{};
#endif
    stai_network_info info_{};
    stai_ptr outputs_[memory_allocator::kMaxModelOutputs]{};
    bool dynamic_outputs_ = false;
    npu::Status last_npu_status_{};
    bool initialized_ = false;
    ModelKind model_kind_ =
#if defined(AI_MODEL_SEGMENTATION)
        ModelKind::kSegmentation;
#elif defined(AI_MODEL_FACE)
        ModelKind::kFace;
#else
        ModelKind::kPerson;
#endif
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
#if defined(AI_DYNAMIC_MODEL_SWITCHING) || defined(AI_MODEL_SEGMENTATION)
    std::uint8_t mask_buffer_index_ = 0U;
#endif
};

} // namespace uai::ai

#endif
