#ifndef UAI_AI2_MIDDLEWARE_CPU_TASK_MONITOR_HPP
#define UAI_AI2_MIDDLEWARE_CPU_TASK_MONITOR_HPP

#include <cstddef>
#include <cstdint>

#include <tk/dbgspt.h>
#include <tk/tkernel.h>

#include "common/error.hpp"

#ifndef UAI_CPU_TASK_MONITOR
#define UAI_CPU_TASK_MONITOR 0
#endif

namespace uai::ai::middleware::cpu_task_monitor {

#if UAI_CPU_TASK_MONITOR

inline constexpr std::uint32_t kCpuTaskMonitorTraceMagic = 0x43544D4EU;
inline constexpr std::uint16_t kCpuTaskMonitorTraceVersion = 1U;
inline constexpr std::uint32_t kCpuTaskMonitorTraceCommitMagic = 0x43544D43U;

enum class CpuTaskMonitorTraceRecordType : std::uint8_t {
    kReport = 1U,
    kTask = 2U,
};

struct alignas(32) CpuTaskMonitorTraceHeader {
    std::uint32_t magic = kCpuTaskMonitorTraceMagic;
    std::uint16_t version = kCpuTaskMonitorTraceVersion;
    std::uint16_t header_size = 64U;
    std::uint32_t record_size = 0U;
    std::uint32_t capacity = 0U;
    std::uint32_t write_index = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t dropped_count = 0U;
    std::uint32_t boot_count = 0U;
    std::uint32_t next_sequence = 0U;
    std::uint32_t monitor_task_id = 0U;
    std::uint32_t report_task_id = 0U;
    std::uint32_t last_period_cycles = 0U;
    std::uint32_t last_interrupt_percent = 0U;
    std::uint32_t reserved[3] = {};
};

struct alignas(32) CpuTaskMonitorTraceRecord {
    std::uint32_t sequence = 0U;
    std::uint32_t timestamp_cycles = 0U;
    std::uint32_t period_cycles = 0U;
    std::uint32_t task_id = 0U;
    std::uint32_t state = 0U;
    std::uint32_t cycles = 0U;
    std::uint32_t dispatch_count = 0U;
    std::uint32_t usage_percent = 0U;
    std::uint32_t interrupt_percent = 0U;
    std::uint32_t interrupt_count = 0U;
    std::uint32_t unknown_task_events = 0U;
    std::uint8_t type = static_cast<std::uint8_t>(
        CpuTaskMonitorTraceRecordType::kTask);
    std::uint8_t reserved0[3] = {};
    std::uint32_t commit_marker = 0U;
    std::uint32_t reserved1[3] = {};
};

static_assert(sizeof(CpuTaskMonitorTraceHeader) == 64U);
static_assert(sizeof(CpuTaskMonitorTraceRecord) == 64U);

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
    CpuTaskMonitorTraceHeader *trace_header_ = nullptr;
    CpuTaskMonitorTraceRecord *trace_records_ = nullptr;
    std::uint32_t trace_capacity_ = 0U;
};

#else

/* Keep application code source-compatible when monitoring is compiled out. */
class CpuTaskMonitor final {
public:
    common::Error Start() { return {}; }
    common::Error Stop() { return {}; }
    common::Error InitializeTraceBuffer() { return {}; }
    void Report() {}
    bool Active() const { return false; }
};

#endif /* UAI_CPU_TASK_MONITOR */

} // namespace uai::ai::middleware::cpu_task_monitor

#endif
