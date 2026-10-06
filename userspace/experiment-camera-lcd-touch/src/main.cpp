#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#include "driver/camera_driver.hpp"
#include "driver/display_driver.hpp"
#include "driver/frame_buffer.hpp"
#include "driver/overlay_ui.hpp"
#include "driver/touch_driver.hpp"

/* T-Monitor and HAL headers expose C APIs to this C++ translation unit. */
extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
void tm_com_init(void);
}

namespace {

constexpr SZ kCameraTouchTaskStackSize = 32 * 1024;
alignas(8) INT g_camera_touch_task_stack[kCameraTouchTaskStackSize / sizeof(INT)]
    __attribute__((section(".camera_task_stack"), used));

void InitializeCameraTouchUart()
{
    RCC_PeriphCLKInitTypeDef peripheral_clock{};
    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    peripheral_clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK) {
        return;
    }

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* RAM execution skips the FSBL USART1 MSP setup. */
    tm_com_init();
}

[[noreturn]] void HaltWithMessage(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

void CameraTouchTask(INT, void *)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_lcd_touch: initializing LCD, GT911, and IMX335 camera\n")));

    uai::camera_lcd_touch::driver::DisplayDriver display;
    uai::camera_lcd_touch::driver::CameraDriver camera;
    uai::camera_lcd_touch::driver::TouchDriver touch;

    /* Give the LCD a deterministic black camera plane until the first frame. */
    for (std::size_t offset = 0U;
         offset < uai::camera_lcd_touch::driver::kFrameBytes; offset += 2U) {
        uai::camera_lcd_touch::driver::FrameBuffer()[offset] = 0U;
        uai::camera_lcd_touch::driver::FrameBuffer()[offset + 1U] = 0U;
    }
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(
            uai::camera_lcd_touch::driver::FrameBuffer()),
        static_cast<std::int32_t>(uai::camera_lcd_touch::driver::kFrameBytes));

    if (!uai::camera_lcd_touch::driver::IsOk(display.Initialize())) {
        HaltWithMessage("camera_lcd_touch: display initialization failed\n");
    }

    uai::camera_lcd_touch::driver::TouchInitDiagnostics touch_diagnostics{};
    if (!uai::camera_lcd_touch::driver::IsOk(
            touch.Initialize(&touch_diagnostics))) {
        tm_printf(reinterpret_cast<const UB *>(
                      "touch: init bus=%ld id=%ld/%08lx ctrl=%ld\n"),
                  static_cast<long>(touch_diagnostics.bus_status),
                  static_cast<long>(touch_diagnostics.id_status),
                  static_cast<unsigned long>(touch_diagnostics.id),
                  static_cast<long>(touch_diagnostics.controller_status));
        HaltWithMessage("camera_lcd_touch: GT911 initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_lcd_touch: GT911 polling ready\n")));

    if (!uai::camera_lcd_touch::driver::IsOk(camera.Initialize())) {
        HaltWithMessage("camera_lcd_touch: camera initialization failed\n");
    }

    if (!uai::camera_lcd_touch::driver::IsOk(camera.Start())) {
        HaltWithMessage("camera_lcd_touch: camera start failed\n");
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_lcd_touch: pipe1=started preview=started\n")));

    bool touch_active = false;
    for (;;) {
        uai::camera_lcd_touch::driver::TouchSample sample{};
        if (!uai::camera_lcd_touch::driver::IsOk(touch.Read(&sample))) {
            HaltWithMessage("camera_lcd_touch: touch read failed\n");
        }
        if (sample.active && !touch_active) {
            const auto action = display.HandleTouchPress(sample.x, sample.y);
            if (action != uai::camera_lcd_touch::driver::TouchAction::kNone) {
                tm_printf(reinterpret_cast<const UB *>(
                              "touch: action=%s x=%u y=%u\n"),
                          uai::camera_lcd_touch::driver::TouchActionName(action),
                          static_cast<UINT>(sample.x),
                          static_cast<UINT>(sample.y));
            }
        } else if (!sample.active && touch_active) {
            display.HandleTouchRelease();
        }
        touch_active = sample.active;

        if (!uai::camera_lcd_touch::driver::IsOk(camera.Process())) {
            HaltWithMessage("camera_lcd_touch: camera background process failed\n");
        }
        if (!uai::camera_lcd_touch::driver::IsOk(display.Process())) {
            HaltWithMessage("camera_lcd_touch: display process failed\n");
        }
        tk_dly_tsk(5);
    }
}

} // namespace

/* The initial kernel task has a 1 KiB stack; keep it limited to task setup. */
extern "C" INT usermain(void)
{
    InitializeCameraTouchUart();

    T_CTSK camera_touch_task{};
    camera_touch_task.tskatr = TA_HLNG | TA_USERBUF;
    camera_touch_task.task = reinterpret_cast<FP>(CameraTouchTask);
    camera_touch_task.itskpri = 10;
    camera_touch_task.stksz = kCameraTouchTaskStackSize;
    camera_touch_task.bufptr = g_camera_touch_task_stack;

    const ID task_id = tk_cre_tsk(&camera_touch_task);
    if (task_id < E_OK) {
        HaltWithMessage("camera_lcd_touch: task creation failed\n");
    }
    if (tk_sta_tsk(task_id, 0) != E_OK) {
        HaltWithMessage("camera_lcd_touch: task start failed\n");
    }

    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
