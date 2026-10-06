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
    common::Error TryReceive(Message *message) { return channel_.TryReceive(message); }
    common::Error ReceiveBlocking(Message *message) { return channel_.Receive(message); }

    /* Only a full queue triggers replacement, and only once. */
    common::Error SendReplacingOldestOnce(const Message &message)
    {
        const common::Error status = channel_.TrySend(message);
        if (status.Code() != common::ErrorCode::kBufferOverflow) return status;
        Message discarded{};
        const common::Error receive_status = channel_.TryReceive(&discarded);
        if (!receive_status.Ok() && !Drained(receive_status)) return receive_status;
        return channel_.TrySend(message);
    }

    /* Any send failure discards the oldest message and retries, at most
     * Depth + 1 times so other producers cannot starve this caller. Returns
     * the last send or receive error; the caller still owns message. */
    template <typename OnDiscard>
    common::Error SendReplacingOldest(const Message &message, OnDiscard on_discard)
    {
        common::Error send_status{};
        for (std::size_t attempt = 0U; attempt <= Depth; ++attempt) {
            send_status = channel_.TrySend(message);
            if (send_status.Ok()) return {};
            Message discarded{};
            const common::Error receive_status = channel_.TryReceive(&discarded);
            if (receive_status.Ok()) {
                on_discard(discarded);
            } else if (!Drained(receive_status)) {
                return receive_status;
            }
        }
        return send_status;
    }

    /* Consume up to Depth + 1 queued messages and keep the newest accepted one.
     * error is kOk after a normal drain with an update, kNoFrame without one,
     * kTimeout if a producer continuously replenishes the queue, otherwise the
     * receive error. updated reports whether *latest was written, including
     * when a later receive fails. */
    template <typename Accept>
    DrainResult DrainLatest(Message *latest, Accept accept)
    {
        if (latest == nullptr) return {{common::ErrorCode::kInvalidArgument}, false};
        Message message{};
        DrainResult result{{common::ErrorCode::kNoFrame}, false};
        for (std::size_t count = 0U; count <= Depth; ++count) {
            const common::Error status = channel_.TryReceive(&message);
            if (Drained(status)) return result;
            if (!status.Ok()) {
                result.error = status;
                return result;
            }
            if (accept(message)) {
                *latest = message;
                result = {{}, true};
            }
        }
        result.error = {common::ErrorCode::kTimeout};
        return result;
    }

private:
    /* A consumer emptied the queue between our calls; not a failure. */
    static bool Drained(common::Error status)
    {
        return status.Code() == common::ErrorCode::kNoFrame;
    }

    MessageChannel<Message, Depth, Backend> channel_;
};

} // namespace uai::ai::message_channel
