#pragma once

#include <cstdint>

namespace uai::ai::common {

struct PeriodDecision {
    std::uint32_t due_count = 0U;
    std::uint32_t lateness = 0U;
    explicit operator bool() const { return due_count != 0U; }
};

class TimePeriod final {
public:
    bool Configure(
        std::uint32_t period,
        std::uint32_t first_deadline
    )
    {
        if (period == 0U || period > 0x7FFFFFFFU)
            return false;
        period_ = period;
        next_ = first_deadline;
        return true;
    }
    void Reset() { period_ = 0U; }
    std::uint32_t Period() const { return period_; }
    std::uint32_t RemainingWait(std::uint32_t clock) const
    {
        return period_ == 0U || static_cast<std::int32_t>(clock - next_) >= 0 ? 0U : next_ - clock;
    }
    PeriodDecision Take(std::uint32_t clock)
    {
        if (period_ == 0U || static_cast<std::int32_t>(clock - next_) < 0)
            return {};
        const auto lateness = clock - next_;
        const auto count = lateness / period_ + 1U;
        next_ += count * period_;
        return {count, lateness};
    }

private:
    std::uint32_t period_ = 0U;
    std::uint32_t next_ = 0U;
};

class FramePeriod final {
public:
    bool Configure(
        std::uint32_t frames,
        std::uint32_t first_sequence
    )
    {
        return period_.Configure(frames, first_sequence);
    }
    std::uint32_t Frames() const { return period_.Period(); }
    PeriodDecision Take(std::uint32_t sequence) { return period_.Take(sequence); }

private:
    TimePeriod period_;
};

struct ScheduleStats {
    std::uint32_t due = 0U;
    std::uint32_t started = 0U;
    std::uint32_t completed = 0U;
    std::uint32_t failed = 0U;
    std::uint32_t skipped = 0U;
    std::uint32_t dropped = 0U;
    std::uint32_t dropped_stride = 0U;
    std::uint32_t dropped_inactive = 0U;
    std::uint32_t dropped_source = 0U;
    std::uint32_t dropped_queue = 0U;
    std::uint32_t max_lateness_ms = 0U;
    std::uint32_t max_frame_delay_ms = 0U;

    void Observe(
        PeriodDecision decision,
        std::uint32_t lateness_ms
    )
    {
        due += decision.due_count;
        if (decision.due_count != 0U)
            skipped += decision.due_count - 1U;
        if (lateness_ms > max_lateness_ms)
            max_lateness_ms = lateness_ms;
    }
    void Finish(bool ok)
    {
        if (ok)
            ++completed;
        else
            ++failed;
    }
};

}