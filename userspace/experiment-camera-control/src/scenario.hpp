#pragma once

#include "runtime.hpp"
#include <initializer_list>

namespace experiment::camera {

struct Frames {
    std::uint32_t pipe1 = 0;
    std::uint32_t pipe2 = 0;
    std::uint32_t failures = 0;
};

inline bool SameRect(
    const Rect &first,
    const Rect &second
)
{
    return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
}

inline bool SameGeometry(
    const Geometry &first,
    const Geometry &second
)
{
    return first.fps == second.fps && first.horizontal == second.horizontal && first.vertical == second.vertical
        && SameRect(first.crop, second.crop);
}

inline bool SameSettings(
    const State &first,
    const State &second
)
{
    const auto difference = [](std::int32_t first_value, std::int32_t second_value) {
        const auto delta = std::int64_t(first_value) - second_value;
        return delta < 0 ? -delta : delta;
    };
    return first.auto_exposure == second.auto_exposure && first.compensation == second.compensation
        && first.auto_white_balance == second.auto_white_balance && SameRect(first.statistics, second.statistics)
        && (first.auto_white_balance || first.color_temperature == second.color_temperature)
        && (first.auto_exposure
            || (difference(first.reported_exposure_us, second.reported_exposure_us) <= 100
                && difference(first.reported_gain_mdB, second.reported_gain_mdB) <= 300));
}

class Scenario {
public:
    Scenario(
        Runtime &runtime,
        console::Writer output,
        std::uint32_t (*clock)(),
        Frames (*frames)()
    )
        : runtime_(runtime),
          output_(output),
          clock_(clock),
          frames_(frames)
    {}

    console::Status Start()
    {
        if (active_) {
            return console::Status::kInvalidState;
        }
        passed_ = failed_ = 0;
        stage_ = 0;
        stage_started_ = false;
        cleanup_status_ = console::Status::kOk;
        const auto status = runtime_.ReadState(original_);
        if (status != console::Status::kOk) {
            Report("setup", "FAIL", "state-read");
            ++failed_;
            Summary();
            return status;
        }
        original_geometry_ = expected_geometry_ = runtime_.Configuration();
        expected_ = original_;
        initial_failures_ = frames_().failures;
        active_ = true;
        output_.Write("\nCAMTEST START readback=driver-state visual=required\n");
        Enter();
        return active_ ? console::Status::kOk : console::Status::kHardware;
    }

    console::Status Stop()
    {
        if (!active_) {
            return console::Status::kInvalidState;
        }
        Fail("aborted");
        return cleanup_status_;
    }

    bool Active() const { return active_; }
    unsigned Passed() const { return passed_; }
    unsigned Failed() const { return failed_; }
    const char *CurrentAction() const { return active_ ? steps_[stage_].name : nullptr; }

