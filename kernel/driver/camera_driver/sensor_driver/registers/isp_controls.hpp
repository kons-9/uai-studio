#pragma once

#include "driver/camera_driver/capture_configuration.hpp"
#include "middleware/foundation/error.hpp"

namespace uai::ai::camera::sensor::registers {

class IspControls final {
public:
    static common::Error Read(State *state);
    static common::Error Apply(const State &state);
    static common::Error AutoExposure(bool enabled);
    static common::Error Compensation(int half_stops);
    static common::Error Manual(
        std::int32_t exposure_us,
        std::int32_t gain_mdB
    );
    static common::Error Statistics(Rect rectangle);
    static common::Error WhiteBalance(std::uint32_t temperature);
    static common::Error ListWhiteBalance(
        std::uint32_t *temperatures,
        std::size_t capacity,
        std::size_t *count
    );
};

}