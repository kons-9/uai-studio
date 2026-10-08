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
    const std::uint16_t wanted = static_cast<std::uint16_t>(id);
    for (const uai::ai::ui::ScreenSpec &screen : uai::ai::app_ui::kScreens) {
        for (std::size_t i = 0U; i < screen.button_count; ++i) {
            if (screen.buttons[i].id == wanted) return screen.buttons[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.label_count; ++i) {
            if (screen.labels[i].id == wanted) return screen.labels[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.slider_count; ++i) {
            if (screen.sliders[i].id == wanted) return screen.sliders[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.dial_count; ++i) {
            if (screen.dials[i].id == wanted) return screen.dials[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.wheel_count; ++i) {
            if (screen.wheels[i].id == wanted) return screen.wheels[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.number_count; ++i) {
            if (screen.numbers[i].id == wanted) return screen.numbers[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.image_count; ++i) {
            if (screen.images[i].id == wanted) return screen.images[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.pad_count; ++i) {
            if (screen.pads[i].id == wanted) return screen.pads[i].bounds;
        }
    }
    return {};
}

const uai::ai::ui::DialSpec &DialOf(uai::ai::app_ui::WidgetId id)
{
    const std::uint16_t wanted = static_cast<std::uint16_t>(id);
    for (const uai::ai::ui::ScreenSpec &screen : uai::ai::app_ui::kScreens) {
        for (std::size_t i = 0U; i < screen.dial_count; ++i) {
            if (screen.dials[i].id == wanted) return screen.dials[i];
        }
    }
    return uai::ai::app_ui::kMenuDials[0];
}

const uai::ai::ui::ButtonSpec &ButtonOf(uai::ai::app_ui::WidgetId id)
{
    const std::uint16_t wanted = static_cast<std::uint16_t>(id);
    for (const uai::ai::ui::ScreenSpec &screen : uai::ai::app_ui::kScreens) {
        for (std::size_t i = 0U; i < screen.button_count; ++i) {
            if (screen.buttons[i].id == wanted) return screen.buttons[i];
        }
    }
    return uai::ai::app_ui::kMainButtons[0];
}

void Tap(uai::ai::app_ui::AppUi &ui, uai::ai::app_ui::WidgetId id)
{
    const uai::ai::ui::Rect bounds = BoundsOf(id);
    ui.HandleTouch({true, static_cast<std::uint16_t>(bounds.x + bounds.width / 2U),
                    static_cast<std::uint16_t>(bounds.y + bounds.height / 2U)});
    ui.HandleTouch({false, 0U, 0U});
}

std::vector<std::uint16_t> PaintToPixels(const uai::ai::app_ui::AppUi &ui)
{
    std::vector<std::uint16_t> pixels(
        static_cast<std::size_t>(uai::ai::app_ui::kScreenWidth) *
            uai::ai::app_ui::kScreenHeight, 0x0000U);
    uai::ai::ui::Canvas canvas(pixels.data(), uai::ai::app_ui::kScreenWidth,
                               uai::ai::app_ui::kScreenHeight);
    ui.Overlay().Paint(canvas);
    return pixels;
}

std::uint16_t PixelAt(const std::vector<std::uint16_t> &pixels, std::uint16_t x,
                      std::uint16_t y)
{
    return pixels[static_cast<std::size_t>(y) * uai::ai::app_ui::kScreenWidth + x];
}

/* Guards the generated header: every widget of every screen must lie on the
 * screen and the main screen must be the camera one. */
TEST(AiAppUiLayout, GeneratedScreensFitDisplay)
{
    static_assert(uai::ai::app_ui::kScreenCount >= 2U);
    static_assert(static_cast<std::size_t>(uai::ai::app_ui::ScreenId::kMain) == 0U);
    EXPECT_EQ(uai::ai::app_ui::kScreens[0].background, uai::ai::ui::Background::kCamera);
    EXPECT_EQ(uai::ai::app_ui::kScreens[1].background, uai::ai::ui::Background::kSolid);

    const auto check = [](const uai::ai::ui::Rect &bounds) {
        EXPECT_LE(bounds.x + bounds.width, uai::ai::app_ui::kScreenWidth);
        EXPECT_LE(bounds.y + bounds.height, uai::ai::app_ui::kScreenHeight);
        EXPECT_GT(bounds.width, 0U);
        EXPECT_GT(bounds.height, 0U);
    };
    for (const uai::ai::ui::ScreenSpec &screen : uai::ai::app_ui::kScreens) {
        for (std::size_t i = 0U; i < screen.button_count; ++i) check(screen.buttons[i].bounds);
        for (std::size_t i = 0U; i < screen.slider_count; ++i) check(screen.sliders[i].bounds);
        for (std::size_t i = 0U; i < screen.dial_count; ++i) check(screen.dials[i].bounds);
        for (std::size_t i = 0U; i < screen.number_count; ++i) check(screen.numbers[i].bounds);
        for (std::size_t i = 0U; i < screen.wheel_count; ++i) {
            check(screen.wheels[i].bounds);
            EXPECT_GT(screen.wheels[i].item_count, 0U);
        }
        for (std::size_t i = 0U; i < screen.image_count; ++i) {
            check(screen.images[i].bounds);
            EXPECT_NE(screen.images[i].pixels, nullptr);
        }
        for (std::size_t i = 0U; i < screen.pad_count; ++i) check(screen.pads[i].bounds);
        for (std::size_t i = 0U; i < screen.label_count; ++i) {
            check(screen.labels[i].bounds);
            EXPECT_LT(std::strlen(screen.labels[i].text), uai::ai::ui::kLabelTextCapacity);
        }
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
    const std::vector<std::uint16_t> pixels = PaintToPixels(ui);
    const auto sample = [&](uai::ai::app_ui::WidgetId id) {
        const uai::ai::ui::Rect b = BoundsOf(id);
        return PixelAt(pixels, static_cast<std::uint16_t>(b.x + 6U),
                       static_cast<std::uint16_t>(b.y + 6U));
    };
    EXPECT_EQ(sample(uai::ai::app_ui::WidgetId::kFace),
              ButtonOf(uai::ai::app_ui::WidgetId::kFace).style.checked_fill);
    EXPECT_EQ(sample(uai::ai::app_ui::WidgetId::kPerson),
              ButtonOf(uai::ai::app_ui::WidgetId::kPerson).style.fill);
}

TEST(AiAppUi, BoxesToggleMaskAndConfidenceFilterVisibleBoxes)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    uai::ai::inference::BoxSet latest{};
    latest.person_valid = true;
    latest.person.count = 2U;
    latest.person.boxes[0].confidence = 0.9F;
    latest.person.boxes[1].confidence = 0.3F;
    latest.face_valid = true;
    latest.face.count = 1U;
    latest.face.boxes[0].confidence = 0.7F;
    latest.segmentation_valid = true;

    /* The layout's initial MIN CONFIDENCE is 50 %: the 0.3 box is hidden. */
    EXPECT_EQ(ui.MinConfidencePercent(), 50);
    uai::ai::inference::BoxSet visible = ui.VisibleBoxes(latest);
    EXPECT_EQ(visible.person.count, 1U);
    EXPECT_FLOAT_EQ(visible.person.boxes[0].confidence, 0.9F);
    EXPECT_EQ(visible.face.count, 1U);
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

TEST(AiAppUi, MenuNavigationAndSliders)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    EXPECT_EQ(ui.CurrentScreen(), uai::ai::app_ui::ScreenId::kMain);
    EXPECT_TRUE(ui.ShowsCamera());

    /* Hamburger -> settings screen, drawn with a solid background. */
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);
    EXPECT_EQ(ui.CurrentScreen(), uai::ai::app_ui::ScreenId::kMenu);
    EXPECT_FALSE(ui.ShowsCamera());
    const std::vector<std::uint16_t> menu = PaintToPixels(ui);
    EXPECT_EQ(PixelAt(menu, 600U, 400U), uai::ai::app_ui::kScreens[1].color);

    /* Main-screen buttons are not reachable while the menu is shown. */
    const std::uint8_t mask_before = models.mask;
    Tap(ui, uai::ai::app_ui::WidgetId::kPerson);
    EXPECT_EQ(models.mask, mask_before);

    /* Dragging the confidence slider to its right end sets 100 %. */
    const uai::ai::ui::Rect slider = BoundsOf(uai::ai::app_ui::WidgetId::kMinConfidence);
    const std::uint16_t y = static_cast<std::uint16_t>(slider.y + slider.height - 4U);
    uai::ai::ui::Event event = ui.HandleTouch(
        {true, static_cast<std::uint16_t>(slider.x + slider.width - 1U), y});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(ui.MinConfidencePercent(), 100);
    event = ui.HandleTouch({true, slider.x, y});
    EXPECT_EQ(event.value, 0);
    EXPECT_EQ(ui.MinConfidencePercent(), 0);
    ui.HandleTouch({false, 0U, 0U});

    /* The dial drives the status refresh period: touching the ring just
     * right of the bottom gap snaps to the maximum. */
    EXPECT_EQ(ui.StatusPeriod(), 500U);
    const uai::ai::ui::Rect disc =
        uai::ai::ui::DialPanel::DiscOf(DialOf(uai::ai::app_ui::WidgetId::kStatusPeriod));
    ui.HandleTouch({true, static_cast<std::uint16_t>(disc.x + disc.width / 2U + disc.width / 10U),
                    static_cast<std::uint16_t>(disc.y + disc.height - disc.height / 20U)});
    ui.HandleTouch({false, 0U, 0U});
    EXPECT_EQ(ui.StatusPeriod(), 2000U);

    /* Back arrow returns to the camera screen. */
    Tap(ui, uai::ai::app_ui::WidgetId::kCloseMenu);
    EXPECT_EQ(ui.CurrentScreen(), uai::ai::app_ui::ScreenId::kMain);
    EXPECT_TRUE(ui.ShowsCamera());
}

/* The MODELS wheel selects a preset mask, and the main-screen buttons keep
 * the wheel on the matching preset. */
TEST(AiAppUi, ModelsWheelSelectsPresetsAndFollowsButtons)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);

    const uai::ai::ui::Rect wheel = BoundsOf(uai::ai::app_ui::WidgetId::kModels);
    const std::uint16_t row = uai::ai::ui::WheelPanel::RowHeightOf(uai::ai::app_ui::kMenuWheels[0]);
    const std::uint16_t x = static_cast<std::uint16_t>(wheel.x + wheel.width / 2U);
    const std::uint16_t start = static_cast<std::uint16_t>(wheel.y + wheel.height - 4U);
    /* Drag up by two rows: ALL -> FACE. */
    ui.HandleTouch({true, x, start});
    const uai::ai::ui::Event event = ui.HandleTouch({true, x, static_cast<std::uint16_t>(start - 2U * row)});
    ui.HandleTouch({false, 0U, 0U});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(event.value, 2);
    EXPECT_EQ(models.mask, uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kFace));

    /* Turning PERSON back on gives PERSON+FACE, the last wheel item. */
    Tap(ui, uai::ai::app_ui::WidgetId::kCloseMenu);
    Tap(ui, uai::ai::app_ui::WidgetId::kPerson);
    EXPECT_EQ(models.mask, uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kPerson) |
                               uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kFace));
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);
    const std::vector<std::uint16_t> menu = PaintToPixels(ui);
    /* The highlight band sits in the middle row of the wheel. */
    EXPECT_EQ(PixelAt(menu, static_cast<std::uint16_t>(wheel.x + 4U),
                      static_cast<std::uint16_t>(wheel.y + wheel.height / 2U)),
              uai::ai::app_ui::kMenuWheels[0].style.highlight);
}

