#pragma once

#include <tk/tkernel.h>

#include "middleware/buffer/stable_aligned_bytes.hpp"
#include "task/task_config.hpp"
#include "ui/exposure_analysis.hpp"

namespace uai::ai::middleware::cpu_task_monitor {
class CpuTaskMonitor;
}

namespace uai::ai::task {

class ExposureAnalysisTask final {
public:
    static ExposureAnalysisTask &Instance()
    {
        static ExposureAnalysisTask task;
        return task;
    }

    static void Entry();
    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);
    bool ReadyToSubmit(
        const app_ui::ExposureVisualizationSettings &settings,
        std::uint32_t now
    ) const
    {
        return analysis_.ReadyToSubmit(settings, now);
    }
    bool TrySubmit(
        const std::uint16_t *source,
        std::uint16_t width,
        std::uint16_t height,
        app_ui::ExposureVisualizationSettings settings,
        std::uint32_t capture_sequence,
        std::uint32_t now
    )
    {
        if (!analysis_.TrySubmit(source, width, height, settings, capture_sequence, now))
            return false;
        (void)tk_set_flg(request_, 1U);
        return true;
    }
    const app_ui::ExposureAnalysisResult *Latest(
        const app_ui::ExposureVisualizationSettings &settings,
        std::uint32_t now
    )
    {
        return analysis_.Latest(settings, now);
    }

private:
    void Run();
    app_ui::ExposureAnalysis analysis_;
    ID request_ = 0;
    middleware::cpu_task_monitor::CpuTaskMonitor *monitor_ = nullptr;
    common::StableAlignedBytes<kExposureAnalysisTaskStackSize> stack_;
};

} // namespace uai::ai::task