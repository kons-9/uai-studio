#include "scenario.hpp"
#include "extension.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
volatile unsigned int camera_pipe2_pipe1_vsync_count = 0;
volatile unsigned int camera_pipe2_pipe2_frame_count = 0;
volatile unsigned int experiment_camera_failures = 0;
}

void Require(bool condition)
{
    if (!condition) { std::cerr << "scenario check failed\n"; std::exit(1); }
}
class Backend final : public experiment::graphics::ScenarioBackend {
public:
    experiment::graphics::Observation observation{0, 0, 0, 0, true};
    std::vector<std::string> names;
    unsigned passed = 0, failed = 0, summaries = 0, runs = 0;
    unsigned fail_at = UINT32_MAX;
    bool error_during_run = false;
    experiment::graphics::Observation Observe() override { return observation; }
    experiment::graphics::Result Run(const experiment::graphics::Case &) override
    {
        if (error_during_run) { ++observation.errors; }
        return {runs++ != fail_at, 123, 0};
    }
    void Report(const char *name, const experiment::graphics::Result &) override { names.emplace_back(name); }
    void Summary(unsigned pass, unsigned fail, std::uint32_t) override { passed = pass; failed = fail; ++summaries; }
};
void Advance(experiment::graphics::Scenario &scenario, Backend &backend, std::uint32_t &now, bool pipe2 = true)
{
    now += 100;
    ++backend.observation.pipe1;
    if (pipe2) { ++backend.observation.pipe2; }
    scenario.Tick(now);
}

class Camera final : public experiment::camera::Device, public experiment::camera::Backend {
public:
    experiment::console::Status Open() override { return experiment::console::Status::kOk; }
    experiment::console::Status Close() override { return experiment::console::Status::kOk; }
    experiment::console::Status Poll() override { return experiment::console::Status::kOk; }
    experiment::console::Status Configure(const experiment::camera::Geometry &) override { return experiment::console::Status::kOk; }
    experiment::console::Status Read(experiment::camera::State &) override { return experiment::console::Status::kOk; }
    experiment::console::Status AutoExposure(bool) override { return experiment::console::Status::kOk; }
    experiment::console::Status Compensation(int) override { return experiment::console::Status::kOk; }
    experiment::console::Status Manual(std::int32_t, std::int32_t) override { return experiment::console::Status::kOk; }
    experiment::console::Status Statistics(experiment::camera::Rect) override { return experiment::console::Status::kOk; }
    experiment::console::Status WhiteBalance(std::uint32_t) override { return experiment::console::Status::kOk; }
    experiment::console::Status ListWhiteBalance(const experiment::console::Writer &) override { return experiment::console::Status::kOk; }
};
std::uint32_t milliseconds = 0;
std::uint32_t Clock() { return milliseconds; }
void Write(void *context, const char *text, std::size_t size) { static_cast<std::string *>(context)->append(text, size); }
void TestFirmware()
{
    Camera device;
    experiment::camera::Runtime camera(device, device);
    Require(camera.Start() == experiment::console::Status::kOk);
    std::string output;
    bool locked = false;
    const experiment::Services services{&camera, {&output, Write}, Clock, nullptr, &locked};
    experiment::console::Command command{};
    Require(experiment::Register(services, &command, 0) == 0 && !locked);
    Require(experiment::Register(services, &command, 1) == 1 && locked);
    const char *manual[] = {"gpu", "dma2d", "copy"};
    Require(command.execute(nullptr, 3, manual, services.output) == experiment::console::Status::kInvalidState);
    const char *start[] = {"gpu", "scenario", "start"};
    Require(command.execute(nullptr, 3, start, services.output) == experiment::console::Status::kInvalidState);
    const char *stat[] = {"gpu", "scenario", "stat"};
    Require(command.execute(nullptr, 3, stat, services.output) == experiment::console::Status::kOk);
    for (unsigned iteration = 0; locked && iteration < 1000; ++iteration) {
        milliseconds += 100;
        ++camera_pipe2_pipe1_vsync_count; ++camera_pipe2_pipe2_frame_count;
        experiment::Tick(milliseconds, camera_pipe2_pipe2_frame_count);
    }
    Require(!locked && output.find("GPU SCENARIO START cases=26") != std::string::npos);
    Require(output.find("GPU SCENARIO SUMMARY dma2d=FAIL") != std::string::npos);
    Require(output.find("gpu2d=UNAVAILABLE npu=excluded") != std::string::npos);
    const auto completed = output.size();
    experiment::Tick(milliseconds + 100, camera_pipe2_pipe2_frame_count);
    Require(output.size() == completed);
    Require(command.execute(nullptr, 3, start, services.output) == experiment::console::Status::kOk && locked);
    for (unsigned iteration = 0; locked && iteration < 1000; ++iteration) {
        milliseconds += 100;
        ++camera_pipe2_pipe1_vsync_count; ++camera_pipe2_pipe2_frame_count;
        experiment::Tick(milliseconds, camera_pipe2_pipe2_frame_count);
    }
    Require(!locked);
}
int main()
{
    Backend backend;
    experiment::graphics::Scenario scenario(backend);
    std::uint32_t now = UINT32_MAX - 1000;
    Require(scenario.Start(now) && !scenario.Start(now));
    while (scenario.Active()) { Advance(scenario, backend, now); }
    Require(backend.passed == experiment::graphics::kCaseCount + 1 && backend.failed == 0 && backend.summaries == 1);
    Require(backend.names.back() == "camera-display-stress-60s");
    scenario.Tick(now + 100);
    Require(backend.summaries == 1);
    backend.fail_at = backend.runs + 3;
    Require(scenario.Start(now));
    while (scenario.Active()) { Advance(scenario, backend, now); }
    Require(backend.failed == 1 && backend.passed == experiment::graphics::kCaseCount && backend.summaries == 2);
    backend.fail_at = UINT32_MAX;
    Require(scenario.Start(now));
    while (scenario.Active()) { Advance(scenario, backend, now, false); }
    Require(backend.failed == 1 && backend.names.back() == "camera-display-stress-60s");
    Require(scenario.Start(now));
    for (std::size_t index = 0; index < experiment::graphics::kCaseCount; ++index) { Advance(scenario, backend, now); }
    ++backend.observation.errors;
    Advance(scenario, backend, now);
    Require(!scenario.Active() && backend.failed == 1);
    Require(scenario.Start(now));
    for (std::size_t index = 0; index < experiment::graphics::kCaseCount; ++index) { Advance(scenario, backend, now); }
    now += 1501;
    scenario.Tick(now);
    Require(!scenario.Active() && backend.failed == 1);
    Require(scenario.Start(now));
    for (std::size_t index = 0; index < experiment::graphics::kCaseCount; ++index) { Advance(scenario, backend, now); }
    backend.error_during_run = true;
    Advance(scenario, backend, now);
    Require(!scenario.Active() && backend.failed == 1);
    TestFirmware();
}