#ifndef UAI_AI_MEMORY_MANAGER_FIXED_POOL_ALLOCATOR_HPP
#define UAI_AI_MEMORY_MANAGER_FIXED_POOL_ALLOCATOR_HPP

#include "middleware/memory/dynamic_memory_allocator.hpp"
#include "middleware/memory/fixed_pool.hpp"
#include "memory_manager/memory_config.hpp"

namespace uai::ai::memory_allocator {

class FixedPoolAllocator final : public DynamicMemoryAllocator {
public:
    static constexpr BufferPoolId kDisplayPool = 1U;
    static constexpr BufferPoolId kInferencePool = 2U;

    struct SlotInfo {
        Buffer buffer{};
        BufferState state = BufferState::kFree;
        std::uint64_t lease_token = 0U;
    };

    common::Error Acquire(const BufferRequest &request,
                          memory_manager::UniquePointer *pointer) override;
    common::Error Validate(
        const memory_manager::PointerIdentity &identity) const override;
    common::Error Initialize() override;

    bool IsInitialized() const { return initialized_; }
    std::size_t PoolSize(BufferPoolId pool) const;
    common::Error Inspect(BufferPoolId pool, std::uint8_t index,
                          SlotInfo *slot) const;
    common::Error Transition(BufferPoolId pool, std::uint8_t index,
                             BufferState expected, BufferState next);
    common::Error ReserveSlot(BufferPoolId pool, std::uint8_t index,
                              BufferState expected, BufferState next,
                              std::uint64_t *lease_token);

private:
    struct Slot {
        Buffer buffer{};
        BufferState state = BufferState::kFree;
        memory_manager::PointerControl pointer{};
    };

    using DisplayPool =
        FixedPool<Slot, memory_manager::kDisplayBufferCount>;
    using InferencePool =
        FixedPool<Slot, memory_manager::kInferenceBufferCount>;

    DisplayPool display_{};
    InferencePool inference_{};
    std::uint64_t display_lease_token_[memory_manager::kDisplayBufferCount]{};
    std::uint64_t inference_lease_token_[
        memory_manager::kInferenceBufferCount]{};
    std::uint64_t next_lease_token_ = 0U;
    std::uint64_t next_inference_lease_token_ = 0U;
    bool initialized_ = false;

    Slot *FindSlot(BufferPoolId pool, std::uint8_t index);
    const Slot *FindSlot(BufferPoolId pool, std::uint8_t index) const;
    std::uint64_t NextLeaseToken();
    std::uint64_t NextInferenceLeaseToken();
    static common::Error Make(common::ErrorCode code, std::uint32_t detail,
                              const char *operation);
    static bool SameBuffer(const Buffer &lhs, const Buffer &rhs);
    static void ReleasePointer(
        void *context,
        const memory_manager::PointerIdentity &identity) noexcept;
    static common::Error ValidatePointer(
        const void *context,
        const memory_manager::PointerIdentity &identity) noexcept;
    void ReleasePointer(
        const memory_manager::PointerIdentity &identity) noexcept;
};

} // namespace uai::ai::memory_allocator

#endif
