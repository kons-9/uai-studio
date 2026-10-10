#pragma once

#include <cstdint>
#include <tk/tkernel.h>

#include "middleware/foundation/error.hpp"
#include "middleware/task/event_notification.hpp"

namespace uai::ai::task {

class CameraRenderWake final {
public:
    enum : UINT {
        kCapture = 1U,
        kPipe2 = 2U,
        kVsync = 4U,
        kResult = 8U,
        kRequest = 16U,
        kError = 32U
    };
    CameraRenderWake() = default;
    CameraRenderWake(const CameraRenderWake &) = delete;
    CameraRenderWake &operator=(const CameraRenderWake &) = delete;

    common::Error Create()
    {
        if (flag_ > 0)
            return {};
        T_CFLG configuration{};
        configuration.flgatr = TA_TFIFO;
        flag_ = tk_cre_flg(&configuration);
        if (flag_ <= 0) {
            flag_ = 0;
            return {common::ErrorCode::kHardware};
        }
        return {};
    }
    void Close()
    {
        if (flag_ > 0)
            (void)tk_del_flg(flag_);
        flag_ = 0;
    }
    common::EventNotification Bind(std::uint32_t reason)
    {
        return {
            this,
            [](void *context, std::uint32_t bits) {
                static_cast<CameraRenderWake *>(context)->Notify(bits);
            },
            reason
        };
    }
    void Notify(std::uint32_t reasons)
    {
        if (flag_ > 0)
            (void)tk_set_flg(flag_, reasons);
    }
    common::Error Wait(
        std::uint32_t remaining_ms,
        UINT *reasons
    )
    {
        if (flag_ <= 0 || reasons == nullptr)
            return {common::ErrorCode::kInvalidState};
        *reasons = 0U;
        const TMO timeout = remaining_ms == UINT32_MAX
            ? TMO_FEVR
            : static_cast<TMO>(remaining_ms > 0x7FFFFFFFU ? 0x7FFFFFFFU : remaining_ms);
        const ER status = tk_wai_flg(flag_, 0x3FU, TWF_ORW | TWF_BITCLR, reasons, timeout);
        return status == E_OK || status == E_TMOUT ? common::Error{} : common::Error{common::ErrorCode::kHardware};
    }

private:
    ID flag_ = 0;
};

}