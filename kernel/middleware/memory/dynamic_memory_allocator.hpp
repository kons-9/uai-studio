#ifndef UAI_AI_MIDDLEWARE_MEMORY_DYNAMIC_MEMORY_ALLOCATOR_HPP
#define UAI_AI_MIDDLEWARE_MEMORY_DYNAMIC_MEMORY_ALLOCATOR_HPP

#include <utility>

#include "middleware/foundation/error.hpp"
#include "middleware/memory/buffer_pointer.hpp"
#include "middleware/memory/buffer_types.hpp"

namespace uai::ai::memory_allocator {

using BufferPoolId = std::uint32_t;
inline constexpr BufferPoolId kInvalidBufferPool = 0U;
inline constexpr std::uint8_t kAnyBufferIndex = 0xFFU;

/* A generic request. Domain concepts such as camera capture or inference are
 * resolved by MemoryManager before this interface is called. */
struct BufferRequest {
    BufferPoolId pool = kInvalidBufferPool;
    std::uint8_t index = kAnyBufferIndex;
    std::size_t size = 0U;
    std::size_t alignment = 0U;
};

/* The dynamic abstraction owns only generic buffer acquisition and
 * validation. Domain-specific lifecycle operations belong to MemoryManager. */
class DynamicMemoryAllocator {
public:
    virtual ~DynamicMemoryAllocator() = default;

    virtual common::Error Acquire(
        const BufferRequest &request,
        memory_manager::UniquePointer *pointer) = 0;
    virtual common::Error Validate(
        const memory_manager::PointerIdentity &identity) const = 0;

    common::Error AcquireShared(
        const BufferRequest &request,
        memory_manager::SharedPointer *pointer)
    {
        if (pointer == nullptr) {
            return {common::ErrorCode::kInvalidArgument, 0U,
                    "memory.pointer.acquire_shared.null_output"};
        }
        memory_manager::UniquePointer unique;
        const common::Error status = Acquire(request, &unique);
        if (!status.Ok()) {
            return status;
        }
        *pointer = std::move(unique).Share();
        return status;
    }

    common::Error Validate(const memory_manager::UniquePointer &pointer) const
    {
        return Validate(pointer.identity());
    }

    common::Error Validate(
        const memory_manager::SharedPointer &pointer) const
    {
        return Validate(pointer.identity());
    }

    virtual common::Error Initialize() = 0;
};

} // namespace uai::ai::memory_allocator

#endif
