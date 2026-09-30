#include "middleware/ai_model_monitor/ai_model_monitor.hpp"

#include <cstring>

#include "common/log.hpp"
#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"
#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::middleware::ai_model_monitor {
namespace {

constexpr PRI kMonitorPriority = 4;
constexpr RELTIM kMonitorPeriodTicks = 100U;
constexpr std::uint32_t kOperationTimeoutTicks = 6000U;
constexpr SZ kMonitorStackSize = 4U * 1024U;

alignas(8) INT g_monitor_stack[kMonitorStackSize / sizeof(INT)];

} // namespace

AiModelMonitor::PendingTraceEvent
    AiModelMonitor::pending_trace_events_[AiModelMonitor::kPendingTraceEventCapacity];

bool AiModelMonitor::InitializeTraceBuffer()
{
    const auto &region = static_memory_layout::Region::GetRegionFromKey(
        static_memory_layout::Key::kThreadMonitor);
    if (region.begin == nullptr ||
        region.size() < kThreadMonitorTraceDataOffset +
                            sizeof(ThreadMonitorTraceRecord)) {
        return false;
    }

    trace_header_ = reinterpret_cast<ThreadMonitorTraceHeader *>(
        region.address());
    trace_model_names_ = reinterpret_cast<ThreadMonitorTraceModelName *>(
        region.address() + sizeof(ThreadMonitorTraceHeader));
    trace_records_ = reinterpret_cast<ThreadMonitorTraceRecord *>(
        region.address() + kThreadMonitorTraceDataOffset);
    trace_capacity_ = static_cast<std::uint32_t>(
        (region.size() - kThreadMonitorTraceDataOffset) /
        sizeof(ThreadMonitorTraceRecord));
    if (trace_capacity_ == 0U) {
        trace_header_ = nullptr;
        trace_model_names_ = nullptr;
        trace_records_ = nullptr;
        return false;
    }

    if (!TraceHeaderValid() || !TraceModelNamesMatch()) {
        std::memset(reinterpret_cast<void *>(region.address()), 0U,
                    region.size());
        *trace_header_ = ThreadMonitorTraceHeader{};
        trace_header_->record_size = sizeof(ThreadMonitorTraceRecord);
        trace_header_->capacity = trace_capacity_;
        trace_header_->boot_count = 1U;
    } else {
        ++trace_header_->boot_count;
    }
    UpdateTraceModelNames();
    trace_header_->monitored_task_id =
        static_cast<std::uint32_t>(monitored_task_id_);
    trace_header_->monitor_task_id = 0U;
    FlushTrace(trace_header_, sizeof(*trace_header_));
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "ai: runtime trace addr=%x bytes=%u records=%u version=%u\n"),
                 static_cast<unsigned int>(region.address()),
                 static_cast<unsigned int>(region.size()),
                 static_cast<unsigned int>(trace_capacity_),
                 static_cast<unsigned int>(trace_header_->version));
    return true;
}

bool AiModelMonitor::TraceHeaderValid() const
{
    return trace_header_ != nullptr &&
           trace_header_->magic == kThreadMonitorTraceMagic &&
           trace_header_->version == kThreadMonitorTraceVersion &&
           trace_header_->header_size == kThreadMonitorTraceDataOffset &&
           trace_header_->record_size == sizeof(ThreadMonitorTraceRecord) &&
           trace_header_->model_name_entry_size ==
               sizeof(ThreadMonitorTraceModelName) &&
           trace_header_->model_name_count <=
               kThreadMonitorModelNameCapacity &&
           trace_header_->capacity == trace_capacity_ &&
           trace_header_->capacity != 0U &&
           trace_header_->write_index < trace_header_->capacity &&
           trace_header_->record_count <= trace_header_->capacity;
}

