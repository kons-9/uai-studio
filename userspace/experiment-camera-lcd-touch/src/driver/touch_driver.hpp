#pragma once

#include <cstdint>

#include "driver/driver_status.hpp"

extern "C" {
#include "gt911.h"
}

namespace uai::camera_lcd_touch::driver {

struct TouchSample {
    bool active = false;
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
};

struct TouchInitDiagnostics {
    std::int32_t bus_status = -999;
    std::int32_t id_status = -999;
    std::uint32_t id = 0U;
    std::int32_t controller_status = -999;
};

class TouchDriver final {
public:
    DriverStatus Initialize(TouchInitDiagnostics *diagnostics = nullptr);
    DriverStatus Read(TouchSample *sample);

private:
    GT911_Object_t controller_{};
    bool initialized_ = false;
};

} // namespace uai::camera_lcd_touch::driver
