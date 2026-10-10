#include "middleware/ui/canvas.hpp"
#include "middleware/ui/widget.hpp"
#include "ui/app_ui.hpp"
#include "ui/ui_layout.hpp"
#include "task/camera_render_state.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

struct UiCamera {
    uai::ai::camera::State state{};
    uai::ai::camera::Geometry geometry{};
    struct Counters {
        unsigned frame_event_count = 0U;
        unsigned pipe2_frame_event_count = 0U;
        unsigned dcmipp_error_count = 0U;
        unsigned csi_error_count = 0U;
        unsigned isp_error_count = 0U;
    };
    Counters GetDiagnostics() const { return {}; }
    uai::ai::common::Error ReadState(uai::ai::camera::State *output)
    {
        *output = state;
        return {};
    }
    uai::ai::common::Error GetGeometry(uai::ai::camera::Geometry *output)
    {
        *output = geometry;
        return {};
    }
    uai::ai::common::Error AutoExposure(bool enabled)
    {
        state.auto_exposure = enabled;
        return {};
    }
    uai::ai::common::Error Compensation(int value)
    {
        state.compensation = value;
        return {};
    }
    uai::ai::common::Error Manual(
        std::int32_t exposure,
        std::int32_t gain
    )
    {
        state.reported_exposure_us = exposure;
        state.reported_gain_mdB = gain;
        return {};
    }
    uai::ai::common::Error Statistics(uai::ai::camera::Rect area)
    {
        state.statistics = area;
        return {};
    }
    uai::ai::common::Error Configure(const uai::ai::camera::Geometry &next)
    {
        geometry = next;
        return {};
    }
    uai::ai::common::Error ApplyState(const uai::ai::camera::State &previous)
    {
        state = previous;
        return {};
    }
};

TEST(
    CameraRenderState,
    PreservesInitialUiAndResultOwnership
)
{
    FakeModels models;
    uai::ai::inference::BoxSet initial{};
    initial.person_valid = true;
    initial.person.count = 1U;
    initial.person.boxes[0U].confidence = 0.75F;
    uai::ai::task::CameraRenderState state(models, initial, 100U);
    initial.person.count = 0U;
    EXPECT_EQ(state.results.boxes.person.count, 1U);
    EXPECT_EQ(state.ui.MinConfidencePercent(), 50);
    EXPECT_EQ(state.ui.StatusPeriod(), 500U);
    EXPECT_EQ(state.previous_exposure_enabled, state.ui.AiExposureEnabled());
    EXPECT_EQ(state.schedule.FrameStride(), 1U);
    EXPECT_TRUE(state.schedule.TouchDue(100U));
    EXPECT_EQ(state.loop_count, 0U);
    EXPECT_EQ(state.last_exposure_error_tick, 0U);
    state.ui.SetShowBoxes(false);
    EXPECT_EQ(state.ui.VisibleBoxes(state.results.boxes).person.count, 0U);
    EXPECT_EQ(state.results.boxes.person.count, 1U);
}

TEST(
    ButtonAvailability,
    DisablingPressedButtonCancelsTapAndDimsCheckedState
)
{
    uai::ai::ui::ButtonSpec button;
    button.id = 42U;
    button.bounds = {0U, 0U, 40U, 40U};
    button.label = "";
    uai::ai::ui::ButtonPanel panel(&button, 1U);
    panel.SetChecked(42U, true);
    EXPECT_EQ(panel.Update({true, 20U, 20U}).type, uai::ai::ui::EventType::kPress);
    panel.SetEnabled(42U, false);
    EXPECT_FALSE(panel.IsPressed(42U));
    EXPECT_FALSE(panel.IsEnabled(42U));
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
    EXPECT_EQ(panel.Update({true, 20U, 20U}).type, uai::ai::ui::EventType::kNone);
    std::vector<std::uint16_t> pixels(40U * 40U);
    uai::ai::ui::Canvas canvas(pixels.data(), 40U, 40U);
    panel.Paint(canvas);
    EXPECT_EQ(pixels[20U * 40U + 20U], (button.style.checked_fill & 0xF7DEU) >> 1U);
    panel.Update({false, 0U, 0U});
    panel.SetEnabled(42U, true);
    EXPECT_TRUE(panel.IsChecked(42U));
    EXPECT_EQ(panel.Update({true, 20U, 20U}).type, uai::ai::ui::EventType::kPress);
}

