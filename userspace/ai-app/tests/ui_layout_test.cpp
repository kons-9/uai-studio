#include "middleware/ui/canvas.hpp"
#include "middleware/ui/widget.hpp"
#include "ui/app_ui.hpp"
#include "ui/ui_layout.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct FakeModels final : public uai::ai::task::ModelControl {
    std::uint8_t mask = uai::ai::task::kAllModelsMask;
    uai::ai::task::PipelineStats stats{};
    std::uint8_t ModelMask() const override { return mask; }
    void SetModelMask(std::uint8_t value) override { mask = value; }
    uai::ai::task::PipelineStats Stats() const override { return stats; }
};

uai::ai::ui::Rect BoundsOf(uai::ai::app_ui::WidgetId id)
{
    for (const auto &button : uai::ai::app_ui::kButtons) {
        if (button.id == static_cast<std::uint16_t>(id)) return button.bounds;
    }
    for (const auto &label : uai::ai::app_ui::kLabels) {
        if (label.id == static_cast<std::uint16_t>(id)) return label.bounds;
    }
    return {};
}

void Tap(uai::ai::app_ui::AppUi &ui, uai::ai::app_ui::WidgetId id)
{
    const uai::ai::ui::Rect bounds = BoundsOf(id);
    ui.HandleTouch({true, static_cast<std::uint16_t>(bounds.x + bounds.width / 2U),
                    static_cast<std::uint16_t>(bounds.y + bounds.height / 2U)});
    ui.HandleTouch({false, 0U, 0U});
}

/* Guards the generated header: it must compile against ui::ButtonSpec and
 * describe widgets that lie on the screen. */
TEST(AiAppUiLayout, GeneratedWidgetsFitScreen)
{
    static_assert(uai::ai::app_ui::kButtonCount >= 1U);
    static_assert(uai::ai::app_ui::kLabelCount >= 1U);
    for (std::size_t i = 0U; i < uai::ai::app_ui::kButtonCount; ++i) {
        const uai::ai::ui::Rect &bounds = uai::ai::app_ui::kButtons[i].bounds;
        EXPECT_LE(bounds.x + bounds.width, uai::ai::app_ui::kScreenWidth);
        EXPECT_LE(bounds.y + bounds.height, uai::ai::app_ui::kScreenHeight);
        EXPECT_GT(bounds.width, 0U);
        EXPECT_GT(bounds.height, 0U);
    }
    for (std::size_t i = 0U; i < uai::ai::app_ui::kLabelCount; ++i) {
        const uai::ai::ui::Rect &bounds = uai::ai::app_ui::kLabels[i].bounds;
        EXPECT_LE(bounds.x + bounds.width, uai::ai::app_ui::kScreenWidth);
        EXPECT_LE(bounds.y + bounds.height, uai::ai::app_ui::kScreenHeight);
        EXPECT_LT(std::strlen(uai::ai::app_ui::kLabels[i].text),
                  uai::ai::ui::kLabelTextCapacity);
    }
}

TEST(AiAppUi, ModelButtonsToggleMaskAndCheckedState)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);

    Tap(ui, uai::ai::app_ui::WidgetId::kFace);
    EXPECT_EQ(models.mask, uai::ai::task::kAllModelsMask &
                               ~uai::ai::task::ModelMaskBit(
                                   uai::ai::task::ModelBit::kFace));
    Tap(ui, uai::ai::app_ui::WidgetId::kFace);
    EXPECT_EQ(models.mask, uai::ai::task::kAllModelsMask);
    Tap(ui, uai::ai::app_ui::WidgetId::kPerson);
    Tap(ui, uai::ai::app_ui::WidgetId::kSegmentation);
    EXPECT_EQ(models.mask, uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kFace));

    /* The overlay reflects the mask: the checked FACE button is painted with
     * checked_fill, the unchecked PERSON button with fill. */
    std::vector<std::uint16_t> pixels(
        static_cast<std::size_t>(uai::ai::app_ui::kScreenWidth) *
            uai::ai::app_ui::kScreenHeight, 0U);
    uai::ai::ui::Canvas canvas(pixels.data(), uai::ai::app_ui::kScreenWidth,
                               uai::ai::app_ui::kScreenHeight);
    ui.Overlay().Paint(canvas);
    const auto sample = [&](uai::ai::app_ui::WidgetId id) {
        const uai::ai::ui::Rect b = BoundsOf(id);
        return pixels[static_cast<std::size_t>(b.y + 6U) *
                          uai::ai::app_ui::kScreenWidth + b.x + 6U];
    };
    EXPECT_EQ(sample(uai::ai::app_ui::WidgetId::kFace),
              uai::ai::app_ui::kButtons[1].style.checked_fill);
    EXPECT_EQ(sample(uai::ai::app_ui::WidgetId::kPerson),
              uai::ai::app_ui::kButtons[0].style.fill);
}

