#include "extension.hpp"
#include "exposure.hpp"
#include "scenario.hpp"
#include <new>

extern "C" {
extern volatile unsigned int camera_pipe2_pipe1_vsync_count;
extern volatile unsigned int camera_pipe2_pipe2_frame_count;
extern volatile unsigned int experiment_camera_failures;
}

namespace experiment {
namespace {
Services services{};
camera::ExposureController controller;
camera::Subject subject{};
std::uint32_t expires = 0;
bool enabled = false;
alignas(camera::Scenario) unsigned char scenario_storage[sizeof(camera::Scenario)];
camera::Scenario *scenario = nullptr;
bool autostart = false;

camera::Frames ReadFrames()
{
    return {camera_pipe2_pipe1_vsync_count, camera_pipe2_pipe2_frame_count, experiment_camera_failures};
}

console::Status ScenarioCommand(void *, int count, const char *const *arguments,
                            const console::Writer &writer)
{
    if (count != 2 || !scenario) { return console::Status::kInvalidArgument; }
    if (std::strcmp(arguments[1], "stat") == 0) {
        char line[96];
        std::snprintf(line, sizeof(line), "CAMTEST STATUS running=%u pass=%u fail=%u\n",
                      unsigned(scenario->Active()), scenario->Passed(), scenario->Failed());
        writer.Write(line); return console::Status::kOk;
    }
    if (std::strcmp(arguments[1], "start") == 0) {
        enabled = false;
        const auto status = scenario->Start();
        if (services.controls_locked) { *services.controls_locked = scenario->Active(); }
        return status;
    }
    if (std::strcmp(arguments[1], "stop") == 0) {
        autostart = false;
        const auto status = scenario->Stop();
        if (services.controls_locked) { *services.controls_locked = scenario->Active(); }
        return status;
    }
    return console::Status::kInvalidArgument;
}

console::Status SubjectCommand(void *, int count, const char *const *arguments, const console::Writer &writer)
{
    if ((scenario && scenario->Active()) || (services.controls_locked && *services.controls_locked)) {
        return console::Status::kInvalidState;
    }
    if (count == 2 && std::strcmp(arguments[1], "off") == 0) { enabled = false; writer.Write("OK subject off\n"); return console::Status::kOk; }
    if (count != 8) { return console::Status::kInvalidArgument; }
    std::int32_t values[7]{};
    for (int index = 1; index < count; ++index) {
        if (!camera::ParseInteger(arguments[index], values[index - 1]) || values[index - 1] < 0) { return console::Status::kInvalidArgument; }
    }
    if (values[0] < 1 || values[0] > 3 || values[1] < 1 || values[1] > 1000 || values[6] > 60000) { return console::Status::kInvalidArgument; }
    camera::Rect rectangle{std::uint32_t(values[2]), std::uint32_t(values[3]), std::uint32_t(values[4]), std::uint32_t(values[5])};
    if (!camera::Inside(rectangle, 400, 480)) { return console::Status::kInvalidArgument; }
    subject = {static_cast<camera::SubjectKind>(values[0]), static_cast<std::uint16_t>(values[1]), rectangle};
    expires = services.clock() + static_cast<std::uint32_t>(values[6]); enabled = true;
    writer.Write("OK subject\n"); return console::Status::kOk;
}
}

std::size_t Register(const Services &provided, console::Command *commands, std::size_t capacity)
{
    services = provided;
    if (capacity < 2) { return capacity + 1; }
    scenario = new (scenario_storage) camera::Scenario(*services.camera, services.output, services.clock, ReadFrames);
    commands[0] = {"subject", "subject <kind 1..3> <confidence 1..1000> <x> <y> <w> <h> <ttl-ms> | off", SubjectCommand, nullptr};
    commands[1] = {"scenario", "scenario start|stop|stat (one pass at boot; visual review required)", ScenarioCommand, nullptr};
    autostart = true;
    if (services.controls_locked) { *services.controls_locked = true; }
    return 2;
}

void Tick(std::uint32_t milliseconds, std::uint32_t sequence)
{
    if (autostart) {
        autostart = false;
        enabled = false;
        scenario->Start();
    }
    if (scenario) {
        scenario->Tick();
        if (services.controls_locked) { *services.controls_locked = scenario->Active(); }
        if (scenario->Active()) { return; }
    }
    if (!enabled || !services.camera->Running()) { return; }
    camera::Rect rectangle{};
    const bool present = static_cast<std::int32_t>(expires - milliseconds) > 0;
    if (controller.Propose(sequence, &subject, present ? 1 : 0, {0, 0, 400, 480}, services.camera->Configuration().crop, rectangle)) {
        char coordinates[4][12]{};
        std::snprintf(coordinates[0], 12, "%lu", static_cast<unsigned long>(rectangle.x));
        std::snprintf(coordinates[1], 12, "%lu", static_cast<unsigned long>(rectangle.y));
        std::snprintf(coordinates[2], 12, "%lu", static_cast<unsigned long>(rectangle.width));
        std::snprintf(coordinates[3], 12, "%lu", static_cast<unsigned long>(rectangle.height));
        const char *arguments[] = {"cam", "area", coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
        if (camera::Runtime::ControlCommand(services.camera, 6, arguments, services.output) == console::Status::kOk) { controller.Applied(rectangle); }
    }
}
}