TEST(
    CameraRenderState,
    SettingsCanRepaintWithoutCaptureAndLiveCameraCannotReuseReturnedFrame
)
{
    FakeModels models;
    uai::ai::task::CameraRenderState state(models, {}, 0U);
    EXPECT_FALSE(state.ShouldPresent(false, true, false));
    state.ui.ShowScreen(uai::ai::app_ui::ScreenId::kMenu);
    EXPECT_TRUE(state.ShouldPresent(false, true, false));
    EXPECT_FALSE(state.ShouldPresent(false, false, false));
    EXPECT_FALSE(state.ShouldPresent(true, true, true));
    EXPECT_TRUE(state.ShouldPresent(true, false, false));
}

TEST(
    CameraRenderState,
    DisabledTouchStillKeepsExposureAndStatusDeadline
)
{
    struct Camera {
        uai::ai::common::Error ReadState(uai::ai::camera::State *state)
        {
            *state = {};
            return {};
        }
        uai::ai::common::Error GetGeometry(uai::ai::camera::Geometry *geometry)
        {
            *geometry = {};
            return {};
        }
        uai::ai::common::Error AutoExposure(bool) { return {}; }
        uai::ai::common::Error Statistics(uai::ai::camera::Rect) { return {}; }
    } camera;
    FakeModels models;
    uai::ai::task::CameraRenderState state(models, {}, 100U);
    state.ui.UpdateStatus(100U);
    EXPECT_TRUE(state.exposure.Process(camera, 100U).Ok());
    state.schedule.TouchPolled(100U);
    EXPECT_EQ(state.RemainingWait(100U, true), 10U);
    EXPECT_EQ(state.RemainingWait(100U, false), 250U);
    EXPECT_EQ(state.RemainingWait(350U, false), 0U);
}

TEST(
    AppUiExposure,
    DisplaysControllerReadbackAndFailure
)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    uai::ai::exposure_control::Values values{};
    values.source = uai::ai::exposure_control::Source::kFace;
    values.available = true;
    values.auto_exposure = true;
    values.exposure_us = 12000;
    values.gain_mdB = 3000;
    ui.UpdateStatus(100, &values);
    EXPECT_STREQ(ui.ExposureText(), "AE FACE 12000us 3000mdB");
    values.error_code = 7;
    ui.UpdateStatus(1000, &values);
    EXPECT_STREQ(ui.ExposureText(), "AE ERROR 7");
    ui.UpdateStatus(2000);
    EXPECT_STREQ(ui.ExposureText(), "AI AE OFF");
}

