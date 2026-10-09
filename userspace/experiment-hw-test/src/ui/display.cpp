#include "display_log.hpp"
#include "ui/log_buffer.hpp"
#include "ui/ui_layout.hpp"

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

std::uint16_t *Page(unsigned index)
{
    return index == 0 ? experiment_hwtest_display_framebuffer : experiment_hwtest_display_backbuffer;
}

std::uint16_t Id(layout::WidgetId id)
{
    return static_cast<std::uint16_t>(id);
}

struct Handlers {
    Action action = Action::kNone;
    void OnPrevious(const ui::Event &) { action = Action::kPrevious; }
    void OnNext(const ui::Event &) { action = Action::kNext; }
    void OnRun(const ui::Event &) { action = Action::kRun; }
    void OnStop(const ui::Event &) { action = Action::kStop; }
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

void Begin()
{
    tk_loc_mtx(log_mutex, TMO_FEVR);
    logs.Begin();
    tk_unl_mtx(log_mutex);
}

void Selection(const char *name)
{
    std::snprintf(selection, sizeof(selection), "%s", name);
}

void Target(unsigned index) { target = index; }

Action Process(const std::uint16_t *pipe1, const std::uint16_t *pipe2,
               const ui::TouchPoint &touch, std::uint32_t first, std::uint32_t second)
{
    if (!ready) {
        return Action::kNone;
    }
    Handlers handlers;
    if (compare) {
        if (touch_was_active && !touch.active) {
            compare = false;
            screen.Update({});
        }
    } else {
        layout::Dispatch(handlers, screen.Update(touch));
    }
    touch_was_active = touch.active;
    if (pending) {
        if ((LTDC->SRCR & LTDC_SRCR_VBR) != 0) {
            if (HAL_GetTick() - pending_begin >= 1000) {
                ready = false;
            }
            return handlers.action;
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
        char summary[40]{}, status[49]{};
        tk_loc_mtx(log_mutex, TMO_FEVR);
        std::snprintf(summary, sizeof(summary), "PASS %u FAIL %u", logs.Passed(), logs.Failed());
        std::snprintf(status, sizeof(status), "%s", logs.Status());
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
        for (std::size_t index = 0; index < LogBuffer::kVisible; ++index) {
            canvas.DrawText(8, 120 + index * 16, lines[index].text, 1,
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
        return handlers.action;
    }
    pending = true;
    pending_begin = HAL_GetTick();
    return handlers.action;
}

}