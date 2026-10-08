#pragma once

#include "camera_control.hpp"

namespace experiment::camera {

class IspCamera final : public Backend {
public:
    console::Status Read(State &state) override;
    console::Status AutoExposure(bool enabled) override;
    console::Status Compensation(int half_stops) override;
    console::Status Manual(std::int32_t exposure_us, std::int32_t gain_mdB) override;
    console::Status Statistics(Rect rectangle) override;
    console::Status WhiteBalance(std::uint32_t temperature) override;
    console::Status ListWhiteBalance(const console::Writer &writer) override;
};

}