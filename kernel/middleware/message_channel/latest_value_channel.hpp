#pragma once

#include <cstddef>

#include <tk/tkernel.h>

#include "middleware/foundation/error.hpp"
#include "middleware/message_channel/message_channel.hpp"

namespace uai::ai::message_channel {

/* FIFO channel whose senders prefer the newest message when the queue is
 * full. Discarded messages are handed back to the caller, which owns any
 * resources they reference. */
template <typename Message, std::size_t Depth>
class LatestValueChannel final {
public:
    ID Create() { return channel_.Create(); }

    common::Error TrySend(const Message &message)
    {
        if (channel_.id() < E_OK) {
            return {common::ErrorCode::kNotInitialized};
        }
        const ER error = channel_.Send(message, TMO_POL);
        if (error == E_TMOUT) {
            return {common::ErrorCode::kBufferOverflow};
        }
        if (error != E_OK) {
            return {common::ErrorCode::kHardware};
        }
        return {};
    }

    bool TryReceive(Message *message)
    {
        return message != nullptr && Receive(message, TMO_POL);
    }

    bool ReceiveBlocking(Message *message)
    {
        return message != nullptr && Receive(message, TMO_FEVR);
    }

    /* Only a full queue triggers replacement, and only once. */
    common::Error SendReplacingOldestOnce(const Message &message)
    {
        const common::Error status = TrySend(message);
        if (status.Code() != common::ErrorCode::kBufferOverflow) return status;
        Message discarded{};
        TryReceive(&discarded);
        return TrySend(message);
    }

    /* Any send failure discards the oldest message and retries. Returns
     * false when nothing could be discarded; the caller still owns message. */
    template <typename OnDiscard>
    bool SendReplacingOldest(const Message &message, OnDiscard on_discard)
    {
        for (;;) {
            if (TrySend(message).Ok()) return true;
            Message discarded{};
            if (!TryReceive(&discarded)) return false;
            on_discard(discarded);
        }
    }

    /* Consume every queued message and keep the newest one accept() approves.
     * *latest is left untouched when none qualifies. */
    template <typename Accept>
    bool DrainLatest(Message *latest, Accept accept)
    {
        if (latest == nullptr) return false;
        Message message{};
        bool found = false;
        for (;;) {
            const INT size = channel_.Receive(&message, TMO_POL);
            if (size < 0) break;
            if (size == static_cast<INT>(sizeof(message)) && accept(message)) {
                *latest = message;
                found = true;
            }
        }
        return found;
    }

private:
    bool Receive(Message *message, TMO timeout)
    {
        return channel_.Receive(message, timeout) ==
               static_cast<INT>(sizeof(*message));
    }

    MessageChannel<Message, Depth> channel_;
};

} // namespace uai::ai::message_channel
