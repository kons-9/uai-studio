#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"

#if UAI_CPU_TASK_MONITOR

#include <cstring>

#include "middleware/foundation/log.hpp"
#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::middleware::cpu_task_monitor {
namespace {

constexpr RELTIM kReportPeriodMs = 1000U;

class InterruptMaskGuard final {
public:
    InterruptMaskGuard() : primask_(__get_PRIMASK())
    {
        __disable_irq();
    }

    ~InterruptMaskGuard()
    {
        if ((primask_ & 1U) == 0U) {
            __enable_irq();
        }
    }

private:
    std::uint32_t primask_;
};

std::uint32_t ReadCycles()
{
    return DWT->CYCCNT;
}

void EnableCycleCounter()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

} // namespace

CpuTaskMonitor *volatile CpuTaskMonitor::active_instance_ = nullptr;

void CpuTaskMonitor::ResetCounters()
{
    for (auto &slot : task_slots_) {
        slot = {};
    }

    const ID task_id = tk_get_tid();
    current_task_id_ = task_id > 0 ? task_id : 0;
    last_cycles_ = ReadCycles();
    report_start_cycles_ = last_cycles_;
    interrupt_start_cycles_ = 0U;
    interrupt_depth_ = 0U;
    interrupt_cycles_ = 0U;
    interrupt_count_ = 0U;
    unknown_task_events_ = 0U;
}

common::Error CpuTaskMonitor::InitializeTraceBuffer()
{
    if (trace_header_ != nullptr) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai2.cpu_task_monitor.trace_buffer"};
    }

    const auto &region = static_memory_layout::Region::GetRegionFromKey(
        static_memory_layout::Key::kCpuTaskMonitor);
    if (region.begin == nullptr ||
        region.size() < kCpuTaskMonitorTraceDataOffset +
                            sizeof(CpuTaskMonitorTraceRecord)) {
        return {common::ErrorCode::kInvalidState, 0U,
                "ai2.cpu_task_monitor.trace_region"};
    }

    trace_header_ = reinterpret_cast<CpuTaskMonitorTraceHeader *>(
        region.address());
    trace_task_names_ = reinterpret_cast<CpuTaskMonitorTraceTaskName *>(
        region.address() + sizeof(CpuTaskMonitorTraceHeader));
    trace_records_ = reinterpret_cast<CpuTaskMonitorTraceRecord *>(
        region.address() + kCpuTaskMonitorTraceDataOffset);
    trace_capacity_ = static_cast<std::uint32_t>(
        (region.size() - kCpuTaskMonitorTraceDataOffset) /
        sizeof(CpuTaskMonitorTraceRecord));
    if (trace_capacity_ == 0U) {
        trace_header_ = nullptr;
        trace_task_names_ = nullptr;
        trace_records_ = nullptr;
        return {common::ErrorCode::kInvalidState, 0U,
                "ai2.cpu_task_monitor.trace_capacity"};
    }

    if (!TraceHeaderValid()) {
        std::memset(reinterpret_cast<void *>(region.address()), 0U,
                    region.size());
        *trace_header_ = CpuTaskMonitorTraceHeader{};
        trace_header_->record_size = sizeof(CpuTaskMonitorTraceRecord);
        trace_header_->header_size = kCpuTaskMonitorTraceDataOffset;
        trace_header_->capacity = trace_capacity_;
        trace_header_->boot_count = 1U;
    } else {
        ++trace_header_->boot_count;
    }
    trace_header_->monitor_task_id =
        static_cast<std::uint32_t>(tk_get_tid());
    trace_header_->report_task_id =
        static_cast<std::uint32_t>(tk_get_tid());
    for (std::size_t index = 0U; index < kTaskSlotCount; ++index) {
        UpdateTraceTaskName(static_cast<ID>(index));
    }
    FlushTrace(trace_header_, sizeof(*trace_header_));
    UAI_LOG_INFO("ai: cpu task trace addr=%x bytes=%u records=%u version=%u\n",
                 static_cast<unsigned int>(region.address()),
                 static_cast<unsigned int>(region.size()),
                 static_cast<unsigned int>(trace_capacity_),
                 static_cast<unsigned int>(trace_header_->version));
    return {common::ErrorCode::kOk, 0U,
            "ai2.cpu_task_monitor.trace_buffer"};
}

