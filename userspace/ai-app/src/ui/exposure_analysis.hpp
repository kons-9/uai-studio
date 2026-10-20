#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace uai::ai::app_ui {

inline constexpr std::uint8_t kDefaultBlackLevel = 5U;
inline constexpr std::uint8_t kDefaultWhiteLevel = 250U;
inline constexpr std::uint16_t kExposureSampleWidth = 100U;
inline constexpr std::uint16_t kExposureSampleHeight = 60U;
inline constexpr std::size_t kExposureSampleCount = kExposureSampleWidth * kExposureSampleHeight;
inline constexpr std::uint32_t kExposureAnalysisPeriodMs = 200U;
inline constexpr std::uint32_t kExposureResultMaxAgeMs = 1000U;

struct ExposureVisualizationSettings {
    bool histogram = false;
    bool zebra_black = false;
    bool zebra_white = false;
    std::uint8_t black_level = kDefaultBlackLevel;
    std::uint8_t white_level = kDefaultWhiteLevel;

    bool Enabled() const { return histogram || zebra_black || zebra_white; }
    bool Matches(const ExposureVisualizationSettings &other) const
    {
        return histogram == other.histogram && zebra_black == other.zebra_black && zebra_white == other.zebra_white
            && black_level == other.black_level && white_level == other.white_level;
    }
};

constexpr std::uint8_t Rgb565Luminance(std::uint16_t pixel)
{
    const std::uint32_t red = ((pixel >> 11U) & 0x1FU) * 255U / 31U;
    const std::uint32_t green = ((pixel >> 5U) & 0x3FU) * 255U / 63U;
    const std::uint32_t blue = (pixel & 0x1FU) * 255U / 31U;
    return static_cast<std::uint8_t>((77U * red + 150U * green + 29U * blue) >> 8U);
}

struct LuminanceHistogram {
    std::array<std::uint32_t, 256U> bins{};
    std::uint32_t samples = 0U;
    std::uint32_t shadows = 0U;
    std::uint32_t highlights = 0U;

    void
    Add(std::uint8_t luminance,
        std::uint8_t black_level = kDefaultBlackLevel,
        std::uint8_t white_level = kDefaultWhiteLevel)
    {
        ++bins[luminance];
        ++samples;
        shadows += luminance <= black_level ? 1U : 0U;
        highlights += luminance >= white_level ? 1U : 0U;
    }

    static std::uint32_t PercentTenths(
        std::uint32_t count,
        std::uint32_t total
    )
    {
        return total == 0U ? 0U : static_cast<std::uint32_t>(static_cast<std::uint64_t>(count) * 1000U / total);
    }
};

inline LuminanceHistogram AnalyzeLuminance(
    const std::uint16_t *pixels,
    std::size_t count,
    std::uint8_t black_level = kDefaultBlackLevel,
    std::uint8_t white_level = kDefaultWhiteLevel
)
{
    LuminanceHistogram histogram{};
    if (pixels != nullptr) {
        for (std::size_t index = 0U; index < count; ++index)
            histogram.Add(Rgb565Luminance(pixels[index]), black_level, white_level);
    }
    return histogram;
}

struct ExposureAnalysisResult {
    ExposureVisualizationSettings settings{};
    LuminanceHistogram histogram{};
    std::array<std::uint8_t, kExposureSampleCount> zebra{};
    std::array<std::uint8_t, 256U> heights{};
    std::array<char, 16U> shadows_text{};
    std::array<char, 16U> highlights_text{};
    std::uint32_t capture_sequence = 0U;
    std::uint32_t sampled_ms = 0U;
    bool valid = false;
};

class ExposureAnalysis final {
public:
    bool ReadyToSubmit(
        const ExposureVisualizationSettings &settings,
        std::uint32_t now
    ) const
    {
        return settings.Enabled() && (!submitted_ || now - last_submit_ms_ >= kExposureAnalysisPeriodMs)
            && input_state_.load(std::memory_order_acquire) == InputState::kIdle;
    }

