#pragma once

#include <cstdint>

#include "exposure_control/runtime.hpp"
#include "shell/owner.hpp"
#include "ui/app_ui.hpp"
#include "task/model_result_snapshot.hpp"

namespace uai::ai::task {

class CameraDiagnosticState final {
public:
    explicit CameraDiagnosticState(std::uint32_t now) : last_report_tick_(now) {}

    template <typename Snapshot>
    bool ShouldReport(
        const Snapshot &snapshot,
        std::uint32_t now
    ) const
    {
        return ShouldReport(snapshot, [now] {
            return now;
        });
    }

    template <
        typename Snapshot,
        typename Clock>
    bool ShouldReport(
        const Snapshot &snapshot,
        Clock clock
    ) const
    {
        const bool changed = pipe_errors_ != snapshot.dcmipp_error_count
            || camera_errors_ != snapshot.camera_error_count || csi_errors_ != snapshot.csi_error_count
            || isp_errors_ != snapshot.isp_error_count || pipe1_timeouts_ != snapshot.pipe1_timeout_count
            || pipe2_timeouts_ != snapshot.pipe2_timeout_count || anomaly_tick_ != snapshot.last_anomaly_tick;
        return changed
            && ((pipe_errors_ == 0U && camera_errors_ == 0U && csi_errors_ == 0U && isp_errors_ == 0U)
                || clock() - last_report_tick_ >= 1000U);
    }

    template <typename Snapshot>
    void MarkReported(
        const Snapshot &snapshot,
        std::uint32_t now
    )
    {
        pipe_errors_ = snapshot.dcmipp_error_count;
        camera_errors_ = snapshot.camera_error_count;
        csi_errors_ = snapshot.csi_error_count;
        isp_errors_ = snapshot.isp_error_count;
        pipe1_timeouts_ = snapshot.pipe1_timeout_count;
        pipe2_timeouts_ = snapshot.pipe2_timeout_count;
        anomaly_tick_ = snapshot.last_anomaly_tick;
        last_report_tick_ = now;
    }

    template <typename Snapshot>
    bool ShouldReportRecovery(const Snapshot &snapshot) const
    {
        return recoveries_ != snapshot.recovery_count || recovery_errors_ != snapshot.recovery_error_count;
    }

    template <typename Snapshot>
    void MarkRecoveryReported(const Snapshot &snapshot)
    {
        recoveries_ = snapshot.recovery_count;
        recovery_errors_ = snapshot.recovery_error_count;
    }

private:
    std::uint32_t pipe_errors_ = 0U;
    std::uint32_t camera_errors_ = 0U;
    std::uint32_t csi_errors_ = 0U;
    std::uint32_t isp_errors_ = 0U;
    std::uint32_t pipe1_timeouts_ = 0U;
    std::uint32_t pipe2_timeouts_ = 0U;
    std::uint32_t anomaly_tick_ = 0U;
    std::uint32_t recoveries_ = 0U;
    std::uint32_t recovery_errors_ = 0U;
    std::uint32_t last_report_tick_;
};

struct DisplayResultState {
    explicit DisplayResultState(const inference::BoxSet &initial = {}) : boxes(initial) {}

    inference::BoxSet boxes{};

    void Reset(std::uint32_t generation)
    {
        boxes = {};
        stamps_ = {};
        generation_ = generation;
    }

    bool Accept(const ModelResultSnapshot &snapshot)
    {
        if (snapshot.generation != generation_)
            return false;
        bool updated = false;
        for (std::size_t index = 0U; index < stamps_.size(); ++index) {
            const auto &incoming = snapshot.stamps[index];
            auto &current = stamps_[index];
            if (!incoming.valid
                || (current.valid
                    && static_cast<std::int32_t>(incoming.update_sequence - current.update_sequence) <= 0))
                continue;
            current = incoming;
            if (index == 0U) {
                boxes.person = snapshot.boxes.person;
                boxes.person_valid = snapshot.boxes.person_valid;
            } else if (index == 1U) {
                boxes.face = snapshot.boxes.face;
                boxes.face_valid = snapshot.boxes.face_valid;
            } else {
                boxes.segmentation = snapshot.boxes.segmentation;
                boxes.segmentation_valid = snapshot.boxes.segmentation_valid;
            }
            updated = true;
        }
        if (updated) {
            boxes.model_sequence = snapshot.boxes.model_sequence;
            boxes.capture_sequence = snapshot.boxes.capture_sequence;
        }
        return updated;
    }

    bool Expire(std::uint32_t now)
    {
        bool changed = false;
        for (std::size_t index = 0U; index < stamps_.size(); ++index) {
            if (!stamps_[index].valid || now - stamps_[index].completed_ms < 3000U)
                continue;
            if (index == 0U && boxes.person_valid) {
                boxes.person_valid = false;
                boxes.person = {};
                changed = true;
            } else if (index == 1U && boxes.face_valid) {
                boxes.face_valid = false;
                boxes.face = {};
                changed = true;
            } else if (index == 2U && boxes.segmentation_valid) {
                boxes.segmentation_valid = false;
                boxes.segmentation = {};
                changed = true;
            }
        }
        return changed;
    }

