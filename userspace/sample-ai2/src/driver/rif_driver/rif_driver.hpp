#ifndef UAI_AI_RIF_DRIVER_HPP
#define UAI_AI_RIF_DRIVER_HPP

#include "common/error.hpp"
#include "driver/driver_ownership.hpp"

namespace uai::ai::rif {

/*
 * Application-facing RIF setup driver.
 *
 * RIF is configured once during boot and has no runtime data operation, so
 * Initialize() is intentionally the only public operation. Register and HAL
 * access remain private to this implementation.
 */
class RifDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    common::Error Initialize();
    common::Error AcquireWriter(Writer *writer, TMO timeout = TMO_FEVR) const
    { return management_.Acquire(writer, timeout); }

private:
    driver::ResourceManagement management_{};
    bool initialized_ = false;
};

} // namespace uai::ai::rif

#endif // UAI_AI_RIF_DRIVER_HPP
