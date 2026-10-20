#include "middleware/ui/canvas.hpp"
#include "middleware/ui/widget.hpp"
#include "ui/app_ui.hpp"
#include "ui/ui_layout.hpp"
#include "ui/exposure_visualization.hpp"
#include "task/camera_render_state.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

TEST(
    ExposureAnalysis,
    SamplingDoesNotWaitAndCalculationOnlyRunsOnWorker
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureAnalysis analysis;
    app_ui::ExposureVisualizationSettings settings;
    settings.histogram = true;
    std::vector<std::uint16_t> source(800U * 480U, 0xFFFFU);
    EXPECT_EQ(analysis.Latest(settings, 0U), nullptr);
    EXPECT_TRUE(analysis.ReadyToSubmit(settings, 0U));
    ASSERT_TRUE(analysis.TrySubmit(source.data(), 800U, 480U, settings, 1U, 0U));
    EXPECT_FALSE(analysis.ReadyToSubmit(settings, 200U));
    EXPECT_EQ(analysis.Latest(settings, 0U), nullptr);
    EXPECT_FALSE(analysis.TrySubmit(source.data(), 800U, 480U, settings, 2U, 200U));
    std::fill(source.begin(), source.end(), 0U);
    ASSERT_TRUE(analysis.ProcessPending());
    const auto *result = analysis.Latest(settings, 200U);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->histogram.samples, app_ui::kExposureSampleCount);
    EXPECT_EQ(result->histogram.highlights, app_ui::kExposureSampleCount);
    EXPECT_EQ(result->capture_sequence, 1U);
    EXPECT_STREQ(result->highlights_text.data(), "W 100.0%");
    EXPECT_FALSE(analysis.ProcessPending());
    EXPECT_FALSE(analysis.ReadyToSubmit(settings, 199U));
    EXPECT_TRUE(analysis.ReadyToSubmit(settings, 200U));
    EXPECT_FALSE(analysis.TrySubmit(source.data(), 800U, 480U, settings, 2U, 199U));
    EXPECT_TRUE(analysis.TrySubmit(source.data(), 800U, 480U, settings, 2U, 200U));
}

TEST(
    ExposureAnalysis,
    PublishedResultStaysImmutableUntilConsumerTakesLatest
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureAnalysis analysis;
    app_ui::ExposureVisualizationSettings settings;
    settings.histogram = true;
    settings.zebra_white = true;
    const std::uint16_t white = 0xFFFFU;
    const std::uint16_t black = 0U;
    ASSERT_TRUE(analysis.TrySubmit(&white, 1U, 1U, settings, 1U, 0U));
    ASSERT_TRUE(analysis.ProcessPending());
    const auto *held = analysis.Latest(settings, 0U);
    ASSERT_NE(held, nullptr);
    for (std::uint32_t sequence = 2U; sequence <= 5U; ++sequence) {
        ASSERT_TRUE(analysis.TrySubmit(&black, 1U, 1U, settings, sequence, sequence * 200U));
        ASSERT_TRUE(analysis.ProcessPending());
        EXPECT_EQ(held->capture_sequence, 1U);
        EXPECT_EQ(held->histogram.highlights, app_ui::kExposureSampleCount);
    }
    const auto *latest = analysis.Latest(settings, 1000U);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->capture_sequence, 5U);
    EXPECT_EQ(latest->histogram.shadows, app_ui::kExposureSampleCount);
    EXPECT_EQ(latest->zebra[0U], 0U);
    EXPECT_EQ(analysis.Latest(settings, 2001U), latest);
    settings.white_level = 240U;
    EXPECT_EQ(analysis.Latest(settings, 1000U), nullptr);
    settings = {};
    EXPECT_FALSE(analysis.TrySubmit(&white, 1U, 1U, settings, 6U, 2200U));
    EXPECT_EQ(analysis.Latest(settings, 1000U), nullptr);
}

