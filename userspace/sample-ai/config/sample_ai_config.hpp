#pragma once

#include <cstdint>

namespace uai::ai::config {

enum class PipeFrameRate : std::uint8_t {
    kAll,
    kOneOverTwo,
    kOneOverFour,
};

struct CameraConfig {
    /* -1 disables the IMX335 sensor test pattern. */
    std::int32_t imx335_test_pattern_mode;
    bool bypass_downsize;
    bool raw_dump;
    bool demosaic_linear;
    bool disable_demosaic;
    PipeFrameRate pipe2_frame_rate;
};

/* Board/application configuration. Change this file when selecting a camera
 * diagnostic mode; runtime diagnostics live in TaskContext instead. */
inline constexpr CameraConfig kCamera{
    -1,
    false,
    false,
    false,
    false,
    PipeFrameRate::kOneOverTwo,
};

inline constexpr std::uintptr_t kModelNorProbeOffset = 0x00380000UL;

static_assert(!(kCamera.demosaic_linear && kCamera.disable_demosaic),
              "demosaic modes are mutually exclusive");

} // namespace uai::ai::config
