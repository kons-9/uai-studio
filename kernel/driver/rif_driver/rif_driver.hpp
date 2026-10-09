#pragma once

#include "middleware/foundation/error.hpp"
#include "driver/driver_ownership.hpp"

namespace uai::ai::rif {

class RifManagement;

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

private:
    friend class RifManagement;
    RifDriver(driver::ResourceManagement &management) : management_(&management) {}
    ~RifDriver() = default;
    common::Error Initialize();
    driver::ResourceManagement *management_;
    bool initialized_ = false;
};

class RifManagement final {
public:
    using Writer = RifDriver::Writer;
    using Accessor = driver::ResourceAccessor<RifDriver>;
    static RifManagement &Instance()
    {
        static RifManagement m;
        return m;
    }
    common::Error Initialize() { return driver_.Initialize(); }
    common::Error Acquire(
        Accessor *a,
        TMO timeout = TMO_FEVR
    )
    {
        if (!a)
            return {common::ErrorCode::kInvalidArgument};
        *a = {};
        Writer w;
        auto s = ownership_.Acquire(&w, timeout);
        if (s.Ok())
            *a = Accessor(driver_, static_cast<Writer &&>(w));
        return s;
    }
    RifManagement(const RifManagement &) = delete;
    RifManagement &operator=(const RifManagement &) = delete;

private:
    RifManagement() : driver_(ownership_) {}
    ~RifManagement() = default;
    driver::ResourceManagement ownership_{};
    RifDriver driver_;
};

} // namespace uai::ai::rif