uai::ai::ui::Rect BoundsOf(uai::ai::app_ui::WidgetId id)
{
    const std::uint16_t wanted = static_cast<std::uint16_t>(id);
    for (const uai::ai::ui::ScreenSpec &screen : uai::ai::app_ui::kScreens) {
        for (std::size_t i = 0U; i < screen.button_count; ++i) {
            if (screen.buttons[i].id == wanted)
                return screen.buttons[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.label_count; ++i) {
            if (screen.labels[i].id == wanted)
                return screen.labels[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.slider_count; ++i) {
            if (screen.sliders[i].id == wanted)
                return screen.sliders[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.dial_count; ++i) {
            if (screen.dials[i].id == wanted)
                return screen.dials[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.wheel_count; ++i) {
            if (screen.wheels[i].id == wanted)
                return screen.wheels[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.number_count; ++i) {
            if (screen.numbers[i].id == wanted)
                return screen.numbers[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.image_count; ++i) {
            if (screen.images[i].id == wanted)
                return screen.images[i].bounds;
        }
        for (std::size_t i = 0U; i < screen.pad_count; ++i) {
            if (screen.pads[i].id == wanted)
                return screen.pads[i].bounds;
        }
    }
    return {};
}

const uai::ai::ui::DialSpec &DialOf(uai::ai::app_ui::WidgetId id)
{
    const std::uint16_t wanted = static_cast<std::uint16_t>(id);
    for (const uai::ai::ui::ScreenSpec &screen : uai::ai::app_ui::kScreens) {
        for (std::size_t i = 0U; i < screen.dial_count; ++i) {
            if (screen.dials[i].id == wanted)
                return screen.dials[i];
        }
    }
    return uai::ai::app_ui::kMenuDials[0];
}

const uai::ai::ui::ButtonSpec &ButtonOf(uai::ai::app_ui::WidgetId id)
{
    const std::uint16_t wanted = static_cast<std::uint16_t>(id);
    for (const uai::ai::ui::ScreenSpec &screen : uai::ai::app_ui::kScreens) {
        for (std::size_t i = 0U; i < screen.button_count; ++i) {
            if (screen.buttons[i].id == wanted)
                return screen.buttons[i];
        }
    }
    return uai::ai::app_ui::kMainButtons[0];
}

uai::ai::ui::Event
Tap(uai::ai::app_ui::AppUi &ui,
    uai::ai::app_ui::WidgetId id,
    bool apply = true)
{
    const uai::ai::ui::Rect bounds = BoundsOf(id);
    ui.HandleTouch(
        {true,
         static_cast<std::uint16_t>(bounds.x + bounds.width / 2U),
         static_cast<std::uint16_t>(bounds.y + bounds.height / 2U)}
    );
    const auto event = ui.HandleTouch({false, 0U, 0U});
    if (apply) {
        UiCamera camera;
        uai::ai::shell::ExposureMode mode;
        uai::ai::shell::Reply reply;
        if (uai::ai::shell::ApplyTouch(event, camera, ui, mode, &reply)) {
            EXPECT_EQ(reply.code, 0);
            ui.RecordOperationResult(reply.code);
        }
    }
    return event;
}

std::vector<std::uint16_t> PaintToPixels(
    const uai::ai::app_ui::AppUi &ui,
    std::uint16_t background = 0x0000U
)
{
    std::vector<std::uint16_t> pixels(
        static_cast<std::size_t>(uai::ai::app_ui::kScreenWidth) * uai::ai::app_ui::kScreenHeight, background
    );
    uai::ai::ui::Canvas canvas(pixels.data(), uai::ai::app_ui::kScreenWidth, uai::ai::app_ui::kScreenHeight);
    ui.Overlay().Paint(canvas);
    return pixels;
}

std::uint16_t PixelAt(
    const std::vector<std::uint16_t> &pixels,
    std::uint16_t x,
    std::uint16_t y
)
{
    return pixels[static_cast<std::size_t>(y) * uai::ai::app_ui::kScreenWidth + x];
}

TEST(
    AiAppUi,
    TouchAndShellReachTheSameStateAndOperationDisplay
)
{
    FakeModels touch_models;
    FakeModels shell_models;
    uai::ai::app_ui::AppUi touch_ui(touch_models);
    uai::ai::app_ui::AppUi shell_ui(shell_models);
    Tap(touch_ui, uai::ai::app_ui::WidgetId::kOpenMenu);
    UiCamera camera;
    uai::ai::shell::ExposureMode mode;
    const auto check = [&](uai::ai::app_ui::WidgetId widget, const uai::ai::shell::Request &request) {
        Tap(touch_ui, widget);
        const auto reply = uai::ai::shell::Apply(request, camera, shell_ui, mode);
        shell_ui.RecordOperationResult(reply.code);
        EXPECT_EQ(reply.code, 0);
        EXPECT_EQ(touch_ui.ModelMask(), shell_ui.ModelMask());
        EXPECT_EQ(touch_ui.ShowBoxes(), shell_ui.ShowBoxes());
        EXPECT_EQ(touch_ui.AiExposureEnabled(), shell_ui.AiExposureEnabled());
        EXPECT_STREQ(touch_ui.OperationText(), shell_ui.OperationText());
    };
    using uai::ai::shell::Action;
    using uai::ai::app_ui::WidgetId;
    check(WidgetId::kPerson, {Action::kModels, {6}});
    check(WidgetId::kFace, {Action::kModels, {4}});
    check(WidgetId::kSegmentation, {Action::kModels, {0}});
    check(WidgetId::kToggleBoxes, {Action::kBoxes, {0}});
    check(WidgetId::kAiExposure, {Action::kAiExposure, {0}});
    check(WidgetId::kAiExposure, {Action::kAiExposure, {1}});
    touch_ui.RecordOperationResult(7);
    touch_ui.UpdateStatus(100U);
    EXPECT_STREQ(touch_ui.OperationText(), "APPLY ERROR 7");
}

/* Guards the generated header: every widget of every screen must lie on the
 * screen and the main screen must be the camera one. */
TEST(
    AiAppUiLayout,
    GeneratedScreensFitDisplay
)
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
        for (std::size_t i = 0U; i < screen.button_count; ++i)
            check(screen.buttons[i].bounds);
        for (std::size_t i = 0U; i < screen.slider_count; ++i)
            check(screen.sliders[i].bounds);
        for (std::size_t i = 0U; i < screen.dial_count; ++i)
            check(screen.dials[i].bounds);
        for (std::size_t i = 0U; i < screen.number_count; ++i)
            check(screen.numbers[i].bounds);
        for (std::size_t i = 0U; i < screen.wheel_count; ++i) {
            check(screen.wheels[i].bounds);
            EXPECT_GT(screen.wheels[i].item_count, 0U);
        }
        for (std::size_t i = 0U; i < screen.image_count; ++i) {
            check(screen.images[i].bounds);
            EXPECT_NE(screen.images[i].pixels, nullptr);
        }
        for (std::size_t i = 0U; i < screen.pad_count; ++i)
            check(screen.pads[i].bounds);
        for (std::size_t i = 0U; i < screen.label_count; ++i) {
            check(screen.labels[i].bounds);
            EXPECT_LT(std::strlen(screen.labels[i].text), uai::ai::ui::kLabelTextCapacity);
        }
    }
}

TEST(
    AiAppUi,
    CameraScreenKeepsControlsInMenu
)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    const auto header = BoundsOf(uai::ai::app_ui::WidgetId::kStatus);
    const auto exposure = BoundsOf(uai::ai::app_ui::WidgetId::kExposureStatus);
    const auto menu_button = BoundsOf(uai::ai::app_ui::WidgetId::kOpenMenu);
    const std::uint16_t camera_pixel = 0x07e0U;
    const auto pixels = PaintToPixels(ui, camera_pixel);

    EXPECT_EQ(header.x, 0U);
    EXPECT_EQ(header.y, 0U);
    EXPECT_EQ(header.x + header.width, menu_button.x);
    EXPECT_EQ(menu_button.y, 0U);
    EXPECT_EQ(exposure.y, header.height);
    EXPECT_EQ(menu_button.height, header.height + exposure.height);
    EXPECT_EQ(menu_button.x + menu_button.width, uai::ai::app_ui::kScreenWidth);
    for (std::uint16_t column = 0U; column < uai::ai::app_ui::kScreenWidth; ++column) {
        EXPECT_EQ(PixelAt(pixels, column, 0U), 0x0000U);
    }
    const auto camera_start =
        pixels.begin() + static_cast<std::size_t>(menu_button.height) * uai::ai::app_ui::kScreenWidth;
    EXPECT_EQ(std::count(camera_start, pixels.end(), camera_pixel), pixels.end() - camera_start);

    for (const auto id :
         {uai::ai::app_ui::WidgetId::kPerson,
          uai::ai::app_ui::WidgetId::kFace,
          uai::ai::app_ui::WidgetId::kSegmentation,
          uai::ai::app_ui::WidgetId::kToggleBoxes,
          uai::ai::app_ui::WidgetId::kMinConfidence,
          uai::ai::app_ui::WidgetId::kStatusPeriod}) {
        Tap(ui, id);
    }
    EXPECT_EQ(models.mask, uai::ai::task::kAllModelsMask);
    EXPECT_TRUE(ui.ShowBoxes());
    EXPECT_EQ(ui.MinConfidencePercent(), 50);
    EXPECT_EQ(ui.StatusPeriod(), 500U);
    EXPECT_EQ(ui.CurrentScreen(), uai::ai::app_ui::ScreenId::kMain);
}

TEST(
    AiAppUi,
    ModelButtonsToggleMaskAndCheckedState
)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);

    Tap(ui, uai::ai::app_ui::WidgetId::kFace);
    EXPECT_EQ(
        models.mask, uai::ai::task::kAllModelsMask & ~uai::ai::task::ModelMaskBit(uai::ai::task::ModelBit::kFace)
    );
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
        return PixelAt(pixels, static_cast<std::uint16_t>(b.x + 6U), static_cast<std::uint16_t>(b.y + 6U));
    };
    EXPECT_EQ(sample(uai::ai::app_ui::WidgetId::kFace), ButtonOf(uai::ai::app_ui::WidgetId::kFace).style.checked_fill);
    EXPECT_EQ(sample(uai::ai::app_ui::WidgetId::kPerson), ButtonOf(uai::ai::app_ui::WidgetId::kPerson).style.fill);
}

