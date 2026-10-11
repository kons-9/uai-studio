#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "middleware/ui/widget.hpp"

namespace uai::ai::app_ui {

inline constexpr std::uint8_t kDefaultBlackLevel = 5U;
inline constexpr std::uint8_t kDefaultWhiteLevel = 250U;
inline constexpr std::uint16_t kBlackZebraColor = ui::Rgb565(0x00U, 0xD8U, 0xF0U);
inline constexpr std::uint16_t kWhiteZebraColor = ui::Rgb565(0xFFU, 0x40U, 0x40U);

struct ExposureVisualizationSettings {
    bool histogram = false;
    bool zebra_black = false;
    bool zebra_white = false;
    std::uint8_t black_level = kDefaultBlackLevel;
    std::uint8_t white_level = kDefaultWhiteLevel;

    bool Enabled() const { return histogram || zebra_black || zebra_white; }
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
    if (pixels == nullptr)
        return histogram;
    for (std::size_t index = 0U; index < count; ++index)
        histogram.Add(Rgb565Luminance(pixels[index]), black_level, white_level);
    return histogram;
}

class ExposureImagePainter final : public ui::Painter {
public:
    ExposureImagePainter(
        const std::uint16_t *source,
        std::uint16_t width,
        std::uint16_t height,
        ExposureVisualizationSettings settings
    )
        : source_(source),
          width_(width),
          height_(height),
          settings_(settings)
    {}

    const LuminanceHistogram &Histogram() const { return histogram_; }
    const ExposureVisualizationSettings &Settings() const { return settings_; }

    void Paint(ui::Canvas &canvas) const override
    {
        histogram_ = {};
        if (source_ == nullptr || !settings_.Enabled())
            return;
        for (std::uint16_t row = 0U; row < height_; ++row) {
            for (std::uint16_t column = 0U; column < width_; ++column) {
                const auto luminance = Rgb565Luminance(source_[static_cast<std::size_t>(row) * width_ + column]);
                if (settings_.histogram)
                    histogram_.Add(luminance, settings_.black_level, settings_.white_level);
                if (((static_cast<std::uint32_t>(column) + row) / 6U) % 2U != 0U)
                    continue;
                if (settings_.zebra_black && luminance <= settings_.black_level)
                    canvas.PutPixel(column, row, kBlackZebraColor);
                else if (settings_.zebra_white && luminance >= settings_.white_level)
                    canvas.PutPixel(column, row, kWhiteZebraColor);
            }
        }
    }

private:
    const std::uint16_t *source_;
    std::uint16_t width_;
    std::uint16_t height_;
    ExposureVisualizationSettings settings_;
    mutable LuminanceHistogram histogram_{};
};

class LuminanceHistogramPainter final : public ui::Painter {
public:
    explicit LuminanceHistogramPainter(const ExposureImagePainter &image) : image_(image) {}

    void Paint(ui::Canvas &canvas) const override
    {
        const auto &settings = image_.Settings();
        const auto &histogram = image_.Histogram();
        if (!settings.histogram || histogram.samples == 0U || canvas.Width() < 288U || canvas.Height() < 210U)
            return;
        const ui::Rect panel{12U, static_cast<std::uint16_t>(canvas.Height() - 142U), 276U, 130U};
        const ui::Rect plot{22U, static_cast<std::uint16_t>(panel.y + 28U), 256U, 64U};
        canvas.FillRect(panel, ui::Rgb565(0x10U, 0x10U, 0x10U));
        canvas.DrawFrame(panel, 1U, ui::Rgb565(0x80U, 0x80U, 0x80U));
        canvas.DrawText(plot.x, static_cast<std::uint16_t>(panel.y + 6U), "LUMA", 2U, 0xFFFFU);
        canvas.FillRect(
            {plot.x, plot.y, static_cast<std::uint16_t>(settings.black_level + 1U), plot.height},
            ui::Rgb565(0x00U, 0x30U, 0x38U)
        );
        canvas.FillRect(
            {static_cast<std::uint16_t>(plot.x + settings.white_level),
             plot.y,
             static_cast<std::uint16_t>(256U - settings.white_level),
             plot.height},
            ui::Rgb565(0x40U, 0x10U, 0x10U)
        );
        std::uint32_t peak = 1U;
        for (const auto count : histogram.bins)
            peak = count > peak ? count : peak;
        for (std::uint16_t level = 0U; level < histogram.bins.size(); ++level) {
            const auto count = histogram.bins[level];
            if (count == 0U)
                continue;
            const auto scaled = static_cast<std::uint32_t>(static_cast<std::uint64_t>(count) * plot.height / peak);
            const auto height = static_cast<std::uint16_t>(scaled == 0U ? 1U : scaled);
            const auto color = level <= settings.black_level ? kBlackZebraColor
                : level >= settings.white_level              ? kWhiteZebraColor
                                                             : static_cast<std::uint16_t>(0xFFFFU);
            canvas.FillRect(
                {static_cast<std::uint16_t>(plot.x + level),
                 static_cast<std::uint16_t>(plot.y + plot.height - height),
                 1U,
                 height},
                color
            );
        }
        const auto axis_y = static_cast<std::uint16_t>(plot.y + plot.height + 3U);
        canvas.DrawText(plot.x, axis_y, "0", 1U, 0xFFFFU);
        canvas.DrawText(static_cast<std::uint16_t>(plot.x + 119U), axis_y, "128", 1U, 0xFFFFU);
        canvas.DrawText(static_cast<std::uint16_t>(plot.x + 239U), axis_y, "255", 1U, 0xFFFFU);
        char text[16U];
        const auto shadows = LuminanceHistogram::PercentTenths(histogram.shadows, histogram.samples);
        const auto highlights = LuminanceHistogram::PercentTenths(histogram.highlights, histogram.samples);
        std::snprintf(
            text,
            sizeof(text),
            "B %u.%u%%",
            static_cast<unsigned int>(shadows / 10U),
            static_cast<unsigned int>(shadows % 10U)
        );
        canvas.DrawText(plot.x, static_cast<std::uint16_t>(panel.y + 110U), text, 2U, kBlackZebraColor);
        std::snprintf(
            text,
            sizeof(text),
            "W %u.%u%%",
            static_cast<unsigned int>(highlights / 10U),
            static_cast<unsigned int>(highlights % 10U)
        );
        canvas.DrawText(
            static_cast<std::uint16_t>(plot.x + 128U),
            static_cast<std::uint16_t>(panel.y + 110U),
            text,
            2U,
            kWhiteZebraColor
        );
    }

private:
    const ExposureImagePainter &image_;
};

} // namespace uai::ai::app_ui