TEST(AiAppUi, StatusLabelShowsRatesPerEnabledModel)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    EXPECT_STREQ(ui.StatusText(), "AI STARTING");

    models.stats.enabled = false;
    ui.UpdateStatus(1000U);
    EXPECT_STREQ(ui.StatusText(), "AI PIPELINE OFF");
    EXPECT_EQ(ui.DetectionsValue(), 0);

    models.stats.enabled = true;
    models.stats.last_detection_count = 3U;
    ui.UpdateStatus(1200U);  /* within the 500 ms period: unchanged */
    EXPECT_STREQ(ui.StatusText(), "AI PIPELINE OFF");

    models.stats.person_completed = 15U;
    models.stats.face_completed = 5U;
    Tap(ui, uai::ai::app_ui::WidgetId::kSegmentation);
    ui.UpdateStatus(2000U);  /* 1000 ms since the last sample */
    EXPECT_STREQ(ui.StatusText(), "PERSON 15.0  FACE 5.0  SEG --  FPS");
    EXPECT_EQ(ui.DetectionsValue(), 3);
    EXPECT_EQ(ui.PersonRateTenths(), 150);

    models.stats.person_completed = 20U;
    ui.UpdateStatus(4000U);  /* 5 completions in 2000 ms */
    EXPECT_STREQ(ui.StatusText(), "PERSON 2.5  FACE 0.0  SEG --  FPS");
    EXPECT_EQ(ui.PersonRateTenths(), 25);
}