TEST(
    ExposureAnalysis,
    ConcurrentWorkerCannotOverwriteTheDisplayedResult
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureAnalysis analysis;
    app_ui::ExposureVisualizationSettings settings;
    settings.histogram = true;
    settings.zebra_white = true;
    const std::uint16_t white = 0xFFFFU;
    const std::uint16_t black = 0U;
    ASSERT_TRUE(analysis.TrySubmit(&white, 1U, 1U, settings, 1U, 0U));
    ASSERT_TRUE(analysis.ProcessPending());
    std::atomic<bool> done{false};
    std::atomic<std::uint32_t> published{0U};
    std::thread worker([&] {
        while (!done.load(std::memory_order_acquire)) {
            if (analysis.ProcessPending())
                published.fetch_add(1U, std::memory_order_relaxed);
            else
                std::this_thread::yield();
        }
        if (analysis.ProcessPending())
            published.fetch_add(1U, std::memory_order_relaxed);
    });
    for (std::uint32_t sequence = 2U; sequence <= 2000U; ++sequence) {
        const auto now = sequence * 200U;
        const auto *source = sequence % 2U == 0U ? &black : &white;
        (void)analysis.TrySubmit(source, 1U, 1U, settings, sequence, now);
        const auto *held = analysis.Latest(settings, now);
        if (held != nullptr) {
            const auto held_sequence = held->capture_sequence;
            const auto highlights = held_sequence % 2U == 0U ? 0U : app_ui::kExposureSampleCount;
            const auto shadows = app_ui::kExposureSampleCount - highlights;
            std::this_thread::yield();
            EXPECT_EQ(held->capture_sequence, held_sequence);
            EXPECT_EQ(held->histogram.highlights, highlights);
            EXPECT_EQ(held->histogram.shadows, shadows);
            EXPECT_EQ(held->histogram.samples, app_ui::kExposureSampleCount);
            EXPECT_EQ(held->zebra[0U], highlights == 0U ? 0U : 2U);
        } else {
            std::this_thread::yield();
        }
    }
    done.store(true, std::memory_order_release);
    worker.join();
    EXPECT_GT(published.load(std::memory_order_relaxed), 0U);
}

TEST(
    ExposureAnalysis,
    PeriodAndRetainedResultRemainCorrectAcrossClockWrap
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureAnalysis analysis;
    app_ui::ExposureVisualizationSettings settings;
    settings.histogram = true;
    const std::uint16_t white = 0xFFFFU;
    ASSERT_TRUE(analysis.TrySubmit(&white, 1U, 1U, settings, 1U, UINT32_MAX - 99U));
    ASSERT_TRUE(analysis.ProcessPending());
    EXPECT_FALSE(analysis.ReadyToSubmit(settings, 99U));
    EXPECT_TRUE(analysis.ReadyToSubmit(settings, 100U));
    const auto *result = analysis.Latest(settings, 900U);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(analysis.Latest(settings, 901U), result);
    EXPECT_EQ(analysis.Latest(settings, 60000U), result);
}

TEST(
    ExposureVisualization,
    CountsOriginalLuminanceAndClippingWithoutChangingPixels
)
{
    const std::array<std::uint16_t, 6U> pixels = {0U, 0xFFFFU, 0x8410U, 0xF800U, 0x07E0U, 0x001FU};
    const auto original = pixels;
    const auto histogram = uai::ai::app_ui::AnalyzeLuminance(pixels.data(), pixels.size());
    EXPECT_EQ(histogram.samples, pixels.size());
    EXPECT_EQ(histogram.bins[0U], 1U);
    EXPECT_EQ(histogram.bins[255U], 1U);
    EXPECT_EQ(histogram.shadows, 1U);
    EXPECT_EQ(histogram.highlights, 1U);
    EXPECT_EQ(uai::ai::app_ui::Rgb565Luminance(0xF800U), 76U);
    EXPECT_EQ(uai::ai::app_ui::Rgb565Luminance(0x07E0U), 149U);
    EXPECT_EQ(uai::ai::app_ui::Rgb565Luminance(0x001FU), 28U);
    EXPECT_EQ(pixels, original);
}

