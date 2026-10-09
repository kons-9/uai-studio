#include "scenario.hpp"
#include "extension.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

extern "C" {
volatile unsigned int camera_pipe2_pipe1_vsync_count = 0;
volatile unsigned int camera_pipe2_pipe2_frame_count = 0;
volatile unsigned int experiment_camera_failures = 0;
}

namespace {
enum class Failure {
    kNone,
    kPipe1,
    kSlowFps,
    kState,
    kRestore,
    kRecovery,
    kProcessing,
    kStopLeak,
    kDeadline,
    kObservationGap
};
std::uint32_t milliseconds = 0;
experiment::camera::Frames counters{};
std::uint32_t Clock()
{
    return milliseconds;
}
experiment::camera::Frames ReadFrames()
{
    return counters;
}
void Require(bool condition)
{
    if (!condition) {
        std::cerr << "scenario check failed\n";
        std::exit(1);
    }
}

struct Fake final : experiment::camera::Device, experiment::camera::Backend {
    experiment::camera::State state{};
    bool opened = false;
    std::uint32_t fps = 30;
    std::uint32_t frame_remainder = 0;
    Failure failure = Failure::kNone;
    experiment::console::Status Open() override
    {
        opened = true;
        state = {};
        state.sensor_width = 2592;
        state.sensor_height = 1944;
        state.statistics = {0, 0, 2592, 1944};
        counters = {};
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Close() override
    {
        opened = false;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Poll() override { return experiment::console::Status::kOk; }
    experiment::console::Status Configure(const experiment::camera::Geometry &geometry) override
    {
        if (failure == Failure::kDeadline && geometry.fps == 10) {
            milliseconds += 6000;
        }
        if (failure != Failure::kSlowFps || geometry.fps != 10) {
            fps = geometry.fps;
        }
        frame_remainder = 0;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Read(experiment::camera::State &result) override
    {
        result = state;
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
        if (failure == Failure::kRestore && gain == 3000) {
            return experiment::console::Status::kHardware;
        }
        if (failure == Failure::kState && gain == 0) {
            return experiment::console::Status::kOk;
        }
        state.reported_exposure_us = exposure;
        state.reported_gain_mdB = gain;
        return experiment::console::Status::kOk;
    }
    experiment::console::Status Statistics(experiment::camera::Rect bounds) override
    {
        state.statistics = bounds;
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

void Write(
    void *context,
    const char *text,
    std::size_t size
)
{
    static_cast<std::string *>(context)->append(text, size);
}

void Run(
    bool stalled,
    bool abort,
    std::uint32_t initial_time,
    Failure failure = Failure::kNone
)
{
    milliseconds = initial_time;
    Fake device;
    experiment::camera::Runtime runtime(device, device);
    Require(runtime.Start() == experiment::console::Status::kOk);
    device.AutoExposure(false);
    device.Manual(12000, 3000);
    device.WhiteBalance(4015);
    Require(runtime.Save() == experiment::console::Status::kOk);
    const auto original = device.state;
    device.failure = failure;
    std::string output;
    experiment::camera::Scenario scenario(runtime, {&output, Write}, Clock, ReadFrames);
    Require(scenario.Start() == experiment::console::Status::kOk);
    for (unsigned iteration = 0; scenario.Active() && iteration < 1000; ++iteration) {
        milliseconds += 250;
        if ((device.opened || failure == Failure::kStopLeak) && !stalled) {
            device.frame_remainder += device.fps * 250;
            const auto frames = device.frame_remainder / 1000;
            device.frame_remainder %= 1000;
            if (failure != Failure::kPipe1 || output.find("CAMTEST stability BEGIN") == std::string::npos) {
                counters.pipe1 += frames;
            }
            counters.pipe2 += frames;
        }
        if (failure == Failure::kRecovery && iteration == 2) {
            Require(runtime.Recover() == experiment::console::Status::kOk);
        }
        if (failure == Failure::kProcessing && iteration == 2) {
            ++counters.failures;
        }
        if (failure == Failure::kObservationGap && iteration == 4) {
            milliseconds += 2000;
        }
        if (abort && iteration == 2) {
            Require(scenario.Stop() == experiment::console::Status::kOk);
        }
        scenario.Tick();
    }
    Require(!scenario.Active());
    Require(runtime.Running());
    if (failure != Failure::kRestore) {
        Require(experiment::camera::SameSettings(device.state, original));
    }
    Require(output.find("CAMTEST SUMMARY") != std::string::npos);
    Require(output.find("visual=required") != std::string::npos);
    if (failure == Failure::kRestore) {
        Require(scenario.Failed() == 2 && output.find("CAMTEST cleanup FAIL") != std::string::npos);
    } else if (stalled || abort || failure != Failure::kNone) {
        Require(scenario.Failed() == 1);
    } else {
        Require(scenario.Failed() == 0 && scenario.Passed() == 32 && runtime.Recoveries() == 2);
    }
    if (stalled) {
        Require(output.find("frame-timeout") != std::string::npos);
    }
    if (abort) {
        Require(output.find("aborted") != std::string::npos);
    }
    if (failure == Failure::kPipe1) {
        Require(output.find("frame-timeout") != std::string::npos);
    }
    if (failure == Failure::kSlowFps) {
        Require(output.find("fps-out-of-range") != std::string::npos);
    }
    if (failure == Failure::kState) {
        Require(output.find("state-mismatch") != std::string::npos);
    }
    if (failure == Failure::kRecovery) {
        Require(output.find("unexpected-recovery") != std::string::npos);
    }
    if (failure == Failure::kProcessing) {
        Require(output.find("camera-or-display-error") != std::string::npos);
    }
    if (failure == Failure::kStopLeak) {
        Require(output.find("frames-while-stopped") != std::string::npos);
    }
    if (failure == Failure::kDeadline) {
        Require(output.find("action-deadline") != std::string::npos);
    }
    if (failure == Failure::kObservationGap) {
        Require(output.find("observation-gap") != std::string::npos);
    }
}

void TestExtension()
{
    milliseconds = 0;
    Fake device;
    experiment::camera::Runtime runtime(device, device);
    Require(runtime.Start() == experiment::console::Status::kOk);
    const auto original = device.state;
    std::string output;
    bool locked = false;
    experiment::console::Command commands[2]{};
    const experiment::Services services{&runtime, {&output, Write}, Clock, nullptr, &locked};
    Require(experiment::Register(services, commands, 2) == 2 && locked);
    const char *subject[] = {"subject", "off"};
    Require(commands[0].execute(nullptr, 2, subject, services.output) == experiment::console::Status::kInvalidState);
    experiment::Tick(milliseconds, 0);
    Require(locked && output.find("CAMTEST START") != std::string::npos);
    const char *stat[] = {"scenario", "stat"};
    Require(commands[1].execute(nullptr, 2, stat, services.output) == experiment::console::Status::kOk);
    Require(output.find("running=1") != std::string::npos);
    const char *start[] = {"scenario", "start"};
    Require(commands[1].execute(nullptr, 2, start, services.output) == experiment::console::Status::kInvalidState);
    const char *stop[] = {"scenario", "stop"};
    Require(commands[1].execute(nullptr, 2, stop, services.output) == experiment::console::Status::kOk);
    Require(!locked && experiment::camera::SameSettings(device.state, original));
    output.clear();
    Require(commands[1].execute(nullptr, 2, start, services.output) == experiment::console::Status::kOk && locked);
    for (unsigned iteration = 0; locked && iteration < 1000; ++iteration) {
        milliseconds += 250;
        if (device.opened) {
            device.frame_remainder += device.fps * 250;
            const auto frames = device.frame_remainder / 1000;
            device.frame_remainder %= 1000;
            counters.pipe1 += frames;
            counters.pipe2 += frames;
        }
        camera_pipe2_pipe1_vsync_count = counters.pipe1;
        camera_pipe2_pipe2_frame_count = counters.pipe2;
        experiment::Tick(milliseconds, counters.pipe2);
    }
    Require(!locked && runtime.Running() && experiment::camera::SameSettings(device.state, original));
    Require(output.find("CAMTEST SUMMARY pass=32 fail=0 skip=0 visual=required") != std::string::npos);
    const auto completed = output.size();
    milliseconds += 1000;
    experiment::Tick(milliseconds, counters.pipe2);
    Require(output.size() == completed);
}
}

int main()
{
    Run(false, false, 0);
    Run(true, false, 0);
    Run(false, true, 0);
    Run(false, false, UINT32_MAX - 1000);
    for (const auto failure :
         {Failure::kPipe1,
          Failure::kSlowFps,
          Failure::kState,
          Failure::kRestore,
          Failure::kRecovery,
          Failure::kProcessing,
          Failure::kStopLeak,
          Failure::kDeadline,
          Failure::kObservationGap}) {
        Run(false, false, 0, failure);
    }
    TestExtension();
}