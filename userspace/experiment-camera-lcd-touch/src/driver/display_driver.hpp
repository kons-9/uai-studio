#pragma once

#include <cstdint>

#include "driver/driver_status.hpp"
#include "driver/overlay_ui.hpp"

namespace uai::camera_lcd_touch::driver {

class DisplayDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Process();
    TouchAction HandleTouchPress(
        std::uint16_t x,
        std::uint16_t y
    );
    void HandleTouchRelease();

private:
    OverlayUi overlay_ui_{};
    std::uint8_t front_buffer_index_ = 0U;
    std::uint8_t pending_buffer_index_ = 0U;
    std::uint32_t pending_reload_count_ = 0U;
    bool initialized_ = false;
    bool flip_pending_ = false;
};

} // namespace uai::camera_lcd_touch::driver
