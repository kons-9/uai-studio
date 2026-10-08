#pragma once

#include "runtime.hpp"
#include "driver/camera_driver.hpp"

namespace experiment::camera {

class BspDevice final : public Device {
public:
    console::Status Open() override;
    console::Status Close() override;
    console::Status Poll() override;
    console::Status Configure(const Geometry &geometry) override;
private:
    uai::camera_pipe2::driver::CameraDriver camera_;
    bool opened_ = false;
};

}