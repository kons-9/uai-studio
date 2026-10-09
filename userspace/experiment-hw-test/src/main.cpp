#include "commands.hpp"
#include "display_log.hpp"
#include "tests/suite.hpp"
#include "integration.hpp"
#include "camera_runtime/rx_queue.hpp"
#include "camera_runtime/driver/frame_buffer.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <tk/tkernel.h>

/* The Cube ISP algorithms call printf from camera frame callbacks. Those
 * uncoordinated diagnostics corrupt the machine-readable hwtest UART stream. */
extern "C" int printf(const char *, ...)
{
    return 0;
}

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
void tm_com_init(void);
}

#if defined(HWTEST_AUTORUN_TEST)
extern "C" {
struct alignas(32) HwtestDiagnosticLog {
    volatile std::uint32_t size;
    std::uint32_t header_padding[7];
    volatile char bytes[16U * 1024U];
};

HwtestDiagnosticLog hwtest_diagnostic_log{};
}
#endif

namespace {

alignas(8) INT task_stack[16 * 1024 / sizeof(INT)];
alignas(8) INT ui_stack[32 * 1024 / sizeof(INT)];
ID worker_events = 0, ui_events = 0, uart_mutex = 0;
std::atomic<bool> active{false}, cancelled{false};
experiment::console::RxQueue received;
char requested_test[49] = "all";
bool allow_destructive = false;
std::size_t selected_test = 0;

const char *SelectedName(std::size_t index)
{
    if (index == 0) { return "all"; }
    if (index == 1) { return "all-stress"; }
    return experiment::hwtest::tests::cases[index - 2].name;
}

const char *SelectedLabel(std::size_t index)
{
    if (index == 0) { return "ALL QUICK"; }
    if (index == 1) { return "ALL-STRESS"; }
    return experiment::hwtest::tests::cases[index - 2].name;
}

void CaptureDiagnostic(const char *text, std::size_t size)
{
#if defined(HWTEST_AUTORUN_TEST)
    std::uint32_t offset = hwtest_diagnostic_log.size;
    const std::size_t capacity = sizeof(hwtest_diagnostic_log.bytes);
    for (std::size_t index = 0; index < size && offset < capacity; ++index, ++offset) {
        hwtest_diagnostic_log.bytes[offset] = text[index];
    }
    hwtest_diagnostic_log.size = offset;
#else
    (void)text;
    (void)size;
#endif
}

void FlushDiagnostic()
{
#if defined(HWTEST_AUTORUN_TEST)
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(&hwtest_diagnostic_log),
        static_cast<std::int32_t>(sizeof(hwtest_diagnostic_log))
    );
    __DSB();
#endif
}

void Write(
    void *,
    const char *text,
    std::size_t size
)
{
    if (uart_mutex > 0) {
        tk_loc_mtx(uart_mutex, TMO_FEVR);
    }
    CaptureDiagnostic(text, size);
    for (std::size_t index = 0; index < size; ++index) {
        tm_putchar(static_cast<unsigned char>(text[index]));
    }
    if (uart_mutex > 0) {
        tk_unl_mtx(uart_mutex);
    }
}

const experiment::console::Writer output{nullptr, Write};

void Trace(const char *line)
{
    Write(nullptr, line, std::strlen(line));
    Write(nullptr, "\n", 1);
    experiment::hwtest::display_log::Write(line);
    experiment::hwtest::display_log::Write("\n");
}

void OwnerWrite(void *, const char *text, std::size_t size)
{
    Write(nullptr, text, size);
    experiment::hwtest::display_log::Write(text);
}

experiment::console::Status Submit(void *, int count, const char *const *arguments,
                                    const experiment::console::Writer &writer)
{
    using namespace experiment;
    if (count == 2 && std::strcmp(arguments[1], "list") == 0) {
        hwtest::Registry registry{hwtest::tests::cases, hwtest::tests::case_count,
            {HAL_GetTick, [](std::uint32_t delay) { tk_dly_tsk(delay); }, Trace}};
        return hwtest::Execute(&registry, count, arguments, writer);
    }
    if (count == 2 && std::strcmp(arguments[1], "stop") == 0) {
        if (!active.load()) { return console::Status::kInvalidState; }
        cancelled.store(true);
        return console::Status::kOk;
    }
    const bool all = count == 2 && (std::strcmp(arguments[1], "all") == 0
                                    || std::strcmp(arguments[1], "all-stress") == 0);
    const bool single = (count == 3 || count == 4) && std::strcmp(arguments[1], "run") == 0
        && hwtest::ValidName(arguments[2]);
    if ((!all && !single) || (count == 4 && std::strcmp(arguments[3], "allow-destructive") != 0)) {
        return console::Status::kInvalidArgument;
    }
    if (active.exchange(true)) { return console::Status::kInvalidState; }
    cancelled.store(false);
    std::snprintf(requested_test, sizeof(requested_test), "%s", all ? arguments[1] : arguments[2]);
    allow_destructive = count == 4;
    tk_set_flg(worker_events, 1);
    return console::Status::kOk;
}

