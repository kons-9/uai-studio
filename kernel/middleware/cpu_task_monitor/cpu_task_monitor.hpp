#ifndef UAI_AI2_MIDDLEWARE_CPU_TASK_MONITOR_HPP
#define UAI_AI2_MIDDLEWARE_CPU_TASK_MONITOR_HPP

#include <cstddef>
#include <cstdint>

#include <tk/dbgspt.h>
#include <tk/tkernel.h>

#include "middleware/foundation/error.hpp"
#include "middleware/trace_format/cpu_task_trace.hpp"

#ifndef UAI_CPU_TASK_MONITOR
#define UAI_CPU_TASK_MONITOR 0
#endif

namespace uai::ai::middleware::cpu_task_monitor {

#if UAI_CPU_TASK_MONITOR

/*
 * CPU execution monitor backed by the µT-Kernel/DS dispatch and HLL
 * interrupt hooks.  Hook callbacks only update fixed-size counters; reporting
 * is performed by a low-priority task so the scheduler and interrupt paths do
 * not call T-Monitor or any other blocking API.
 */
class CpuTaskMonitor final {
public:
    common::Error Start();
    common::Error Stop();
    common::Error RegisterTask(ID task_id, const char *name);
    static common::Error RegisterTaskForActiveMonitor(
        ID task_id, const char *name);
    std::uint32_t BeginTaskLoop() const;
    void RecordTaskLoop(ID task_id, std::uint32_t start_cycles);
    /* Must be called after external PSRAM has been initialized. */
    common::Error InitializeTraceBuffer();
    void Report();

    bool Active() const { return active_; }

private:
    static constexpr std::size_t kTaskSlotCount = 33U;

    struct TaskSlot {
        ID task_id = 0;
        UINT state = 0U;
        std::uint32_t cycles = 0U;
        std::uint32_t dispatch_count = 0U;
        std::uint32_t loop_count = 0U;
        std::uint32_t loop_total_cycles = 0U;
        std::uint32_t loop_max_cycles = 0U;
        std::uint32_t loop_last_cycles = 0U;
    };

    static void DispatchExec(ID task_id, ID lsid);
    static void DispatchStop(ID task_id, ID lsid, UINT state);
    static void InterruptEnter(UINT intno);
    static void InterruptLeave(UINT intno);

    void OnDispatchExec(ID task_id);
    void OnDispatchStop(ID task_id, UINT state);
    void OnInterruptEnter();
    void OnInterruptLeave();
    void AccountTask(ID task_id, std::uint32_t cycles);
    TaskSlot *FindTaskSlot(ID task_id);
    void UpdateTraceTaskName(ID task_id);
    void ResetCounters();
    bool TraceHeaderValid() const;
    void AppendTraceRecord(CpuTaskMonitorTraceRecord record);
    void FlushTrace(const void *address, std::size_t size) const;

    static CpuTaskMonitor *volatile active_instance_;

    volatile bool active_ = false;
    volatile ID current_task_id_ = 0;
    volatile std::uint32_t last_cycles_ = 0U;
    volatile std::uint32_t report_start_cycles_ = 0U;
    volatile std::uint32_t interrupt_start_cycles_ = 0U;
    volatile std::uint32_t interrupt_depth_ = 0U;
    volatile std::uint32_t interrupt_cycles_ = 0U;
    volatile std::uint32_t interrupt_count_ = 0U;
    volatile std::uint32_t unknown_task_events_ = 0U;
    TaskSlot task_slots_[kTaskSlotCount] = {};
    char task_names_[kTaskSlotCount][kCpuTaskMonitorTaskNameBytes] = {};
    CpuTaskMonitorTraceHeader *trace_header_ = nullptr;
    CpuTaskMonitorTraceTaskName *trace_task_names_ = nullptr;
    CpuTaskMonitorTraceRecord *trace_records_ = nullptr;
    std::uint32_t trace_capacity_ = 0U;
};

#else

/* Keep application code source-compatible when monitoring is compiled out. */
class CpuTaskMonitor final {
public:
    common::Error Start() { return {}; }
    common::Error Stop() { return {}; }
    common::Error RegisterTask(ID, const char *) { return {}; }
    static common::Error RegisterTaskForActiveMonitor(ID, const char *)
    {
        return {};
    }
    std::uint32_t BeginTaskLoop() const { return 0U; }
    void RecordTaskLoop(ID, std::uint32_t) {}
    common::Error InitializeTraceBuffer() { return {}; }
    void Report() {}
    bool Active() const { return false; }
};

#endif /* UAI_CPU_TASK_MONITOR */

} // namespace uai::ai::middleware::cpu_task_monitor

#endif