TEST(
    AiAppUi,
    BoxesToggleMaskAndConfidenceFilterVisibleBoxes
)
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

    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);
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
    ui.HandleTouch({true, 540U, 448U});
    ui.HandleTouch({false, 0U, 0U});
    EXPECT_FALSE(ui.ShowBoxes());
}

TEST(
    AiAppUi,
    AiExposureToggleIsIndependentFromInferenceOverlay
)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    EXPECT_TRUE(ui.AiExposureEnabled());
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);
    const auto sample = [&] {
        const auto bounds = BoundsOf(uai::ai::app_ui::WidgetId::kAiExposure);
        const auto pixels = PaintToPixels(ui);
        return PixelAt(pixels, static_cast<std::uint16_t>(bounds.x + 6U), static_cast<std::uint16_t>(bounds.y + 6U));
    };
    EXPECT_EQ(sample(), ButtonOf(uai::ai::app_ui::WidgetId::kAiExposure).style.checked_fill);
    const auto exposure_tap = Tap(ui, uai::ai::app_ui::WidgetId::kAiExposure, false);
    EXPECT_EQ(exposure_tap.type, uai::ai::ui::EventType::kTap);
    EXPECT_TRUE(ui.AiExposureEnabled());
    ui.SetAiExposureEnabled(false);
    EXPECT_TRUE(ui.ShowBoxes());
    EXPECT_EQ(sample(), ButtonOf(uai::ai::app_ui::WidgetId::kAiExposure).style.fill);
    uai::ai::exposure_control::Values exposure{};
    exposure.available = true;
    exposure.auto_exposure = true;
    exposure.exposure_us = 12000;
    ui.UpdateStatus(1U, &exposure);
    EXPECT_STREQ(ui.ExposureText(), "AI AE OFF 12000us 0mdB");
    Tap(ui, uai::ai::app_ui::WidgetId::kToggleBoxes);
    EXPECT_FALSE(ui.ShowBoxes());
    EXPECT_FALSE(ui.AiExposureEnabled());
    Tap(ui, uai::ai::app_ui::WidgetId::kAiExposure, false);
    EXPECT_FALSE(ui.AiExposureEnabled());
    ui.SetAiExposureEnabled(true);
    EXPECT_TRUE(ui.AiExposureEnabled());
    ui.UpdateStatus(2U, &exposure);
    EXPECT_STREQ(ui.ExposureText(), "AE FULL 12000us 0mdB");
}