    bool TrySubmit(
        const std::uint16_t *source,
        std::uint16_t width,
        std::uint16_t height,
        ExposureVisualizationSettings settings,
        std::uint32_t capture_sequence,
        std::uint32_t now
    )
    {
        if (source == nullptr || width == 0U || height == 0U || !ReadyToSubmit(settings, now))
            return false;
        InputState expected = InputState::kIdle;
        if (!input_state_.compare_exchange_strong(expected, InputState::kWriting, std::memory_order_acquire))
            return false;
        for (std::size_t row = 0U; row < kExposureSampleHeight; ++row) {
            const auto source_row = (2U * row + 1U) * height / (2U * kExposureSampleHeight);
            for (std::size_t column = 0U; column < kExposureSampleWidth; ++column) {
                const auto source_column = (2U * column + 1U) * width / (2U * kExposureSampleWidth);
                samples_[row * kExposureSampleWidth + column] = source[source_row * width + source_column];
            }
        }
        input_settings_ = settings;
        input_sequence_ = capture_sequence;
        input_ms_ = now;
        last_submit_ms_ = now;
        submitted_ = true;
        input_state_.store(InputState::kReady, std::memory_order_release);
        return true;
    }

    bool ProcessPending()
    {
        InputState expected = InputState::kReady;
        if (!input_state_.compare_exchange_strong(expected, InputState::kAnalyzing, std::memory_order_acquire))
            return false;
        auto &result = results_[back_];
        result = {};
        result.settings = input_settings_;
        result.capture_sequence = input_sequence_;
        result.sampled_ms = input_ms_;
        for (std::size_t index = 0U; index < samples_.size(); ++index) {
            const auto luminance = Rgb565Luminance(samples_[index]);
            if (result.settings.histogram)
                result.histogram.Add(luminance, result.settings.black_level, result.settings.white_level);
            const auto row = index / kExposureSampleWidth;
            const auto column = index % kExposureSampleWidth;
            if ((row + column) % 2U != 0U)
                continue;
            if (result.settings.zebra_black && luminance <= result.settings.black_level)
                result.zebra[index] = 1U;
            else if (result.settings.zebra_white && luminance >= result.settings.white_level)
                result.zebra[index] = 2U;
        }
        std::uint32_t peak = 1U;
        for (const auto count : result.histogram.bins)
            peak = count > peak ? count : peak;
        for (std::size_t level = 0U; level < result.heights.size(); ++level) {
            const auto count = result.histogram.bins[level];
            const auto scaled = count * 64U / peak;
            result.heights[level] = static_cast<std::uint8_t>(count == 0U ? 0U : scaled == 0U ? 1U : scaled);
        }
        const auto shadows = LuminanceHistogram::PercentTenths(result.histogram.shadows, result.histogram.samples);
        const auto highlights =
            LuminanceHistogram::PercentTenths(result.histogram.highlights, result.histogram.samples);
        std::snprintf(
            result.shadows_text.data(),
            result.shadows_text.size(),
            "B %u.%u%%",
            static_cast<unsigned int>(shadows / 10U),
            static_cast<unsigned int>(shadows % 10U)
        );
        std::snprintf(
            result.highlights_text.data(),
            result.highlights_text.size(),
            "W %u.%u%%",
            static_cast<unsigned int>(highlights / 10U),
            static_cast<unsigned int>(highlights % 10U)
        );
        result.valid = true;
        back_ = middle_.exchange(back_ | kDirty, std::memory_order_acq_rel) & kIndexMask;
        input_state_.store(InputState::kIdle, std::memory_order_release);
        return true;
    }

    const ExposureAnalysisResult *Latest(
        const ExposureVisualizationSettings &settings,
        std::uint32_t now
    )
    {
        if ((middle_.load(std::memory_order_acquire) & kDirty) != 0U)
            front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndexMask;
        const auto &result = results_[front_];
        return settings.Enabled() && result.valid && result.settings.Matches(settings)
                && now - result.sampled_ms <= kExposureResultMaxAgeMs
            ? &result
            : nullptr;
    }

private:
    enum class InputState : std::uint32_t {
        kIdle,
        kWriting,
        kReady,
        kAnalyzing
    };
    static constexpr std::uint32_t kDirty = 4U;
    static constexpr std::uint32_t kIndexMask = 3U;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<InputState>::is_always_lock_free);
    std::array<std::uint16_t, kExposureSampleCount> samples_{};
    ExposureVisualizationSettings input_settings_{};
    std::uint32_t input_sequence_ = 0U;
    std::uint32_t input_ms_ = 0U;
    std::atomic<InputState> input_state_{InputState::kIdle};
    std::array<ExposureAnalysisResult, 3U> results_{};
    std::atomic<std::uint32_t> middle_{2U};
    std::uint32_t front_ = 0U;
    std::uint32_t back_ = 1U;
    std::uint32_t last_submit_ms_ = 0U;
    bool submitted_ = false;
};

} // namespace uai::ai::app_ui