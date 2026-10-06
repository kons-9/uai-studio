#include "middleware/ui/canvas.hpp"
#include "middleware/ui/widget.hpp"
#include "ui/ui_layout.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

/* Guards the generated header: it must compile against ui::ButtonSpec and
 * describe buttons that lie on the screen and respond to taps. */
TEST(AiAppUiLayout, GeneratedButtonsFitScreenAndRespond)
{
    static_assert(uai::ai::app_ui::kButtonCount >= 1U);
    static_assert(static_cast<std::uint16_t>(
                      uai::ai::app_ui::WidgetId::kToggleBoxes) ==
                  uai::ai::app_ui::kButtons[0].id);

    for (std::size_t i = 0U; i < uai::ai::app_ui::kButtonCount; ++i) {
        const uai::ai::ui::Rect &bounds = uai::ai::app_ui::kButtons[i].bounds;
        EXPECT_LE(bounds.x + bounds.width, uai::ai::app_ui::kScreenWidth);
        EXPECT_LE(bounds.y + bounds.height, uai::ai::app_ui::kScreenHeight);
        EXPECT_GT(bounds.width, 0U);
        EXPECT_GT(bounds.height, 0U);
    }

    uai::ai::ui::ButtonPanel panel(uai::ai::app_ui::kButtons,
                                   uai::ai::app_ui::kButtonCount);
    const uai::ai::ui::Rect &first = uai::ai::app_ui::kButtons[0].bounds;
    const std::uint16_t cx = static_cast<std::uint16_t>(first.x + first.width / 2U);
    const std::uint16_t cy = static_cast<std::uint16_t>(first.y + first.height / 2U);
    EXPECT_EQ(panel.Update({true, cx, cy}).type, uai::ai::ui::EventType::kPress);
    const uai::ai::ui::Event tap = panel.Update({false, 0U, 0U});
    EXPECT_EQ(tap.type, uai::ai::ui::EventType::kTap);
    EXPECT_EQ(tap.widget_id,
              static_cast<std::uint16_t>(uai::ai::app_ui::WidgetId::kToggleBoxes));

    std::vector<std::uint16_t> pixels(
        static_cast<std::size_t>(uai::ai::app_ui::kScreenWidth) *
            uai::ai::app_ui::kScreenHeight, 0U);
    uai::ai::ui::Canvas canvas(pixels.data(), uai::ai::app_ui::kScreenWidth,
                               uai::ai::app_ui::kScreenHeight);
    panel.Paint(canvas);
    EXPECT_EQ(pixels[static_cast<std::size_t>(first.y) *
                         uai::ai::app_ui::kScreenWidth + first.x],
              uai::ai::app_ui::kButtons[0].style.border);
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

    uai::ai::ui::Event unknown = tap;
    unknown.widget_id = 0xFFFFU;
    EXPECT_FALSE(uai::ai::app_ui::Dispatch(handlers, unknown));
    EXPECT_EQ(handlers.tap_count, 1);
}

} // namespace
