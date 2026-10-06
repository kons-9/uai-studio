#pragma once

#include <cstddef>

#include <tk/tkernel.h>

#include "middleware/foundation/error.hpp"
#include "middleware/message_channel/fixed_message_slots.hpp"

namespace uai::ai::message_channel {

/* μT-Kernel message buffer behind the typed channel. Owns the user buffer and
 * maps ER/INT/TMO to common::Error; nothing outside this header sees them. */
template <typename Message, std::size_t Depth>
class MicroTKernelBackend final {
public:
    MicroTKernelBackend() = default;
    MicroTKernelBackend(const MicroTKernelBackend &) = delete;
    MicroTKernelBackend &operator=(const MicroTKernelBackend &) = delete;

    common::Error Create()
    {
        T_CMBF config{};
        config.mbfatr = TA_TFIFO | TA_USERBUF;
        config.bufsz = static_cast<SZ>(slots_.size_bytes());
        config.maxmsz = static_cast<INT>(slots_.message_bytes());
        config.bufptr = slots_.data();
        const ID id = tk_cre_mbf(&config);
        if (id < E_OK) return {common::ErrorCode::kHardware};
        id_ = id;
        return {};
    }

    bool created() const { return id_ >= E_OK; }

    common::Error Send(const Message &message, bool wait)
    {
        if (!created()) return {common::ErrorCode::kNotInitialized};
        const ER error =
            tk_snd_mbf(id_, &message, sizeof(message), wait ? TMO_FEVR : TMO_POL);
        if (error == E_OK) return {};
        if (error == E_TMOUT) return {common::ErrorCode::kBufferOverflow};
        return {common::ErrorCode::kHardware};
    }

    common::Error Receive(Message *message, bool wait)
    {
        if (!created()) return {common::ErrorCode::kNotInitialized};
        const INT size = tk_rcv_mbf(id_, message, wait ? TMO_FEVR : TMO_POL);
        if (size == static_cast<INT>(sizeof(*message))) return {};
        if (size == E_TMOUT) return {common::ErrorCode::kNoFrame};
        return {common::ErrorCode::kHardware};
    }

private:
    ID id_ = -1;
    FixedMessageSlots<Message, Depth> slots_;
};

} // namespace uai::ai::message_channel