experiment::console::Status Control(void *context, int count, const char *const *arguments,
                                    const experiment::console::Writer &writer)
{
    const bool read_only = count == 2 && (std::strcmp(arguments[1], "stat") == 0 || std::strcmp(arguments[1], "wb-list") == 0);
    if (active.load() && !read_only) { return experiment::console::Status::kInvalidState; }
    return experiment::hwtest::integrated::Control(context, count, arguments, writer);
}

experiment::console::Status Capture(void *context, int count, const char *const *arguments,
                                    const experiment::console::Writer &writer)
{
    if (active.load()) { return experiment::console::Status::kInvalidState; }
    return experiment::hwtest::integrated::Capture(context, count, arguments, writer);
}

experiment::console::Status Frames(void *, int count, const char *const *, const experiment::console::Writer &writer)
{
    if (count != 1) { return experiment::console::Status::kInvalidArgument; }
    const auto state = experiment::hwtest::integrated::Observe();
    char line[128];
    std::snprintf(line, sizeof(line), "frames: pipe1=%lu pipe2=%lu errors=%lu recoveries=%lu running=%u\n",
        static_cast<unsigned long>(state.pipe1), static_cast<unsigned long>(state.pipe2),
        static_cast<unsigned long>(state.errors), static_cast<unsigned long>(state.recoveries), unsigned(state.running));
    writer.Write(line);
    return experiment::console::Status::kOk;
}

void TestTask(
    INT,
    void *
)
{
    experiment::hwtest::Registry registry{
        experiment::hwtest::tests::cases,
        experiment::hwtest::tests::case_count,
        {HAL_GetTick, [](std::uint32_t delay) { tk_dly_tsk(delay); }, Trace,
         [] { return cancelled.load(); },
         [](const char *name, unsigned current, unsigned total) {
             experiment::hwtest::display_log::Progress(name, current, total);
         }}
    };
    for (;;) {
        UINT pattern = 0;
        tk_wai_flg(worker_events, 1, TWF_ORW | TWF_BITCLR, &pattern, TMO_FEVR);
        const auto expected_total = experiment::hwtest::CountSelected(
            experiment::hwtest::tests::cases, experiment::hwtest::tests::case_count, requested_test);
        experiment::hwtest::display_log::Begin(expected_total);
        if (std::strcmp(requested_test, "all") == 0 || std::strcmp(requested_test, "all-stress") == 0) {
            const char *arguments[] = {"hwtest", requested_test};
            experiment::hwtest::Execute(&registry, 2, arguments, output);
        } else {
            const char *arguments[] = {"hwtest", "run", requested_test, "allow-destructive"};
            experiment::hwtest::Execute(&registry, allow_destructive ? 4 : 3, arguments, output);
        }
        FlushDiagnostic();
        active.store(false);
    }
}

