#include "shell/task.hpp"

#include "driver/console_driver/console_driver.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"
#include "middleware/trace_format/ai_model_trace.hpp"
#include "middleware/trace_format/cpu_task_trace.hpp"
#include "middleware/task/task.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "shell/commands.hpp"
#include "shell/trace_stream.hpp"
#include "task/task_context.hpp"

namespace uai::ai::shell {

void ShellTask::Wake(void *context)
{
    const ID flag = *static_cast<ID *>(context);
    (void)tk_set_flg(flag, 1U);
}

void ShellTask::Entry()
{
    auto &root = task::GetTaskContext();
    root.shell_task.Run(root.shell_mailbox, root.shell_ready, root.cpu_task_monitor);
}

void ShellTask::Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor, ID &wake_flag)
{
    const auto status = console::ConsoleManagement::Instance().Initialize({&wake_flag, Wake});
    if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized) {
        status.LogStatus("shell: uart");
        return;
    }
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_, 7, "shell");
}

void ShellTask::Run(Mailbox &mailbox, ID wake_flag, middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    Output output{nullptr, [](void *, const char *data, std::size_t size) {
        (void)console::ConsoleManagement::Instance().Write(data, size);
    }};
    Engine engine(output, true);
    Context commands{engine, mailbox};
    commands.now = [] { return common::Task::Now(); };
    commands.wait_ms = [](std::uint32_t milliseconds) { tk_dly_tsk(static_cast<RELTIM>(milliseconds)); };
    commands.list_tasks = [](const Output &out) {
        for (ID task_id = 1; task_id <= 32; ++task_id) {
            T_RTSK status{};
            if (tk_ref_tsk(task_id, &status) == E_OK)
                out.Printf("task id=%ld state=%u priority=%ld\r\n", static_cast<long>(task_id),
                    static_cast<unsigned int>(status.tskstat), static_cast<long>(status.tskpri));
        }
    };
    commands.memory_usage = [](const Output &out) {
        out.Printf("memory reserved capture=%lu x %lu display=%lu x %lu inference=%lu x %lu bytes\r\n",
            static_cast<unsigned long>(memory_manager::kCaptureBufferCount),
            static_cast<unsigned long>(memory_manager::kCaptureBufferBytes),
            static_cast<unsigned long>(memory_manager::kDisplayBufferCount),
            static_cast<unsigned long>(memory_manager::kCaptureBufferBytes),
            static_cast<unsigned long>(memory_manager::kInferenceBufferCount),
            static_cast<unsigned long>(memory_manager::kInferenceBufferBytes));
        out.Write("memory: buffers are statically reserved; live heap usage unavailable\r\n");
    };
    commands.transfer_trace = [](const Output &out, bool cpu) {
        auto &root = task::GetTaskContext();
        const auto status = cpu ? root.cpu_task_monitor.PauseTrace() : root.pipeline_task.PauseAiTrace();
        if (!status.Ok()) {
            out.Printf("error: trace pause code=%ld\r\n", static_cast<long>(status.Code()));
            return;
        }
        const auto key = cpu ? static_memory_layout::Key::kCpuTaskMonitor
                             : static_memory_layout::Key::kThreadMonitor;
        const auto region = static_memory_layout::Region::GetRegionFromKey(key);
        bool valid = region.begin != nullptr;
        unsigned version = 0U;
        if (valid && cpu && region.size() >= sizeof(middleware::cpu_task_monitor::CpuTaskMonitorTraceHeader)) {
            const auto *header = reinterpret_cast<const middleware::cpu_task_monitor::CpuTaskMonitorTraceHeader *>(region.begin);
            valid = header->magic == middleware::cpu_task_monitor::kCpuTaskMonitorTraceMagic
                && header->version == middleware::cpu_task_monitor::kCpuTaskMonitorTraceVersion;
            version = header->version;
        } else if (valid && !cpu && region.size() >= sizeof(middleware::ai_model_monitor::ThreadMonitorTraceHeader)) {
            const auto *header = reinterpret_cast<const middleware::ai_model_monitor::ThreadMonitorTraceHeader *>(region.begin);
            valid = header->magic == middleware::ai_model_monitor::kThreadMonitorTraceMagic
                && header->version == middleware::ai_model_monitor::kThreadMonitorTraceVersion;
            version = header->version;
        } else {
            valid = false;
        }
        if (valid)
            StreamTrace(out, cpu ? "cpu" : "ai", version, region.begin, region.size(), [] { (void)tk_dly_tsk(1); });
        else
            out.Write("error: trace buffer unavailable\r\n");
        if (cpu)
            root.cpu_task_monitor.ResumeTrace();
        else
            root.pipeline_task.ResumeAiTrace();
    };
    if (!RegisterAll(engine, commands))
        common::Task::Halt("shell: register failed\n");
    output.Write("shell ready; type help\r\n");
    common::Task::RunForever(monitor, "shell", [wake_flag] {
        UINT pattern = 0U;
        (void)tk_wai_flg(wake_flag, 1U, TWF_ANDW | TWF_BITCLR, &pattern, 100);
    }, [&] {
        for (;;) {
            console::Input input{};
            const auto status = console::ConsoleManagement::Instance().Read(&input);
            if (status.Code() == common::ErrorCode::kNoFrame)
                break;
            if (!status.Ok()) {
                engine.Feed(0, true);
                break;
            }
            engine.Feed(input.value, input.error);
        }
    });
}

} // namespace uai::ai::shell