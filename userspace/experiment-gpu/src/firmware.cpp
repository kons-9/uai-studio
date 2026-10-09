#include "extension.hpp"
#include "dma2d.hpp"
#include "verification.hpp"
#include <cstring>

extern "C" {
extern volatile unsigned int camera_pipe2_pipe1_vsync_count;
extern volatile unsigned int camera_pipe2_pipe2_frame_count;
extern volatile unsigned int experiment_camera_failures;
}

namespace experiment {
namespace {
alignas(32) std::uint8_t source[64 * 32 * 3], background[64 * 32 * 3], actual[64 * 32 * 3], expected[64 * 32 * 3];
graphics::Dma2d dma;
Services services{};
class Backend final : public graphics::ScenarioBackend {
public:
    graphics::Observation Observe() override
    {
        return {camera_pipe2_pipe1_vsync_count, camera_pipe2_pipe2_frame_count,
            experiment_camera_failures, services.camera->Recoveries(), services.camera->Running()};
    }
    graphics::Result Run(const graphics::Case &test) override
    {
        const graphics::VerificationCache cache{
            [](void *address, std::int32_t bytes) { SCB_CleanInvalidateDCache_by_Addr(address, bytes); __DSB(); },
            [](void *address, std::int32_t bytes) { __DSB(); SCB_InvalidateDCache_by_Addr(address, bytes); __DSB(); }};
        return verification_.Run(test, dma, [] { return DWT->CYCCNT; }, cache);
    }
    void Report(const char *name, const graphics::Result &result) override
    {
        char text[192];
        std::snprintf(text, sizeof(text), "GPU SCENARIO %s %s cycles=%lu max_channel_error=%u corrupted_bytes=%u\n",
            name, result.passed ? "PASS" : "FAIL", static_cast<unsigned long>(result.cycles), result.maximum_error, result.corrupted_bytes);
        services.output.Write(text);
    }
    void Summary(unsigned passed, unsigned failed, std::uint32_t transfers) override
    {
        char text[224];
        std::snprintf(text, sizeof(text), "GPU SCENARIO SUMMARY dma2d=%s pass=%u fail=%u transfers=%lu gpu2d=UNAVAILABLE npu=excluded visual=required\n",
            failed == 0 ? "PASS" : "FAIL", passed, failed, static_cast<unsigned long>(transfers));
        services.output.Write(text);
        if (services.controls_locked) { *services.controls_locked = false; }
    }
private:
    graphics::Verification verification_;
};
Backend backend;
graphics::Scenario scenario(backend);
bool announce = false;
bool StartScenario(std::uint32_t now)
{
    if (!scenario.Start(now)) { return false; }
    if (services.controls_locked) { *services.controls_locked = true; }
    announce = true;
    return true;
}
console::Status Execute(void *, int count, const char *const *arguments, const console::Writer &writer)
{
    if (count == 3 && !std::strcmp(arguments[1], "scenario")) {
        if (!std::strcmp(arguments[2], "start")) { return StartScenario(services.clock()) ? console::Status::kOk : console::Status::kInvalidState; }
        if (!std::strcmp(arguments[2], "stat")) { writer.Write(scenario.Active() ? "GPU SCENARIO active\n" : "GPU SCENARIO idle\n"); return console::Status::kOk; }
        return console::Status::kInvalidArgument;
    }
    if (scenario.Active()) { return console::Status::kInvalidState; }
    if (count != 3 || (std::strcmp(arguments[1], "cpu") && std::strcmp(arguments[1], "dma2d"))) { return console::Status::kInvalidArgument; }
    graphics::Operation operation;
    if (!std::strcmp(arguments[2], "copy") || !std::strcmp(arguments[2], "convert")) { operation = graphics::Operation::kBlit; }
    else if (!std::strcmp(arguments[2], "fill")) { operation = graphics::Operation::kFill; }
    else if (!std::strcmp(arguments[2], "blend")) { operation = graphics::Operation::kBlend; }
    else if (!std::strcmp(arguments[2], "resize")) { operation = graphics::Operation::kResize; }
    else { return console::Status::kInvalidArgument; }
    const auto format = !std::strcmp(arguments[2], "copy") ? graphics::Format::kRgb888 : graphics::Format::kRgb565;
    const std::uint32_t width = operation == graphics::Operation::kResize ? 32 : 64;
    const std::uint32_t height = operation == graphics::Operation::kResize ? 16 : 32;
    for (std::size_t index = 0; index < sizeof(source); ++index) { source[index] = static_cast<std::uint8_t>(index * 37); background[index] = static_cast<std::uint8_t>(255 - source[index]); }
    std::memset(actual, 0xa5, sizeof(actual)); std::memset(expected, 0xa5, sizeof(expected));
    graphics::Request request{operation, {source, sizeof(source), 64, 32, 192, graphics::Format::kRgb888},
        {background, sizeof(background), 64, 32, 192, graphics::Format::kRgb888},
        {expected, sizeof(expected), width, height, width * graphics::PixelBytes(format), format}, 0xc02070, 128};
    if (!graphics::Reference(request)) { return console::Status::kInvalidArgument; }
    request.destination.data = actual;
    const auto begin = DWT->CYCCNT;
    const bool success = !std::strcmp(arguments[1], "cpu") ? graphics::Reference(request) : dma.Run(request, 100);
    const auto cycles = DWT->CYCCNT - begin;
    unsigned maximum_error = 0;
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const auto result = graphics::ReadPixel(request.destination, column, row);
            auto reference = request.destination; reference.data = expected;
            const auto wanted = graphics::ReadPixel(reference, column, row);
            for (unsigned shift = 0; shift <= 16; shift += 8) {
                const int difference = int((result >> shift) & 255) - int((wanted >> shift) & 255);
                const auto error = static_cast<unsigned>(difference < 0 ? -difference : difference);
                if (error > maximum_error) { maximum_error = error; }
            }
        }
    }
    char text[128];
    const bool match = maximum_error <= (operation == graphics::Operation::kBlend ? 8U : 0U);
    std::snprintf(text, sizeof(text), "GPU %s %s %s cycles=%lu max_channel_error=%u\n", arguments[1], arguments[2], success && match ? "PASS" : "FAIL", static_cast<unsigned long>(cycles), maximum_error);
    writer.Write(text);
    return success && match ? console::Status::kOk : console::Status::kHardware;
}
}
std::size_t Register(const Services &provided, console::Command *commands, std::size_t capacity)
{
    if (!capacity || !provided.camera || !provided.clock) { return 0; }
    services = provided;
    commands[0] = {"gpu", "gpu scenario start|stat | gpu cpu|dma2d copy|convert|fill|blend|resize", Execute, nullptr};
    StartScenario(services.clock());
    return 1;
}
void Tick(std::uint32_t milliseconds, std::uint32_t)
{
    if (announce) {
        char text[128];
        std::snprintf(text, sizeof(text), "GPU SCENARIO START cases=%u stress_ms=60000 npu=excluded\n",
            static_cast<unsigned>(graphics::kCaseCount + 1));
        services.output.Write(text);
        announce = false;
    }
    scenario.Tick(milliseconds);
}
}