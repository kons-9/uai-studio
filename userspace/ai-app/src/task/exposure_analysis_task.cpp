#include "task/exposure_analysis_task.hpp"

#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"
#include "middleware/task/task.hpp"

namespace uai::ai::task {

void ExposureAnalysisTask::Entry()
{
    Instance().Run();
}

void ExposureAnalysisTask::Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    T_CFLG configuration{};
    configuration.flgatr = TA_TFIFO;
    request_ = tk_cre_flg(&configuration);
    if (request_ <= 0)
        common::Task::Halt("ai: exposure analysis event create failed\n");
    monitor_ = &monitor;
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_, 15, "exposure_analysis");
}

void ExposureAnalysisTask::Run()
{
    common::Task::RunForever(
        *monitor_,
        "exposure_analysis",
        [&] {
            UINT reasons = 0U;
            if (tk_wai_flg(request_, 1U, TWF_ORW | TWF_BITCLR, &reasons, TMO_FEVR) != E_OK)
                common::Task::Halt("ai: exposure analysis wait failed\n");
        },
        [&] {
            (void)analysis_.ProcessPending();
        }
    );
}

} // namespace uai::ai::task