bool AiModelMonitor::TraceModelNamesMatch() const
{
    if (trace_header_ == nullptr || trace_model_names_ == nullptr) {
        return false;
    }
    std::uint32_t registered_count = 0U;
    for (std::size_t index = 0U;
         index < kThreadMonitorModelNameCapacity; ++index) {
        const ThreadMonitorTraceModelName &registered =
            registered_model_names_[index];
        const ThreadMonitorTraceModelName &traced = trace_model_names_[index];
        if (registered.model_kind_id != kUnknownModelKindId) {
            ++registered_count;
        }
        if (registered.model_kind_id != traced.model_kind_id ||
            std::memcmp(registered.name, traced.name,
                        kThreadMonitorModelNameBytes) != 0) {
            return false;
        }
    }
    return trace_header_->model_name_count == registered_count;
}

void AiModelMonitor::UpdateTraceModelNames()
{
    if (trace_header_ == nullptr || trace_model_names_ == nullptr) return;
    std::uint32_t name_count = 0U;
    for (std::size_t index = 0U;
         index < kThreadMonitorModelNameCapacity; ++index) {
        trace_model_names_[index] = registered_model_names_[index];
        if (registered_model_names_[index].model_kind_id !=
            kUnknownModelKindId) {
            ++name_count;
        }
    }
    trace_header_->model_name_count = name_count;
    trace_header_->model_name_entry_size =
        sizeof(ThreadMonitorTraceModelName);
    trace_header_->header_size = kThreadMonitorTraceDataOffset;
    FlushTrace(trace_model_names_,
               kThreadMonitorModelNameCapacity *
                   sizeof(ThreadMonitorTraceModelName));
    FlushTrace(trace_header_, sizeof(*trace_header_));
}

common::Error AiModelMonitor::RegisterModelName(
    ai_runtime::AiModelId model_id, const char *name)
{
    const std::uint32_t id = static_cast<std::uint32_t>(model_id);
    if (id == kUnknownModelKindId || name == nullptr || name[0] == '\0') {
        return {common::ErrorCode::kInvalidArgument, id,
                "ai2.ai_model_monitor.register_model_name"};
    }
    if (trace_header_ != nullptr || monitor_task_id_ != 0) {
        return {common::ErrorCode::kInvalidState, id,
                "ai2.ai_model_monitor.register_model_name"};
    }

    std::size_t slot = kThreadMonitorModelNameCapacity;
    for (std::size_t index = 0U;
         index < kThreadMonitorModelNameCapacity; ++index) {
        if (registered_model_names_[index].model_kind_id == id) {
            slot = index;
            break;
        }
        if (slot == kThreadMonitorModelNameCapacity &&
            registered_model_names_[index].model_kind_id ==
                kUnknownModelKindId) {
            slot = index;
        }
    }
    if (slot == kThreadMonitorModelNameCapacity) {
        return {common::ErrorCode::kQueueFull,
                static_cast<std::uint32_t>(kThreadMonitorModelNameCapacity),
                "ai2.ai_model_monitor.register_model_name"};
    }

    ThreadMonitorTraceModelName &entry = registered_model_names_[slot];
    entry = {};
    entry.model_kind_id = id;
    const std::size_t name_length = std::strlen(name);
    const std::size_t copy_length =
        name_length < kThreadMonitorModelNameBytes - 1U
            ? name_length
            : kThreadMonitorModelNameBytes - 1U;
    std::memcpy(entry.name, name, copy_length);
    return {common::ErrorCode::kOk, 0U,
            "ai2.ai_model_monitor.register_model_name"};
}

