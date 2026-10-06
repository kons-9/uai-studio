#pragma once

#include <cstddef>

#include "middleware/foundation/error.hpp"
#include "middleware/message_channel/message_channel.hpp"

namespace uai::ai::message_channel {

struct DrainResult {
    common::Error error{};
    bool updated = false;
};

/* FIFO channel whose senders prefer the newest message when the queue is
 * full. Discarded messages are handed back to the caller, which owns any
 * resources they reference. Never blocks. */
template <typename Message, std::size_t Depth, typename Backend>
class LatestValueChannel final {
public:
    common::Error Create() { return channel_.Create(); }
    common::Error TrySend(const Message &message) { return channel_.TrySend(message); }
    bool TryReceive(Message *message) { return channel_.TryReceive(message).Ok(); }
    bool ReceiveBlocking(Message *message) { return channel_.Receive(message).Ok(); }

    /* Only a full queue triggers replacement, and only once. */
    common::Error SendReplacingOldestOnce(const Message &message)
    {
        const common::Error status = channel_.TrySend(message);
        if (status.Code() != common::ErrorCode::kBufferOverflow) return status;
        Message discarded{};
        (void)channel_.TryReceive(&discarded);
        return channel_.TrySend(message);
    }

    /* Any send failure discards the oldest message and retries. Returns
     * false when nothing could be discarded; the caller still owns message. */
    template <typename OnDiscard>
    bool SendReplacingOldest(const Message &message, OnDiscard on_discard)
    {
        for (;;) {
            if (channel_.TrySend(message).Ok()) return true;
            Message discarded{};
            if (!channel_.TryReceive(&discarded).Ok()) return false;
            on_discard(discarded);
        }
    }

    /* Consume every queued message and keep the newest one accept() approves.
     * error is kOk after a normal drain with an update, kNoFrame without one,
     * otherwise the receive error. updated reports whether *latest was written,
     * including when a later receive fails. */
    template <typename Accept>
    DrainResult DrainLatest(Message *latest, Accept accept)
    {
        if (latest == nullptr) return {{common::ErrorCode::kInvalidArgument}, false};
        Message message{};
        DrainResult result{{common::ErrorCode::kNoFrame}, false};
        for (;;) {
            const common::Error status = channel_.TryReceive(&message);
            if (status.Code() == common::ErrorCode::kNoFrame) return result;
            if (!status.Ok()) {
                result.error = status;
                return result;
            }
            if (accept(message)) {
                *latest = message;
                result = {{}, true};
            }
        }
    }

private:
    MessageChannel<Message, Depth, Backend> channel_;
};

} // namespace uai::ai::message_channel