TEST(
    ExposureVisualization,
    ThresholdsAreInclusiveAndEmptyImagesAreSafe
)
{
    uai::ai::app_ui::LuminanceHistogram histogram;
    for (const std::uint8_t luminance : {0U, 5U, 6U, 249U, 250U, 255U})
        histogram.Add(luminance);
    EXPECT_EQ(histogram.shadows, 2U);
    EXPECT_EQ(histogram.highlights, 2U);
    EXPECT_EQ(histogram.PercentTenths(histogram.shadows, histogram.samples), 333U);
    EXPECT_EQ(histogram.PercentTenths(0U, 0U), 0U);
    EXPECT_EQ(histogram.PercentTenths(384000U, 384000U), 1000U);
    EXPECT_EQ(uai::ai::app_ui::AnalyzeLuminance(nullptr, 1U).samples, 0U);
    EXPECT_EQ(uai::ai::app_ui::AnalyzeLuminance(nullptr, 0U).highlights, 0U);
}

TEST(
    ExposureVisualization,
    ZebraModesAreIndependentAndPreserveAlternatingSourcePixels
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureAnalysis analysis;
    std::vector<std::uint16_t> source(800U * 480U, 0U);
    for (std::size_t row = 0U; row < 480U; ++row)
        std::fill_n(source.begin() + row * 800U + 400U, 400U, 0xFFFFU);
    auto pixels = source;
    uai::ai::ui::Canvas canvas(pixels.data(), 800U, 480U);
    app_ui::ExposureVisualizationSettings settings;
    app_ui::ExposureImagePainter(nullptr, settings).Paint(canvas);
    EXPECT_EQ(pixels, source);
    const auto analyze = [&](std::uint32_t now) {
        EXPECT_TRUE(analysis.TrySubmit(source.data(), 800U, 480U, settings, now, now));
        EXPECT_TRUE(analysis.ProcessPending());
        return analysis.Latest(settings, now);
    };
    settings.zebra_white = true;
    app_ui::ExposureImagePainter(analyze(0U), settings).Paint(canvas);
    EXPECT_EQ(pixels[0U], source[0U]);
    EXPECT_EQ(pixels[400U], app_ui::kWhiteZebraColor);
    EXPECT_EQ(pixels[408U], source[408U]);
    pixels = source;
    settings.zebra_white = false;
    settings.zebra_black = true;
    app_ui::ExposureImagePainter(analyze(200U), settings).Paint(canvas);
    EXPECT_EQ(pixels[0U], app_ui::kBlackZebraColor);
    EXPECT_EQ(pixels[8U], source[8U]);
    EXPECT_EQ(pixels[400U], source[400U]);
    settings.zebra_white = true;
    settings.histogram = true;
    const auto *result = analyze(400U);
    ASSERT_NE(result, nullptr);
    app_ui::ExposureImagePainter image(result, settings);
    image.Paint(canvas);
    EXPECT_EQ(pixels[0U], app_ui::kBlackZebraColor);
    EXPECT_EQ(pixels[400U], app_ui::kWhiteZebraColor);
    EXPECT_EQ(result->histogram.bins[0U], app_ui::kExposureSampleCount / 2U);
    EXPECT_EQ(result->histogram.bins[255U], app_ui::kExposureSampleCount / 2U);
    EXPECT_EQ(result->histogram.samples, app_ui::kExposureSampleCount);
    EXPECT_EQ(source[0U], 0U);
    EXPECT_EQ(source[400U], 0xFFFFU);
}

