#include "extension.hpp"
#include "exposure.hpp"

namespace experiment {
namespace {
Services services{};
camera::ExposureController controller;
camera::Subject subject{};
std::uint32_t expires = 0;
bool enabled = false;
camera::State demo_original{};
std::uint32_t demo_next_change = 0;
std::size_t demo_step = 0;
bool demo_running = false;
bool demo_autostart_pending = false;

struct DemoSetting {
    enum class Control { WhiteBalance, Exposure } control;
    std::uint32_t white_balance_kelvin;
    std::int32_t exposure_us;
    const char *description;
};

constexpr DemoSetting demo_settings[] = {
    {DemoSetting::Control::WhiteBalance, 0, 0, "WB=auto"},
    {DemoSetting::Control::Exposure, 0, 1000, "exposure=1000us"},
    {DemoSetting::Control::WhiteBalance, 2810, 0, "WB=2810K"},
    {DemoSetting::Control::Exposure, 0, 4000, "exposure=4000us"},
    {DemoSetting::Control::WhiteBalance, 4015, 0, "WB=4015K"},
    {DemoSetting::Control::Exposure, 0, 12000, "exposure=12000us"},
    {DemoSetting::Control::WhiteBalance, 6650, 0, "WB=6650K"},
    {DemoSetting::Control::Exposure, 0, 28000, "exposure=28000us"}
};

console::Status ApplyDemoSetting(camera::Runtime &runtime, const char *control,
                                 const char *value, const char *extra = nullptr)
{
    const char *arguments[] = {"cam", control, value, extra};
    return camera::Runtime::ControlCommand(&runtime, extra ? 4 : 3, arguments, services.output);
}

console::Status RestoreDemoSettings(camera::Runtime &runtime)
{
    return runtime.RestoreState(demo_original);
}

console::Status StartDemo(camera::Runtime &runtime, std::uint32_t milliseconds)
{
    if (demo_running) { return console::Status::kInvalidState; }
    camera::State current;
    const auto read_status = runtime.ReadState(current);
    if (read_status != console::Status::kOk) { return read_status; }
    demo_original = current;
    const auto status = ApplyDemoSetting(runtime, "ae", "off");
    if (status != console::Status::kOk) { return status; }
    demo_step = 0;
    demo_next_change = milliseconds + 1000;
    demo_running = true;
    return console::Status::kOk;
}

console::Status DemoCommand(void *context, int count, const char *const *arguments,
                            const console::Writer &writer)
{
    auto &runtime = *static_cast<camera::Runtime *>(context);
    if (count != 2) { return console::Status::kInvalidArgument; }
    if (std::strcmp(arguments[1], "start") == 0) {
        const auto status = StartDemo(runtime, services.clock());
        if (status == console::Status::kOk) { writer.Write("OK demo started; white balance and manual exposure change every 1 second\n"); }
        return status;
    }
    if (std::strcmp(arguments[1], "stop") == 0) {
        if (!demo_running) { return console::Status::kInvalidState; }
        demo_running = false;
        const auto status = RestoreDemoSettings(runtime);
        if (status == console::Status::kOk) { writer.Write("OK demo stopped; previous settings restored\n"); }
        return status;
    }
    return console::Status::kInvalidArgument;
}

void TickDemo(std::uint32_t milliseconds)
{
    if (!demo_running || !services.camera->Running() ||
        static_cast<std::int32_t>(milliseconds - demo_next_change) < 0) { return; }
    const auto &setting = demo_settings[demo_step];
    console::Status status = console::Status::kOk;
    if (setting.control == DemoSetting::Control::WhiteBalance) {
        char color_temperature[16]{};
        if (setting.white_balance_kelvin == 0) {
            std::snprintf(color_temperature, sizeof(color_temperature), "auto");
        } else {
            std::snprintf(color_temperature, sizeof(color_temperature), "%lu",
                          static_cast<unsigned long>(setting.white_balance_kelvin));
        }
        status = ApplyDemoSetting(*services.camera, "wb", color_temperature);
    } else {
        char exposure[16]{};
        std::snprintf(exposure, sizeof(exposure), "%ld", static_cast<long>(setting.exposure_us));
        status = ApplyDemoSetting(*services.camera, "manual", exposure, "0");
    }
    camera::State applied;
    if (status == console::Status::kOk) { status = services.camera->ReadState(applied); }
    if (status != console::Status::kOk) {
        demo_running = false;
        const auto restored = RestoreDemoSettings(*services.camera);
        services.output.Write(restored == console::Status::kOk
                                  ? "demo: setting failed; previous settings restored\n"
                                  : "demo: setting and restore failed\n");
        return;
    }
    char text[64]{};
    if (setting.control == DemoSetting::Control::WhiteBalance) {
        std::snprintf(text, sizeof(text), "demo: %s\n", setting.description);
    } else {
        std::snprintf(text, sizeof(text), "demo: %s applied_us=%ld applied_mdB=%ld\n",
                      setting.description, static_cast<long>(applied.reported_exposure_us),
                      static_cast<long>(applied.reported_gain_mdB));
    }
    services.output.Write(text);
    demo_step = (demo_step + 1) % (sizeof(demo_settings) / sizeof(demo_settings[0]));
    demo_next_change = milliseconds + 1000;
}

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
    if (capacity < 2) { return capacity + 1; }
    commands[0] = {"subject", "subject <kind 1..3> <confidence 1..1000> <x> <y> <w> <h> <ttl-ms> | off", SubjectCommand, nullptr};
    commands[1] = {"demo", "demo start|stop (alternate white balance and manual exposure every second; auto-starts at boot)", DemoCommand, services.camera};
    demo_autostart_pending = true;
    return 2;
}

void Tick(std::uint32_t milliseconds, std::uint32_t sequence)
{
    if (demo_autostart_pending) {
        demo_autostart_pending = false;
        if (StartDemo(*services.camera, milliseconds) == console::Status::kOk) {
            services.output.Write("demo: auto-started\n");
        } else {
            services.output.Write("demo: auto-start failed\n");
        }
    }
    TickDemo(milliseconds);
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