/* The logo bitmap is blitted with its transparent corners left to the
 * background. */
TEST(AiAppUi, MenuLogoIsBlittedWithTransparency)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);
    const std::vector<std::uint16_t> menu = PaintToPixels(ui);
    const uai::ai::ui::Rect logo = BoundsOf(uai::ai::app_ui::WidgetId::kLogo);
    const uai::ai::ui::ImageSpec &spec = uai::ai::app_ui::kMenuImages[0];
    EXPECT_TRUE(spec.has_transparent);
    EXPECT_EQ(PixelAt(menu, logo.x, logo.y), uai::ai::app_ui::kScreens[1].color);
    EXPECT_EQ(PixelAt(menu, static_cast<std::uint16_t>(logo.x + logo.width / 2U),
                      static_cast<std::uint16_t>(logo.y + 2U)),
              spec.pixels[2U * logo.width + logo.width / 2U]);
    EXPECT_NE(PixelAt(menu, static_cast<std::uint16_t>(logo.x + logo.width / 2U),
                      static_cast<std::uint16_t>(logo.y + 2U)),
              uai::ai::app_ui::kScreens[1].color);
}

/* Dispatch routes taps, navigation, and slider changes to the bound
 * handlers and nothing else. */
TEST(AiAppUiLayout, DispatchRoutesEventsToBoundHandlers)
{
    struct Handlers {
        int taps = 0;
        int changes = 0;
        std::int32_t last_value = 0;
        uai::ai::app_ui::ScreenId shown = uai::ai::app_ui::ScreenId::kMain;
        void OnToggleBoxesTap(const uai::ai::ui::Event &) { ++taps; }
        void OnPersonTap(const uai::ai::ui::Event &) { ++taps; }
        void OnFaceTap(const uai::ai::ui::Event &) { ++taps; }
        void OnSegmentationTap(const uai::ai::ui::Event &) { ++taps; }
        void OnModelsPrevTap(const uai::ai::ui::Event &) { ++taps; }
        void OnModelsNextTap(const uai::ai::ui::Event &) { ++taps; }
        void OnNavTap(const uai::ai::ui::Event &e) { ++taps; last_value = e.value; }
        void OnNavRotate(const uai::ai::ui::Event &e) { ++changes; last_value = e.value; }
        void OnMinConfidenceChange(const uai::ai::ui::Event &e) { ++changes; last_value = e.value; }
        void OnStatusPeriodChange(const uai::ai::ui::Event &e) { ++changes; last_value = e.value; }
        void OnModelsChange(const uai::ai::ui::Event &e) { ++changes; last_value = e.value; }
        void ShowScreen(uai::ai::app_ui::ScreenId screen) { shown = screen; }
    } handlers;

    uai::ai::ui::Event tap{};
    tap.type = uai::ai::ui::EventType::kTap;
    tap.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kOpenMenu);
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, tap));
    EXPECT_EQ(handlers.shown, uai::ai::app_ui::ScreenId::kMenu);
    EXPECT_EQ(handlers.taps, 0);

    uai::ai::ui::Event change{};
    change.type = uai::ai::ui::EventType::kChange;
    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kMinConfidence);
    change.value = 35;
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, change));
    EXPECT_EQ(handlers.changes, 1);
    EXPECT_EQ(handlers.last_value, 35);

    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kModels);
    change.value = 4;
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, change));
    EXPECT_EQ(handlers.changes, 2);
    EXPECT_EQ(handlers.last_value, 4);

    /* Numbers and images have no events. */
    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kDetections);
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, change));

    uai::ai::ui::Event press = tap;
    press.type = uai::ai::ui::EventType::kPress;
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, press));

    uai::ai::ui::Event label_tap = tap;
    label_tap.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kStatus);
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, label_tap));

    /* The pad is bound for taps and ring turns but not presses. */
    uai::ai::ui::Event pad = tap;
    pad.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kNav);
    pad.value = static_cast<std::int32_t>(uai::ai::ui::PadSegment::kLeft);
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, pad));
    EXPECT_EQ(handlers.taps, 1);
    EXPECT_EQ(handlers.last_value, 3);
    pad.type = uai::ai::ui::EventType::kChange;
    pad.value = -1;
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, pad));
    EXPECT_EQ(handlers.changes, 3);
    pad.type = uai::ai::ui::EventType::kPress;
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, pad));
}