common::Error AiModelMonitor::Start()
{
    if (monitor_task_id_ != 0) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai2.ai_model_monitor.start"};
    }

    monitored_task_id_ = tk_get_tid();
    if (monitored_task_id_ < E_OK) {
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(monitored_task_id_),
                "ai2.ai_model_monitor.owner"};
    }

    stop_requested_ = false;
    operation_active_ = false;
    faulted_ = false;
    last_progress_tick_ = Now();
    last_step_begin_ms_ = 0U;
    last_step_id_ = 0U;
    last_inference_id_ = 0U;
    last_step_begin_valid_ = false;
    __atomic_store_n(&pending_write_index_, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&pending_read_index_, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&pending_dropped_count_, 0U, __ATOMIC_RELEASE);
    if (!InitializeTraceBuffer()) {
        monitored_task_id_ = 0;
        return {common::ErrorCode::kInvalidState, 0U,
                "ai2.ai_model_monitor.trace_buffer"};
    }

    T_CTSK task = {};
    task.exinf = this;
    task.tskatr = TA_HLNG | TA_USERBUF;
    task.task = reinterpret_cast<FP>(Entry);
    task.itskpri = kMonitorPriority;
    task.stksz = kMonitorStackSize;
    task.bufptr = g_monitor_stack;

    monitor_task_id_ = tk_cre_tsk(&task);
    if (monitor_task_id_ < E_OK) {
        trace_header_->monitor_task_id = 0U;
        FlushTrace(trace_header_, sizeof(*trace_header_));
        monitored_task_id_ = 0;
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(monitor_task_id_),
                "ai2.ai_model_monitor.create"};
    }

    const common::Error task_name_status =
        cpu_task_monitor::CpuTaskMonitor::RegisterTaskForActiveMonitor(
            monitor_task_id_, "ai_model_monitor");
    if (!task_name_status.Ok()) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: CPU task name registration failed name=ai_model_monitor code=%u detail=%u\n"),
                     static_cast<unsigned int>(task_name_status.code),
                     static_cast<unsigned int>(task_name_status.detail));
    }

    trace_header_->monitor_task_id =
        static_cast<std::uint32_t>(monitor_task_id_);
    FlushTrace(trace_header_, sizeof(*trace_header_));
    const ER start_status = tk_sta_tsk(monitor_task_id_, 0);
    if (start_status != E_OK) {
        (void)tk_del_tsk(monitor_task_id_);
        trace_header_->monitor_task_id = 0U;
        FlushTrace(trace_header_, sizeof(*trace_header_));
        monitor_task_id_ = 0;
        monitored_task_id_ = 0;
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(start_status),
                "ai2.ai_model_monitor.start_task"};
    }
    return {common::ErrorCode::kOk, 0U, "ai2.ai_model_monitor.start"};
}

common::Error AiModelMonitor::Stop()
{
    if (monitor_task_id_ == 0) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai2.ai_model_monitor.stop"};
    }

    stop_requested_ = true;
    const ID task_id = monitor_task_id_;
    const ER terminate_status = tk_ter_tsk(task_id);
    const ER delete_status = tk_del_tsk(task_id);
    if (terminate_status == E_OK) FlushPendingTraceEvents();
    if (trace_header_ != nullptr) {
        trace_header_->monitor_task_id = 0U;
        FlushTrace(trace_header_, sizeof(*trace_header_));
    }
    monitor_task_id_ = 0;
    monitored_task_id_ = 0;
    operation_active_ = false;

    if (terminate_status != E_OK) {
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(terminate_status),
                "ai2.ai_model_monitor.terminate"};
    }
    if (delete_status != E_OK) {
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(delete_status),
                "ai2.ai_model_monitor.delete"};
    }
    return {common::ErrorCode::kOk, 0U, "ai2.ai_model_monitor.stop"};
}

void AiModelMonitor::ObserveAiRuntimeStep(const ai_runtime::StepTrace &trace)
{
    if (monitor_task_id_ == 0 || trace_header_ == nullptr) return;

    if (trace.begin) {
        if (trace.step_id == 0U) {
            operation_active_ = true;
            last_progress_tick_ = trace.timestamp;
        }
        last_step_begin_ms_ = trace.timestamp;
        last_step_id_ = trace.step_id;
        last_inference_id_ = trace.inference_id;
        last_step_begin_valid_ = true;
    }

    std::uint32_t elapsed_ms = 0U;
    if (!trace.begin && last_step_begin_valid_ &&
        trace.step_id == last_step_id_ &&
        trace.inference_id == last_inference_id_) {
        elapsed_ms = trace.timestamp - last_step_begin_ms_;
        last_step_begin_valid_ = false;
    }

    if (operation_active_ && !faulted_) last_progress_tick_ = trace.timestamp;
    (void)QueueTraceEvent({trace.timestamp,
                           elapsed_ms,
                           trace.inference_id,
                           static_cast<std::uint32_t>(trace.model_id),
                           trace.step_id,
                           static_cast<std::uint32_t>(trace.context),
                           trace.begin});

    if (!trace.begin && trace.step_id == 2U) operation_active_ = false;
}

