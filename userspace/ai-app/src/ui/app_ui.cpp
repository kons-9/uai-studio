#include "ui/app_ui.hpp"

#include <cstdio>

#include "middleware/foundation/log.hpp"

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

void DropBelow(inference::DetectionSet &set, float min_confidence)
{
    std::uint32_t kept = 0U;
    const std::uint32_t count =
        set.count < inference::kMaxBoxes ? set.count : inference::kMaxBoxes;
    for (std::uint32_t i = 0U; i < count; ++i) {
        if (set.boxes[i].confidence >= min_confidence) {
            set.boxes[kept++] = set.boxes[i];
        }
    }
    set.count = kept;
}

} // namespace

void AppUi::Initialize()
{
    /* Controls start from the layout's initial values; mirror them here so
     * the application state and the widgets agree before the first touch. */
    min_confidence_percent_ = Menu().Sliders().Value(Id(WidgetId::kMinConfidence));
    status_period_ms_ =
        static_cast<std::uint32_t>(Menu().Dials().Value(Id(WidgetId::kStatusPeriod)));
    Menu().Buttons().SetChecked(Id(WidgetId::kToggleBoxes), show_boxes_);
    SyncModelWidgets();
}

bool AppUi::ShowsCamera() const
{
    return screens_[current_].Spec().background == ui::Background::kCamera;
}

ui::Event AppUi::HandleTouch(const ui::TouchPoint &sample)
{
    const ui::Event event = screens_[current_].Update(sample);
    (void)Dispatch(*this, event);
    return event;
}

void AppUi::ShowScreen(ScreenId screen)
{
    const std::size_t index = static_cast<std::size_t>(screen);
    if (index >= kScreenCount || index == current_) {
        return;
    }
    current_ = index;
    UAI_LOG_INFO("ui: screen=%u\n", static_cast<unsigned int>(index));
}

void AppUi::SyncModelWidgets()
{
    const std::uint8_t mask = models_.ModelMask();
    ui::ButtonPanel &buttons = Menu().Buttons();
    buttons.SetChecked(Id(WidgetId::kPerson),
                       (mask & task::ModelMaskBit(task::ModelBit::kPerson)) != 0U);
    buttons.SetChecked(Id(WidgetId::kFace),
                       (mask & task::ModelMaskBit(task::ModelBit::kFace)) != 0U);
    buttons.SetChecked(Id(WidgetId::kSegmentation),
                       (mask & task::ModelMaskBit(task::ModelBit::kSegmentation)) != 0U);
}

void AppUi::ToggleModel(task::ModelBit bit, std::uint16_t widget_id)
{
    const std::uint8_t mask = static_cast<std::uint8_t>(
        models_.ModelMask() ^ task::ModelMaskBit(bit));
    models_.SetModelMask(mask);
    SyncModelWidgets();
    UAI_LOG_INFO("ui: tap id=%u models=%x\n",
                 static_cast<unsigned int>(widget_id),
                 static_cast<unsigned int>(mask));
}

void AppUi::SetMinConfidence(std::int32_t percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    min_confidence_percent_ = percent;
    Menu().Sliders().SetValue(Id(WidgetId::kMinConfidence), percent);
    UAI_LOG_INFO("ui: min confidence=%d%%\n", static_cast<int>(percent));
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

void AppUi::OnToggleBoxesTap(const ui::Event &)
{
    ToggleBoxes();
}

void AppUi::ToggleBoxes()
{
    show_boxes_ = !show_boxes_;
    Menu().Buttons().SetChecked(Id(WidgetId::kToggleBoxes), show_boxes_);
    UAI_LOG_INFO("ui: boxes=%s\n", show_boxes_ ? "on" : "off");
}

void AppUi::OnMinConfidenceChange(const ui::Event &event)
{
    SetMinConfidence(event.value);
}

void AppUi::OnStatusPeriodChange(const ui::Event &event)
{
    status_period_ms_ = static_cast<std::uint32_t>(event.value > 0 ? event.value : 1);
    UAI_LOG_INFO("ui: status period=%u ms\n",
                 static_cast<unsigned int>(status_period_ms_));
}

void AppUi::UpdateStatus(std::uint32_t now_ms)
{
    if (last_stats_tick_ != 0U && now_ms - last_stats_tick_ < status_period_ms_) {
        return;
    }
    const task::PipelineStats stats = models_.Stats();
    const std::uint32_t elapsed =
        last_stats_tick_ == 0U ? 0U : now_ms - last_stats_tick_;
    const std::uint8_t mask = models_.ModelMask();
    const std::int32_t person_tenths =
        RateTenths(stats.person_completed - last_stats_.person_completed, elapsed,
                   (mask & task::ModelMaskBit(task::ModelBit::kPerson)) != 0U);

    char status[ui::kLabelTextCapacity];
    if (!stats.enabled) {
        std::snprintf(status, sizeof(status), "AI PIPELINE OFF");
    } else {
        int used = 0;
        used += AppendRate(status + used, static_cast<int>(sizeof(status)) - used, "PERSON",
                           person_tenths);
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
    Main().Labels().SetText(Id(WidgetId::kStatus), status);

    last_stats_ = stats;
    last_stats_tick_ = now_ms;
}

inference::BoxSet AppUi::VisibleBoxes(const inference::BoxSet &latest) const
{
    inference::BoxSet visible = latest;
    const std::uint8_t mask = models_.ModelMask();
    const float min_confidence =
        static_cast<float>(min_confidence_percent_) / 100.0F;
    if (!show_boxes_ ||
        (mask & task::ModelMaskBit(task::ModelBit::kPerson)) == 0U) {
        visible.person.count = 0U;
        visible.person_valid = false;
    } else {
        DropBelow(visible.person, min_confidence);
    }
    if (!show_boxes_ ||
        (mask & task::ModelMaskBit(task::ModelBit::kFace)) == 0U) {
        visible.face.count = 0U;
        visible.face_valid = false;
    } else {
        DropBelow(visible.face, min_confidence);
    }
    if (!show_boxes_ ||
        (mask & task::ModelMaskBit(task::ModelBit::kSegmentation)) == 0U) {
        visible.segmentation_valid = false;
    }
    return visible;
}

const char *AppUi::StatusText() const
{
    return Main().Labels().Text(Id(WidgetId::kStatus));
}

} // namespace uai::ai::app_ui
