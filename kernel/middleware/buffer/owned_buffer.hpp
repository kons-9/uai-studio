#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "middleware/foundation/error.hpp"

namespace uai::ai::common {

template <typename Type, std::size_t Capacity>
struct OwnedBuffer {
    static_assert(std::is_trivially_copyable_v<Type>);
    std::array<Type, Capacity> bytes{};

    Error CopyFrom(const Type *source, std::size_t length)
    {
        if (length > Capacity) {
            return {ErrorCode::kBufferOverflow};
        }
        if (source == nullptr) {
            return {ErrorCode::kInvalidArgument};
        }
        for (std::size_t index = 0U; index < length; ++index) {
            bytes[index] = source[index];
        }
        return {};
    }

    const Type *data() const { return bytes.data(); }
};

} // namespace uai::ai::common