#include "runtime.hpp"
#include <cstdlib>
#include <iostream>

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "runtime check failed\n";
        std::exit(1);
    }
}

struct Fake final : experiment::camera::Device, experiment::camera::Backend {
    experiment::camera::State state{};
    bool fail_configure = false;
    unsigned opens = 0;
    experiment::console::Status Open() override
    {
        ++opens;
        state = {};
        state.sensor_width = 2592;
        state.sensor_height = 1944;
        state.statistics = {0, 0, 2592, 1944};
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Close() override { return experiment::console::Status::kOk; }
    experiment::console::Status Poll() override { return experiment::console::Status::kOk; }
    experiment::console::Status Configure(const experiment::camera::Geometry &) override
    {
        if (fail_configure) {
            fail_configure = false;
            return experiment::console::Status::kHardware;
        }
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Read(experiment::camera::State &target) override
    {
        target = state;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status AutoExposure(bool enabled) override
    {
        state.auto_exposure = enabled;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Compensation(int value) override
    {
        state.compensation = value;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Manual(
        std::int32_t exposure,
        std::int32_t gain
    ) override
    {
        Require(!state.auto_exposure);
        state.reported_exposure_us = exposure;
        state.reported_gain_mdB = gain;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Statistics(experiment::camera::Rect rectangle) override
    {
        state.statistics = rectangle;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status WhiteBalance(std::uint32_t value) override
    {
        state.auto_white_balance = value == 0;
        state.color_temperature = value;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status ListWhiteBalance(const experiment::console::Writer &) override
    {
        return experiment::console::Status::kOk;
    }
};

int main()
{
    Fake device;
    experiment::camera::Runtime runtime(device, device);
    Require(runtime.Start() == experiment::console::Status::kOk);
    device.AutoExposure(false);
    device.Manual(12000, 3000);
    device.Compensation(2);
    device.Statistics({100, 100, 800, 600});
    Require(runtime.Stop() == experiment::console::Status::kOk && !runtime.Running());
    Require(runtime.Start() == experiment::console::Status::kOk);
    Require(
        !device.state.auto_exposure && device.state.reported_exposure_us == 12000 && device.state.statistics.x == 100
    );
    auto geometry = runtime.Configuration();
    geometry.fps = 20;
    Require(runtime.Change(geometry) == experiment::console::Status::kOk);
    geometry.fps = 10;
    device.fail_configure = true;
    Require(runtime.Change(geometry) == experiment::console::Status::kHardware);
    Require(runtime.Running() && runtime.Configuration().fps == 20 && runtime.Recoveries() == 1);
    geometry.crop.x = UINT32_MAX;
    Require(runtime.Change(geometry) == experiment::console::Status::kInvalidArgument);
    Require(device.opens == 3);
}