#include "extension.hpp"
#include "exposure.hpp"

namespace experiment {
namespace {
Services services{};
camera::ExposureController controller;
camera::Subject subject{};
std::uint32_t expires = 0;
bool enabled = false;

console::Status SubjectCommand(void *, int count, const char *const *arguments, const console::Writer &writer)
{
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
    if (capacity == 0) { return 0; }
    commands[0] = {"subject", "subject <kind 1..3> <confidence 1..1000> <x> <y> <w> <h> <ttl-ms> | off", SubjectCommand, nullptr};
    return 1;
}

void Tick(std::uint32_t milliseconds, std::uint32_t sequence)
{
    if (!enabled || !services.camera->Running()) { return; }
    camera::Rect rectangle{};
    const bool present = static_cast<std::int32_t>(expires - milliseconds) > 0;
    if (controller.Propose(sequence, &subject, present ? 1 : 0, {0, 0, 400, 480}, services.camera->Configuration().crop, rectangle)) {
        char coordinates[4][12]{};
        std::snprintf(coordinates[0], 12, "%u", rectangle.x); std::snprintf(coordinates[1], 12, "%u", rectangle.y);
        std::snprintf(coordinates[2], 12, "%u", rectangle.width); std::snprintf(coordinates[3], 12, "%u", rectangle.height);
        const char *arguments[] = {"cam", "area", coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
        if (camera::Runtime::ControlCommand(services.camera, 6, arguments, services.output) == console::Status::kOk) { controller.Applied(rectangle); }
    }
}
}