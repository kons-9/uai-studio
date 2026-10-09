#include "camera_control.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition)
{
    if (!condition) {
        std::cerr << "camera check failed\n";
        std::exit(1);
    }
}

class FakeCamera final : public experiment::camera::Backend {
public:
    experiment::camera::State current{};
    int mutations = 0;
    experiment::console::Status result = experiment::console::Status::kOk;
    experiment::console::Status Read(experiment::camera::State &state) override
    {
        state = current;
        return result;
    }
    experiment::console::Status AutoExposure(bool enabled) override
    {
        ++mutations;
        current.auto_exposure = enabled;
        return result;
    }
    experiment::console::Status Compensation(int half_stops) override
    {
        ++mutations;
        current.compensation = half_stops;
        return result;
    }
    experiment::console::Status Manual(
        std::int32_t exposure,
        std::int32_t gain
    ) override
    {
        ++mutations;
        current.reported_exposure_us = exposure;
        current.reported_gain_mdB = gain;
        return result;
    }
    experiment::console::Status Statistics(experiment::camera::Rect rectangle) override
    {
        ++mutations;
        current.statistics = rectangle;
        return result;
    }
    experiment::console::Status WhiteBalance(std::uint32_t temperature) override
    {
        ++mutations;
        current.color_temperature = temperature;
        return result;
    }
    experiment::console::Status ListWhiteBalance(const experiment::console::Writer &writer) override
    {
        writer.Write("2800 6500\n");
        return result;
    }
};

}

int main()
{
    FakeCamera camera;
    camera.current.sensor_width = 2592;
    camera.current.sensor_height = 1944;
    std::string output;
    const experiment::console::Command commands[] = {{"cam", "cam", experiment::camera::Execute, &camera}};
    experiment::console::Shell shell(commands, 1, {&output, [](void *context, const char *text, std::size_t size) {
                                                       static_cast<std::string *>(context)->append(text, size);
                                                   }});
    auto send = [&](const char *text) {
        auto status = experiment::console::Status::kOk;
        for (; *text; ++text) {
            status = shell.Feed(*text);
        }
        return status;
    };
    Require(send("cam manual 1000 0\n") == experiment::console::Status::kInvalidState);
    Require(send("cam ae maybe\n") == experiment::console::Status::kInvalidArgument);
    Require(send("cam area 2590 0 8 10\n") == experiment::console::Status::kInvalidArgument);
    Require(send("cam area 0 0 0 10\n") == experiment::console::Status::kInvalidArgument);
    Require(send("cam ev 4294967296\n") == experiment::console::Status::kInvalidArgument);
    Require(camera.mutations == 0);
    Require(send("cam ae off\n") == experiment::console::Status::kOk);
    Require(send("cam manual 12000 3000\n") == experiment::console::Status::kOk);
    Require(camera.current.reported_exposure_us == 12000 && camera.current.reported_gain_mdB == 3000);
    Require(send("cam ev -2\n") == experiment::console::Status::kOk && camera.current.compensation == -2);
    Require(send("cam area 0 0 2592 1944\n") == experiment::console::Status::kOk);
    Require(
        send("cam stat\n") == experiment::console::Status::kOk && output.find("reported_us=12000") != std::string::npos
    );
    camera.result = experiment::console::Status::kHardware;
    output.clear();
    Require(
        send("cam ae on\n") == experiment::console::Status::kHardware && output.find("OK applied") == std::string::npos
    );
    std::int32_t parsed = 0;
    Require(experiment::camera::ParseInteger("-2147483648", parsed) && parsed == INT32_MIN);
    Require(!experiment::camera::ParseInteger("-2147483649", parsed));
    Require(!experiment::camera::ParseInteger("12x", parsed));
    experiment::camera::Rect mapped{9, 9, 9, 9};
    Require(experiment::camera::MapToSensor({0, 0, 480, 480}, {0, 96, 480, 288}, {0, 194, 2592, 1555}, mapped));
    Require(mapped.x == 0 && mapped.y == 194 && mapped.width == 2592 && mapped.height == 1555);
    Require(!experiment::camera::MapToSensor({0, 0, 480, 96}, {0, 96, 480, 288}, {0, 0, 2592, 1944}, mapped));
    Require(!experiment::camera::MapToSensor({UINT32_MAX, 0, 5, 5}, {0, 0, 480, 480}, {0, 0, 2592, 1944}, mapped));
}