TEST(
    ExposureVisualization,
    HistogramUsesRawFrameAndCanBeShownWithoutZebra
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureAnalysis analysis;
    std::vector<std::uint16_t> source(800U * 480U, 0x8410U);
    source[4U * 800U + 4U] = 0U;
    source[476U * 800U + 796U] = 0xFFFFU;
    auto pixels = source;
    uai::ai::ui::Canvas canvas(pixels.data(), 800U, 480U);
    app_ui::ExposureVisualizationSettings settings;
    settings.histogram = true;
    ASSERT_TRUE(analysis.TrySubmit(source.data(), 800U, 480U, settings, 1U, 0U));
    ASSERT_TRUE(analysis.ProcessPending());
    const auto *result = analysis.Latest(settings, 0U);
    ASSERT_NE(result, nullptr);
    app_ui::ExposureImagePainter image(result, settings);
    image.Paint(canvas);
    EXPECT_EQ(pixels, source);
    EXPECT_EQ(result->histogram.samples, app_ui::kExposureSampleCount);
    EXPECT_EQ(result->histogram.shadows, 1U);
    EXPECT_EQ(result->histogram.highlights, 1U);
    canvas.FillRect({0U, 0U, 800U, 32U}, 0U);
    app_ui::LuminanceHistogramPainter(image).Paint(canvas);
    EXPECT_EQ(result->histogram.shadows, 1U);
    EXPECT_EQ(result->histogram.highlights, 1U);
    EXPECT_EQ(pixels[429U * 800U + 22U], app_ui::kBlackZebraColor);
    EXPECT_EQ(pixels[429U * 800U + 277U], app_ui::kWhiteZebraColor);
    EXPECT_EQ(pixels[200U * 800U + 400U], source[200U * 800U + 400U]);
    EXPECT_EQ(source[4U * 800U + 4U], 0U);
    EXPECT_EQ(source[476U * 800U + 796U], 0xFFFFU);
}

TEST(
    ExposureVisualization,
    MissingOrMismatchedResultsLeaveTheCameraUntouched
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureVisualizationSettings settings;
    settings.histogram = true;
    settings.zebra_white = true;
    std::vector<std::uint16_t> pixels(800U * 480U, 0x8410U);
    const auto original = pixels;
    uai::ai::ui::Canvas canvas(pixels.data(), 800U, 480U);
    app_ui::ExposureImagePainter missing(nullptr, settings);
    missing.Paint(canvas);
    app_ui::LuminanceHistogramPainter(missing).Paint(canvas);
    app_ui::ExposureAnalysisResult result;
    result.valid = true;
    result.settings = settings;
    result.settings.white_level = 240U;
    app_ui::ExposureImagePainter mismatched(&result, settings);
    mismatched.Paint(canvas);
    app_ui::LuminanceHistogramPainter(mismatched).Paint(canvas);
    EXPECT_EQ(pixels, original);
}

