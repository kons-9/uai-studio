#ifndef UAI_AI_MIDDLEWARE_BUFFER_BUFFER_TYPES_HPP
#define UAI_AI_MIDDLEWARE_BUFFER_BUFFER_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace uai::ai::memory_allocator {

enum class Region : std::uint8_t {
    kCapture,
    kDisplay,
    kInference,
};

enum class BufferState : std::uint8_t {
    kFree,
    kFilling,
    kReady,
    kScanning,
    kReadyForAi,
    kInUseByAi,
};

struct Buffer {
    std::uintptr_t address = 0U;
    std::size_t size = 0U;
    std::uint8_t index = 0U;
    Region region = Region::kCapture;
    std::size_t alignment = 32U;

    explicit operator bool() const { return address != 0U && size != 0U; }
};

} // namespace uai::ai::memory_allocator

#endif
