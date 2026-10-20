#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "middleware/ui/widget.hpp"
#include "ui/exposure_analysis.hpp"

namespace uai::ai::app_ui {

inline constexpr std::uint16_t kBlackZebraColor = ui::Rgb565(0x00U, 0xD8U, 0xF0U);
inline constexpr std::uint16_t kWhiteZebraColor = ui::Rgb565(0xFFU, 0x40U, 0x40U);

class ExposureImagePainter final : public ui::Painter {
public:
    ExposureImagePainter(
        const ExposureAnalysisResult *result,
        ExposureVisualizationSettings settings
    )
        : result_(result != nullptr && result->valid && result->settings.Matches(settings) ? result : nullptr),
          settings_(settings)
    {}

    const ExposureAnalysisResult *Result() const { return result_; }
    const ExposureVisualizationSettings &Settings() const { return settings_; }

    void Paint(ui::Canvas &canvas) const override
    {
        if (result_ == nullptr || (!settings_.zebra_black && !settings_.zebra_white))
            return;
        for (std::uint16_t row = 0U; row < kExposureSampleHeight; ++row) {
            const auto top = static_cast<std::uint16_t>(row * canvas.Height() / kExposureSampleHeight);
            const auto bottom = static_cast<std::uint16_t>((row + 1U) * canvas.Height() / kExposureSampleHeight);
            for (std::uint16_t column = 0U; column < kExposureSampleWidth; ++column) {
                const auto marker = result_->zebra[static_cast<std::size_t>(row) * kExposureSampleWidth + column];
                if (marker == 0U)
                    continue;
                const auto left = static_cast<std::uint16_t>(column * canvas.Width() / kExposureSampleWidth);
                const auto right = static_cast<std::uint16_t>((column + 1U) * canvas.Width() / kExposureSampleWidth);
                canvas.FillRect(
                    {left, top, static_cast<std::uint16_t>(right - left), static_cast<std::uint16_t>(bottom - top)},
                    marker == 1U ? kBlackZebraColor : kWhiteZebraColor
                );
            }
        }
    }

private:
    const ExposureAnalysisResult *result_;
    ExposureVisualizationSettings settings_;
};

class LuminanceHistogramPainter final : public ui::Painter {
public:
    explicit LuminanceHistogramPainter(const ExposureImagePainter &image) : image_(image) {}

    void Paint(ui::Canvas &canvas) const override
    {
        const auto &settings = image_.Settings();
        const auto *result = image_.Result();
        if (result == nullptr || !settings.histogram || result->histogram.samples == 0U || canvas.Width() < 288U
            || canvas.Height() < 210U)
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
        for (std::uint16_t level = 0U; level < result->heights.size(); ++level) {
            const auto height = result->heights[level];
            if (height == 0U)
                continue;
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
        canvas.DrawText(
            plot.x, static_cast<std::uint16_t>(panel.y + 110U), result->shadows_text.data(), 2U, kBlackZebraColor
        );
        canvas.DrawText(
            static_cast<std::uint16_t>(plot.x + 128U),
            static_cast<std::uint16_t>(panel.y + 110U),
            result->highlights_text.data(),
            2U,
            kWhiteZebraColor
        );
    }

private:
    const ExposureImagePainter &image_;
};

} // namespace uai::ai::app_ui