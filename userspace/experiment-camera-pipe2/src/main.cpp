#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#include "driver/camera_driver.hpp"
#include "driver/display_driver.hpp"
#include "driver/frame_buffer.hpp"

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
#if PIPE2_BUFFER_PSRAM
#include "stm32n6570_discovery_xspi.h"
#endif

extern volatile unsigned int camera_pipe2_pipe1_vsync_count;
extern volatile unsigned int camera_pipe2_pipe2_frame_count;
void tm_com_init(void);
}

#if PIPE2_PIPE_DUAL
constexpr const char *kPipeMode = "dual-400x480";
#else
constexpr const char *kPipeMode = "single-800x480";
#endif
#if PIPE2_IMX335_MIPI891
constexpr const char *kSensorProfile = "mipi891-bt900";
#else
constexpr const char *kSensorProfile = "stock-bt1600";
#endif
#if PIPE2_BUFFER_PSRAM
constexpr const char *kBufferMode = "fixed-psram";
#else
constexpr const char *kBufferMode = "sram";
#endif
#if PIPE2_CROP_NATIVE
constexpr const char *kCropMode = "native-800x480";
#elif PIPE2_CROP_INTEGER4
constexpr const char *kCropMode = "centered-integer-4x";
#else
constexpr const char *kCropMode = "downsize";
#endif

namespace {

constexpr SZ kCameraTaskStackSize = 32 * 1024;
alignas(8) INT g_camera_task_stack[kCameraTaskStackSize / sizeof(INT)];

void FillRgb565(std::uint8_t *buffer, std::uint16_t color)
{
    for (std::size_t offset = 0U;
         offset < uai::camera_pipe2::driver::kFrameBytes; offset += 2U) {
        buffer[offset] = static_cast<std::uint8_t>(color & 0xffU);
        buffer[offset + 1U] = static_cast<std::uint8_t>(color >> 8U);
    }
}

void InitializePipe2Uart()
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

void halt_with_message(const char *message)
{
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(message)));
    for (;;) {
        tk_dly_tsk(1000);
    }
}

void CameraPipe2Task(INT, void *)
{
#if PIPE2_BUFFER_PSRAM
    if (BSP_XSPI_RAM_Init(0U) != BSP_ERROR_NONE ||
        BSP_XSPI_RAM_EnableMemoryMappedMode(0U) != BSP_ERROR_NONE) {
        halt_with_message("camera_pipe2: PSRAM initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: PSRAM memory mapped\n")));
#endif
    tm_printf(reinterpret_cast<const UB *>(
                  "camera_pipe2: mode=%s sensor=%s buffers=%s crop=%s test_pattern=%d\n"),
              kPipeMode, kSensorProfile, kBufferMode, kCropMode,
              PIPE2_IMX335_TEST_PATTERN_MODE);

    uai::camera_pipe2::driver::DisplayDriver display;
    uai::camera_pipe2::driver::CameraDriver camera;

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: display init begin\n")));
    if (!uai::camera_pipe2::driver::IsOk(display.Initialize())) {
        halt_with_message("camera_pipe2: display initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: display init done\n")));

    /* Make both LTDC layers visible while the camera is being configured. */
    FillRgb565(uai::camera_pipe2::driver::MainPipeFrameBuffer(), 0xf800U);
#if PIPE2_PIPE_DUAL
    FillRgb565(uai::camera_pipe2::driver::AncillaryPipeFrameBuffer(), 0x001fU);
#endif

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: camera init begin\n")));
    if (!uai::camera_pipe2::driver::IsOk(camera.Initialize())) {
        halt_with_message("camera_pipe2: camera initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: camera init done\n")));

    if (!uai::camera_pipe2::driver::IsOk(camera.Start())) {
        halt_with_message("camera_pipe2: camera start failed\n");
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: preview started\n")));

    std::uint32_t last_report_tick = HAL_GetTick();
    for (;;) {
        if (!uai::camera_pipe2::driver::IsOk(camera.Process())) {
            halt_with_message("camera_pipe2: camera background process failed\n");
        }
        if (!uai::camera_pipe2::driver::IsOk(display.Process())) {
            halt_with_message("camera_pipe2: display process failed\n");
        }
        const std::uint32_t now = HAL_GetTick();
        if (now - last_report_tick >= 1000U) {
            last_report_tick = now;
            tm_printf(reinterpret_cast<const UB *>(
                          "camera_pipe2: pipe1_vsync=%u pipe2_frame=%u\n"),
                      camera_pipe2_pipe1_vsync_count,
                      camera_pipe2_pipe2_frame_count);
        }
        tk_dly_tsk(1);
    }
}

} // namespace

extern "C" INT usermain(void)
{
    InitializePipe2Uart();
    T_CTSK camera_task = {};
    camera_task.tskatr = TA_HLNG | TA_USERBUF;
    camera_task.task = reinterpret_cast<FP>(CameraPipe2Task);
    camera_task.itskpri = 10;
    camera_task.stksz = kCameraTaskStackSize;
    camera_task.bufptr = g_camera_task_stack;
    const ID camera_task_id = tk_cre_tsk(&camera_task);
    if (camera_task_id < E_OK) {
        halt_with_message("camera_pipe2: camera task creation failed\n");
    }
    if (tk_sta_tsk(camera_task_id, 0) != E_OK) {
        halt_with_message("camera_pipe2: camera task start failed\n");
    }
    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}

/* Override the kernel's weak BusFault handler only in this pipe2 executable so
 * the stacked fault PC is available over UART during camera bring-up. */
extern "C" [[gnu::noreturn]] void uai_busfault_dump(
    const std::uint32_t *exception_stack, std::uint32_t exception_return)
{
    /* Cortex-M55 keeps the basic R0-R3, R12, LR, PC, xPSR frame first;
     * any extended FP context follows it at higher addresses. */
    const std::uint32_t *core_frame = exception_stack;
    tm_printf(reinterpret_cast<const UB *>(
                  "BusFault: CFSR=%08x BFARVALID=%u BFAR=%08x EXC_RETURN=%08x frame=%08x PC=%08x LR=%08x R0=%08x R1=%08x R2=%08x R3=%08x XPSR=%08x\n"),
              SCB->CFSR,
              static_cast<std::uint32_t>((SCB->CFSR & (1U << 15U)) != 0U),
              SCB->BFAR,
              static_cast<std::uint32_t>(
                  exception_return),
              static_cast<std::uint32_t>(
                  reinterpret_cast<std::uintptr_t>(core_frame)),
              core_frame[6], core_frame[5], core_frame[0], core_frame[1],
              core_frame[2], core_frame[3], core_frame[7]);
    for (std::uint32_t offset = 0U; offset < 40U; offset += 4U) {
        tm_printf(reinterpret_cast<const UB *>(
                      "BusFault stack[%02u]=%08x %08x %08x %08x\n"),
                  offset, exception_stack[offset], exception_stack[offset + 1U],
                  exception_stack[offset + 2U], exception_stack[offset + 3U]);
    }
    for (;;) {
    }
}

extern "C" __attribute__((naked, noreturn)) void knl_busfault_handler(void)
{
    __asm volatile("mov r1, lr\n"
                   "tst r1, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b uai_busfault_dump\n");
}