    void Tick()
    {
        if (!active_) {
            return;
        }
        const auto now = clock_();
        const auto &step = steps_[stage_];
        if (frames_().failures != initial_failures_) {
            Fail("camera-or-display-error");
            return;
        }
        if (runtime_.Recoveries() != recoveries_) {
            Fail("unexpected-recovery");
            return;
        }
        if (runtime_.Running() != (step.action != Action::kStop)) {
            Fail("capture-state");
            return;
        }
        if (!measuring_) {
            if (now - entered_ < kWarmupMs) {
                return;
            }
            baseline_ = frames_();
            latest_ = baseline_;
            measurement_ = now;
            pipe1_progress_ = pipe2_progress_ = sampled_ = now;
            measuring_ = true;
            return;
        }
        const auto current = frames_();
        const auto first = current.pipe1 - baseline_.pipe1;
        const auto second = current.pipe2 - baseline_.pipe2;
        if (step.action != Action::kStop) {
            if (now - sampled_ > 1500) {
                Fail("observation-gap");
                return;
            }
            if (current.pipe1 != latest_.pipe1) {
                pipe1_progress_ = now;
            }
            if (current.pipe2 != latest_.pipe2) {
                pipe2_progress_ = now;
            }
            if (now - pipe1_progress_ >= 1500 || now - pipe2_progress_ >= 1500) {
                Fail("frame-timeout");
                return;
            }
        }
        latest_ = current;
        sampled_ = now;
        if (now - measurement_ < step.duration_ms) {
            return;
        }
        if (step.action == Action::kStop) {
            if (first || second) {
                Fail("frames-while-stopped");
                return;
            }
        } else {
            if (!first || !second) {
                Fail("frame-timeout");
                return;
            }
            State state;
            if (runtime_.ReadState(state) != console::Status::kOk || !SameSettings(state, expected_)
                || !SameGeometry(runtime_.Configuration(), expected_geometry_)) {
                Fail("state-mismatch");
                return;
            }
            if (step.action == Action::kFps) {
                const auto elapsed = now - measurement_;
                const auto target = expected_geometry_.fps;
                for (const auto count : {first, second}) {
                    const auto rate = std::uint64_t(count) * 1000;
                    if (rate < std::uint64_t(target) * elapsed * 7 / 10
                        || rate > std::uint64_t(target) * elapsed * 13 / 10) {
                        Fail("fps-out-of-range");
                        return;
                    }
                }
            }
        }
        char detail[128];
        std::snprintf(
            detail,
            sizeof(detail),
            "pipe1=%lu pipe2=%lu elapsed_ms=%lu",
            static_cast<unsigned long>(first),
            static_cast<unsigned long>(second),
            static_cast<unsigned long>(now - measurement_)
        );
        Report(step.name, "PASS", detail);
        ++passed_;
        if (++stage_ == sizeof(steps_) / sizeof(steps_[0])) {
            active_ = false;
            Summary();
            return;
        }
        Enter();
    }

private:
    // Keep the scenario responsive while allowing enough frames to check state
    // changes. FPS checks use a longer window so their tolerance stays useful.
    inline static constexpr std::uint32_t kWarmupMs = 250;
    inline static constexpr std::uint32_t kCheckMs = 1000;
    inline static constexpr std::uint32_t kFpsCheckMs = 2000;
    inline static constexpr std::uint32_t kShortCheckMs = 500;
    inline static constexpr std::uint32_t kStabilityCheckMs = 10000;

    enum class Action {
        kObserve,
        kManual,
        kExposure,
        kWhiteBalance,
        kAutoExposure,
        kCompensation,
        kStatistics,
        kFps,
        kFlip,
        kCrop,
        kInvalid,
        kStop,
        kStart,
        kRecover,
        kRestore
    };
    struct Step {
        const char *name;
        Action action;
        std::uint32_t duration_ms;
        std::int32_t value;
    };
    inline static constexpr Step steps_[] = {
        {"baseline", Action::kObserve, kCheckMs, 0},
        {"manual", Action::kManual, kCheckMs, 0},
        {"exposure-1000", Action::kExposure, kCheckMs, 1000},
        {"wb-auto", Action::kWhiteBalance, kCheckMs, 0},
        {"wb-2810", Action::kWhiteBalance, kCheckMs, 2810},
        {"exposure-4000", Action::kExposure, kCheckMs, 4000},
        {"wb-4015", Action::kWhiteBalance, kCheckMs, 4015},
        {"exposure-12000", Action::kExposure, kCheckMs, 12000},
        {"wb-6650", Action::kWhiteBalance, kCheckMs, 6650},
        {"exposure-28000", Action::kExposure, kCheckMs, 28000},
        {"ae-on", Action::kAutoExposure, kCheckMs, 0},
        {"ev-minus", Action::kCompensation, kCheckMs, -2},
        {"ev-plus", Action::kCompensation, kCheckMs, 2},
        {"statistics-center", Action::kStatistics, kCheckMs, 0},
        {"manual-fixed", Action::kManual, kCheckMs, 0},
        {"fps-10", Action::kFps, kFpsCheckMs, 10},
        {"fps-15", Action::kFps, kFpsCheckMs, 15},
        {"fps-20", Action::kFps, kFpsCheckMs, 20},
        {"fps-25", Action::kFps, kFpsCheckMs, 25},
        {"fps-30", Action::kFps, kFpsCheckMs, 30},
        {"flip-horizontal", Action::kFlip, kCheckMs, 1},
        {"flip-vertical", Action::kFlip, kCheckMs, 2},
        {"flip-both", Action::kFlip, kCheckMs, 3},
        {"flip-normal", Action::kFlip, kCheckMs, 0},
        {"crop", Action::kCrop, kCheckMs, 0},
        {"invalid-input", Action::kInvalid, kShortCheckMs, 0},
        {"stop", Action::kStop, kShortCheckMs, 0},
        {"restart", Action::kStart, kCheckMs, 0},
        {"recovery-1", Action::kRecover, kCheckMs, 0},
        {"recovery-2", Action::kRecover, kCheckMs, 0},
        {"stability", Action::kObserve, kStabilityCheckMs, 0},
        {"restore", Action::kRestore, kShortCheckMs, 0},
    };