bool CpuTaskMonitor::TraceHeaderValid() const
{
    return trace_header_ != nullptr &&
           trace_header_->magic == kCpuTaskMonitorTraceMagic &&
           trace_header_->version == kCpuTaskMonitorTraceVersion &&
           trace_header_->header_size == kCpuTaskMonitorTraceDataOffset &&
           trace_header_->record_size == sizeof(CpuTaskMonitorTraceRecord) &&
           trace_header_->task_name_entry_size ==
               sizeof(CpuTaskMonitorTraceTaskName) &&
           trace_header_->task_name_count <= kTaskSlotCount &&
           trace_header_->capacity == trace_capacity_ &&
           trace_header_->capacity != 0U &&
           trace_header_->write_index < trace_header_->capacity &&
           trace_header_->record_count <= trace_header_->capacity;
}

void CpuTaskMonitor::AppendTraceRecord(CpuTaskMonitorTraceRecord record)
{
    if (trace_header_ == nullptr || trace_records_ == nullptr ||
        trace_capacity_ == 0U) {
        return;
    }

    InterruptMaskGuard guard;

    const std::uint32_t sequence = trace_header_->next_sequence;
    record.sequence = sequence;
    record.commit_marker = kCpuTaskMonitorTraceCommitMagic ^ sequence;
    CpuTaskMonitorTraceRecord &slot =
        trace_records_[trace_header_->write_index];
    slot = record;
    FlushTrace(&slot, sizeof(slot));
    if (trace_header_->record_count < trace_capacity_) {
        ++trace_header_->record_count;
    } else {
        ++trace_header_->dropped_count;
    }
    trace_header_->write_index =
        (trace_header_->write_index + 1U) % trace_header_->capacity;
    trace_header_->next_sequence = sequence + 1U;
    if (record.type == static_cast<std::uint8_t>(
                           CpuTaskMonitorTraceRecordType::kReport)) {
        trace_header_->last_period_cycles = record.period_cycles;
        trace_header_->last_interrupt_percent = record.interrupt_percent;
    }
    FlushTrace(trace_header_, sizeof(*trace_header_));
}

void CpuTaskMonitor::FlushTrace(const void *address, std::size_t size) const
{
    if (address == nullptr || size == 0U) return;
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(const_cast<void *>(address)),
        static_cast<std::int32_t>(size));
}

common::Error CpuTaskMonitor::RegisterTask(ID task_id, const char *name)
{
    if (task_id <= 0 || task_id >= static_cast<ID>(kTaskSlotCount) ||
        name == nullptr || name[0] == '\0') {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(task_id),
                "ai2.cpu_task_monitor.register_task"};
    }

    char *registered_name = task_names_[static_cast<std::size_t>(task_id)];
    const std::size_t name_length = std::strlen(name);
    const std::size_t copy_length =
        name_length < kCpuTaskMonitorTaskNameBytes - 1U
            ? name_length
            : kCpuTaskMonitorTaskNameBytes - 1U;
    const bool unchanged =
        std::strncmp(registered_name, name, copy_length) == 0 &&
        registered_name[copy_length] == '\0';
    if (!unchanged) {
        {
            InterruptMaskGuard guard;
            std::memset(registered_name, 0, kCpuTaskMonitorTaskNameBytes);
            std::memcpy(registered_name, name, copy_length);
        }
        UpdateTraceTaskName(task_id);
        UAI_LOG_INFO("cpu: task_name id=%u name=%s\n",
                     static_cast<unsigned int>(task_id),
                     registered_name);
    }
    return {common::ErrorCode::kOk, 0U,
            "ai2.cpu_task_monitor.register_task"};
}

common::Error CpuTaskMonitor::RegisterTaskForActiveMonitor(
    ID task_id, const char *name)
{
    CpuTaskMonitor *monitor = active_instance_;
    if (monitor == nullptr || !monitor->active_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai2.cpu_task_monitor.active_instance"};
    }
    return monitor->RegisterTask(task_id, name);
}

std::uint32_t CpuTaskMonitor::BeginTaskLoop() const
{
    return ReadCycles();
}

void CpuTaskMonitor::RecordTaskLoop(ID task_id, std::uint32_t start_cycles)
{
    const std::uint32_t end_cycles = ReadCycles();
    const std::uint32_t elapsed = end_cycles - start_cycles;
    {
        InterruptMaskGuard guard;
        TaskSlot *slot = FindTaskSlot(task_id);
        if (slot == nullptr) {
            ++unknown_task_events_;
            return;
        }
        ++slot->loop_count;
        slot->loop_total_cycles += elapsed;
        slot->loop_last_cycles = elapsed;
        if (elapsed > slot->loop_max_cycles) {
            slot->loop_max_cycles = elapsed;
        }
    }

    CpuTaskMonitorTraceRecord interval{};
    interval.timestamp_cycles = end_cycles;
    interval.task_id = static_cast<std::uint32_t>(task_id);
    interval.cycles = elapsed;
    interval.type = static_cast<std::uint8_t>(
        CpuTaskMonitorTraceRecordType::kTaskLoopInterval);
    AppendTraceRecord(interval);
}

