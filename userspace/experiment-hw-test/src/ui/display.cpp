#include "display_log.hpp"
#include "ui/log_buffer.hpp"
#include "ui/ui_layout.hpp"
#include "tests/suite.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <tk/tkernel.h>

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
extern LTDC_HandleTypeDef hlcd_ltdc;
alignas(32) std::uint16_t experiment_hwtest_display_framebuffer[800U * 480U]
    __attribute__((section(".display_frame_0")));
alignas(32) std::uint16_t experiment_hwtest_display_backbuffer[800U * 480U]
    __attribute__((section(".display_frame_1")));
}

namespace experiment::hwtest::display_log {
namespace {
namespace ui = uai::ai::ui;
namespace layout = uai::ai::hwtest_layout;
ui::Screen screen(layout::kScreens[0]);
LogBuffer logs;
ID log_mutex = 0;
bool ready = false, pending = false, compare = false, touch_was_active = false;
unsigned front = 0, selected_pipe = 0, target = 5;
std::uint32_t pending_begin = 0;
char selection[49] = "ALL";
bool list_touch = false, list_moved = false, log_touch = false;
std::uint16_t list_origin_y = 0, list_last_y = 0;
int list_drag = 0, log_drag = 0;
std::size_t first_choice = 0;
constexpr int kChoiceLeft = 8, kChoiceRight = 320, kChoiceTop = 136;
constexpr int kChoiceRowHeight = 16;
constexpr std::size_t kChoiceVisible = 17;
constexpr int kLogLeft = 336, kLogRight = 792, kLogTop = 132, kLogBottom = 417, kLogRowHeight = 15;

const char *ChoiceName(std::size_t index)
{
    if (index == 0) { return "ALL QUICK"; }
    if (index == 1) { return "ALL-STRESS"; }
    return experiment::hwtest::tests::cases[index - 2].name;
}

void KeepChoiceVisible(std::size_t selected, std::size_t count)
{
    if (selected < first_choice) {
        first_choice = selected;
    } else if (selected >= first_choice + kChoiceVisible) {
        first_choice = selected - kChoiceVisible + 1;
    }
    const auto limit = count > kChoiceVisible ? count - kChoiceVisible : 0;
    if (first_choice > limit) {
        first_choice = limit;
    }
}

std::uint16_t *Page(unsigned index)
{
    return index == 0 ? experiment_hwtest_display_framebuffer : experiment_hwtest_display_backbuffer;
}

std::uint16_t Id(layout::WidgetId id)
{
    return static_cast<std::uint16_t>(id);
}

struct Handlers {
    ActionKind action = ActionKind::kNone;
    void OnRun(const ui::Event &) { action = ActionKind::kRun; }
    void OnStop(const ui::Event &) { action = ActionKind::kStop; }
    void OnLogUp(const ui::Event &)
    {
        tk_loc_mtx(log_mutex, TMO_FEVR);
        logs.Scroll(true);
        tk_unl_mtx(log_mutex);
    }
    void OnLogDown(const ui::Event &)
    {
        tk_loc_mtx(log_mutex, TMO_FEVR);
        logs.Scroll(false);
        tk_unl_mtx(log_mutex);
    }
    void OnPipe1(const ui::Event &) { selected_pipe = 0; }
    void OnPipe2(const ui::Event &) { selected_pipe = 1; }
    void OnCompare(const ui::Event &) { compare = true; }
};

void CopyCamera(std::uint16_t *destination, unsigned x, const std::uint16_t *source)
{
    if (!source) {
        return;
    }
    SCB_InvalidateDCache_by_Addr(const_cast<std::uint16_t *>(source), 400 * 480 * 2);
    __DSB();
    for (unsigned row = 0; row < 480; ++row) {
        std::memcpy(destination + row * 800 + x, source + row * 400, 400 * sizeof(std::uint16_t));
    }
}
}

bool Initialize()
{
    __HAL_RCC_RIFSC_CLK_ENABLE();
    RIMC_MasterConfig_t master{};
    master.MasterCID = RIF_CID_1;
    master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2, &master);
    for (const auto peripheral : {RIF_RISC_PERIPH_INDEX_LTDC, RIF_RISC_PERIPH_INDEX_LTDCL1,
                                  RIF_RISC_PERIPH_INDEX_LTDCL2, RIF_RCC_PERIPH_INDEX_CACHEAXIRAM,
                                  RIF_RCC_PERIPH_INDEX_CACHECONFIG, RIF_RCC_PERIPH_INDEX_AXISRAM1,
                                  RIF_RCC_PERIPH_INDEX_AXISRAM2, RIF_RCC_PERIPH_INDEX_FLEXRAM}) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(peripheral, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    }
    T_CMTX mutex{};
    mutex.mtxatr = TA_INHERIT;
    log_mutex = tk_cre_mtx(&mutex);
    if (log_mutex <= 0 || BSP_LCD_Init(0U, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return false;
    }
    std::fill(Page(0), Page(0) + 800 * 480, 0);
    std::fill(Page(1), Page(1) + 800 * 480, 0);
    SCB_CleanDCache_by_Addr(Page(0), 800 * 480 * 2);
    SCB_CleanDCache_by_Addr(Page(1), 800 * 480 * 2);
    BSP_LCD_LayerConfig_t layer{};
    layer.Address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(Page(0)));
    layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    layer.X1 = 800;
    layer.Y1 = 480;
    if (BSP_LCD_ConfigLayer(0, 0, &layer) != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(0, 1, DISABLE) != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(0, 0, ENABLE) != BSP_ERROR_NONE
        || BSP_LCD_DisplayOn(0) != BSP_ERROR_NONE) {
        return false;
    }
    ready = true;
    return true;
}