    console::Status RestoreOriginal()
    {
        auto status = runtime_.Running() ? console::Status::kOk : runtime_.Start();
        if (status == console::Status::kOk && !SameGeometry(runtime_.Configuration(), original_geometry_)) {
            status = runtime_.Change(original_geometry_);
        }
        if (status == console::Status::kOk) {
            status = runtime_.RestoreState(original_);
        }
        State actual;
        if (status == console::Status::kOk) {
            status = runtime_.ReadState(actual);
            if (status == console::Status::kOk && !SameSettings(actual, original_)) {
                status = console::Status::kHardware;
            }
        }
        return status;
    }

    static void Discard(
        void *,
        const char *,
        std::size_t
    )
    {}

    console::Status Control(
        int count,
        const char *const *arguments
    )
    {
        return Runtime::ControlCommand(&runtime_, count, arguments, {nullptr, Discard});
    }

    console::Status Apply(const Step &step)
    {
        char value[16];
        std::snprintf(value, sizeof(value), "%ld", static_cast<long>(step.value));
        switch (step.action) {
        case Action::kManual: {
            const char *disable[] = {"cam", "ae", "off"};
            auto status = Control(3, disable);
            const char *manual[] = {"cam", "manual", "12000", "0"};
            if (status == console::Status::kOk) {
                status = Control(4, manual);
            }
            expected_.auto_exposure = false;
            expected_.reported_exposure_us = 12000;
            expected_.reported_gain_mdB = 0;
            return status;
        }
        case Action::kExposure: {
            const char *arguments[] = {"cam", "manual", value, "0"};
            expected_.reported_exposure_us = step.value;
            expected_.reported_gain_mdB = 0;
            return Control(4, arguments);
        }
        case Action::kWhiteBalance: {
            const char *arguments[] = {"cam", "wb", step.value == 0 ? "auto" : value};
            expected_.auto_white_balance = step.value == 0;
            expected_.color_temperature = static_cast<std::uint32_t>(step.value);
            return Control(3, arguments);
        }
        case Action::kAutoExposure: {
            const char *arguments[] = {"cam", "ae", "on"};
            expected_.auto_exposure = true;
            auto status = Control(3, arguments);
            const char *rejected[] = {"cam", "manual", "12000", "0"};
            if (status == console::Status::kOk && Control(4, rejected) != console::Status::kInvalidState) {
                status = console::Status::kHardware;
            }
            return status;
        }
        case Action::kCompensation: {
            const char *arguments[] = {"cam", "ev", value};
            expected_.compensation = step.value;
            return Control(3, arguments);
        }
        case Action::kStatistics: {
            if (original_.sensor_width < 4 || original_.sensor_height < 4) {
                return console::Status::kInvalidArgument;
            }
            expected_.statistics = {
                original_.sensor_width / 4,
                original_.sensor_height / 4,
                original_.sensor_width / 2,
                original_.sensor_height / 2
            };
            char coordinates[4][16];
            const auto &bounds = expected_.statistics;
            std::snprintf(coordinates[0], 16, "%lu", static_cast<unsigned long>(bounds.x));
            std::snprintf(coordinates[1], 16, "%lu", static_cast<unsigned long>(bounds.y));
            std::snprintf(coordinates[2], 16, "%lu", static_cast<unsigned long>(bounds.width));
            std::snprintf(coordinates[3], 16, "%lu", static_cast<unsigned long>(bounds.height));
            const char *arguments[] = {"cam", "area", coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
            return Control(6, arguments);
        }
        case Action::kFps:
            expected_geometry_.fps = static_cast<std::uint32_t>(step.value);
            return runtime_.Change(expected_geometry_);
        case Action::kFlip:
            expected_geometry_.horizontal = (step.value & 1) != 0;
            expected_geometry_.vertical = (step.value & 2) != 0;
            return runtime_.Change(expected_geometry_);
        case Action::kCrop:
            expected_geometry_.crop = {100, 0, 1600, 1920};
            return runtime_.Change(expected_geometry_);
        case Action::kInvalid: {
            const char *fps[] = {"capture", "fps", "12"};
            const char *flip[] = {"capture", "flip", "2", "0"};
            const char *crop[] = {"capture", "crop", "0", "0", "0", "480"};
            for (const auto arguments : {fps, flip, crop}) {
                const int count = arguments == fps ? 3 : arguments == flip ? 4 : 6;
                if (Runtime::CaptureCommand(&runtime_, count, arguments, {nullptr, Discard})
                    != console::Status::kInvalidArgument) {
                    return console::Status::kHardware;
                }
            }
            const char *exposure[] = {"cam", "manual", "99", "0"};
            return Control(4, exposure) == console::Status::kInvalidArgument ? console::Status::kOk
                                                                             : console::Status::kHardware;
        }
        case Action::kStop:
            return runtime_.Stop();
        case Action::kStart:
            return runtime_.Start();
        case Action::kRecover:
            return runtime_.Recover();
        case Action::kRestore:
            expected_ = original_;
            expected_geometry_ = original_geometry_;
            return RestoreOriginal();
        default:
            return console::Status::kOk;
        }
    }

    void Enter()
    {
        const auto &step = steps_[stage_];
        stage_started_ = true;
        Report(step.name, "BEGIN", "");
        const auto begin = clock_();
        const auto status = Apply(step);
        if (status != console::Status::kOk) {
            Fail(console::StatusName(status));
            return;
        }
        if (clock_() - begin > 5000) {
            Fail("action-deadline");
            return;
        }
        recoveries_ = runtime_.Recoveries();
        entered_ = clock_();
        measuring_ = false;
        if (step.action != Action::kStop) {
            State actual;
            if (runtime_.ReadState(actual) != console::Status::kOk || !SameSettings(actual, expected_)
                || !SameGeometry(runtime_.Configuration(), expected_geometry_)) {
                Fail("state-mismatch");
                return;
            }
            char settings[224];
            std::snprintf(
                settings,
                sizeof(settings),
                "CAMTEST STATE ae=%u ev_half=%d exposure_us=%ld gain_mdB=%ld wb_auto=%u wb_kelvin=%lu fps=%lu "
                "flip=%u,%u crop=%lu,%lu,%lu,%lu\n",
                unsigned(actual.auto_exposure),
                actual.compensation,
                static_cast<long>(actual.reported_exposure_us),
                static_cast<long>(actual.reported_gain_mdB),
                unsigned(actual.auto_white_balance),
                static_cast<unsigned long>(actual.color_temperature),
                static_cast<unsigned long>(expected_geometry_.fps),
                unsigned(expected_geometry_.horizontal),
                unsigned(expected_geometry_.vertical),
                static_cast<unsigned long>(expected_geometry_.crop.x),
                static_cast<unsigned long>(expected_geometry_.crop.y),
                static_cast<unsigned long>(expected_geometry_.crop.width),
                static_cast<unsigned long>(expected_geometry_.crop.height)
            );
            output_.Write(settings);
        }
    }

    void Report(
        const char *name,
        const char *result,
        const char *detail
    )
    {
        char line[256];
        std::snprintf(line, sizeof(line), "\nCAMTEST %s %s %s\n", name, result, detail);
        output_.Write(line);
    }

    void Summary()
    {
        char line[96];
        const auto total = sizeof(steps_) / sizeof(steps_[0]);
        const auto skipped = stage_ < total ? total - stage_ - (stage_started_ ? 1 : 0) : 0;
        std::snprintf(
            line,
            sizeof(line),
            "CAMTEST SUMMARY pass=%u fail=%u skip=%lu visual=required\n",
            passed_,
            failed_,
            static_cast<unsigned long>(skipped)
        );
        output_.Write(line);
    }

    void Fail(const char *detail)
    {
        Report(steps_[stage_].name, "FAIL", detail);
        ++failed_;
        active_ = false;
        cleanup_status_ = RestoreOriginal();
        if (cleanup_status_ != console::Status::kOk) {
            Report("cleanup", "FAIL", console::StatusName(cleanup_status_));
            ++failed_;
        } else {
            output_.Write("CAMTEST CLEANUP driver-state-restored\n");
        }
        Summary();
    }

    Runtime &runtime_;
    console::Writer output_;
    std::uint32_t (*clock_)();
    Frames (*frames_)();
    State original_{}, expected_{};
    Geometry original_geometry_{}, expected_geometry_{};
    Frames baseline_{}, latest_{};
    std::uint32_t entered_ = 0, measurement_ = 0, recoveries_ = 0, initial_failures_ = 0;
    std::uint32_t pipe1_progress_ = 0, pipe2_progress_ = 0, sampled_ = 0;
    std::size_t stage_ = 0;
    unsigned passed_ = 0, failed_ = 0;
    bool active_ = false, measuring_ = false, stage_started_ = false;
    console::Status cleanup_status_ = console::Status::kOk;
};

}
