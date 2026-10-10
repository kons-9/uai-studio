#include "tests/commands.hpp"
#include "ui/display.hpp"
#include "tests/suite.hpp"
#include "tests/integration.hpp"
#include "driver/console_driver/console_driver.hpp"
#include "driver/board/time.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "tests/camera/buffers.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <tk/tkernel.h>

/* The Cube ISP algorithms call printf from camera frame callbacks. Those
 * uncoordinated diagnostics corrupt the machine-readable hwtest UART stream. */
extern "C" int printf(
    const char *,
    ...
)
{
    return 0;
}

extern "C" {
volatile UW uai_systick_count;
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
ID worker_events = 0, ui_events = 0;
std::atomic<bool> active{false}, cancelled{false};
char requested_test[49] = "all";
bool allow_destructive = false;
std::size_t selected_test = 0;

const char *SelectedName(std::size_t index)
{
    if (index == 0) {
        return "all";
    }
    if (index == 1) {
        return "all-stress";
    }
    return uai::hwtest::tests::cases[index - 2].name;
}

const char *SelectedLabel(std::size_t index)
{
    if (index == 0) {
        return "ALL QUICK";
    }
    if (index == 1) {
        return "ALL-STRESS";
    }
    return uai::hwtest::tests::cases[index - 2].name;
}

void CaptureDiagnostic(
    const char *text,
    std::size_t size
)
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
    (void)uai::ai::cache::CacheDriver::Clean(&hwtest_diagnostic_log, sizeof(hwtest_diagnostic_log));
#endif
}

void Write(
    void *,
    const char *text,
    std::size_t size
)
{
    uai::ai::console::ConsoleManagement::Accessor uart;
    if (!uai::ai::console::ConsoleManagement::Instance().Acquire(&uart).Ok())
        return;
    CaptureDiagnostic(text, size);
    (void)uart->Write(text, size, uart.Ownership());
}

const uai::hwtest::console::Writer output{nullptr, Write};

void Trace(const char *line)
{
    Write(nullptr, line, std::strlen(line));
    Write(nullptr, "\n", 1);
    uai::hwtest::display_log::Write(line);
    uai::hwtest::display_log::Write("\n");
}

void OwnerWrite(
    void *,
    const char *text,
    std::size_t size
)
{
    Write(nullptr, text, size);
    uai::hwtest::display_log::Write(text);
}

uai::hwtest::console::Status Submit(
    void *,
    int count,
    const char *const *arguments,
    const uai::hwtest::console::Writer &writer
)
{
    using namespace uai::hwtest;
    if (count == 2 && std::strcmp(arguments[1], "list") == 0) {
        uai::hwtest::Registry registry{
            uai::hwtest::tests::cases,
            uai::hwtest::tests::case_count,
            {uai::ai::driver::board::Milliseconds,
             [](std::uint32_t delay) {
                 tk_dly_tsk(delay);
             },
             Trace}
        };
        return uai::hwtest::Execute(&registry, count, arguments, writer);
    }
    if (count == 2 && std::strcmp(arguments[1], "stop") == 0) {
        if (!active.load()) {
            return console::Status::kInvalidState;
        }
        cancelled.store(true);
        return console::Status::kOk;
    }
    const bool all =
        count == 2 && (std::strcmp(arguments[1], "all") == 0 || std::strcmp(arguments[1], "all-stress") == 0);
    const bool single =
        (count == 3 || count == 4) && std::strcmp(arguments[1], "run") == 0 && uai::hwtest::ValidName(arguments[2]);
    if ((!all && !single) || (count == 4 && std::strcmp(arguments[3], "allow-destructive") != 0)) {
        return console::Status::kInvalidArgument;
    }
    if (active.exchange(true)) {
        return console::Status::kInvalidState;
    }
    cancelled.store(false);
    std::snprintf(requested_test, sizeof(requested_test), "%s", all ? arguments[1] : arguments[2]);
    allow_destructive = count == 4;
    tk_set_flg(worker_events, 1);
    return console::Status::kOk;
}

uai::hwtest::console::Status Control(
    void *context,
    int count,
    const char *const *arguments,
    const uai::hwtest::console::Writer &writer
)
{
    const bool read_only =
        count == 2 && (std::strcmp(arguments[1], "stat") == 0 || std::strcmp(arguments[1], "wb-list") == 0);
    if (active.load() && !read_only) {
        return uai::hwtest::console::Status::kInvalidState;
    }
    return uai::hwtest::integrated::Control(context, count, arguments, writer);
}

uai::hwtest::console::Status Capture(
    void *context,
    int count,
    const char *const *arguments,
    const uai::hwtest::console::Writer &writer
)
{
    if (active.load()) {
        return uai::hwtest::console::Status::kInvalidState;
    }
    return uai::hwtest::integrated::Capture(context, count, arguments, writer);
}

uai::hwtest::console::Status Frames(
    void *,
    int count,
    const char *const *,
    const uai::hwtest::console::Writer &writer
)
{
    if (count != 1) {
        return uai::hwtest::console::Status::kInvalidArgument;
    }
    const auto state = uai::hwtest::integrated::Observe();
    char line[128];
    std::snprintf(
        line,
        sizeof(line),
        "frames: pipe1=%lu pipe2=%lu errors=%lu recoveries=%lu running=%u\n",
        static_cast<unsigned long>(state.pipe1),
        static_cast<unsigned long>(state.pipe2),
        static_cast<unsigned long>(state.errors),
        static_cast<unsigned long>(state.recoveries),
        unsigned(state.running)
    );
    writer.Write(line);
    return uai::hwtest::console::Status::kOk;
}