bool Ready() { return ready; }
bool SelfTest() { return ready && IsFramebuffer(FramebufferAddress()); }
std::uintptr_t FramebufferAddress() { return hlcd_ltdc.LayerCfg[0].FBStartAdress; }
bool IsFramebuffer(std::uintptr_t address)
{
    return address == reinterpret_cast<std::uintptr_t>(Page(0)) || address == reinterpret_cast<std::uintptr_t>(Page(1));
}

void Write(const char *text)
{
    if (!ready) {
        return;
    }
    tk_loc_mtx(log_mutex, TMO_FEVR);
    logs.Write(text);
    tk_unl_mtx(log_mutex);
}

void Begin(unsigned expected_total)
{
    tk_loc_mtx(log_mutex, TMO_FEVR);
    logs.Begin(expected_total);
    tk_unl_mtx(log_mutex);
}

void Selection(const char *name)
{
    std::snprintf(selection, sizeof(selection), "%s", name);
}

void Progress(const char *name, unsigned current, unsigned total)
{
    tk_loc_mtx(log_mutex, TMO_FEVR);
    logs.Progress(name, current, total);
    tk_unl_mtx(log_mutex);
}

void Target(unsigned index) { target = index; }

Action Process(const std::uint16_t *pipe1, const std::uint16_t *pipe2,
               const ui::TouchPoint &touch, std::uint32_t first, std::uint32_t second,
               std::size_t selected, std::size_t choice_count)
{
    if (!ready) {
        return {};
    }
    KeepChoiceVisible(selected, choice_count);
    Handlers handlers;
    const bool was_active = touch_was_active;
    if (compare) {
        if (touch_was_active && !touch.active) {
            compare = false;
            screen.Update({});
        }
    } else {
        layout::Dispatch(handlers, screen.Update(touch));
    }

    Action action{handlers.action, selected};
    if (!compare && action.kind == ActionKind::kNone) {
        if (touch.active) {
            if (!was_active) {
                list_touch = touch.x >= kChoiceLeft && touch.x < kChoiceRight
                    && touch.y >= kChoiceTop
                    && touch.y < kChoiceTop + kChoiceRowHeight * kChoiceVisible;
                log_touch = touch.x >= kLogLeft && touch.x < kLogRight
                    && touch.y >= kLogTop && touch.y < kLogBottom;
                list_moved = false;
                list_drag = 0;
                log_drag = 0;
                list_origin_y = list_last_y = touch.y;
            } else if (list_touch) {
                list_drag += static_cast<int>(list_last_y) - static_cast<int>(touch.y);
                list_last_y = touch.y;
                const int steps = list_drag / kChoiceRowHeight;
                if (steps != 0) {
                    list_drag -= steps * kChoiceRowHeight;
                    const auto candidate = static_cast<std::size_t>(std::clamp(
                        static_cast<int>(selected) + steps, 0, static_cast<int>(choice_count - 1)));
                    if (candidate != selected) {
                        list_moved = true;
                        KeepChoiceVisible(candidate, choice_count);
                        action = {ActionKind::kSelect, candidate};
                    }
                }
            } else if (log_touch) {
                log_drag += static_cast<int>(list_last_y) - static_cast<int>(touch.y);
                list_last_y = touch.y;
                while (log_drag >= kLogRowHeight) {
                    tk_loc_mtx(log_mutex, TMO_FEVR);
                    logs.Scroll(true);
                    tk_unl_mtx(log_mutex);
                    log_drag -= kLogRowHeight;
                }
                while (log_drag <= -kLogRowHeight) {
                    tk_loc_mtx(log_mutex, TMO_FEVR);
                    logs.Scroll(false);
                    tk_unl_mtx(log_mutex);
                    log_drag += kLogRowHeight;
                }
            }
        } else if (was_active && (list_touch || log_touch)) {
            if (list_touch) {
                if (!list_moved && list_origin_y >= kChoiceTop) {
                    const auto row = static_cast<std::size_t>((list_origin_y - kChoiceTop) / kChoiceRowHeight);
                    const auto candidate = first_choice + row;
                    if (candidate < choice_count) {
                        action = {ActionKind::kSelect, candidate};
                    }
                }
            }
            list_touch = false;
            log_touch = false;
        }
    }
    touch_was_active = touch.active;
    if (pending) {
        if ((LTDC->SRCR & LTDC_SRCR_VBR) != 0) {
            if (HAL_GetTick() - pending_begin >= 1000) {
                ready = false;
            }
            return action;
        }
        front = 1 - front;
        pending = false;
    }
    auto *back = Page(1 - front);
    std::fill(back, back + 800 * 480, ui::Rgb565(16, 20, 20));
    CopyCamera(back, 400, selected_pipe == 0 || compare ? pipe1 : pipe2);
    ui::Canvas canvas(back, 800, 480);
    if (compare) {
        CopyCamera(back, 0, pipe1);
        CopyCamera(back, 400, pipe2);
        canvas.FillRect({0, 0, 800, 24}, 0);
    } else {
        LogBuffer::Line lines[LogBuffer::kVisible];
        char summary[40]{}, status[49]{}, progress_name[49]{};
        unsigned progress_current = 0, progress_total = 0;
        tk_loc_mtx(log_mutex, TMO_FEVR);
        std::snprintf(summary, sizeof(summary), "P%u F%u T%u", logs.Passed(), logs.Failed(), logs.Total());
        std::snprintf(status, sizeof(status), "%s", logs.Status());
        std::snprintf(progress_name, sizeof(progress_name), "%s", logs.ProgressName());
        progress_current = logs.ProgressCurrent();
        progress_total = logs.ProgressTotal();
        for (std::size_t index = 0; index < LogBuffer::kVisible; ++index) {
            lines[index] = logs.Visible(index);
        }
        tk_unl_mtx(log_mutex);
        screen.Labels().SetText(Id(layout::WidgetId::kSummary), summary);
        screen.Labels().SetText(Id(layout::WidgetId::kStatus), status);
        screen.Labels().SetText(Id(layout::WidgetId::kSelection), selection);
        screen.Buttons().SetChecked(Id(layout::WidgetId::kPipe1), selected_pipe == 0);
        screen.Buttons().SetChecked(Id(layout::WidgetId::kPipe2), selected_pipe == 1);
        screen.Paint(canvas);

        canvas.FillRect({8, 112, 312, 304}, ui::Rgb565(16, 20, 20));
        char test_count[48];
        std::snprintf(test_count, sizeof(test_count), "TESTS %u - SWIPE OR TAP",
                      static_cast<unsigned>(experiment::hwtest::tests::case_count));
        canvas.DrawText(12, 118, test_count, 1, ui::Rgb565(80, 220, 152));
        for (std::size_t row = 0; row < kChoiceVisible; ++row) {
            const auto index = first_choice + row;
            if (index >= choice_count) { break; }
            const int y = kChoiceTop + static_cast<int>(row) * kChoiceRowHeight;
            if (index == selected) {
                canvas.FillRect({8, static_cast<std::uint16_t>(y - 2), 304,
                                 static_cast<std::uint16_t>(kChoiceRowHeight)}, ui::Rgb565(24, 92, 60));
            }
            char choice[64];
            std::snprintf(choice, sizeof(choice), "%s", ChoiceName(index));
            if (index >= 2 && progress_total != 0
                && std::strcmp(progress_name, experiment::hwtest::tests::cases[index - 2].name) == 0) {
                std::snprintf(choice, sizeof(choice), "%s %u/%u", progress_name, progress_current, progress_total);
            }
            canvas.DrawText(16, y, choice, 1,
                index == selected ? 0xffff : ui::Rgb565(208, 220, 220));
        }
        canvas.FillRect({316, kChoiceTop, 4, kChoiceRowHeight * static_cast<int>(kChoiceVisible)},
                        ui::Rgb565(48, 56, 56));
        const auto choice_limit = choice_count > kChoiceVisible ? choice_count - kChoiceVisible : 0;
        const int thumb_height = choice_count > kChoiceVisible
            ? (kChoiceRowHeight * static_cast<int>(kChoiceVisible) * static_cast<int>(kChoiceVisible))
                / static_cast<int>(choice_count)
            : kChoiceRowHeight * static_cast<int>(kChoiceVisible);
        const int thumb_travel = kChoiceRowHeight * static_cast<int>(kChoiceVisible) - thumb_height;
        const int thumb_y = choice_limit == 0 ? kChoiceTop
            : kChoiceTop + thumb_travel * static_cast<int>(first_choice) / static_cast<int>(choice_limit);
        canvas.FillRect({316, static_cast<std::uint16_t>(thumb_y), 4,
                         static_cast<std::uint16_t>(thumb_height)}, ui::Rgb565(80, 220, 152));
        canvas.DrawText(336, 118, "RESULTS", 1, ui::Rgb565(80, 220, 152));
        for (std::size_t index = 0; index < LogBuffer::kVisible; ++index) {
            canvas.DrawText(336, 132 + index * 15, lines[index].text, 1,
                lines[index].failed ? 0xf980 : lines[index].passed ? 0x56d3 : 0xffff);
        }
    }
    static std::uint32_t fps_tick = 0, fps_first = 0, fps_second = 0;
    static unsigned first_fps = 0, second_fps = 0;
    const auto now = HAL_GetTick();
    const auto elapsed = now - fps_tick;
    if (elapsed >= 1000) {
        first_fps = static_cast<unsigned>(std::uint64_t(first - fps_first) * 1000 / elapsed);
        second_fps = static_cast<unsigned>(std::uint64_t(second - fps_second) * 1000 / elapsed);
        fps_first = first;
        fps_second = second;
        fps_tick = now;
    }
    char frames[64];
    std::snprintf(frames, sizeof(frames), "P1 %uFPS %lu P2 %uFPS %lu", first_fps, static_cast<unsigned long>(first), second_fps, static_cast<unsigned long>(second));
    canvas.FillRect({400, 0, 400, 24}, 0);
    canvas.DrawText(408, 6, frames, 1, 0xffff);
    if (target < 5) {
        constexpr ui::Rect targets[] = {{420, 40, 48, 48}, {732, 40, 48, 48}, {420, 412, 48, 48},
                                      {732, 412, 48, 48}, {576, 216, 48, 48}};
        canvas.FillRect(targets[target], 0xffe0);
    }
    SCB_CleanDCache_by_Addr(back, 800 * 480 * 2);
    __DSB();
    if (HAL_LTDC_SetAddress_NoReload(&hlcd_ltdc, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(back)), LTDC_LAYER_1) != HAL_OK
        || HAL_LTDC_Reload(&hlcd_ltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) {
        ready = false;
        return action;
    }
    pending = true;
    pending_begin = HAL_GetTick();
    return action;
}

}
