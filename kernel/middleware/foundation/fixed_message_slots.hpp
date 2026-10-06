#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace uai::ai::common {

template <typename Message, std::size_t Depth>
class FixedMessageSlots final {
    static_assert(std::is_trivially_copyable_v<Message>);
    static_assert(Depth > 0U);
    static_assert(sizeof(Message) <=
                  static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()));

    static constexpr std::size_t kHeaderBytes = sizeof(std::int32_t);
    static constexpr std::size_t kSlotBytes =
        kHeaderBytes + (sizeof(Message) + kHeaderBytes - 1U) /
                           kHeaderBytes * kHeaderBytes;
    static_assert(Depth <= std::numeric_limits<std::size_t>::max() / kSlotBytes);
    static_assert(Depth * kSlotBytes <=
                  static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()));

public:
    FixedMessageSlots() = default;
    FixedMessageSlots(const FixedMessageSlots &) = delete;
    FixedMessageSlots &operator=(const FixedMessageSlots &) = delete;
    FixedMessageSlots(FixedMessageSlots &&) = delete;
    FixedMessageSlots &operator=(FixedMessageSlots &&) = delete;

    static constexpr std::size_t size_bytes() { return kSlotBytes * Depth; }
    static constexpr std::size_t message_bytes() { return sizeof(Message); }
    void *data() { return storage_.data(); }

private:
    alignas(8) std::array<std::byte, size_bytes()> storage_{};
};

} // namespace uai::ai::common