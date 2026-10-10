#include "tests/integration.hpp"
#include "ui/display.hpp"
#include "tests/camera/scenario.hpp"
#include "tests/camera/buffers.hpp"
#include "driver/dma2d_driver/dma2d_driver.hpp"
#include "driver/touch_driver/touch_driver.hpp"
#include "driver/board/time.hpp"

#include <atomic>
#include <cstdio>

namespace uai::hwtest::integrated {
namespace {
uai::ai::camera::CaptureConfiguration CameraConfiguration()
{
    uai::ai::camera::CaptureConfiguration configuration;
    configuration.pipe1.buffer = {uai::hwtest::camera::MainPipeFrameBufferAddress(), 400 * 480 * 2};
    configuration.pipe2.buffer = {uai::hwtest::camera::AncillaryPipeFrameBufferAddress(), 400 * 480 * 2};
    configuration.automatic_recovery = false;
    return configuration;
}
camera::Runtime camera(
    uai::ai::camera::CameraManagement::Instance(),
    CameraConfiguration()
);
console::Writer output{};
std::atomic<std::uint32_t> errors{0}, recoveries{0}, touch_errors{0}, touch_reads{0};
std::atomic<bool> running{false}, owner_done{true}, abort_requested{false};
enum class Request : unsigned {
    kNone,
    kCamera,
    kTouch
};
std::atomic<Request> request{Request::kNone};
Result owner_result{Outcome::kFail, "not-started"};
char owner_detail[128]{};
bool camera_test = false, touch_test = false, touch_ready = false, previous_touch = false, pressed_target = false;
unsigned touch_target = 0;
std::uint32_t touch_begin = 0, touch_error_baseline = 0, last_touch_poll = 0;
std::uint32_t last_frame = 0, last_progress = 0;
uai::ai::ui::TouchPoint touch;

camera::Frames Frames()
{
    const auto diagnostics = uai::ai::camera::CameraManagement::Instance().GetDiagnostics();
    return {
        diagnostics.frame_event_count,
        diagnostics.pipe2_frame_event_count,
        errors.load() + diagnostics.isp_error_count + diagnostics.dcmipp_error_count + diagnostics.camera_error_count
            + diagnostics.recovery_error_count
    };
}

void ScenarioWrite(
    void *,
    const char *text,
    std::size_t size
)
{
    output.write(output.context, text, size);
}

camera::Scenario scenario(
    camera,
    {nullptr,
     ScenarioWrite},
    uai::ai::driver::board::Milliseconds,
    Frames
);

void Finish(Result result)
{
    owner_result = result;
    camera_test = touch_test = false;
    display_log::Target(5);
    owner_done.store(true, std::memory_order_release);
}

Result Wait(
    Request operation,
    const Context &context
)
{
    if (!owner_done.load(std::memory_order_acquire)) {
        return {Outcome::kFail, "owner-busy"};
    }
    abort_requested.store(false);
    owner_done.store(false);
    request.store(operation, std::memory_order_release);
    const auto begin = context.clock();
    while (!owner_done.load(std::memory_order_acquire)) {
        if (context.Cancelled() || context.Expired(begin, 110000)) {
            abort_requested.store(true);
        }
        if (context.Expired(begin, 116000)) {
            return {Outcome::kFail, "owner-unresponsive-cleanup-not-confirmed"};
        }
        context.wait(10);
    }
    return owner_result;
}

bool InitializeTouch()
{
    const auto status = uai::ai::touch::TouchManagement::Instance().Initialize();
    return status.Ok() || status.Code() == uai::ai::common::ErrorCode::kAlreadyInitialized;
}

void PollTouch(std::uint32_t now)
{
    if (!touch_ready || now - last_touch_poll < 20) {
        return;
    }
    last_touch_poll = now;
    if (!uai::ai::touch::TouchManagement::Instance().ReadRaw(&touch).Ok()) {
        ++touch_errors;
        touch = {};
    } else {
        ++touch_reads;
        if (touch.active && (touch.x >= 800 || touch.y >= 480)) {
            ++touch_errors;
            touch = {};
        }
    }
    if (touch_test) {
        constexpr std::uint16_t centers[][2] = {{444, 64}, {756, 64}, {444, 436}, {756, 436}, {600, 240}};
        if (touch.active && !previous_touch) {
            const auto center_x = centers[touch_target][0], center_y = centers[touch_target][1];
            pressed_target = touch.x >= center_x - 24 && touch.x < center_x + 24 && touch.y >= center_y - 24
                && touch.y < center_y + 24;
        }
        if (!touch.active && previous_touch && pressed_target) {
            pressed_target = false;
            char line[96];
            std::snprintf(line, sizeof(line), "TRACE touch target=%u PASS press-release\n", touch_target + 1);
            output.Write(line);
            if (++touch_target == 5) {
                display_log::Progress("touch", 5, 5);
                Finish({Outcome::kPass, "five-targets-press-release visual=required"});
            } else {
                display_log::Target(touch_target);
                display_log::Progress("touch", touch_target, 5);
            }
        }
        if (touch_test && (touch_errors.load() != touch_error_baseline || now - touch_begin >= 60000)) {
            Finish({Outcome::kFail, "touch-read-error-or-target-timeout"});
        }
    }
    previous_touch = touch.active;
}
}

void Initialize(const console::Writer &writer)
{
    output = writer;
    const auto dma_status = uai::ai::dma2d::Dma2dManagement::Instance().Initialize();
    if (!dma_status.Ok() && dma_status.Code() != uai::ai::common::ErrorCode::kAlreadyInitialized) {
        ++errors;
        output.Write("dma2d: shared driver initialization failed\n");
    }
    if (!display_log::Initialize()) {
        output.Write("display: initialization failed\n");
    }
    std::memset(uai::hwtest::camera::MainPipeFrameBuffer(), 0, 400 * 480 * 2);
    std::memset(uai::hwtest::camera::AncillaryPipeFrameBuffer(), 0, 400 * 480 * 2);
    if (camera.Start() == console::Status::kOk) {
        running.store(true);
        output.Write("camera: pipe1=started pipe2=started\n");
    } else {
        ++errors;
        output.Write("camera: initialization failed\n");
    }
    touch_ready = display_log::Ready() && InitializeTouch();
    output.Write(touch_ready ? "touch: GT911 ready\n" : "touch: initialization failed\n");
    last_progress = uai::ai::driver::board::Milliseconds();
}

void Service()
{
    const auto now = uai::ai::driver::board::Milliseconds();
    const auto operation = request.exchange(Request::kNone, std::memory_order_acquire);
    if (operation == Request::kCamera) {
        if (scenario.Start() != console::Status::kOk) {
            Finish({Outcome::kFail, "camera-scenario-setup"});
        } else {
            camera_test = true;
        }
    } else if (operation == Request::kTouch) {
        if (!touch_ready) {
            Finish({Outcome::kFail, "GT911-initialization"});
        } else {
            touch_test = true;
            touch_target = 0;
            touch_begin = now;
            touch_error_baseline = touch_errors.load();
            previous_touch = touch.active;
            pressed_target = false;
            display_log::Target(0);
            display_log::Progress("touch", 0, 5);
        }
    }
    if (abort_requested.load() && (camera_test || touch_test)) {
        if (camera_test) {
            scenario.Stop();
        }
        Finish({Outcome::kFail, "cancelled-restoration-attempted"});
    }
    if (camera.Poll() != console::Status::kOk) {
        ++errors;
    }
    const auto frame = uai::ai::camera::CameraManagement::Instance().GetDiagnostics().pipe2_frame_event_count;
    if (frame != last_frame || !camera.Running()) {
        last_frame = frame;
        last_progress = now;
    }
    if (!camera_test && camera.Running() && now - last_progress >= 2000) {
        ++errors;
        const auto status = camera.Recover();
        output.Write(status == console::Status::kOk ? "camera: recovered\n" : "camera: recovery failed\n");
        last_progress = uai::ai::driver::board::Milliseconds();
    }
    if (camera_test) {
        scenario.Tick();
        const auto completed = scenario.Passed() + scenario.Failed();
        const auto total = camera::Scenario::StepCount();
        display_log::Progress("camera-control", completed < total ? completed : total, total);
        if (!scenario.Active()) {
            std::snprintf(
                owner_detail,
                sizeof(owner_detail),
                "stages_pass=%u stages_fail=%u readback=driver-state visual=required",
                scenario.Passed(),
                scenario.Failed()
            );
            Finish({scenario.Passed() == 32 && scenario.Failed() == 0 ? Outcome::kPass : Outcome::kFail, owner_detail});
        }
    }
    running.store(camera.Running());
    recoveries.store(camera.Recoveries());
    PollTouch(now);
}

Observation Observe()
{
    const auto frames = Frames();
    return {frames.pipe1, frames.pipe2, frames.failures, recoveries.load(), running.load()};
}

void DisplayFailure()
{
    ++errors;
}

uai::ai::ui::TouchPoint Touch()
{
    return touch;
}

Result CameraPipes(const Context &context)
{
    context.Progress(0, 1);
    const auto baseline = Observe();
    if (!baseline.running || !display_log::Ready()) {
        return {Outcome::kFail, "camera-not-running"};
    }
    const auto begin = context.clock();
    auto previous = baseline;
    auto first_progress = begin, second_progress = begin;
    while (!context.Expired(begin, 60000)) {
        if (context.Cancelled()) {
            return {Outcome::kFail, "cancelled"};
        }
        const auto now = context.clock();
        const auto current = Observe();
        if (current.pipe1 != previous.pipe1) {
            first_progress = now;
        }
        if (current.pipe2 != previous.pipe2) {
            second_progress = now;
        }
        if (!current.running || current.errors != baseline.errors || current.recoveries != baseline.recoveries
            || now - first_progress >= 1500 || now - second_progress >= 1500) {
            return {Outcome::kFail, "pipe-stall-camera-display-error-or-recovery"};
        }
        previous = current;
        context.wait(20);
    }
    static char detail[128];
    std::snprintf(
        detail,
        sizeof(detail),
        "pipe1=%lu pipe2=%lu elapsed_ms=60000 visual=required",
        static_cast<unsigned long>(previous.pipe1 - baseline.pipe1),
        static_cast<unsigned long>(previous.pipe2 - baseline.pipe2)
    );
    context.Progress(1, 1);
    return {Outcome::kPass, detail};
}

Result CameraControl(const Context &context)
{
    return Wait(Request::kCamera, context);
}
Result TouchInteractive(const Context &context)
{
    return Wait(Request::kTouch, context);
}

Result TouchRead(const Context &context)
{
    const auto reads = touch_reads.load(), failures = touch_errors.load();
    context.wait(250);
    return touch_reads.load() > reads && touch_errors.load() == failures
        ? Result{Outcome::kPass, "GT911-ID-and-repeated-poll"}
        : Result{Outcome::kFail, "GT911-init-read-or-coordinate-error"};
}

console::Status Control(
    void *,
    int count,
    const char *const *arguments,
    const console::Writer &writer
)
{
    if (camera_test || touch_test || !owner_done.load()) {
        return console::Status::kInvalidState;
    }
    return camera::Runtime::ControlCommand(&camera, count, arguments, writer);
}
console::Status Capture(
    void *,
    int count,
    const char *const *arguments,
    const console::Writer &writer
)
{
    if (camera_test || touch_test || !owner_done.load()) {
        return console::Status::kInvalidState;
    }
    return camera::Runtime::CaptureCommand(&camera, count, arguments, writer);
}

}
