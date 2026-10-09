#include "isp_camera.hpp"
#include "bsp_device.hpp"
#include "extension.hpp"
#include "rx_queue.hpp"
#include "driver/display_driver.hpp"
#include "driver/frame_buffer.hpp"
#include "middleware/ui/canvas.hpp"

#include <tk/tkernel.h>

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
void tm_com_init(void);
extern volatile unsigned int camera_pipe2_pipe1_vsync_count;
extern volatile unsigned int camera_pipe2_pipe2_frame_count;
volatile unsigned int experiment_camera_failures = 0;
}

namespace {

alignas(8) INT task_stack[32 * 1024 / sizeof(INT)];
experiment::console::RxQueue received;
ID events = 0;
std::uint32_t loops = 0;
std::uint64_t busy_cycles = 0;

void Write(
    void *,
    const char *text,
    std::size_t size
)
{
    for (std::size_t index = 0; index < size; ++index) {
        tm_putchar(static_cast<unsigned char>(text[index]));
    }
}

const experiment::console::Writer output{nullptr, Write};

void DrawScenarioAction(const char *action)
{
    if (action == nullptr) {
        return;
    }

    char label[40];
    std::snprintf(label, sizeof(label), "GPU TEST: %s", action);
    constexpr std::uint16_t x = 8;
    constexpr std::uint16_t y = 8;
    constexpr std::uint16_t padding = 8;
    constexpr std::uint8_t scale = 2;
    const auto text_width = uai::ai::ui::TextWidth(label, scale);
    const auto box_width = static_cast<std::uint16_t>(text_width + padding * 2U);
    constexpr std::uint16_t box_height = 30;

    auto *frame = reinterpret_cast<std::uint16_t *>(hlcd_ltdc.LayerCfg[0].FBStartAdress);
    uai::ai::ui::Canvas canvas(
        frame,
        static_cast<std::uint16_t>(uai::camera_pipe2::driver::kDisplayWidth),
        static_cast<std::uint16_t>(uai::camera_pipe2::driver::kFrameHeight)
    );
    canvas.FillRect({x, y, box_width, box_height}, uai::ai::ui::Rgb565(0, 104, 232));
    canvas.DrawText(x + padding, y + 7U, label, scale, uai::ai::ui::Rgb565(255, 255, 255));

    const auto address = reinterpret_cast<std::uintptr_t>(frame);
    const auto pitch = uai::camera_pipe2::driver::kDisplayWidth * sizeof(std::uint16_t);
    const auto first = address + static_cast<std::uintptr_t>(y) * pitch + x * sizeof(std::uint16_t);
    const auto last = address + static_cast<std::uintptr_t>(y + box_height - 1U) * pitch
        + static_cast<std::uintptr_t>(x + box_width) * sizeof(std::uint16_t);
    constexpr std::uintptr_t cache_line = 32U;
    const auto aligned_first = first & ~(cache_line - 1U);
    const auto aligned_last = (last + cache_line - 1U) & ~(cache_line - 1U);
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(aligned_first), static_cast<std::int32_t>(aligned_last - aligned_first)
    );
}

struct ConsoleCamera {
    experiment::camera::Runtime *runtime;
    bool locked = false;
};

experiment::console::Status CameraControl(
    void *context,
    int count,
    const char *const *arguments,
    const experiment::console::Writer &writer
)
{
    auto &camera = *static_cast<ConsoleCamera *>(context);
    const bool read_only = count == 2 && (!std::strcmp(arguments[1], "stat") || !std::strcmp(arguments[1], "wb-list"));
    if (camera.locked && !read_only) {
        return experiment::console::Status::kInvalidState;
    }
    return experiment::camera::Runtime::ControlCommand(camera.runtime, count, arguments, writer);
}

experiment::console::Status CameraCapture(
    void *context,
    int count,
    const char *const *arguments,
    const experiment::console::Writer &writer
)
{
    auto &camera = *static_cast<ConsoleCamera *>(context);
    if (camera.locked) {
        return experiment::console::Status::kInvalidState;
    }
    return experiment::camera::Runtime::CaptureCommand(camera.runtime, count, arguments, writer);
}

[[noreturn]] void Halt(const char *message)
{
    output.Write(message);
    for (;;) {
        tk_dly_tsk(1000);
    }
}

experiment::console::Status Uptime(
    void *,
    int count,
    const char *const *,
    const experiment::console::Writer &writer
)
{
    if (count != 1) {
        return experiment::console::Status::kInvalidArgument;
    }
    char text[128];
    std::snprintf(
        text,
        sizeof(text),
        "uptime_ms=%lu loops=%lu busy_cycles=%llu core_hz=%lu\n",
        static_cast<unsigned long>(HAL_GetTick()),
        static_cast<unsigned long>(loops),
        static_cast<unsigned long long>(busy_cycles),
        static_cast<unsigned long>(SystemCoreClock)
    );
    writer.Write(text);
    return experiment::console::Status::kOk;
}

experiment::console::Status Frames(
    void *,
    int count,
    const char *const *,
    const experiment::console::Writer &writer
)
{
    if (count != 1) {
        return experiment::console::Status::kInvalidArgument;
    }
    char text[96];
    std::snprintf(
        text,
        sizeof(text),
        "frames: pipe1_vsync=%u pipe2_frame=%u\n",
        camera_pipe2_pipe1_vsync_count,
        camera_pipe2_pipe2_frame_count
    );
    writer.Write(text);
    return experiment::console::Status::kOk;
}

