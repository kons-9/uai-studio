#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::camera_lcd_touch::driver {

enum class TouchAction {
    kNone,
    kSelectRed,
    kSelectGreen,
    kSelectBlue,
    kClearMarker,
    kMarkImage,
};

const char *TouchActionName(TouchAction action);

class OverlayUi final {
public:
    DriverStatus Initialize();
    void DrawOn(std::uint16_t *rgb565_frame) const;
    TouchAction PressAt(
        std::uint16_t x,
        std::uint16_t y
    );
    void Release();

private:
    std::uint16_t selected_color_ = 0xF800U;
    std::uint16_t marker_x_ = 0U;
    std::uint16_t marker_y_ = 0U;
    std::int8_t pressed_button_ = -1;
    bool marker_visible_ = false;
    bool initialized_ = false;
};

} // namespace uai::camera_lcd_touch::driver
