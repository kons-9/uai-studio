#pragma once

#include <array>
#include <cstddef>

namespace uai::ai::common {

template <std::size_t Bytes>
class StableAlignedBytes final {
    static_assert(Bytes > 0U);
    static_assert(Bytes % 8U == 0U);

public:
    StableAlignedBytes() = default;
    StableAlignedBytes(const StableAlignedBytes &) = delete;
    StableAlignedBytes &operator=(const StableAlignedBytes &) = delete;
    StableAlignedBytes(StableAlignedBytes &&) = delete;
    StableAlignedBytes &operator=(StableAlignedBytes &&) = delete;

    static constexpr std::size_t size_bytes() { return Bytes; }
    void *data() { return storage_.data(); }

private:
    alignas(8) std::array<
        std::byte,
        Bytes> storage_{};
};

} // namespace uai::ai::common