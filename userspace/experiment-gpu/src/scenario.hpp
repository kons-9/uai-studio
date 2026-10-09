#pragma once
#include "operations.hpp"

namespace experiment::graphics {

enum class Rejection { kNone, kOverlap, kBackgroundOverlap, kShortBuffer, kStride, kUnaligned, kResize };
struct Case {
    const char *name;
    Operation operation;
    Format source;
    Format destination;
    std::uint8_t alpha = 255;
    std::uint32_t color = 0xc02070;
    bool padded = false;
    Rejection rejection = Rejection::kNone;
};

inline constexpr Case kCases[] = {
    {"copy-rgb888", Operation::kBlit, Format::kRgb888, Format::kRgb888},
    {"copy-rgb565", Operation::kBlit, Format::kRgb565, Format::kRgb565},
    {"copy-padded", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, true},
    {"convert-888-to-565", Operation::kBlit, Format::kRgb888, Format::kRgb565},
    {"convert-565-to-888", Operation::kBlit, Format::kRgb565, Format::kRgb888},
    {"convert-padded", Operation::kBlit, Format::kRgb888, Format::kRgb565, 255, 0, true},
    {"fill-565-red", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0xff0000},
    {"fill-565-green", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0x00ff00},
    {"fill-565-blue", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0x0000ff},
    {"fill-888-red", Operation::kFill, Format::kRgb888, Format::kRgb888, 255, 0xff0000},
    {"fill-888-green", Operation::kFill, Format::kRgb888, Format::kRgb888, 255, 0x00ff00},
    {"fill-888-blue", Operation::kFill, Format::kRgb888, Format::kRgb888, 255, 0x0000ff},
    {"fill-padded", Operation::kFill, Format::kRgb888, Format::kRgb565, 255, 0xc02070, true},
    {"blend-565-alpha0", Operation::kBlend, Format::kRgb888, Format::kRgb565, 0},
    {"blend-565-alpha128", Operation::kBlend, Format::kRgb888, Format::kRgb565, 128},
    {"blend-565-alpha255", Operation::kBlend, Format::kRgb888, Format::kRgb565, 255},
    {"blend-888-alpha128", Operation::kBlend, Format::kRgb888, Format::kRgb888, 128},
    {"blend-padded", Operation::kBlend, Format::kRgb565, Format::kRgb888, 128, 0, true},
    {"reject-overlap", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kOverlap},
    {"reject-background-overlap", Operation::kBlend, Format::kRgb888, Format::kRgb888, 128, 0, false, Rejection::kBackgroundOverlap},
    {"reject-short-buffer", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kShortBuffer},
    {"reject-stride", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kStride},
    {"reject-unaligned", Operation::kBlit, Format::kRgb888, Format::kRgb888, 255, 0, false, Rejection::kUnaligned},
    {"reject-resize", Operation::kResize, Format::kRgb888, Format::kRgb565, 255, 0, false, Rejection::kResize},
    {"reuse-after-rejection", Operation::kBlit, Format::kRgb888, Format::kRgb565}
};
inline constexpr std::size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);
inline constexpr std::size_t kStressCaseCount = [] {
    std::size_t count = 0;
    while (count < kCaseCount && kCases[count].rejection == Rejection::kNone) { ++count; }
    return count;
}();

struct Observation {
    std::uint32_t pipe1 = 0;
    std::uint32_t pipe2 = 0;
    std::uint32_t errors = 0;
    std::uint32_t recoveries = 0;
    bool running = false;
};
struct Result {
    bool passed = false;
    std::uint32_t cycles = 0;
    unsigned maximum_error = 0;
    unsigned corrupted_bytes = 0;
};
class ScenarioBackend {
public:
    virtual ~ScenarioBackend() = default;
    virtual Observation Observe() = 0;
    virtual Result Run(const Case &) = 0;
    virtual void Report(const char *, const Result &) = 0;
    virtual void Summary(unsigned passed, unsigned failed, std::uint32_t transfers) = 0;
};

class Scenario {
public:
    explicit Scenario(ScenarioBackend &backend) : backend_(backend) {}
    bool Start(std::uint32_t now)
    {
        if (active_) { return false; }
        index_ = 0; passed_ = 0; failed_ = 0; transfers_ = 0;
        baseline_ = backend_.Observe();
        window_ = baseline_;
        last_tick_ = now;
        active_ = true;
        return true;
    }
    void Tick(std::uint32_t now)
    {
        if (!active_) { return; }
        auto observation = backend_.Observe();
        const bool healthy = Healthy(observation) && now - last_tick_ <= 1500;
        last_tick_ = now;
        if (index_ < kCaseCount) {
            auto result = backend_.Run(kCases[index_]);
            observation = backend_.Observe();
            result.passed = result.passed && healthy && Healthy(observation);
            Record(kCases[index_++].name, result);
            if (index_ == kCaseCount) { stress_begin_ = now; window_begin_ = now; window_ = observation; }
            return;
        }
        auto result = backend_.Run(kCases[transfers_ % kStressCaseCount]);
        observation = backend_.Observe();
        result.passed = result.passed && Healthy(observation);
        ++transfers_;
        bool progress = true;
        if (now - window_begin_ >= 1500) {
            progress = observation.pipe1 != window_.pipe1 && observation.pipe2 != window_.pipe2;
            window_ = observation; window_begin_ = now;
        }
        if (!result.passed || !healthy || !progress || now - stress_begin_ >= 60000) {
            result.passed = result.passed && healthy && progress &&
                observation.pipe1 != baseline_.pipe1 && observation.pipe2 != baseline_.pipe2;
            Record("camera-display-stress-60s", result);
            active_ = false;
            backend_.Summary(passed_, failed_, transfers_);
        }
    }
    bool Active() const { return active_; }
private:
    bool Healthy(const Observation &observation) const
    {
        return observation.running && observation.errors == baseline_.errors && observation.recoveries == baseline_.recoveries;
    }
    void Record(const char *name, const Result &result)
    {
        if (result.passed) { ++passed_; } else { ++failed_; }
        backend_.Report(name, result);
    }
    ScenarioBackend &backend_;
    Observation baseline_{}, window_{};
    std::size_t index_ = 0;
    unsigned passed_ = 0, failed_ = 0;
    std::uint32_t transfers_ = 0, stress_begin_ = 0, window_begin_ = 0, last_tick_ = 0;
    bool active_ = false;
};

}