/* The camera-screen pad drives confidence, model presets and boxes, and
 * the menu's triangle buttons step the preset with wrap-around. */
TEST(AiAppUi, PadAndTriangleButtonsControlPresetsAndConfidence)
{
    using uai::ai::app_ui::WidgetId;
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    const uai::ai::ui::Rect pad = BoundsOf(WidgetId::kNav);
    const std::uint16_t cx = static_cast<std::uint16_t>(pad.x + pad.width / 2U);
    const std::uint16_t cy = static_cast<std::uint16_t>(pad.y + pad.height / 2U);
    const auto tap_at = [&](std::uint16_t x, std::uint16_t y) {
        ui.HandleTouch({true, x, y});
        ui.HandleTouch({false, 0U, 0U});
    };

    EXPECT_EQ(ui.MinConfidencePercent(), 50);
    tap_at(cx, static_cast<std::uint16_t>(pad.y + 8U));              /* up */
    EXPECT_EQ(ui.MinConfidencePercent(), 55);
    tap_at(cx, static_cast<std::uint16_t>(pad.y + pad.height - 9U));  /* down */
    tap_at(cx, static_cast<std::uint16_t>(pad.y + pad.height - 9U));
    EXPECT_EQ(ui.MinConfidencePercent(), 45);
    /* The menu slider follows the pad-driven value: its knob sits left of
     * where 50 % would put it. */
    Tap(ui, WidgetId::kOpenMenu);
    const uai::ai::ui::Rect track = uai::ai::ui::SliderPanel::TrackOf(uai::ai::app_ui::kMenuSliders[0]);
    const std::vector<std::uint16_t> menu = PaintToPixels(ui);
    const std::uint16_t knob_x = static_cast<std::uint16_t>(track.x + (track.width - 1U) * 45U / 100U);
    EXPECT_EQ(PixelAt(menu, knob_x, track.y), uai::ai::app_ui::kMenuSliders[0].style.knob);
    EXPECT_EQ(PixelAt(menu, static_cast<std::uint16_t>(track.x + (track.width - 1U) / 2U + 10U), track.y),
              uai::ai::app_ui::kMenuSliders[0].style.track);
    Tap(ui, WidgetId::kCloseMenu);

    tap_at(static_cast<std::uint16_t>(pad.x + pad.width - 9U), cy);   /* right */
    EXPECT_EQ(models.mask, uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kPerson));
    tap_at(static_cast<std::uint16_t>(pad.x + 8U), cy);               /* left */
    EXPECT_EQ(models.mask, uai::ai::task::kAllModelsMask);
    tap_at(static_cast<std::uint16_t>(pad.x + 8U), cy);               /* wraps to last */
    EXPECT_EQ(models.mask, uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kPerson) |
                               uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kFace));

    EXPECT_TRUE(ui.ShowBoxes());
    tap_at(cx, cy);                                                    /* centre */
    EXPECT_FALSE(ui.ShowBoxes());

    /* A quarter turn clockwise around the ring is two detents: +10 %. */
    ui.HandleTouch({true, cx, static_cast<std::uint16_t>(pad.y + 8U)});
    ui.HandleTouch({true, static_cast<std::uint16_t>(pad.x + pad.width - 28U),
                    static_cast<std::uint16_t>(pad.y + 28U)});
    ui.HandleTouch({true, static_cast<std::uint16_t>(pad.x + pad.width - 9U), cy});
    ui.HandleTouch({false, 0U, 0U});
    EXPECT_EQ(ui.MinConfidencePercent(), 55);
    EXPECT_FALSE(ui.ShowBoxes());  /* turning never taps */

    Tap(ui, WidgetId::kOpenMenu);
    Tap(ui, WidgetId::kModelsNext);
    EXPECT_EQ(models.mask, uai::ai::task::kAllModelsMask);
    Tap(ui, WidgetId::kModelsPrev);
    Tap(ui, WidgetId::kModelsPrev);
    EXPECT_EQ(models.mask, uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kSegmentation));
}

} // namespace