void CpuTaskMonitor::UpdateTraceTaskName(ID task_id)
{
    if (trace_header_ == nullptr || trace_task_names_ == nullptr ||
        task_id < 0 || task_id >= static_cast<ID>(kTaskSlotCount)) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(task_id);
    CpuTaskMonitorTraceTaskName &entry = trace_task_names_[index];
    entry = {};
    if (task_id > 0 && task_names_[index][0] != '\0') {
        entry.task_id = static_cast<std::uint32_t>(task_id);
        std::memcpy(entry.name, task_names_[index],
                    kCpuTaskMonitorTaskNameBytes);
    }
    std::uint32_t count = 0U;
    for (std::size_t slot = 1U; slot < kTaskSlotCount; ++slot) {
        if (task_names_[slot][0] != '\0') ++count;
    }
    trace_header_->task_name_count = count;
    FlushTrace(&entry, sizeof(entry));
    FlushTrace(trace_header_, sizeof(*trace_header_));
}

common::Error CpuTaskMonitor::Start()
{
    if (active_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai2.cpu_task_monitor.start"};
    }
    if (active_instance_ != nullptr) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai2.cpu_task_monitor.instance"};
    }

    EnableCycleCounter();
    ResetCounters();
    active_instance_ = this;
    active_ = true;

    TD_HDSP dispatch_hook = {};
    dispatch_hook.exec = reinterpret_cast<FP>(&DispatchExec);
    dispatch_hook.stop = reinterpret_cast<FP>(&DispatchStop);
    ER status = td_hok_dsp(&dispatch_hook);
    if (status != E_OK) {
        active_ = false;
        active_instance_ = nullptr;
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(status),
                "ai2.cpu_task_monitor.hok_dsp"};
    }

    TD_HINT interrupt_hook = {};
    interrupt_hook.enter = reinterpret_cast<FP>(&InterruptEnter);
    interrupt_hook.leave = reinterpret_cast<FP>(&InterruptLeave);
    status = td_hok_int(&interrupt_hook);
    if (status != E_OK) {
        (void)td_hok_dsp(nullptr);
        active_ = false;
        active_instance_ = nullptr;
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(status),
                "ai2.cpu_task_monitor.hok_int"};
    }

    UAI_LOG_INFO("ai: cpu task monitor started period=%u ms\n",
                 static_cast<unsigned int>(kReportPeriodMs));
    return {common::ErrorCode::kOk, 0U, "ai2.cpu_task_monitor.start"};
}

common::Error CpuTaskMonitor::Stop()
{
    if (!active_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai2.cpu_task_monitor.stop"};
    }

    const ER dispatch_status = td_hok_dsp(nullptr);
    const ER interrupt_status = td_hok_int(nullptr);
    active_ = false;
    active_instance_ = nullptr;

    if (dispatch_status != E_OK) {
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(dispatch_status),
                "ai2.cpu_task_monitor.unhook_dsp"};
    }
    if (interrupt_status != E_OK) {
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(interrupt_status),
                "ai2.cpu_task_monitor.unhook_int"};
    }
    return {common::ErrorCode::kOk, 0U, "ai2.cpu_task_monitor.stop"};
}

CpuTaskMonitor::TaskSlot *CpuTaskMonitor::FindTaskSlot(ID task_id)
{
    if (task_id <= 0 ||
        task_id >= static_cast<ID>(kTaskSlotCount)) {
        return nullptr;
    }

    TaskSlot &slot = task_slots_[static_cast<std::size_t>(task_id)];
    slot.task_id = task_id;
    return &slot;
}

void CpuTaskMonitor::AccountTask(ID task_id, std::uint32_t cycles)
{
    TaskSlot *slot = FindTaskSlot(task_id);
    if (slot == nullptr) {
        ++unknown_task_events_;
        return;
    }
    slot->cycles += cycles;
}

void CpuTaskMonitor::OnDispatchStop(ID task_id, UINT state)
{
    const std::uint32_t now = ReadCycles();
    if (interrupt_depth_ == 0U) {
        AccountTask(task_id, now - last_cycles_);
    }
    TaskSlot *slot = FindTaskSlot(task_id);
    if (slot != nullptr) {
        slot->state = state;
    }
    current_task_id_ = task_id;
    last_cycles_ = now;
}