bool AiModelMonitor::QueueTraceEvent(const PendingTraceEvent &event)
{
    const std::uint32_t write =
        __atomic_load_n(&pending_write_index_, __ATOMIC_RELAXED);
    const std::uint32_t read =
        __atomic_load_n(&pending_read_index_, __ATOMIC_ACQUIRE);
    const std::uint32_t next =
        (write + 1U) % kPendingTraceEventCapacity;
    if (next == read) {
        __atomic_fetch_add(&pending_dropped_count_, 1U, __ATOMIC_RELAXED);
        return false;
    }
    pending_trace_events_[write] = event;
    __atomic_store_n(&pending_write_index_, next, __ATOMIC_RELEASE);
    return true;
}

void AiModelMonitor::RecordSample(std::uint32_t now,
                                  const T_RTSK &task_status)
{
    WriteRecord(now, TraceRecordType::kSample, &task_status, E_OK,
                TraceFaultCode::kNone);
}

void AiModelMonitor::WriteRecord(std::uint32_t now, TraceRecordType type,
                                 const T_RTSK *task_status,
                                 ER reference_status,
                                 TraceFaultCode fault_code,
                                 const PendingTraceEvent *event)
{
    if (trace_header_ == nullptr || trace_records_ == nullptr ||
        trace_capacity_ == 0U) {
        return;
    }

    const std::uint32_t sequence = trace_header_->next_sequence;
    ThreadMonitorTraceRecord record{};
    record.sequence = sequence;
    record.timestamp_ms = now;
    record.monitored_task_id = static_cast<std::uint32_t>(monitored_task_id_);
    record.monitor_task_id = static_cast<std::uint32_t>(monitor_task_id_);
    record.progress_tick = last_progress_tick_;
    record.reference_status = static_cast<std::int32_t>(reference_status);
    record.fault_code = static_cast<std::uint32_t>(fault_code);
    record.type = static_cast<std::uint8_t>(type);
    record.flags = (operation_active_ ? kTraceFlagOperationActive : 0U) |
                   (faulted_ ? kTraceFlagFaulted : 0U);
    if (task_status != nullptr) {
        record.task_state = task_status->tskstat;
        record.wait_factor = task_status->tskwait;
        record.wait_object_id = static_cast<std::uint32_t>(task_status->wid);
        record.current_priority =
            static_cast<std::int32_t>(task_status->tskpri);
        record.base_priority =
            static_cast<std::int32_t>(task_status->tskbpri);
    }
    if (event != nullptr && type == TraceRecordType::kAiRuntimeStep) {
        /* Reuse the stable 64-byte record for the ai_runtime identity. */
        record.task_state = event->inference_id;
        record.wait_factor = event->lane;
        record.wait_object_id = event->step_id;
        record.reference_status = event->begin ? 1 : 0;
        record.phase_id = static_cast<std::uint16_t>(event->step_id);
        record.npu_elapsed_ms = event->elapsed_ms;
        record.model_kind_id = event->model_kind_id;
        if (event->begin) {
            record.flags |= kTraceFlagAiRuntimeBegin;
        } else {
            record.flags |= kTraceFlagTimingValid;
        }
    }
    record.commit_marker = kThreadMonitorTraceCommitMagic ^ sequence;

    ThreadMonitorTraceRecord &slot = trace_records_[trace_header_->write_index];
    slot = record;
    FlushTrace(&slot, sizeof(slot));
    if (trace_header_->record_count < trace_capacity_) {
        ++trace_header_->record_count;
    } else {
        ++trace_header_->dropped_count;
    }
    trace_header_->write_index =
        (trace_header_->write_index + 1U) % trace_capacity_;
    trace_header_->next_sequence = sequence + 1U;
    if (type == TraceRecordType::kFault) {
        ++trace_header_->fault_count;
        trace_header_->last_fault_code =
            static_cast<std::uint32_t>(fault_code);
    }
    FlushTrace(trace_header_, sizeof(*trace_header_));
}

