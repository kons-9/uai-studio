#pragma once

#include <cstdint>

#include "driver/driver_status.hpp"

namespace uai::camera_pipe2::driver {

void SetVisualOverlayPreserved(bool preserve);
bool SetDisplayPipe2(bool pipe2);
bool SetUiPanelBuffer(std::uint16_t *pixels);
void SetResultOverlay(
    bool visible,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height
);

class DisplayDriver final {
public:
    DriverStatus Initialize();
    DriverStatus Process();

private:
    bool initialized_ = false;
};

} // namespace uai::camera_pipe2::driver