void CpuTaskMonitor::OnDispatchExec(ID task_id)
{
    const std::uint32_t now = ReadCycles();
    TaskSlot *slot = FindTaskSlot(task_id);
    if (slot != nullptr) {
        ++slot->dispatch_count;
    }
    current_task_id_ = task_id;
    last_cycles_ = now;
}

void CpuTaskMonitor::OnInterruptEnter()
{
    const std::uint32_t now = ReadCycles();
    if (interrupt_depth_ == 0U) {
        if (current_task_id_ > 0) {
            AccountTask(current_task_id_, now - last_cycles_);
        }
        interrupt_start_cycles_ = now;
        last_cycles_ = now;
    }
    ++interrupt_depth_;
}

void CpuTaskMonitor::OnInterruptLeave()
{
    if (interrupt_depth_ == 0U) {
        return;
    }

    const std::uint32_t now = ReadCycles();
    --interrupt_depth_;
    if (interrupt_depth_ == 0U) {
        interrupt_cycles_ += now - interrupt_start_cycles_;
        ++interrupt_count_;
        last_cycles_ = now;
    }
}

void CpuTaskMonitor::DispatchExec(ID task_id, ID)
{
    CpuTaskMonitor *monitor = active_instance_;
    if (monitor != nullptr && monitor->active_) {
        monitor->OnDispatchExec(task_id);
    }
}

void CpuTaskMonitor::DispatchStop(ID task_id, ID, UINT state)
{
    CpuTaskMonitor *monitor = active_instance_;
    if (monitor != nullptr && monitor->active_) {
        monitor->OnDispatchStop(task_id, state);
    }
}

void CpuTaskMonitor::InterruptEnter(UINT)
{
    CpuTaskMonitor *monitor = active_instance_;
    if (monitor != nullptr && monitor->active_) {
        monitor->OnInterruptEnter();
    }
}

void CpuTaskMonitor::InterruptLeave(UINT)
{
    CpuTaskMonitor *monitor = active_instance_;
    if (monitor != nullptr && monitor->active_) {
        monitor->OnInterruptLeave();
    }
}