void AiModelMonitor::FlushPendingTraceEvents()
{
    if (trace_header_ == nullptr || trace_records_ == nullptr) return;

    const std::uint32_t dropped =
        __atomic_exchange_n(&pending_dropped_count_, 0U, __ATOMIC_ACQ_REL);
    trace_header_->dropped_count += dropped;

    std::uint32_t read =
        __atomic_load_n(&pending_read_index_, __ATOMIC_RELAXED);
    for (;;) {
        const std::uint32_t write =
            __atomic_load_n(&pending_write_index_, __ATOMIC_ACQUIRE);
        if (read == write) break;
        const PendingTraceEvent event = pending_trace_events_[read];
        WriteRecord(event.timestamp_ms, TraceRecordType::kAiRuntimeStep,
                    nullptr, E_OK, TraceFaultCode::kNone, &event);
        read = (read + 1U) % kPendingTraceEventCapacity;
        __atomic_store_n(&pending_read_index_, read, __ATOMIC_RELEASE);
    }
    if (dropped != 0U) FlushTrace(trace_header_, sizeof(*trace_header_));
}

void AiModelMonitor::FlushTrace(const void *address, std::size_t size) const
{
    if (address == nullptr || size == 0U) return;
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(const_cast<void *>(address)),
        static_cast<std::int32_t>(size));
}

void AiModelMonitor::Entry(INT, void *exinf)
{
    auto *monitor = static_cast<AiModelMonitor *>(exinf);
    if (monitor != nullptr) monitor->Run();
    tk_ext_tsk();
}

void AiModelMonitor::Run()
{
    for (;;) {
        if (stop_requested_) return;
        (void)tk_dly_tsk(kMonitorPeriodTicks);
        if (stop_requested_) return;

        T_RTSK task_status = {};
        const ER reference_status =
            tk_ref_tsk(monitored_task_id_, &task_status);
        const std::uint32_t now = Now();
        FlushPendingTraceEvents();
        if (reference_status != E_OK) {
            ReportFault(now, nullptr, reference_status,
                        TraceFaultCode::kTaskReference);
            continue;
        }
        RecordSample(now, task_status);
        if (operation_active_ &&
            now - last_progress_tick_ > kOperationTimeoutTicks) {
            ReportFault(now, &task_status, E_OK,
                        TraceFaultCode::kOperationTimeout);
        }
    }
}

void AiModelMonitor::ReportFault(std::uint32_t now,
                                 const T_RTSK *task_status,
                                 ER reference_status,
                                 TraceFaultCode fault_code)
{
    if (faulted_) return;
    faulted_ = true;
    WriteRecord(now, TraceRecordType::kFault, task_status, reference_status,
                fault_code);
    const unsigned int task_state =
        task_status == nullptr ? 0U
                               : static_cast<unsigned int>(task_status->tskstat);
    const unsigned int wait_factor =
        task_status == nullptr ? 0U
                               : static_cast<unsigned int>(task_status->tskwait);
    UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                      "ai: ai_model_monitor fault task=%d ref=%x state=%x wait=%x now=%u last=%u\n"),
                  static_cast<int>(monitored_task_id_),
                  static_cast<unsigned int>(reference_status), task_state,
                  wait_factor, static_cast<unsigned int>(now),
                  static_cast<unsigned int>(last_progress_tick_));
}

std::uint32_t AiModelMonitor::Now() const
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

} // namespace uai::ai::middleware::ai_model_monitor
