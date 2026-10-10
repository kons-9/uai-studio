#pragma once

#include "middleware/foundation/error.hpp"
#include "driver/driver_ownership.hpp"
#include "middleware/ui/touch_point.hpp"
#include "driver/touch_driver/registers/touch_registers.hpp"

namespace uai::ai::touch {

class TouchManagement;

/*
 * GT911 capacitive touch controller on the STM32N6570-DK (I2C2, polled).
 * Coordinates are reported in LCD pixels, 800x480 landscape.
 */
class TouchDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    common::Error Read(
        ui::TouchPoint *sample,
        const Writer &writer
    );
    common::Error ReadRaw(
        ui::TouchPoint *sample,
        const Writer &writer
    );

private:
    friend class TouchManagement;
    TouchDriver(driver::ResourceManagement &management) : management_(&management) {}
    ~TouchDriver() = default;
    common::Error Initialize();
    driver::ResourceManagement *management_;
    registers::TouchRegisterLayer registers_{};
    bool initialized_ = false;
};

class TouchManagement final {
public:
    using Writer = TouchDriver::Writer;
    using Accessor = driver::ResourceAccessor<TouchDriver>;
    static TouchManagement &Instance()
    {
        static TouchManagement m;
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
    common::Error Read(ui::TouchPoint *sample)
    {
        Accessor a;
        auto s = Acquire(&a);
        return s.Ok() ? a.Get()->Read(sample, a.Ownership()) : s;
    }
    common::Error ReadRaw(ui::TouchPoint *sample)
    {
        Accessor accessor;
        auto status = Acquire(&accessor);
        return status.Ok() ? accessor.Get()->ReadRaw(sample, accessor.Ownership()) : status;
    }
    TouchManagement(const TouchManagement &) = delete;
    TouchManagement &operator=(const TouchManagement &) = delete;

private:
    TouchManagement() : driver_(ownership_) {}
    ~TouchManagement() = default;
    driver::ResourceManagement ownership_{};
    TouchDriver driver_;
};

} // namespace uai::ai::touch
