#pragma once

#include <cstdint>

#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/ui/touch_point.hpp"
#include "middleware/ui/widget.hpp"
#include "task/model_control.hpp"

namespace uai::ai::app_ui {

/*
 * On-screen controls for ai-app: one toggle button per model, a BOXES toggle,
 * and two status labels. Widget geometry comes from the generated
 * ui_layout.hpp; this class owns the run-time state and the handlers that
 * the generated Dispatch() calls.
 */
class AppUi final {
public:
    explicit AppUi(task::ModelControl &models);

    /* Overlay to pass to LcdManagement::ComposeAndPresent(). */
    const ui::Painter &Overlay() const { return overlay_; }

    /* Feeds one touch sample; returns the event after handlers ran. */
    ui::Event HandleTouch(const ui::TouchPoint &sample);

    /* Refreshes the status labels at most once per `period_ms`. */
    void UpdateStatus(std::uint32_t now_ms, std::uint32_t period_ms = 500U);

    /* Boxes to draw: hides disabled models and everything when BOXES is off. */
    inference::BoxSet VisibleBoxes(const inference::BoxSet &latest) const;

    bool ShowBoxes() const { return show_boxes_; }
    const char *StatusText() const;
    const char *DetectionsText() const;

    /* Handlers bound in config/ui_layout.json. */
    void OnPersonTap(const ui::Event &event);
    void OnFaceTap(const ui::Event &event);
    void OnSegmentationTap(const ui::Event &event);
    void OnToggleBoxesTap(const ui::Event &event);

private:
    void ToggleModel(task::ModelBit bit, std::uint16_t widget_id);
    void SyncButtons();

    task::ModelControl &models_;
    ui::ButtonPanel buttons_;
    ui::LabelPanel labels_;
    const ui::Painter *painters_[2];
    ui::PainterGroup overlay_;
    bool show_boxes_ = true;
    task::PipelineStats last_stats_{};
    std::uint32_t last_stats_tick_ = 0U;
};

} // namespace uai::ai::app_ui