void TestTask(
    INT,
    void *
)
{
    uai::hwtest::Registry registry{
        uai::hwtest::tests::cases,
        uai::hwtest::tests::case_count,
        {uai::ai::driver::board::Milliseconds,
         [](std::uint32_t delay) {
             tk_dly_tsk(delay);
         },
         Trace,
         [] {
             return cancelled.load();
         },
         [](const char *name, unsigned current, unsigned total) {
             uai::hwtest::display_log::Progress(name, current, total);
         }}
    };
    for (;;) {
        UINT pattern = 0;
        tk_wai_flg(worker_events, 1, TWF_ORW | TWF_BITCLR, &pattern, TMO_FEVR);
        const auto expected_total =
            uai::hwtest::CountSelected(uai::hwtest::tests::cases, uai::hwtest::tests::case_count, requested_test);
        uai::hwtest::display_log::Begin(expected_total);
        if (std::strcmp(requested_test, "all") == 0 || std::strcmp(requested_test, "all-stress") == 0) {
            const char *arguments[] = {"hwtest", requested_test};
            uai::hwtest::Execute(&registry, 2, arguments, output);
        } else {
            const char *arguments[] = {"hwtest", "run", requested_test, "allow-destructive"};
            uai::hwtest::Execute(&registry, allow_destructive ? 4 : 3, arguments, output);
        }
        FlushDiagnostic();
        active.store(false);
    }
}

void UiTask(
    INT,
    void *
)
{
    using namespace uai::hwtest;
    uai::hwtest::integrated::Initialize({nullptr, OwnerWrite});
    const console::Command commands[] = {
        {"hwtest", "hwtest list|all|all-stress|run <name> [allow-destructive]|stop", Submit, nullptr},
        {"cam", "cam stat|ae|ev|manual|area|wb|wb-list", Control, nullptr},
        {"capture", "capture start|stop|recover|fps|flip|crop", Capture, nullptr},
        {"frames", "frames", Frames, nullptr}
    };
    console::Shell shell(commands, sizeof(commands) / sizeof(commands[0]), output);
    output.Write("HWTEST READY\n> ");
#if defined(HWTEST_AUTORUN_TEST)
    const char *group_arguments[] = {"hwtest", HWTEST_AUTORUN_TEST};
    const char *test_arguments[] = {"hwtest", "run", HWTEST_AUTORUN_TEST};
    if (std::strcmp(HWTEST_AUTORUN_TEST, "all") == 0 || std::strcmp(HWTEST_AUTORUN_TEST, "all-stress") == 0) {
        Submit(nullptr, 2, group_arguments, output);
    } else {
        Submit(nullptr, 3, test_arguments, output);
    }
#endif
    for (;;) {
        uai::hwtest::integrated::Service();
        const auto observation = uai::hwtest::integrated::Observe();
        const bool display_was_ready = uai::hwtest::display_log::Ready();
        const auto action = uai::hwtest::display_log::Process(
            reinterpret_cast<const std::uint16_t *>(uai::hwtest::camera::MainPipeFrameBuffer()),
            reinterpret_cast<const std::uint16_t *>(uai::hwtest::camera::AncillaryPipeFrameBuffer()),
            uai::hwtest::integrated::Touch(),
            observation.pipe1,
            observation.pipe2,
            selected_test,
            uai::hwtest::tests::case_count + 2
        );
        if (display_was_ready && !uai::hwtest::display_log::Ready()) {
            uai::hwtest::integrated::DisplayFailure();
            output.Write("display: processing failed\n");
        }
        if (action.kind == uai::hwtest::display_log::ActionKind::kSelect) {
            selected_test = action.selection;
        } else if (action.kind == uai::hwtest::display_log::ActionKind::kRun) {
            const char *arguments[] = {"hwtest", "run", SelectedName(selected_test)};
            const char *group_arguments[] = {"hwtest", SelectedName(selected_test)};
            const auto status =
                selected_test < 2 ? Submit(nullptr, 2, group_arguments, output) : Submit(nullptr, 3, arguments, output);
            if (status != console::Status::kOk) {
                OwnerWrite(nullptr, "HWTEST BUSY\n", 12);
            }
        } else if (action.kind == uai::hwtest::display_log::ActionKind::kStop) {
            cancelled.store(true);
        }
        uai::hwtest::display_log::Selection(SelectedLabel(selected_test));
        for (unsigned budget = 0; budget < 128; ++budget) {
            uai::ai::console::Input input;
            if (!uai::ai::console::ConsoleManagement::Instance().Read(&input).Ok()) {
                break;
            }
            shell.Feed(input.value, input.error);
        }
        UINT pattern = 0;
        tk_wai_flg(ui_events, 1, TWF_ORW | TWF_BITCLR, &pattern, 10);
    }
}

}

extern "C" INT usermain(void)
{
    T_CFLG flags{};
    flags.flgatr = TA_TFIFO;
    worker_events = tk_cre_flg(&flags);
    ui_events = tk_cre_flg(&flags);
    if (worker_events <= 0 || ui_events <= 0) {
        return E_SYS;
    }
    const auto uart = uai::ai::console::ConsoleManagement::Instance().Initialize({nullptr, [](void *) {
                                                                                      if (ui_events > 0)
                                                                                          tk_set_flg(ui_events, 1);
                                                                                  }});
    if (!uart.Ok() && uart.Code() != uai::ai::common::ErrorCode::kAlreadyInitialized)
        return E_SYS;
    uai::ai::driver::board::EnableCycleCounter();

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
    if (ui_identifier <= 0 || tk_sta_tsk(ui_identifier, 0) != E_OK) {
        return E_SYS;
    }
    for (;;) {
        tk_slp_tsk(TMO_FEVR);
    }
}
