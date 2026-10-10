#pragma once

#include <cstdint>

namespace uai::ai::common {

struct EventNotification {
    void *context = nullptr;
    void (*notify)(
        void *,
        std::uint32_t
    ) = nullptr;
    std::uint32_t reason = 0U;

    void Notify() const
    {
        if (notify != nullptr)
            notify(context, reason);
    }
};

}