TEST(
    AiAppUi,
    MenuNavigationAndSliders
)
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
    EXPECT_EQ(PixelAt(menu, 400U, 400U), uai::ai::app_ui::kScreens[1].color);

    /* Dragging the confidence slider to its right end sets 100 %. */
    const uai::ai::ui::Rect slider = BoundsOf(uai::ai::app_ui::WidgetId::kMinConfidence);
    const std::uint16_t y = static_cast<std::uint16_t>(slider.y + slider.height - 4U);
    uai::ai::ui::Event event = ui.HandleTouch({true, static_cast<std::uint16_t>(slider.x + slider.width - 1U), y});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(ui.MinConfidencePercent(), 100);
    event = ui.HandleTouch({true, slider.x, y});
    EXPECT_EQ(event.value, 0);
    EXPECT_EQ(ui.MinConfidencePercent(), 0);
    ui.HandleTouch({false, 0U, 0U});

    /* The dial drives the status refresh period: touching the ring just
     * right of the bottom gap snaps to the maximum. */
    EXPECT_EQ(ui.StatusPeriod(), 500U);
    const uai::ai::ui::Rect disc = uai::ai::ui::DialPanel::DiscOf(DialOf(uai::ai::app_ui::WidgetId::kStatusPeriod));
    ui.HandleTouch(
        {true,
         static_cast<std::uint16_t>(disc.x + disc.width / 2U + disc.width / 10U),
         static_cast<std::uint16_t>(disc.y + disc.height - disc.height / 20U)}
    );
    ui.HandleTouch({false, 0U, 0U});
    EXPECT_EQ(ui.StatusPeriod(), 2000U);

    /* Back arrow returns to the camera screen. */
    Tap(ui, uai::ai::app_ui::WidgetId::kCloseMenu);
    EXPECT_EQ(ui.CurrentScreen(), uai::ai::app_ui::ScreenId::kMain);
    EXPECT_TRUE(ui.ShowsCamera());
}