void CameraTask(
    INT,
    void *
)
{
    uai::camera_pipe2::driver::DisplayDriver display;
    experiment::camera::BspDevice device;
    experiment::camera::IspCamera control;
    experiment::camera::Runtime camera(device, control);
    ConsoleCamera console_camera{&camera};
    T_CFLG flags{};
    flags.flgatr = TA_TFIFO;
    events = tk_cre_flg(&flags);
    if (events < E_OK || !uai::camera_pipe2::driver::IsOk(display.Initialize())
        || camera.Start() != experiment::console::Status::kOk) {
        Halt("camera: initialization failed\n");
    }
    experiment::console::Command commands[16] = {
        {"uptime", "uptime", Uptime, nullptr},
        {"frames", "frames", Frames, nullptr},
        {"cam",
         "cam stat | ae on|off | ev <-4..4> | manual <us> <mdB> | area <x> <y> <w> <h> | wb auto|<kelvin> | wb-list",
         CameraControl,
         &console_camera},
        {"capture",
         "capture start|stop|recover|fps <10|15|20|25|30>|flip <h 0|1> <v 0|1>|crop <x> <y> <w> <h>",
         CameraCapture,
         &console_camera}
    };
    const experiment::Services services{
        &camera,
        output,
        HAL_GetTick,
        [](std::uint32_t delay) {
            tk_dly_tsk(delay);
        },
        &console_camera.locked
    };
    const auto extra = experiment::Register(services, commands + 4, 12);
    if (extra > 12) {
        Halt("extension: command capacity exceeded\n");
    }
    experiment::console::Shell shell(commands, 4 + extra, output);
    HAL_NVIC_SetPriority(USART1_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    SET_BIT(USART1->CR1, USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE);
    SET_BIT(USART1->CR3, USART_CR3_EIE);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    output.Write("camera: pipe1=started pipe2=started\n> ");
    std::uint32_t last_frame = camera_pipe2_pipe2_frame_count;
    std::uint32_t last_progress = HAL_GetTick();
    bool last_running = camera.Running();
    for (;;) {
        const auto begin = DWT->CYCCNT;
        const auto now = HAL_GetTick();
        const bool running = camera.Running();
        if (camera_pipe2_pipe2_frame_count != last_frame || !running || running != last_running) {
            last_frame = camera_pipe2_pipe2_frame_count;
            last_progress = now;
        }
        last_running = running;
        if (camera.Poll() != experiment::console::Status::kOk || (camera.Running() && now - last_progress >= 2000)) {
            ++experiment_camera_failures;
            output.Write(
                camera.Recover() == experiment::console::Status::kOk
                    ? "camera: recovered\n"
                    : "camera: recovery failed; capture recover to retry\n"
            );
            last_progress = HAL_GetTick();
        }
        if (!uai::camera_pipe2::driver::IsOk(display.Process())) {
            ++experiment_camera_failures;
            output.Write("display: processing failed\n");
        }
        experiment::Tick(now, last_frame);
        DrawScenarioAction(experiment::CurrentScenarioAction());
        for (unsigned budget = 0; budget < 128; ++budget) {
            char character = 0;
            bool receive_error = false;
            if (!received.Pop(character, receive_error)) {
                break;
            }
            shell.Feed(character, receive_error);
            if (character == '\r' || character == '\n') {
                break;
            }
        }
        busy_cycles += static_cast<std::uint32_t>(DWT->CYCCNT - begin);
        ++loops;
        if (received.Empty()) {
            UINT pattern = 0;
            tk_wai_flg(events, 1, TWF_ORW | TWF_BITCLR, &pattern, 10);
        }
    }
}

}

extern "C" void USART1_IRQHandler(void)
{
    for (unsigned budget = 0; budget < 64; ++budget) {
        const auto flags = USART1->ISR;
        if ((flags & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0) {
            USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
            received.Error();
            if ((flags & USART_ISR_RXNE_RXFNE) != 0) {
                static_cast<void>(USART1->RDR);
            }
            continue;
        }
        if ((flags & USART_ISR_RXNE_RXFNE) == 0) {
            break;
        }
        received.Push(static_cast<char>(USART1->RDR & 0xffU));
    }
    if (events > 0) {
        tk_set_flg(events, 1);
    }
}

extern "C" void experiment_frame_wake(void)
{
    if (events > 0) {
        tk_set_flg(events, 1);
    }
}

extern "C" INT usermain(void)
{
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) {
        for (;;) {
            tk_dly_tsk(1000);
        }
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
    tm_com_init();
    T_CTSK task{};
    task.tskatr = TA_HLNG | TA_USERBUF;
    task.task = reinterpret_cast<FP>(CameraTask);
    task.itskpri = 10;
    task.stksz = sizeof(task_stack);
    task.bufptr = task_stack;
    const ID identifier = tk_cre_tsk(&task);
    if (identifier < E_OK || tk_sta_tsk(identifier, 0) != E_OK) {
        Halt("camera: task creation failed\n");
    }
    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
