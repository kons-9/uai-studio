#include "ui/app_ui.hpp"

#include <cstdio>

#include "middleware/foundation/log.hpp"
#include "ui/ui_layout.hpp"

namespace uai::ai::app_ui {

namespace {

constexpr std::uint16_t Id(WidgetId id)
{
    return static_cast<std::uint16_t>(id);
}

/* Tenths of frames per second, or -1 when the model is disabled. */
std::int32_t RateTenths(std::uint32_t delta, std::uint32_t elapsed_ms,
                        bool enabled)
{
    if (!enabled) return -1;
    if (elapsed_ms == 0U) return 0;
    return static_cast<std::int32_t>((delta * 10000U) / elapsed_ms);
}

int AppendRate(char *out, int capacity, const char *name, std::int32_t tenths)
{
    if (tenths < 0) {
        return std::snprintf(out, static_cast<std::size_t>(capacity), "%s --  ", name);
    }
    return std::snprintf(out, static_cast<std::size_t>(capacity), "%s %d.%d  ", name,
                         static_cast<int>(tenths / 10),
                         static_cast<int>(tenths % 10));
}

} // namespace

AppUi::AppUi(task::ModelControl &models)
    : models_(models),
      buttons_(kButtons, kButtonCount),
      labels_(kLabels, kLabelCount),
      painters_{&buttons_, &labels_},
      overlay_(painters_, 2U)
{
    buttons_.SetChecked(Id(WidgetId::kToggleBoxes), show_boxes_);
    SyncButtons();
}

ui::Event AppUi::HandleTouch(const ui::TouchPoint &sample)
{
    const ui::Event event = buttons_.Update(sample);
    (void)Dispatch(*this, event);
    return event;
}

void AppUi::SyncButtons()
{
    const std::uint8_t mask = models_.ModelMask();
    buttons_.SetChecked(Id(WidgetId::kPerson),
                        (mask & task::ModelMaskBit(task::ModelBit::kPerson)) != 0U);
    buttons_.SetChecked(Id(WidgetId::kFace),
                        (mask & task::ModelMaskBit(task::ModelBit::kFace)) != 0U);
    buttons_.SetChecked(Id(WidgetId::kSegmentation),
                        (mask & task::ModelMaskBit(task::ModelBit::kSegmentation)) != 0U);
}

void AppUi::ToggleModel(task::ModelBit bit, std::uint16_t widget_id)
{
    const std::uint8_t mask = static_cast<std::uint8_t>(
        models_.ModelMask() ^ task::ModelMaskBit(bit));
    models_.SetModelMask(mask);
    SyncButtons();
    UAI_LOG_INFO("ui: tap id=%u models=%x\n",
                 static_cast<unsigned int>(widget_id),
                 static_cast<unsigned int>(mask));
}

void AppUi::OnPersonTap(const ui::Event &event)
{
    ToggleModel(task::ModelBit::kPerson, event.widget_id);
}

void AppUi::OnFaceTap(const ui::Event &event)
{
    ToggleModel(task::ModelBit::kFace, event.widget_id);
}

void AppUi::OnSegmentationTap(const ui::Event &event)
{
    ToggleModel(task::ModelBit::kSegmentation, event.widget_id);
}

void AppUi::OnToggleBoxesTap(const ui::Event &event)
{
    show_boxes_ = !show_boxes_;
    buttons_.SetChecked(Id(WidgetId::kToggleBoxes), show_boxes_);
    UAI_LOG_INFO("ui: tap id=%u boxes=%s\n",
                 static_cast<unsigned int>(event.widget_id),
                 show_boxes_ ? "on" : "off");
}

void AppUi::UpdateStatus(std::uint32_t now_ms, std::uint32_t period_ms)
{
    if (last_stats_tick_ != 0U && now_ms - last_stats_tick_ < period_ms) {
        return;
    }
    const task::PipelineStats stats = models_.Stats();
    const std::uint32_t elapsed =
        last_stats_tick_ == 0U ? 0U : now_ms - last_stats_tick_;
    const std::uint8_t mask = models_.ModelMask();

    char status[ui::kLabelTextCapacity];
    if (!stats.enabled) {
        std::snprintf(status, sizeof(status), "AI PIPELINE OFF");
    } else {
        int used = 0;
        used += AppendRate(status + used, static_cast<int>(sizeof(status)) - used, "PERSON",
                           RateTenths(stats.person_completed - last_stats_.person_completed,
                                      elapsed,
                                      (mask & task::ModelMaskBit(task::ModelBit::kPerson)) != 0U));
        used += AppendRate(status + used, static_cast<int>(sizeof(status)) - used, "FACE",
                           RateTenths(stats.face_completed - last_stats_.face_completed,
                                      elapsed,
                                      (mask & task::ModelMaskBit(task::ModelBit::kFace)) != 0U));
        used += AppendRate(status + used, static_cast<int>(sizeof(status)) - used, "SEG",
                           RateTenths(stats.segmentation_completed -
                                          last_stats_.segmentation_completed,
                                      elapsed,
                                      (mask & task::ModelMaskBit(task::ModelBit::kSegmentation)) != 0U));
        if (used > 0 && used < static_cast<int>(sizeof(status))) {
            std::snprintf(status + used, sizeof(status) - static_cast<std::size_t>(used), "FPS");
        }
    }
    labels_.SetText(Id(WidgetId::kStatus), status);

    char detections[ui::kLabelTextCapacity];
    std::snprintf(detections, sizeof(detections), "DET %u",
                  static_cast<unsigned int>(stats.last_detection_count));
    labels_.SetText(Id(WidgetId::kDetections), stats.enabled ? detections : "");

    last_stats_ = stats;
    last_stats_tick_ = now_ms;
}

inference::BoxSet AppUi::VisibleBoxes(const inference::BoxSet &latest) const
{
    inference::BoxSet visible = latest;
    const std::uint8_t mask = models_.ModelMask();
    if (!show_boxes_ ||
        (mask & task::ModelMaskBit(task::ModelBit::kPerson)) == 0U) {
        visible.person.count = 0U;
        visible.person_valid = false;
    }
    if (!show_boxes_ ||
        (mask & task::ModelMaskBit(task::ModelBit::kFace)) == 0U) {
        visible.face.count = 0U;
        visible.face_valid = false;
    }
    if (!show_boxes_ ||
        (mask & task::ModelMaskBit(task::ModelBit::kSegmentation)) == 0U) {
        visible.segmentation_valid = false;
    }
    return visible;
}

const char *AppUi::StatusText() const
{
    return labels_.Text(Id(WidgetId::kStatus));
}

const char *AppUi::DetectionsText() const
{
    return labels_.Text(Id(WidgetId::kDetections));
}

} // namespace uai::ai::app_ui
