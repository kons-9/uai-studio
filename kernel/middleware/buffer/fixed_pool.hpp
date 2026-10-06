#ifndef UAI_AI_MIDDLEWARE_BUFFER_FIXED_POOL_HPP
#define UAI_AI_MIDDLEWARE_BUFFER_FIXED_POOL_HPP

#include <array>
#include <cstddef>

namespace uai::ai::memory_allocator {

/*
 * Fixed-capacity storage for allocator entries.
 *
 * The pool deliberately does not define a state machine. Capture, display,
 * and inference buffers have different lifecycle transitions; their owners
 * keep that policy while this type provides the common, heap-free K-sized
 * storage and indexing boundary.
 */
template <typename Entry, std::size_t kCapacity>
class FixedPool final {
    static_assert(kCapacity > 0U, "FixedPool capacity must be non-zero");

public:
    using value_type = Entry;

    static constexpr std::size_t capacity() { return kCapacity; }

    constexpr std::size_t size() const { return kCapacity; }

    Entry &operator[](std::size_t index) { return entries_[index]; }
    const Entry &operator[](std::size_t index) const { return entries_[index]; }

private:
    std::array<Entry, kCapacity> entries_{};
};

} // namespace uai::ai::memory_allocator

#endif
