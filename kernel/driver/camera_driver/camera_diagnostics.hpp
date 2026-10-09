#pragma once

#include <cstdint>

#include "driver/camera_driver/camera_driver.hpp"

namespace uai::ai::camera {

struct SensorDiagnostics {
    std::uint32_t exposure_us = 0U;
    std::uint32_t exposure_lines = 0U;
    std::int32_t gain_mdB = 0;
    std::int32_t register_status = 0;
    std::uint32_t vmax = 0U;
    std::uint32_t shutter = 0U;
    std::uint32_t gain = 0U;
};

SensorDiagnostics ReadSensorDiagnostics();
void DumpCaptureRegisters(
    const pipeline::CaptureFrame &frame,
    const Diagnostics &diagnostics,
    std::uint32_t crc
);

} // namespace uai::ai::camera
