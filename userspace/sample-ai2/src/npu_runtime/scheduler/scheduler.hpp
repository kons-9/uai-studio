#ifndef UAI_AI_NPU_RUNTIME_SCHEDULER_HPP
#define UAI_AI_NPU_RUNTIME_SCHEDULER_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "models/model.hpp"

namespace uai::ai::npu_runtime::scheduler {

/*
 * Internal scheduler for NpuRuntime. It owns the model registry and chooses
 * the active model; NpuRuntime owns the NPU driver. NpuRuntime is the
 * application-facing API, so this class is not included directly by tasks.
 */
class Scheduler final {
public:
    common::Error RegisterModel(const models::ModelBinding &binding);
    common::Error Initialize();
    /* Selects the next registered model according to scheduler policy. */
    common::Error SelectNext();
    common::Error Select(models::ModelKind kind);
    common::Error Shutdown();

    bool Initialized() const { return initialized_; }
    std::size_t BindingCount() const { return binding_count_; }
    const models::ModelBinding *BindingAt(std::size_t index) const;
    const models::ModelBinding *CurrentBinding() const { return active_; }
    const models::ModelBinding *NextBinding() const;
    const models::ModelDescriptor *GetDescriptor() const;

    common::Error ConfigureActiveDecoder(const models::ModelOutputSpec &spec);
    common::Error PrepareActiveInput(memory_allocator::InferenceFrame &frame,
                                     cache::CacheDriver &cache) const;
    common::Error PrepareInputFor(const models::ModelBinding &binding,
                                  memory_allocator::InferenceFrame &frame,
                                  cache::CacheDriver &cache) const;
    common::Error DecodeActiveOutputs(
        const models::InferenceCompletionContext &context,
        models::ModelResult *result) const;
    common::Error ConvertActiveResult(const models::ModelResult &source,
                                      memory_allocator::BoxSet *destination) const;

private:
    static constexpr std::size_t kMaxRegisteredModels = 4U;

    const models::ModelBinding *Find(models::ModelKind kind) const;
    common::Error SelectModel(models::ModelKind kind);
    common::Error InvalidState(const char *operation) const;

    models::ModelBinding bindings_[kMaxRegisteredModels]{};
    std::size_t binding_count_ = 0U;
    const models::ModelBinding *active_ = nullptr;
    bool initialized_ = false;
};

} // namespace uai::ai::npu_runtime::scheduler

#endif