void UiTask(INT, void *)
{
    using namespace experiment;
    hwtest::integrated::Initialize({nullptr, OwnerWrite});
    const console::Command commands[] = {
        {"hwtest", "hwtest list|all|all-stress|run <name> [allow-destructive]|stop", Submit, nullptr},
        {"cam", "cam stat|ae|ev|manual|area|wb|wb-list", Control, nullptr},
        {"capture", "capture start|stop|recover|fps|flip|crop", Capture, nullptr},
        {"frames", "frames", Frames, nullptr}
    };
    console::Shell shell(commands, sizeof(commands) / sizeof(commands[0]), output);
    HAL_NVIC_SetPriority(USART1_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    SET_BIT(USART1->CR1, USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE);
    SET_BIT(USART1->CR3, USART_CR3_EIE);
    output.Write("HWTEST READY\n> ");
#if defined(HWTEST_AUTORUN_TEST)
    const char *group_arguments[] = {"hwtest", HWTEST_AUTORUN_TEST};
    const char *test_arguments[] = {"hwtest", "run", HWTEST_AUTORUN_TEST};
    if (std::strcmp(HWTEST_AUTORUN_TEST, "all") == 0
        || std::strcmp(HWTEST_AUTORUN_TEST, "all-stress") == 0) {
        Submit(nullptr, 2, group_arguments, output);
    } else {
        Submit(nullptr, 3, test_arguments, output);
    }
#endif
    for (;;) {
        hwtest::integrated::Service();
        const auto observation = hwtest::integrated::Observe();
        const bool display_was_ready = hwtest::display_log::Ready();
        const auto action = hwtest::display_log::Process(
            reinterpret_cast<const std::uint16_t *>(uai::camera_pipe2::driver::MainPipeFrameBuffer()),
            reinterpret_cast<const std::uint16_t *>(uai::camera_pipe2::driver::AncillaryPipeFrameBuffer()),
            hwtest::integrated::Touch(), observation.pipe1, observation.pipe2,
            selected_test, hwtest::tests::case_count + 2);
        if (display_was_ready && !hwtest::display_log::Ready()) {
            hwtest::integrated::DisplayFailure();
            output.Write("display: processing failed\n");
        }
        if (action.kind == hwtest::display_log::ActionKind::kSelect) {
            selected_test = action.selection;
        } else if (action.kind == hwtest::display_log::ActionKind::kRun) {
            const char *arguments[] = {"hwtest", "run", SelectedName(selected_test)};
            const char *group_arguments[] = {"hwtest", SelectedName(selected_test)};
            const auto status = selected_test < 2 ? Submit(nullptr, 2, group_arguments, output)
                                                  : Submit(nullptr, 3, arguments, output);
            if (status != console::Status::kOk) { OwnerWrite(nullptr, "HWTEST BUSY\n", 12); }
        } else if (action.kind == hwtest::display_log::ActionKind::kStop) {
            cancelled.store(true);
        }
        hwtest::display_log::Selection(SelectedLabel(selected_test));
        for (unsigned budget = 0; budget < 128; ++budget) {
            char character = 0;
            bool error = false;
            if (!received.Pop(character, error)) { break; }
            shell.Feed(character, error);
        }
        UINT pattern = 0;
        tk_wai_flg(ui_events, 1, TWF_ORW | TWF_BITCLR, &pattern, 10);
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
            if ((flags & USART_ISR_RXNE_RXFNE) != 0) { (void)USART1->RDR; }
            continue;
        }
        if ((flags & USART_ISR_RXNE_RXFNE) == 0) { break; }
        received.Push(static_cast<char>(USART1->RDR & 0xffU));
    }
    tk_set_flg(ui_events, 1);
}

extern "C" void experiment_frame_wake(void)
{
    if (ui_events > 0) { tk_set_flg(ui_events, 1); }
}

extern "C" INT usermain(void)
{
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) {
        return E_SYS;
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

    T_CFLG flags{};
    flags.flgatr = TA_TFIFO;
    worker_events = tk_cre_flg(&flags);
    ui_events = tk_cre_flg(&flags);
    T_CMTX mutex{};
    mutex.mtxatr = TA_INHERIT;
    uart_mutex = tk_cre_mtx(&mutex);
    if (worker_events <= 0 || ui_events <= 0 || uart_mutex <= 0) { return E_SYS; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    T_CTSK task{};
    task.tskatr = TA_HLNG | TA_USERBUF;
    task.task = reinterpret_cast<FP>(TestTask);
    task.itskpri = 11;
    task.stksz = sizeof(task_stack);
    task.bufptr = task_stack;
    const ID identifier = tk_cre_tsk(&task);
    if (identifier < E_OK || tk_sta_tsk(identifier, 0) != E_OK) {
        output.Write("hwtest: task creation failed\n");
        return E_SYS;
    }
    task.task = reinterpret_cast<FP>(UiTask);
    task.itskpri = 10;
    task.stksz = sizeof(ui_stack);
    task.bufptr = ui_stack;
    const ID ui_identifier = tk_cre_tsk(&task);
    if (ui_identifier <= 0 || tk_sta_tsk(ui_identifier, 0) != E_OK) { return E_SYS; }
    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
