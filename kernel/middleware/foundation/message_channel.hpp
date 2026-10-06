#pragma once

#include <cstddef>

#include <tk/tkernel.h>

#include "middleware/foundation/fixed_message_slots.hpp"

namespace uai::ai::common {

template <typename Message, std::size_t Depth>
class MessageChannel final {
public:
    ID Create()
    {
        T_CMBF config{};
        config.mbfatr = TA_TFIFO | TA_USERBUF;
        config.bufsz = static_cast<SZ>(slots_.size_bytes());
        config.maxmsz = static_cast<INT>(slots_.message_bytes());
        config.bufptr = slots_.data();
        id_ = tk_cre_mbf(&config);
        return id_;
    }

    ID id() const { return id_; }

    ER Send(const Message &message, TMO timeout)
    {
        return tk_snd_mbf(id_, &message, sizeof(message), timeout);
    }

    INT Receive(Message *message, TMO timeout)
    {
        return tk_rcv_mbf(id_, message, timeout);
    }

private:
    ID id_ = -1;
    FixedMessageSlots<Message, Depth> slots_;
};

} // namespace uai::ai::common