void CpuTaskMonitor::Report()
{
    struct Snapshot {
        ID task_id;
        UINT state;
        std::uint32_t cycles;
        std::uint32_t dispatch_count;
        std::uint32_t loop_count;
        std::uint32_t loop_total_cycles;
        std::uint32_t loop_max_cycles;
        std::uint32_t loop_last_cycles;
    };

    Snapshot snapshots[kTaskSlotCount] = {};
    std::uint32_t period_cycles = 0U;
    std::uint32_t interrupt_cycles = 0U;
    std::uint32_t interrupt_count = 0U;
    std::uint32_t unknown_task_events = 0U;
    std::uint32_t report_timestamp_cycles = 0U;

    {
        InterruptMaskGuard guard;
        const std::uint32_t now = ReadCycles();
        report_timestamp_cycles = now;
        if (interrupt_depth_ == 0U && current_task_id_ > 0) {
            AccountTask(current_task_id_, now - last_cycles_);
            last_cycles_ = now;
        }
        period_cycles = now - report_start_cycles_;
        report_start_cycles_ = now;
        interrupt_cycles = interrupt_cycles_;
        interrupt_cycles_ = 0U;
        interrupt_count = interrupt_count_;
        interrupt_count_ = 0U;
        unknown_task_events = unknown_task_events_;
        unknown_task_events_ = 0U;
        for (std::size_t index = 0U; index < kTaskSlotCount; ++index) {
            snapshots[index] = {task_slots_[index].task_id,
                                 task_slots_[index].state,
                                 task_slots_[index].cycles,
                                 task_slots_[index].dispatch_count,
                                 task_slots_[index].loop_count,
                                 task_slots_[index].loop_total_cycles,
                                 task_slots_[index].loop_max_cycles,
                                 task_slots_[index].loop_last_cycles};
            task_slots_[index].cycles = 0U;
            task_slots_[index].dispatch_count = 0U;
            task_slots_[index].loop_count = 0U;
            task_slots_[index].loop_total_cycles = 0U;
            task_slots_[index].loop_max_cycles = 0U;
            task_slots_[index].loop_last_cycles = 0U;
        }
    }

    if (period_cycles == 0U) {
        return;
    }

    const auto percentage = [period_cycles](std::uint32_t cycles) {
        const std::uint64_t value =
            (static_cast<std::uint64_t>(cycles) * 100U) / period_cycles;
        return static_cast<std::uint32_t>(value > 100U ? 100U : value);
    };

    const std::uint32_t interrupt_percent = percentage(interrupt_cycles);
    CpuTaskMonitorTraceRecord report_record{};
    report_record.timestamp_cycles = report_timestamp_cycles;
    report_record.period_cycles = period_cycles;
    report_record.cycles = period_cycles;
    report_record.usage_percent = 100U;
    report_record.interrupt_percent = interrupt_percent;
    report_record.interrupt_count = interrupt_count;
    report_record.unknown_task_events = unknown_task_events;
    report_record.type = static_cast<std::uint8_t>(
        CpuTaskMonitorTraceRecordType::kReport);
    AppendTraceRecord(report_record);

    for (const Snapshot &snapshot : snapshots) {
        if (snapshot.task_id <= 0 || snapshot.cycles == 0U) {
            continue;
        }
        CpuTaskMonitorTraceRecord task_record{};
        task_record.timestamp_cycles = report_timestamp_cycles;
        task_record.period_cycles = period_cycles;
        task_record.task_id = static_cast<std::uint32_t>(snapshot.task_id);
        task_record.state = snapshot.state;
        task_record.cycles = snapshot.cycles;
        task_record.dispatch_count = snapshot.dispatch_count;
        task_record.usage_percent = percentage(snapshot.cycles);
        task_record.interrupt_percent = interrupt_percent;
        task_record.interrupt_count = interrupt_count;
        task_record.unknown_task_events = unknown_task_events;
        task_record.type = static_cast<std::uint8_t>(
            CpuTaskMonitorTraceRecordType::kTask);
        AppendTraceRecord(task_record);
    }

    for (const Snapshot &snapshot : snapshots) {
        if (snapshot.task_id <= 0 || snapshot.loop_count == 0U) {
            continue;
        }
        CpuTaskMonitorTraceRecord loop_record{};
        loop_record.timestamp_cycles = report_timestamp_cycles;
        loop_record.period_cycles = period_cycles;
        loop_record.task_id = static_cast<std::uint32_t>(snapshot.task_id);
        loop_record.state = snapshot.loop_last_cycles;
        loop_record.cycles = snapshot.loop_total_cycles;
        loop_record.dispatch_count = snapshot.loop_count;
        loop_record.usage_percent = snapshot.loop_max_cycles;
        loop_record.interrupt_percent = interrupt_percent;
        loop_record.interrupt_count = interrupt_count;
        loop_record.unknown_task_events = unknown_task_events;
        loop_record.type = static_cast<std::uint8_t>(
            CpuTaskMonitorTraceRecordType::kTaskLoop);
        AppendTraceRecord(loop_record);
    }

    UAI_LOG_INFO("cpu: period=%u cycles irq=%u%% count=%u unknown=%u\n",
                 static_cast<unsigned int>(period_cycles),
                 static_cast<unsigned int>(interrupt_percent),
                 static_cast<unsigned int>(interrupt_count),
                 static_cast<unsigned int>(unknown_task_events));
    for (const Snapshot &snapshot : snapshots) {
        if (snapshot.task_id <= 0 || snapshot.cycles == 0U) {
            continue;
        }
        UAI_LOG_INFO("cpu: task=%d usage=%u%% cycles=%u dispatch=%u state=%x\n",
                     static_cast<int>(snapshot.task_id),
                     static_cast<unsigned int>(percentage(snapshot.cycles)),
                     static_cast<unsigned int>(snapshot.cycles),
                     static_cast<unsigned int>(snapshot.dispatch_count),
                     static_cast<unsigned int>(snapshot.state));
    }
    for (const Snapshot &snapshot : snapshots) {
        if (snapshot.task_id <= 0 || snapshot.loop_count == 0U) {
            continue;
        }
        const std::uint32_t average =
            snapshot.loop_total_cycles / snapshot.loop_count;
        UAI_LOG_INFO("cpu: loop id=%u n=%u avg=%u max=%u last=%u\n",
                     static_cast<unsigned int>(snapshot.task_id),
                     static_cast<unsigned int>(snapshot.loop_count),
                     static_cast<unsigned int>(average),
                     static_cast<unsigned int>(snapshot.loop_max_cycles),
                     static_cast<unsigned int>(snapshot.loop_last_cycles));
    }
}

} // namespace uai::ai::middleware::cpu_task_monitor

#endif /* UAI_CPU_TASK_MONITOR */
