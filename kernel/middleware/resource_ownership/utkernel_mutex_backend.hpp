#pragma once

#include <tk/tkernel.h>

#include "middleware/foundation/error.hpp"

namespace uai::ai::resource_ownership {

/* μT-Kernel mutex with priority inheritance. Maps E_TMOUT to kTimeout and
 * every other failure to kOwnership. */
class MicroTKernelMutexBackend final {
public:
    using Timeout = TMO;
    static constexpr Timeout kForever = TMO_FEVR;

    common::Error Create(int *lock)
    {
        T_CMTX config{};
        config.mtxatr = TA_INHERIT;
        const ID id = tk_cre_mtx(&config);
        if (id < E_OK)
            return Map(id);
        *lock = id;
        return {};
    }

    common::Error Lock(
        int lock,
        Timeout timeout
    )
    {
        const ER status = tk_loc_mtx(static_cast<ID>(lock), timeout);
        return status == E_OK ? common::Error{} : Map(status);
    }

    void Unlock(int lock) noexcept { (void)tk_unl_mtx(static_cast<ID>(lock)); }

private:
    static common::Error Map(ER status)
    {
        return {status == E_TMOUT ? common::ErrorCode::kTimeout : common::ErrorCode::kOwnership};
    }
};

} // namespace uai::ai::resource_ownership
