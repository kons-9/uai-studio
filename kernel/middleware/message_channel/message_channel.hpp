#pragma once

#include <cstddef>
#include <type_traits>

#include "middleware/foundation/error.hpp"

namespace uai::ai::message_channel {

/*
 * Typed FIFO of Depth trivially-copyable messages. The Backend owns the
 * storage and talks to the OS; this header never includes OS types.
 *
 * Errors: kNotInitialized before Create(), kBufferOverflow when a non-waiting
 * send finds the queue full, kNoFrame when a non-waiting receive finds it
 * empty, kHardware for anything else the backend reports.
 */
template <typename Message, std::size_t Depth, typename Backend>
class MessageChannel final {
    static_assert(std::is_trivially_copyable_v<Message>);
    static_assert(Depth > 0U);

public:
    static constexpr std::size_t kDepth = Depth;

    MessageChannel() = default;
    MessageChannel(const MessageChannel &) = delete;
    MessageChannel &operator=(const MessageChannel &) = delete;

    common::Error Create() { return backend_.Create(); }
    bool created() const { return backend_.created(); }

    common::Error TrySend(const Message &message) { return backend_.Send(message, false); }
    common::Error Send(const Message &message) { return backend_.Send(message, true); }
    common::Error TryReceive(Message *message)
    {
        if (message == nullptr)
            return {common::ErrorCode::kInvalidArgument};
        return backend_.Receive(message, false);
    }
    common::Error Receive(Message *message)
    {
        if (message == nullptr)
            return {common::ErrorCode::kInvalidArgument};
        return backend_.Receive(message, true);
    }

private:
    Backend backend_;
};

} // namespace uai::ai::message_channel
