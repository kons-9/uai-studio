#include "driver/rif_controller/rif_controller.hpp"

namespace uai::ai::driver {

common::Error RifController::Initialize()
{
    using common::ErrorCode;
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "rif.initialize"};
    }

    const common::Error status = hardware_.Initialize();
    if (!status.Ok()) {
        return status;
    }

    initialized_ = true;
    return {ErrorCode::kOk, 0U, "rif.initialize"};
}

} // namespace uai::ai::driver