TEST(
    AiAppUi,
    StatusLabelShowsRatesPerEnabledModel
)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    EXPECT_STREQ(ui.StatusText(), "AI STARTING");

    models.stats.enabled = false;
    ui.UpdateStatus(1000U);
    EXPECT_STREQ(ui.StatusText(), "AI PIPELINE OFF");

    models.stats.enabled = true;
    ui.UpdateStatus(1200U); /* within the 500 ms period: unchanged */
    EXPECT_STREQ(ui.StatusText(), "AI PIPELINE OFF");

    models.stats.person_completed = 15U;
    models.stats.face_completed = 5U;
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);
    Tap(ui, uai::ai::app_ui::WidgetId::kSegmentation);
    ui.UpdateStatus(2000U); /* 1000 ms since the last sample */
    EXPECT_STREQ(ui.StatusText(), "PERSON 15.0  FACE 5.0  SEG --  FPS");

    models.stats.person_completed = 20U;
    ui.UpdateStatus(4000U); /* 5 completions in 2000 ms */
    EXPECT_STREQ(ui.StatusText(), "PERSON 2.5  FACE 0.0  SEG --  FPS");
}

/* The menu bitmaps are blitted with their transparent corners left to the
 * background. */
TEST(
    AiAppUi,
    MenuImagesAreBlittedWithTransparency
)
{
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    Tap(ui, uai::ai::app_ui::WidgetId::kOpenMenu);
    const std::vector<std::uint16_t> menu = PaintToPixels(ui);
    for (const uai::ai::ui::ImageSpec &spec : uai::ai::app_ui::kMenuImages) {
        ASSERT_TRUE(spec.has_transparent);
        EXPECT_EQ(PixelAt(menu, spec.bounds.x, spec.bounds.y), uai::ai::app_ui::kScreens[1].color);
        const auto end = spec.pixels + spec.bounds.width * spec.bounds.height;
        const auto visible = std::find_if(spec.pixels, end, [&spec](std::uint16_t pixel) {
            return pixel != spec.transparent;
        });
        ASSERT_NE(visible, end);
        const auto offset = visible - spec.pixels;
        const auto horizontal = static_cast<std::uint16_t>(spec.bounds.x + offset % spec.bounds.width);
        const auto vertical = static_cast<std::uint16_t>(spec.bounds.y + offset / spec.bounds.width);
        EXPECT_EQ(PixelAt(menu, horizontal, vertical), *visible);
        EXPECT_NE(PixelAt(menu, horizontal, vertical), uai::ai::app_ui::kScreens[1].color);
    }
}

/* Dispatch routes taps, navigation, and slider changes to the bound
 * handlers and nothing else. */
TEST(
    AiAppUiLayout,
    DispatchRoutesEventsToBoundHandlers
)
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
        void OnMinConfidenceChange(const uai::ai::ui::Event &e)
        {
            ++changes;
            last_value = e.value;
        }
        void OnStatusPeriodChange(const uai::ai::ui::Event &e)
        {
            ++changes;
            last_value = e.value;
        }
        void ShowScreen(uai::ai::app_ui::ScreenId screen) { shown = screen; }
    } handlers;

    uai::ai::ui::Event tap{};
    tap.type = uai::ai::ui::EventType::kTap;
    tap.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kOpenMenu);
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, tap));
    EXPECT_EQ(handlers.shown, uai::ai::app_ui::ScreenId::kMenu);
    EXPECT_EQ(handlers.taps, 0);

    tap.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kAiExposure);
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, tap));
    EXPECT_EQ(handlers.taps, 0);

    uai::ai::ui::Event change{};
    change.type = uai::ai::ui::EventType::kChange;
    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kMinConfidence);
    change.value = 35;
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, change));
    EXPECT_EQ(handlers.changes, 1);
    EXPECT_EQ(handlers.last_value, 35);

    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kStatusPeriod);
    change.value = 1000;
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, change));
    EXPECT_EQ(handlers.changes, 2);
    EXPECT_EQ(handlers.last_value, 1000);

    /* Images have no events. */
    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kLogo);
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, change));

    uai::ai::ui::Event press = tap;
    press.type = uai::ai::ui::EventType::kPress;
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, press));

    uai::ai::ui::Event label_tap = tap;
    label_tap.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kStatus);
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, label_tap));
}

} // namespace
