#ifndef UAI_AI_MIDDLEWARE_MEMORY_BUFFER_POINTER_HPP
#define UAI_AI_MIDDLEWARE_MEMORY_BUFFER_POINTER_HPP

#include <atomic>
#include <cstdint>

#include "middleware/foundation/error.hpp"
#include "middleware/memory/buffer_types.hpp"

namespace uai::ai::memory_allocator {
class DynamicMemoryAllocator;
class FixedPoolAllocator;
}

namespace uai::ai::memory_manager {

/* The identity is the only part that may cross an ownership-validation
 * boundary. The control block itself stays in the allocator's fixed storage. */
struct PointerIdentity {
    memory_allocator::Buffer buffer{};
    std::uint32_t pool = 0U;
    std::uint64_t token = 0U;
};

class UniquePointer;
class SharedPointer;

class PointerControl final {
public:
    PointerControl() = default;

private:
    friend class UniquePointer;
    friend class SharedPointer;
    friend class memory_allocator::FixedPoolAllocator;

    using ReleaseFunction = void (*)(void *,
                                     const PointerIdentity &) noexcept;
    using ValidateFunction = common::Error (*)(
        const void *, const PointerIdentity &) noexcept;

    void Initialize(const PointerIdentity &identity, void *context,
                    ReleaseFunction release, ValidateFunction validate) noexcept
    {
        identity_ = identity;
        context_ = context;
        release_ = release;
        validate_ = validate;
        strong_count_.store(1U, std::memory_order_release);
    }

    bool AcquireShared() noexcept
    {
        std::uint32_t count = strong_count_.load(std::memory_order_acquire);
        while (count != 0U) {
            if (strong_count_.compare_exchange_weak(
                    count, count + 1U, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
        return false;
    }

    void Release() noexcept
    {
        if (strong_count_.fetch_sub(1U, std::memory_order_acq_rel) == 1U &&
            release_ != nullptr) {
            release_(context_, identity_);
        }
    }

    common::Error Validate() const noexcept
    {
        if (strong_count_.load(std::memory_order_acquire) == 0U ||
            validate_ == nullptr) {
            return {common::ErrorCode::kOwnership, 0U,
                    "memory.pointer.validate.expired"};
        }
        return validate_(context_, identity_);
    }

    const PointerIdentity &identity() const { return identity_; }

    PointerIdentity identity_{};
    void *context_ = nullptr;
    ReleaseFunction release_ = nullptr;
    ValidateFunction validate_ = nullptr;
    std::atomic<std::uint32_t> strong_count_{0U};
};

/* Move-only RAII ownership. There is intentionally no public Release(). */
class UniquePointer final {
public:
    UniquePointer() = default;
    ~UniquePointer() { Reset(); }

    UniquePointer(const UniquePointer &) = delete;
    UniquePointer &operator=(const UniquePointer &) = delete;

    UniquePointer(UniquePointer &&other) noexcept : control_(other.control_)
    {
        other.control_ = nullptr;
    }

    UniquePointer &operator=(UniquePointer &&other) noexcept
    {
        if (this != &other) {
            Reset();
            control_ = other.control_;
            other.control_ = nullptr;
        }
        return *this;
    }

    explicit operator bool() const
    {
        return control_ != nullptr && control_->identity().buffer;
    }

    const memory_allocator::Buffer &buffer() const
    {
        return control_ != nullptr ? control_->identity().buffer : EmptyBuffer();
    }

    std::uint64_t token() const
    {
        return control_ != nullptr ? control_->identity().token : 0U;
    }

    PointerIdentity identity() const
    {
        return control_ != nullptr ? control_->identity() : PointerIdentity{};
    }

    common::Error Validate() const
    {
        return control_ != nullptr
                   ? control_->Validate()
                   : common::Error{common::ErrorCode::kOwnership, 0U,
                                   "memory.pointer.validate.empty"};
    }

    SharedPointer Share() &&;

private:
    friend class PointerControl;
    friend class SharedPointer;
    friend class memory_allocator::DynamicMemoryAllocator;
    friend class memory_allocator::FixedPoolAllocator;

    explicit UniquePointer(PointerControl *control) : control_(control) {}

    static const memory_allocator::Buffer &EmptyBuffer()
    {
        static const memory_allocator::Buffer empty{};
        return empty;
    }

    void Reset() noexcept
    {
        if (control_ != nullptr) {
            control_->Release();
            control_ = nullptr;
        }
    }

    PointerControl *control_ = nullptr;
};

/* Copyable RAII ownership backed by the allocator-owned control block. */
class SharedPointer final {
public:
    SharedPointer() = default;
    ~SharedPointer() { Reset(); }

    SharedPointer(const SharedPointer &other) noexcept
        : control_(other.control_)
    {
        if (control_ != nullptr && !control_->AcquireShared()) {
            control_ = nullptr;
        }
    }

    SharedPointer &operator=(const SharedPointer &other) noexcept
    {
        if (this != &other) {
            Reset();
            control_ = other.control_;
            if (control_ != nullptr && !control_->AcquireShared()) {
                control_ = nullptr;
            }
        }
        return *this;
    }

    SharedPointer(SharedPointer &&other) noexcept
        : control_(other.control_)
    {
        other.control_ = nullptr;
    }

    SharedPointer &operator=(SharedPointer &&other) noexcept
    {
        if (this != &other) {
            Reset();
            control_ = other.control_;
            other.control_ = nullptr;
        }
        return *this;
    }

    explicit operator bool() const
    {
        return control_ != nullptr && control_->identity().buffer;
    }

    const memory_allocator::Buffer &buffer() const
    {
        return control_ != nullptr ? control_->identity().buffer : EmptyBuffer();
    }

    std::uint64_t token() const
    {
        return control_ != nullptr ? control_->identity().token : 0U;
    }

    PointerIdentity identity() const
    {
        return control_ != nullptr ? control_->identity() : PointerIdentity{};
    }

    common::Error Validate() const
    {
        return control_ != nullptr
                   ? control_->Validate()
                   : common::Error{common::ErrorCode::kOwnership, 0U,
                                   "memory.pointer.validate.empty"};
    }

private:
    friend class UniquePointer;

    explicit SharedPointer(PointerControl *control) : control_(control) {}

    static const memory_allocator::Buffer &EmptyBuffer()
    {
        static const memory_allocator::Buffer empty{};
        return empty;
    }

    void Reset() noexcept
    {
        if (control_ != nullptr) {
            control_->Release();
            control_ = nullptr;
        }
    }

    PointerControl *control_ = nullptr;
};

inline SharedPointer UniquePointer::Share() &&
{
    SharedPointer shared(control_);
    control_ = nullptr;
    return shared;
}

} // namespace uai::ai::memory_manager

#endif