TEST(AiAppUi, BoxesToggleAndMaskFilterVisibleBoxes)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    uai::ai::inference::BoxSet latest{};
    latest.person_valid = true;
    latest.person.count = 2U;
    latest.face_valid = true;
    latest.face.count = 1U;
    latest.segmentation_valid = true;

    uai::ai::inference::BoxSet visible = ui.VisibleBoxes(latest);
    EXPECT_EQ(visible.person.count, 2U);
    EXPECT_TRUE(visible.segmentation_valid);

    Tap(ui, uai::ai::app_ui::WidgetId::kPerson);
    visible = ui.VisibleBoxes(latest);
    EXPECT_EQ(visible.person.count, 0U);
    EXPECT_FALSE(visible.person_valid);
    EXPECT_EQ(visible.face.count, 1U);

    Tap(ui, uai::ai::app_ui::WidgetId::kToggleBoxes);
    EXPECT_FALSE(ui.ShowBoxes());
    visible = ui.VisibleBoxes(latest);
    EXPECT_EQ(visible.face.count, 0U);
    EXPECT_FALSE(visible.segmentation_valid);

    /* Tapping outside every button changes nothing. */
    ui.HandleTouch({true, 400U, 200U});
    ui.HandleTouch({false, 0U, 0U});
    EXPECT_FALSE(ui.ShowBoxes());
}

TEST(AiAppUi, StatusLabelShowsRatesPerEnabledModel)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    EXPECT_STREQ(ui.StatusText(), "AI STARTING");

    models.stats.enabled = false;
    ui.UpdateStatus(1000U);
    EXPECT_STREQ(ui.StatusText(), "AI PIPELINE OFF");
    EXPECT_STREQ(ui.DetectionsText(), "");

    models.stats.enabled = true;
    models.stats.last_detection_count = 3U;
    ui.UpdateStatus(1200U);  /* within the 500 ms period: unchanged */
    EXPECT_STREQ(ui.StatusText(), "AI PIPELINE OFF");

    models.stats.person_completed = 15U;
    models.stats.face_completed = 5U;
    Tap(ui, uai::ai::app_ui::WidgetId::kSegmentation);
    ui.UpdateStatus(2000U);  /* 1000 ms since the last sample */
    EXPECT_STREQ(ui.StatusText(), "PERSON 15.0  FACE 5.0  SEG --  FPS");
    EXPECT_STREQ(ui.DetectionsText(), "DET 3");

    models.stats.person_completed = 20U;
    ui.UpdateStatus(4000U);  /* 5 completions in 2000 ms */
    EXPECT_STREQ(ui.StatusText(), "PERSON 2.5  FACE 0.0  SEG --  FPS");
}

/* The handler bound in config/ui_layout.json must be called for a tap on the
 * BOXES button and for nothing else. */
TEST(AiAppUiLayout, DispatchRoutesTapToBoundHandler)
{
    struct Handlers {
        int tap_count = 0;
        std::uint16_t last_id = 0U;
        void OnToggleBoxesTap(const uai::ai::ui::Event &event)
        {
            ++tap_count;
            last_id = event.widget_id;
        }
        void OnPersonTap(const uai::ai::ui::Event &) { ++tap_count; }
        void OnFaceTap(const uai::ai::ui::Event &) { ++tap_count; }
        void OnSegmentationTap(const uai::ai::ui::Event &) { ++tap_count; }
    } handlers;

    uai::ai::ui::Event tap{};
    tap.type = uai::ai::ui::EventType::kTap;
    tap.widget_id = static_cast<std::uint16_t>(
        uai::ai::app_ui::WidgetId::kToggleBoxes);
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, tap));
    EXPECT_EQ(handlers.tap_count, 1);
    EXPECT_EQ(handlers.last_id, tap.widget_id);

    uai::ai::ui::Event press = tap;
    press.type = uai::ai::ui::EventType::kPress;
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, press));

    uai::ai::ui::Event label_tap = tap;
    label_tap.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kStatus);
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, label_tap));
    EXPECT_EQ(handlers.tap_count, 1);
}

} // namespace