TEST(
    ExposureVisualization,
    KeepsHistogramVisibleWithoutNewInputOrWhileAnalysisIsPending
)
{
    namespace app_ui = uai::ai::app_ui;
    app_ui::ExposureAnalysis analysis;
    app_ui::ExposureVisualizationSettings settings;
    settings.histogram = true;
    settings.zebra_white = true;
    const std::uint16_t white = 0xFFFFU;
    const std::uint16_t black = 0U;
    ASSERT_TRUE(analysis.TrySubmit(&white, 1U, 1U, settings, 1U, 0U));
    ASSERT_TRUE(analysis.ProcessPending());
    const auto *completed = analysis.Latest(settings, 0U);
    ASSERT_NE(completed, nullptr);
    ASSERT_EQ(analysis.Latest(settings, 5000U), completed);
    ASSERT_TRUE(analysis.TrySubmit(&black, 1U, 1U, settings, 2U, 5200U));
    for (std::uint32_t now = 5200U; now <= 7200U; now += 200U) {
        const auto *result = analysis.Latest(settings, now);
        ASSERT_EQ(result, completed);
        std::vector<std::uint16_t> pixels(800U * 480U, black);
        uai::ai::ui::Canvas canvas(pixels.data(), 800U, 480U);
        app_ui::ExposureImagePainter image(result, settings);
        image.Paint(canvas);
        app_ui::LuminanceHistogramPainter(image).Paint(canvas);
        EXPECT_EQ(pixels[0U], app_ui::kWhiteZebraColor);
        EXPECT_EQ(pixels[338U * 800U + 12U], uai::ai::ui::Rgb565(0x80U, 0x80U, 0x80U));
        EXPECT_EQ(result->capture_sequence, 1U);
    }
    ASSERT_TRUE(analysis.ProcessPending());
    const auto *result = analysis.Latest(settings, 7200U);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->capture_sequence, 2U);
    EXPECT_EQ(result->histogram.highlights, 0U);
}

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
    HistogramAndBothZebrasToggleIndependentlyAndSurviveNavigation
)
{
    using uai::ai::app_ui::WidgetId;
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    EXPECT_FALSE(ui.VisualizationSettings().Enabled());
    EXPECT_EQ(ui.VisualizationSettings().black_level, 5U);
    EXPECT_EQ(ui.VisualizationSettings().white_level, 250U);
    Tap(ui, WidgetId::kOpenMenu);
    Tap(ui, WidgetId::kOpenVisualization);
    EXPECT_EQ(ui.CurrentScreen(), uai::ai::app_ui::ScreenId::kVisualization);
    EXPECT_FALSE(ui.ShowsCamera());
    Tap(ui, WidgetId::kToggleHistogram);
    EXPECT_TRUE(ui.VisualizationSettings().histogram);
    EXPECT_FALSE(ui.VisualizationSettings().zebra_black);
    EXPECT_FALSE(ui.VisualizationSettings().zebra_white);
    Tap(ui, WidgetId::kZebraWhite);
    Tap(ui, WidgetId::kZebraBlack);
    Tap(ui, WidgetId::kToggleHistogram);
    EXPECT_FALSE(ui.VisualizationSettings().histogram);
    EXPECT_TRUE(ui.VisualizationSettings().zebra_black);
    EXPECT_TRUE(ui.VisualizationSettings().zebra_white);
    EXPECT_TRUE(ui.ShowBoxes());
    EXPECT_TRUE(ui.AiExposureEnabled());
    EXPECT_EQ(models.mask, uai::ai::task::kAllModelsMask);
    const auto pixels = PaintToPixels(ui);
    for (const auto widget : {WidgetId::kZebraBlack, WidgetId::kZebraWhite}) {
        const auto bounds = BoundsOf(widget);
        EXPECT_EQ(PixelAt(pixels, bounds.x + 6U, bounds.y + 6U), ButtonOf(widget).style.checked_fill);
    }
    Tap(ui, WidgetId::kCloseVisualization);
    EXPECT_EQ(ui.CurrentScreen(), uai::ai::app_ui::ScreenId::kMenu);
    Tap(ui, WidgetId::kOpenVisualization);
    Tap(ui, WidgetId::kVisualizationCamera);
    EXPECT_TRUE(ui.ShowsCamera());
    EXPECT_TRUE(ui.VisualizationSettings().zebra_black);
    EXPECT_TRUE(ui.VisualizationSettings().zebra_white);
}

TEST(
    AiAppUi,
    ClippingThresholdSlidersRemainSeparateAndClampToSafeRanges
)
{
    using uai::ai::app_ui::WidgetId;
    FakeModels models;
    uai::ai::app_ui::AppUi ui(models);
    Tap(ui, WidgetId::kOpenMenu);
    Tap(ui, WidgetId::kOpenVisualization);
    for (const auto widget : {WidgetId::kBlackLevel, WidgetId::kWhiteLevel}) {
        const auto bounds = BoundsOf(widget);
        const auto event = ui.HandleTouch(
            {true,
             static_cast<std::uint16_t>(bounds.x + bounds.width - 1U),
             static_cast<std::uint16_t>(bounds.y + bounds.height - 4U)}
        );
        EXPECT_EQ(event.type, uai::ai::ui::EventType::kChange);
        ui.HandleTouch({false, 0U, 0U});
    }
    EXPECT_EQ(ui.VisualizationSettings().black_level, 64U);
    EXPECT_EQ(ui.VisualizationSettings().white_level, 255U);
    ui.OnBlackLevelChange({uai::ai::ui::EventType::kChange, 0U, 0U, 0U, -1});
    ui.OnWhiteLevelChange({uai::ai::ui::EventType::kChange, 0U, 0U, 0U, 0});
    EXPECT_EQ(ui.VisualizationSettings().black_level, 0U);
    EXPECT_EQ(ui.VisualizationSettings().white_level, 191U);
    EXPECT_FALSE(ui.VisualizationSettings().Enabled());
}

