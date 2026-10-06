#ifndef UAI_AI_MIDDLEWARE_BUFFER_BUFFER_TYPES_HPP
#define UAI_AI_MIDDLEWARE_BUFFER_BUFFER_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace uai::ai::buffer {

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

/* Non-owning description of a fixed region slot. */
struct Buffer {
    std::uintptr_t address = 0U;
    std::size_t size = 0U;
    std::uint8_t index = 0U;
    Region region = Region::kCapture;
    std::size_t alignment = 32U;

    explicit operator bool() const { return address != 0U && size != 0U; }

    friend bool operator==(const Buffer &lhs, const Buffer &rhs)
    {
        return lhs.address == rhs.address && lhs.size == rhs.size &&
               lhs.index == rhs.index && lhs.region == rhs.region;
    }
    friend bool operator!=(const Buffer &lhs, const Buffer &rhs)
    {
        return !(lhs == rhs);
    }
};

} // namespace uai::ai::buffer

#endif
