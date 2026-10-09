#include "extension.hpp"
#include "dma2d.hpp"
#include "verification.hpp"
#include "middleware/ui/canvas.hpp"
#include "driver/frame_buffer.hpp"
#include "driver/display_driver.hpp"
#include <cstring>

extern "C" {
#include "stm32n6570_discovery_lcd.h"
extern LTDC_HandleTypeDef hlcd_ltdc;
extern volatile unsigned int camera_pipe2_pipe1_vsync_count;
extern volatile unsigned int camera_pipe2_pipe2_frame_count;
extern volatile unsigned int experiment_camera_failures;
}

namespace experiment {
namespace {
alignas(32) std::uint8_t source[64 * 32 * 3],
    background[64 * 32 * 3],
    actual[64 * 32 * 3],
    expected[64 * 32 * 3];
constexpr std::uint32_t kVisualWidth = 320;
constexpr std::uint32_t kVisualHeight = 112;
constexpr std::uint32_t kVisualStride = kVisualWidth * sizeof(std::uint16_t);
constexpr std::size_t kVisualBytes = kVisualStride * kVisualHeight;
constexpr std::uint32_t kVisualX = uai::camera_pipe2::driver::kDisplayWidth - kVisualWidth;
constexpr std::uint32_t kVisualY = 32;
alignas(32) std::uint16_t visual_pixels[kVisualWidth * kVisualHeight];
graphics::Dma2d dma;
Services services{};
bool visual_ready = false;
bool visual_enabled = true;
bool visual_dirty = false;
bool visual_output_verified = false;
bool visual_output_error_reported = false;
const char *visual_error = "not-configured";
char scenario_action[40] = "STARTING";
std::uint32_t last_scenario_tick = 0;
unsigned visual_report_count = 0;
constexpr std::uint32_t kScenarioVisualIntervalMs = 600;

void ConfigureOverlayPersistence(bool preserve)
{
#if defined(EXPERIMENT_GPU_VISUAL)
    uai::camera_pipe2::driver::SetVisualOverlayPreserved(preserve);
#else
    (void)preserve;
#endif
}

graphics::Image VisualRegion(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height
)
{
    const auto offset = y * kVisualStride + x * sizeof(std::uint16_t);
    auto *data = reinterpret_cast<std::uint8_t *>(visual_pixels) + offset;
    return {data, kVisualBytes - offset, width, height, kVisualStride, graphics::Format::kRgb565};
}

bool FillVisualRegion(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t color
)
{
    graphics::Request request{};
    request.operation = graphics::Operation::kFill;
    request.destination = VisualRegion(x, y, width, height);
    request.color = color;
    return dma.Run(request, 100);
}

void DrawVisualStatus(
    const char *heading,
    const char *detail,
    const char *result
)
{
    if (!visual_ready) {
        return;
    }
    uai::ai::ui::Canvas canvas(visual_pixels, kVisualWidth, kVisualHeight);
    canvas.FillRect({0, 24, static_cast<std::uint16_t>(kVisualWidth), 88}, 0x0010);
    canvas.DrawText(8, 28, heading, 2, 0xffff);
    canvas.DrawText(8, 52, detail, 2, 0xffe0);
    canvas.DrawText(8, 76, result, 2, 0x07e0);
    SCB_CleanDCache_by_Addr(visual_pixels, static_cast<std::int32_t>(kVisualBytes));
    __DSB();
    visual_dirty = true;
}

graphics::Image DisplayVisualRegion()
{
    auto *frame = reinterpret_cast<std::uint8_t *>(
        static_cast<std::uintptr_t>(hlcd_ltdc.LayerCfg[0].FBStartAdress)
    );
    if (frame == nullptr) {
        return {};
    }
    constexpr auto stride = uai::camera_pipe2::driver::kDisplayBytesPerLine;
    const auto offset = kVisualY * stride + kVisualX * sizeof(std::uint16_t);
    constexpr auto visible_bytes = (kVisualHeight - 1U) * stride + kVisualStride;
    constexpr auto cache_bytes = (visible_bytes + 31U) & ~std::size_t(31U);
    return {
        frame + offset,
        cache_bytes,
        kVisualWidth,
        kVisualHeight,
        stride,
        graphics::Format::kRgb565
    };
}

bool PresentVisual()
{
    if (!visual_ready) {
        return false;
    }
    if (!visual_enabled) {
        return true;
    }
    if (!visual_dirty) {
        return true;
    }
    auto source = VisualRegion(0, 0, kVisualWidth, kVisualHeight);
    auto destination = DisplayVisualRegion();
    if (destination.data == nullptr) {
        return false;
    }
    graphics::Request request{};
    request.operation = graphics::Operation::kBlit;
    request.source = source;
    request.destination = destination;
    if (!dma.Run(request, 100)) {
        return false;
    }
    if (!visual_output_verified) {
        const bool matches = graphics::ReadPixel(destination, 40, 12) == 0xff0000
            && graphics::ReadPixel(destination, 120, 12) == 0x00ff00
            && graphics::ReadPixel(destination, 200, 12) == 0x0000ff
            && graphics::ReadPixel(destination, 280, 12) == 0xffffff;
        if (!matches) {
            return false;
        }
        visual_output_verified = true;
    }
    visual_dirty = false;
    return true;
}

const char *VisualName(const char *name)
{
    if (!std::strcmp(name, "reject-background-overlap")) {
        return "reject-bg-overlap";
    }
    if (!std::strcmp(name, "reuse-after-rejection")) {
        return "reuse-after-reject";
    }
    if (!std::strcmp(name, "camera-display-stress-60s")) {
        return "camera display stress";
    }
    return name;
}

bool VisualPixelsMatch()
{
    const auto panel = VisualRegion(0, 0, kVisualWidth, kVisualHeight);
    return graphics::ReadPixel(panel, 40, 12) == 0xff0000 && graphics::ReadPixel(panel, 120, 12) == 0x00ff00
        && graphics::ReadPixel(panel, 200, 12) == 0x0000ff && graphics::ReadPixel(panel, 280, 12) == 0xffffff;
}

bool ConfigureVisual()
{
    if (!FillVisualRegion(0, 0, kVisualWidth, kVisualHeight, 0xffe0)
        || !FillVisualRegion(0, 0, 80, 24, 0xff0000) || !FillVisualRegion(80, 0, 80, 24, 0x00ff00)
        || !FillVisualRegion(160, 0, 80, 24, 0x0000ff) || !FillVisualRegion(240, 0, 80, 24, 0xffffff)) {
        visual_error = "dma2d-fill";
        return false;
    }
    if (!VisualPixelsMatch()) {
        visual_error = "pixel-readback";
        return false;
    }

    visual_ready = true;
    DrawVisualStatus("GPU DMA2D TEST", "STARTING", "RUNNING");
    visual_error = "none";
    return true;
}

bool SetVisualVisible(bool visible)
{
    if (!visual_ready) {
        return false;
    }
    visual_enabled = visible;
    visual_dirty = visual_dirty || visible;
    ConfigureOverlayPersistence(visible);
    return true;
}

class Backend final : public graphics::ScenarioBackend {
public:
    graphics::Observation Observe() override
    {
        return {
            camera_pipe2_pipe1_vsync_count,
            camera_pipe2_pipe2_frame_count,
            experiment_camera_failures,
            services.camera->Recoveries(),
            services.camera->Running()
        };
    }
    graphics::Result Run(const graphics::Case &test) override
    {
        const graphics::VerificationCache cache{
            [](void *address, std::int32_t bytes) {
                SCB_CleanInvalidateDCache_by_Addr(address, bytes);
                __DSB();
            },
            [](void *address, std::int32_t bytes) {
                __DSB();
                SCB_InvalidateDCache_by_Addr(address, bytes);
                __DSB();
            }
        };
        return verification_.Run(
            test,
            dma,
            [] {
                return DWT->CYCCNT;
            },
            cache
        );
    }
    void Report(
        const char *name,
        const graphics::Result &result
    ) override
    {
        ++visual_report_count;
        char text[192];
        std::snprintf(
            text,
            sizeof(text),
            "GPU SCENARIO %02u/%02u %s %s cycles=%lu max_channel_error=%u corrupted_bytes=%u\n",
            visual_report_count,
            static_cast<unsigned>(graphics::kCaseCount + 1),
            name,
            result.passed ? "PASS" : "FAIL",
            static_cast<unsigned long>(result.cycles),
            result.maximum_error,
            result.corrupted_bytes
        );
        services.output.Write(text);
        if (visual_report_count == graphics::kCaseCount) {
            std::snprintf(scenario_action, sizeof(scenario_action), "60S STRESS");
            DrawVisualStatus("DMA2D / CAMERA", "60S STRESS RUN", "PIPE1 / PIPE2");
        } else if (visual_report_count > graphics::kCaseCount) {
            std::snprintf(scenario_action, sizeof(scenario_action), "STRESS COMPLETE");
            DrawVisualStatus("CAMERA DISPLAY", "STRESS COMPLETE", result.passed ? "PASS" : "FAIL");
        } else {
            char heading[24];
            std::snprintf(
                heading,
                sizeof(heading),
                "TEST %02u/%02u",
                visual_report_count,
                static_cast<unsigned>(graphics::kCaseCount + 1)
            );
            std::snprintf(
                scenario_action,
                sizeof(scenario_action),
                "%02u/%02u %.26s",
                visual_report_count,
                static_cast<unsigned>(graphics::kCaseCount + 1),
                VisualName(name)
            );
            DrawVisualStatus(heading, VisualName(name), result.passed ? "PASS" : "FAIL");
        }
    }
    void Summary(
        unsigned passed,
        unsigned failed,
        std::uint32_t transfers
    ) override
    {
        char text[224];
        std::snprintf(
            text,
            sizeof(text),
            "GPU SCENARIO SUMMARY dma2d=%s pass=%u fail=%u transfers=%lu gpu2d=UNAVAILABLE npu=excluded "
            "visual=required\n",
            failed == 0 ? "PASS" : "FAIL",
            passed,
            failed,
            static_cast<unsigned long>(transfers)
        );
        services.output.Write(text);
        char detail[32], transfer_text[32];
        std::snprintf(
            detail,
            sizeof(detail),
            "%s %u/%u",
            failed == 0 ? "PASS" : "FAIL",
            passed,
            passed + failed
        );
        std::snprintf(transfer_text, sizeof(transfer_text), "TRANSFERS %lu", static_cast<unsigned long>(transfers));
        std::snprintf(scenario_action, sizeof(scenario_action), "%s %u/%u", failed == 0 ? "PASS" : "FAIL", passed, passed + failed);
        DrawVisualStatus("DMA2D TEST RESULT", detail, transfer_text);
        if (services.controls_locked) {
            *services.controls_locked = false;
        }
    }

private:
    graphics::Verification verification_;
};
Backend backend;
graphics::Scenario scenario(backend);
bool announce = false;
bool StartScenario(std::uint32_t now)
{
    if (!scenario.Start(now)) {
        return false;
    }
    if (services.controls_locked) {
        *services.controls_locked = true;
    }
    visual_report_count = 0;
    last_scenario_tick = now - kScenarioVisualIntervalMs;
    std::snprintf(scenario_action, sizeof(scenario_action), "STARTING 0/%u", static_cast<unsigned>(graphics::kCaseCount + 1));
    DrawVisualStatus("GPU DMA2D TEST", "STARTING", "RUNNING");
    announce = true;
    return true;
}
console::Status Execute(
    void *,
    int count,
    const char *const *arguments,
    const console::Writer &writer
)
{
    if (count == 3 && !std::strcmp(arguments[1], "scenario")) {
        if (!std::strcmp(arguments[2], "start")) {
            return StartScenario(services.clock()) ? console::Status::kOk : console::Status::kInvalidState;
        }
        if (!std::strcmp(arguments[2], "stat")) {
            writer.Write(scenario.Active() ? "GPU SCENARIO active\n" : "GPU SCENARIO idle\n");
            return console::Status::kOk;
        }
        return console::Status::kInvalidArgument;
    }
    if (count == 3 && !std::strcmp(arguments[1], "visual")) {
        const bool visible = !std::strcmp(arguments[2], "on");
        if (!visible && std::strcmp(arguments[2], "off")) {
            return console::Status::kInvalidArgument;
        }
        if (!SetVisualVisible(visible)) {
            return console::Status::kHardware;
        }
        writer.Write(visible ? "GPU VISUAL layer=0 on\n" : "GPU VISUAL layer=0 off\n");
        return console::Status::kOk;
    }
    if (scenario.Active()) {
        return console::Status::kInvalidState;
    }
    if (count != 3 || (std::strcmp(arguments[1], "cpu") && std::strcmp(arguments[1], "dma2d"))) {
        return console::Status::kInvalidArgument;
    }
    graphics::Operation operation;
    if (!std::strcmp(arguments[2], "copy") || !std::strcmp(arguments[2], "convert")) {
        operation = graphics::Operation::kBlit;
    } else if (!std::strcmp(arguments[2], "fill")) {
        operation = graphics::Operation::kFill;
    } else if (!std::strcmp(arguments[2], "blend")) {
        operation = graphics::Operation::kBlend;
    } else if (!std::strcmp(arguments[2], "resize")) {
        operation = graphics::Operation::kResize;
    } else {
        return console::Status::kInvalidArgument;
    }
    const auto format = !std::strcmp(arguments[2], "copy") || operation == graphics::Operation::kFill
        ? graphics::Format::kRgb888
        : graphics::Format::kRgb565;
    const std::uint32_t width = operation == graphics::Operation::kResize ? 32 : 64;
    const std::uint32_t height = operation == graphics::Operation::kResize ? 16 : 32;
    for (std::size_t index = 0; index < sizeof(source); ++index) {
        source[index] = static_cast<std::uint8_t>(index * 37);
        background[index] = static_cast<std::uint8_t>(255 - source[index]);
    }
    std::memset(actual, 0xa5, sizeof(actual));
    std::memset(expected, 0xa5, sizeof(expected));
    graphics::Request request{
        operation,
        {source, sizeof(source), 64, 32, 192, graphics::Format::kRgb888},
        {background, sizeof(background), 64, 32, 192, graphics::Format::kRgb888},
        {expected, sizeof(expected), width, height, width * graphics::PixelBytes(format), format},
        0xc02070,
        128
    };
    if (!graphics::Reference(request)) {
        return console::Status::kInvalidArgument;
    }
    request.destination.data = actual;
    const auto begin = DWT->CYCCNT;
    const bool success = !std::strcmp(arguments[1], "cpu") ? graphics::Reference(request) : dma.Run(request, 100);
    const auto cycles = DWT->CYCCNT - begin;
    unsigned maximum_error = 0;
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const auto result = graphics::ReadPixel(request.destination, column, row);
            auto reference = request.destination;
            reference.data = expected;
            const auto wanted = graphics::ReadPixel(reference, column, row);
            for (unsigned shift = 0; shift <= 16; shift += 8) {
                const int difference = int((result >> shift) & 255) - int((wanted >> shift) & 255);
                const auto error = static_cast<unsigned>(difference < 0 ? -difference : difference);
                if (error > maximum_error) {
                    maximum_error = error;
                }
            }
        }
    }
    auto reference = request.destination;
    reference.data = expected;
    const auto first_actual = graphics::ReadPixel(request.destination, 0, 0);
    const auto first_expected = graphics::ReadPixel(reference, 0, 0);
    char text[192];
    const bool match = maximum_error <= (operation == graphics::Operation::kBlend ? 8U : 0U);
    std::snprintf(
        text,
        sizeof(text),
        "GPU %s %s %s cycles=%lu max_channel_error=%u first=%06lx/%06lx\n",
        arguments[1],
        arguments[2],
        success && match ? "PASS" : "FAIL",
        static_cast<unsigned long>(cycles),
        maximum_error,
        static_cast<unsigned long>(first_actual),
        static_cast<unsigned long>(first_expected)
    );
    writer.Write(text);
    return success && match ? console::Status::kOk : console::Status::kHardware;
}
}
std::size_t Register(
    const Services &provided,
    console::Command *commands,
    std::size_t capacity
)
{
    if (!capacity || !provided.camera || !provided.clock) {
        return 0;
    }
    services = provided;
    commands[0] = {
        "gpu",
        "gpu scenario start|stat | gpu visual on|off | gpu cpu|dma2d copy|convert|fill|blend|resize",
        Execute,
        nullptr
    };
    visual_ready = ConfigureVisual();
    ConfigureOverlayPersistence(visual_ready);
    char visual_status[96];
    std::snprintf(
        visual_status,
        sizeof(visual_status),
        "GPU VISUAL %s dma2d=pixel-checked output=layer0 status=%s\n",
        visual_ready ? "PASS" : "FAIL",
        visual_ready ? "pending" : visual_error
    );
    services.output.Write(visual_status);
    StartScenario(services.clock());
    return 1;
}
void Tick(
    std::uint32_t milliseconds,
    std::uint32_t
)
{
    if (announce) {
        char text[128];
        std::snprintf(
            text,
            sizeof(text),
            "GPU SCENARIO START cases=%u case_interval_ms=%lu stress_ms=60000 npu=excluded\n",
            static_cast<unsigned>(graphics::kCaseCount + 1),
            static_cast<unsigned long>(kScenarioVisualIntervalMs)
        );
        services.output.Write(text);
        announce = false;
    }
    if (!scenario.Active()) {
        if (visual_enabled && PresentVisual()) {
            if (!visual_output_error_reported) {
                services.output.Write("GPU VISUAL PASS framebuffer=layer0 readback=ok\n");
                visual_output_error_reported = true;
            }
        } else if (visual_enabled && !visual_output_error_reported) {
            services.output.Write("GPU VISUAL FAIL framebuffer=layer0\n");
            visual_output_error_reported = true;
        }
        return;
    }
    const auto interval = visual_report_count < graphics::kCaseCount ? kScenarioVisualIntervalMs : 0U;
    if (interval == 0 || milliseconds - last_scenario_tick >= interval) {
        last_scenario_tick = milliseconds;
        scenario.Tick(milliseconds);
    }
    if (visual_enabled && PresentVisual()) {
        if (!visual_output_error_reported) {
            services.output.Write("GPU VISUAL PASS framebuffer=layer0 readback=ok\n");
            visual_output_error_reported = true;
        }
    } else if (visual_enabled && !visual_output_error_reported) {
        services.output.Write("GPU VISUAL FAIL framebuffer=layer0\n");
        visual_output_error_reported = true;
    }
}
const char *CurrentScenarioAction()
{
    return scenario_action[0] ? scenario_action : nullptr;
}
}