TEST(
    AiAppUi,
    CameraFrameLayersKeepZebraBehindAnnotationsAndHistogramInFront
)
{
    namespace app_ui = uai::ai::app_ui;
    FakeModels models;
    app_ui::AppUi ui(models);
    Tap(ui, app_ui::WidgetId::kOpenMenu);
    Tap(ui, app_ui::WidgetId::kOpenVisualization);
    Tap(ui, app_ui::WidgetId::kToggleHistogram);
    Tap(ui, app_ui::WidgetId::kZebraWhite);
    Tap(ui, app_ui::WidgetId::kVisualizationCamera);
    std::vector<std::uint16_t> source(800U * 480U, 0xFFFFU);
    auto pixels = source;
    uai::ai::ui::Canvas canvas(pixels.data(), 800U, 480U);
    app_ui::ExposureAnalysis analysis;
    ASSERT_TRUE(analysis.TrySubmit(source.data(), 800U, 480U, ui.VisualizationSettings(), 1U, 0U));
    ASSERT_TRUE(analysis.ProcessPending());
    const auto *result = analysis.Latest(ui.VisualizationSettings(), 0U);
    ASSERT_NE(result, nullptr);
    app_ui::ExposureImagePainter image(result, ui.VisualizationSettings());
    image.Paint(canvas);
    canvas.DrawFrame({396U, 198U, 24U, 24U}, 2U, 0x07E0U);
    canvas.DrawFrame({12U, 338U, 276U, 130U}, 2U, 0x07E0U);
    app_ui::LuminanceHistogramPainter histogram(image);
    const uai::ai::ui::Painter *painters[] = {&histogram, &ui.Overlay()};
    uai::ai::ui::PainterGroup(painters, 2U).Paint(canvas);
    EXPECT_EQ(result->histogram.highlights, app_ui::kExposureSampleCount);
    EXPECT_EQ(pixels[200U * 800U + 396U], 0x07E0U);
    EXPECT_EQ(pixels[338U * 800U + 12U], uai::ai::ui::Rgb565(0x80U, 0x80U, 0x80U));
    EXPECT_EQ(pixels[5U * 800U + 700U], 0U);
    EXPECT_EQ(source.front(), 0xFFFFU);
    EXPECT_EQ(source.back(), 0xFFFFU);
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
        void OnExposureVisualizationToggle(const uai::ai::ui::Event &) { ++taps; }
        void OnBlackLevelChange(const uai::ai::ui::Event &event)
        {
            ++changes;
            last_value = event.value;
        }
        void OnWhiteLevelChange(const uai::ai::ui::Event &event)
        {
            ++changes;
            last_value = event.value;
        }
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

    for (const auto widget :
         {uai::ai::app_ui::WidgetId::kToggleHistogram,
          uai::ai::app_ui::WidgetId::kZebraBlack,
          uai::ai::app_ui::WidgetId::kZebraWhite}) {
        tap.widget_id = static_cast<std::uint16_t>(widget);
        EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, tap));
    }
    EXPECT_EQ(handlers.taps, 3);
    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kBlackLevel);
    change.value = 12;
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, change));
    EXPECT_EQ(handlers.last_value, 12);
    change.widget_id = static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kWhiteLevel);
    change.value = 240;
    EXPECT_TRUE(uai::ai::app_ui::Dispatch(handlers, change));
    EXPECT_EQ(handlers.changes, 4);
    EXPECT_EQ(handlers.last_value, 240);

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