    std::uint32_t RemainingWait(std::uint32_t now) const
    {
        std::uint32_t remaining = UINT32_MAX;
        const bool visible[] = {boxes.person_valid, boxes.face_valid, boxes.segmentation_valid};
        for (std::size_t index = 0U; index < stamps_.size(); ++index) {
            if (!visible[index] || !stamps_[index].valid)
                continue;
            const auto elapsed = now - stamps_[index].completed_ms;
            const auto wait = elapsed >= 3000U ? 0U : 3000U - elapsed;
            if (wait < remaining)
                remaining = wait;
        }
        return remaining;
    }

private:
    std::array<ModelResultStamp, 3U> stamps_{};
    std::uint32_t generation_ = 0U;
};

enum class RenderOperation : std::size_t {
    kTouch,
    kPipe2,
    kSubmit,
    kResults,
    kExposure,
    kDisplay,
    kCount
};

class RenderSchedule final {
public:
    explicit RenderSchedule(std::uint32_t now = 0U)
    {
        touch_.Configure(kTouchPollPeriod, now);
        report_.Configure(5000U, now + 5000U);
        frames_.Configure(kInferenceFrameStride, 1U);
    }
    common::ScheduleStats &Stats(RenderOperation operation) { return stats[static_cast<std::size_t>(operation)]; }
    void StartEvent(RenderOperation operation)
    {
        auto &sample = Stats(operation);
        ++sample.due;
        ++sample.started;
    }
    bool TouchDue(std::uint32_t now) const { return touch_.RemainingWait(now) == 0U; }
    void TouchPolled(std::uint32_t now)
    {
        const auto decision = touch_.Take(now);
        auto &sample = Stats(RenderOperation::kTouch);
        sample.Observe(decision, decision.lateness);
        ++sample.started;
    }
    common::PeriodDecision TakeInference(
        std::uint32_t sequence,
        std::uint32_t fps
    )
    {
        const auto decision = frames_.Take(sequence);
        const auto delay64 = fps == 0U ? 0U : static_cast<std::uint64_t>(decision.lateness) * 1000U / fps;
        const auto delay = static_cast<std::uint32_t>(delay64 > UINT32_MAX ? UINT32_MAX : delay64);
        Stats(RenderOperation::kSubmit).Observe(decision, delay);
        return decision;
    }
    bool ConfigureFrames(
        std::uint32_t stride,
        std::uint32_t first_sequence
    )
    {
        return frames_.Configure(stride, first_sequence);
    }
    std::uint32_t FrameStride() const { return frames_.Frames(); }
    bool ReportDue(std::uint32_t now) const { return report_.RemainingWait(now) == 0U; }
    void Reported(std::uint32_t now) { (void)report_.Take(now); }
    std::uint32_t RemainingWait(
        std::uint32_t now,
        bool touch_enabled
    ) const
    {
        auto remaining = report_.RemainingWait(now);
        if (touch_enabled && touch_.RemainingWait(now) < remaining)
            remaining = touch_.RemainingWait(now);
        return remaining;
    }
    std::array<common::ScheduleStats, static_cast<std::size_t>(RenderOperation::kCount)> stats{};

private:
    common::TimePeriod touch_;
    common::TimePeriod report_;
    common::FramePeriod frames_;
};

struct CameraRenderState {
    CameraRenderState(
        task::ModelControl &models,
        const inference::BoxSet &initial,
        std::uint32_t now
    )
        : results{initial},
          diagnostics(0U),
          schedule(now),
          ui(models),
          previous_exposure_enabled(ui.AiExposureEnabled())
    {}

    std::uint32_t RemainingWait(
        std::uint32_t now,
        bool touch_enabled
    ) const
    {
        auto wait = schedule.RemainingWait(now, touch_enabled);
        const std::uint32_t remaining[] = {
            results.RemainingWait(now),
            ui.RemainingStatusWait(now),
            kAiExposureControl ? exposure.RemainingWait(now, ui.AiExposureEnabled()) : UINT32_MAX
        };
        for (const auto candidate : remaining) {
            if (candidate < wait)
                wait = candidate;
        }
        return wait;
    }
    bool ShouldPresent(
        bool capture_ready,
        bool ui_dirty,
        bool input_display
    ) const
    {
        return !input_display && (capture_ready || (!ui.ShowsCamera() && ui_dirty));
    }

    DisplayResultState results;
    CameraDiagnosticState diagnostics;
    RenderSchedule schedule;
    app_ui::AppUi ui;
    exposure_control::Runtime exposure;
    shell::ExposureMode exposure_mode{};
    bool previous_exposure_enabled;
    std::uint32_t last_exposure_error_tick = 0U;
    std::uint32_t loop_count = 0U;
    std::uint32_t idle_wakeups = 0U;
    std::uint32_t observed_recovery_count = 0U;
    std::uint32_t observed_pipe2_drops = 0U;
    std::uint32_t observed_queue_drops = 0U;
    camera::Geometry geometry{};
};

}