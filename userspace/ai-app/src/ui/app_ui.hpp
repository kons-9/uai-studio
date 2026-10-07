#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/ui/touch_point.hpp"
#include "middleware/ui/widget.hpp"
#include "task/model_control.hpp"
#include "ui/ui_layout.hpp"

namespace uai::ai::app_ui {

/*
 * On-screen UI for ai-app: a camera screen with model toggles, a status
 * label and a detection counter, and a settings screen with a slider, a
 * dial, a model-preset wheel, a rate read-out and a logo. Widget geometry
 * comes from the generated ui_layout.hpp; this class owns the run-time
 * state of every screen and the handlers that the generated Dispatch() calls.
 */
class AppUi final {
public:
    explicit AppUi(task::ModelControl &models)
        : AppUi(models, std::make_index_sequence<kScreenCount>{}) {}

    /* Overlay of the current screen to pass to the LCD driver. */
    const ui::Painter &Overlay() const { return screens_[current_]; }
    /* True when the current screen is drawn over the live camera frame. */
    bool ShowsCamera() const;
    ScreenId CurrentScreen() const { return static_cast<ScreenId>(current_); }

    /* Feeds one touch sample to the current screen; returns the event after
     * handlers ran. */
    ui::Event HandleTouch(const ui::TouchPoint &sample);

    /* Refreshes the status labels at most once per StatusPeriod(). */
    void UpdateStatus(std::uint32_t now_ms);
    std::uint32_t StatusPeriod() const { return status_period_ms_; }

    /* Boxes to draw: hides disabled models, low-confidence boxes, and
     * everything when BOXES is off. */
    inference::BoxSet VisibleBoxes(const inference::BoxSet &latest) const;

    bool ShowBoxes() const { return show_boxes_; }
    std::int32_t MinConfidencePercent() const { return min_confidence_percent_; }
    const char *StatusText() const;
    std::int32_t DetectionsValue() const;
    std::int32_t PersonRateTenths() const;

    /* Handlers bound in config/ui_layout.json. */
    void ShowScreen(ScreenId screen);
    void OnPersonTap(const ui::Event &event);
    void OnFaceTap(const ui::Event &event);
    void OnSegmentationTap(const ui::Event &event);
    void OnToggleBoxesTap(const ui::Event &event);
    void OnMinConfidenceChange(const ui::Event &event);
    void OnStatusPeriodChange(const ui::Event &event);
    void OnModelsChange(const ui::Event &event);

private:
    template <std::size_t... Index>
    AppUi(task::ModelControl &models, std::index_sequence<Index...>)
        : models_(models), screens_{ui::Screen(kScreens[Index])...}
    {
        Initialize();
    }
    void Initialize();
    void ToggleModel(task::ModelBit bit, std::uint16_t widget_id);
    /* Reflects the model mask on the main buttons and the menu wheel. */
    void SyncModelWidgets();
    ui::Screen &Main() { return screens_[static_cast<std::size_t>(ScreenId::kMain)]; }
    const ui::Screen &Main() const { return screens_[static_cast<std::size_t>(ScreenId::kMain)]; }
    ui::Screen &Menu() { return screens_[static_cast<std::size_t>(ScreenId::kMenu)]; }

    task::ModelControl &models_;
    ui::Screen screens_[kScreenCount];
    std::size_t current_ = static_cast<std::size_t>(ScreenId::kMain);
    bool show_boxes_ = true;
    std::int32_t min_confidence_percent_ = 0;
    std::uint32_t status_period_ms_ = 500U;
    task::PipelineStats last_stats_{};
    std::uint32_t last_stats_tick_ = 0U;
};

} // namespace uai::ai::app_ui
