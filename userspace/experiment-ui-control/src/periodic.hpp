#pragma once
#include <cstdint>

namespace experiment::scheduling {

enum class LatePolicy { kSkip, kCatchUp, kLatestOnly };
struct Stats { std::uint64_t due = 0, runs = 0, skipped = 0; std::uint32_t max_lateness = 0; };

class Periodic {
public:
    bool Configure(std::uint32_t period, std::uint32_t first_deadline, LatePolicy policy, std::uint32_t catch_up_limit = 4)
    {
        if (period == 0 || period > 0x7fffffffU || catch_up_limit == 0) { return false; }
        period_ = period;
        next_ = first_deadline;
        policy_ = policy;
        limit_ = catch_up_limit;
        stats_ = {};
        return true;
    }

    std::uint32_t Take(std::uint32_t clock)
    {
        if (period_ == 0 || static_cast<std::int32_t>(clock - next_) < 0) { return 0; }
        const auto lateness = clock - next_;
        const auto count = lateness / period_ + 1;
        next_ += count * period_;
        stats_.due += count;
        if (lateness > stats_.max_lateness) { stats_.max_lateness = lateness; }
        std::uint32_t runs = 1;
        if (policy_ == LatePolicy::kSkip && lateness % period_ != 0) { runs = 0; }
        if (policy_ == LatePolicy::kCatchUp) { runs = count < limit_ ? count : limit_; }
        stats_.runs += runs;
        stats_.skipped += count - runs;
        return runs;
    }

    const Stats &Statistics() const { return stats_; }

private:
    std::uint32_t period_ = 0;
    std::uint32_t next_ = 0;
    std::uint32_t limit_ = 4;
    LatePolicy policy_ = LatePolicy::kLatestOnly;
    Stats stats_{};
};

}