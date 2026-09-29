#include "npu_runtime/thread_monitor/thread_monitor.hpp"

#include <cstring>

#include "common/log.hpp"
#include "static_memory_layout/static_memory_layout.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::npu_runtime {

namespace {

constexpr PRI kMonitorPriority = 4;
constexpr RELTIM kMonitorPeriodTicks = 100U;
constexpr std::uint32_t kOperationTimeoutTicks = 6000U;
constexpr SZ kMonitorStackSize = 4U * 1024U;

alignas(8) INT g_monitor_stack[kMonitorStackSize / sizeof(INT)];

} // namespace

ThreadMonitor::PendingTraceEvent
    ThreadMonitor::pending_trace_events_[ThreadMonitor::kPendingTraceEventCapacity];

bool ThreadMonitor::InitializeTraceBuffer()
{
    const auto &region = static_memory_layout::kLayout.Get(
        static_memory_layout::Key::kThreadMonitor);
    if (region.begin == nullptr || region.size() <
                                      sizeof(ThreadMonitorTraceHeader) +
                                          sizeof(ThreadMonitorTraceRecord)) {
        return false;
    }

    trace_header_ = reinterpret_cast<ThreadMonitorTraceHeader *>(
        region.address());
    trace_records_ = reinterpret_cast<ThreadMonitorTraceRecord *>(
        region.address() + sizeof(ThreadMonitorTraceHeader));
    trace_capacity_ = static_cast<std::uint32_t>(
        (region.size() - sizeof(ThreadMonitorTraceHeader)) /
        sizeof(ThreadMonitorTraceRecord));
    if (trace_capacity_ == 0U) {
        trace_header_ = nullptr;
        trace_records_ = nullptr;
        return false;
    }

    if (!TraceHeaderValid()) {
        std::memset(reinterpret_cast<void *>(region.address()), 0U,
                    region.size());
        *trace_header_ = ThreadMonitorTraceHeader{};
        trace_header_->record_size = sizeof(ThreadMonitorTraceRecord);
        trace_header_->capacity = trace_capacity_;
        trace_header_->boot_count = 1U;
    } else {
        ++trace_header_->boot_count;
    }
    trace_header_->monitored_task_id =
        static_cast<std::uint32_t>(monitored_task_id_);
    trace_header_->monitor_task_id = 0U;
    FlushTrace(trace_header_, sizeof(*trace_header_));
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "ai: trace buffer addr=%x bytes=%u records=%u version=%u\n"),
                 static_cast<unsigned int>(region.address()),
                 static_cast<unsigned int>(region.size()),
                 static_cast<unsigned int>(trace_capacity_),
                 static_cast<unsigned int>(trace_header_->version));
    return true;
}

bool ThreadMonitor::TraceHeaderValid() const
{
    return trace_header_ != nullptr &&
           trace_header_->magic == kThreadMonitorTraceMagic &&
           trace_header_->version == kThreadMonitorTraceVersion &&
           trace_header_->header_size == sizeof(ThreadMonitorTraceHeader) &&
           trace_header_->record_size == sizeof(ThreadMonitorTraceRecord) &&
           trace_header_->capacity == trace_capacity_ &&
           trace_header_->capacity != 0U &&
           trace_header_->write_index < trace_header_->capacity &&
           trace_header_->record_count <= trace_header_->capacity;
}

common::Error ThreadMonitor::Start()
{
    if (monitor_task_id_ != 0) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai.thread_monitor.start"};
    }

    monitored_task_id_ = tk_get_tid();
    if (monitored_task_id_ < E_OK) {
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(monitored_task_id_),
                "ai.thread_monitor.owner"};
    }

    stop_requested_ = false;
    operation_active_ = false;
    faulted_ = false;
    last_progress_tick_ = Now();
    last_npu_elapsed_ms_ = 0U;
    last_model_kind_id_ = kUnknownModelKindId;
    npu_timing_valid_ = false;
    __atomic_store_n(&pending_write_index_, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&pending_read_index_, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&pending_dropped_count_, 0U, __ATOMIC_RELEASE);
    if (!InitializeTraceBuffer()) {
        monitored_task_id_ = 0;
        return {common::ErrorCode::kInvalidState, 0U,
                "ai.thread_monitor.trace_buffer"};
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
                "ai.thread_monitor.create"};
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
                "ai.thread_monitor.start_task"};
    }
    return {common::ErrorCode::kOk, 0U, "ai.thread_monitor.start"};
}

common::Error ThreadMonitor::Stop()
{
    if (monitor_task_id_ == 0) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.thread_monitor.stop"};
    }

    stop_requested_ = true;
    const ID task_id = monitor_task_id_;
    const ER terminate_status = tk_ter_tsk(task_id);
    const ER delete_status = tk_del_tsk(task_id);
    if (terminate_status == E_OK) {
        /* Once the monitor task is terminated, it is the only remaining trace
         * writer, so it is safe to drain an event reported just before Stop. */
        FlushPendingTraceEvents();
    }
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
                "ai.thread_monitor.terminate"};
    }
    if (delete_status != E_OK) {
        return {common::ErrorCode::kInvalidState,
                static_cast<std::uint32_t>(delete_status),
                "ai.thread_monitor.delete"};
    }
    return {common::ErrorCode::kOk, 0U, "ai.thread_monitor.stop"};
}

void ThreadMonitor::BeginOperation()
{
    if (monitor_task_id_ == 0 || faulted_) {
        return;
    }
    last_progress_tick_ = Now();
    operation_active_ = true;
}

void ThreadMonitor::Progress()
{
    if (operation_active_ && !faulted_) {
        last_progress_tick_ = Now();
    }
}

bool ThreadMonitor::EndOperation()
{
    operation_active_ = false;
    return faulted_;
}

void ThreadMonitor::ObserveNpuExecution(std::uint32_t end_ms,
                                        std::uint32_t elapsed_ms,
                                        std::uint32_t model_kind_id)
{
    ObserveInferencePhase(end_ms, elapsed_ms, InferencePhase::kNpuExecution,
                          model_kind_id);
}

void ThreadMonitor::ObserveNpuEpoch(std::uint32_t end_ms,
                                    std::uint32_t end_cycles,
                                    std::uint32_t elapsed_cycles,
                                    std::uint32_t model_kind_id,
                                    std::uint32_t epoch_index,
                                    std::uint32_t epoch_flags,
                                    std::uint32_t epoch_address,
                                    std::uint32_t callback_type)
{
    if (monitor_task_id_ == 0 || !trace_header_) {
        return;
    }
    (void)QueueTraceEvent({
        end_ms,
        0U,
        model_kind_id,
        epoch_index,
        epoch_flags,
        epoch_address,
        end_cycles,
        elapsed_cycles,
        callback_type,
        static_cast<std::uint8_t>(TraceRecordType::kNpuEpoch),
        0U,
    });
}

void ThreadMonitor::ObserveInferencePhase(std::uint32_t end_ms,
                                          std::uint32_t elapsed_ms,
                                          InferencePhase phase,
                                          std::uint32_t model_kind_id)
{
    if (monitor_task_id_ == 0 || !trace_header_) {
        return;
    }
    if (phase == InferencePhase::kNpuExecution) {
        last_npu_elapsed_ms_ = elapsed_ms;
        last_model_kind_id_ = model_kind_id;
        npu_timing_valid_ = true;
    }

    /* The inference task reports the event, while the monitor task owns the
     * trace ring. Keep this SPSC queue so the two tasks never write a trace
     * slot concurrently. The record timestamp is the phase end time; the
     * host can reconstruct the start from end_ms - elapsed_ms. */
    (void)QueueTraceEvent({
        end_ms,
        elapsed_ms,
        model_kind_id,
        0xFFFFFFFFU,
        0U,
        0U,
        0U,
        0U,
        0U,
        static_cast<std::uint8_t>(phase == InferencePhase::kNpuExecution
                                      ? TraceRecordType::kNpuExecution
                                      : TraceRecordType::kInferencePhase),
        static_cast<std::uint16_t>(phase),
    });
}

void ThreadMonitor::ObservePipelineStage(std::uint32_t end_ms,
                                         std::uint32_t end_cycles,
                                         std::uint32_t elapsed_cycles,
                                         std::uint32_t stage_id,
                                         std::uint32_t model_kind_id)
{
    if (monitor_task_id_ == 0 || !trace_header_) {
        return;
    }

    /* Pipeline records reuse the phase_id field to keep the 64-byte raw
     * format stable. Their record type distinguishes them from legacy phase
    * records, so the host can decode the value as an internal pipeline::Stage ID. The
     * progress_tick and npu_elapsed_ms fields carry the DWT end/duration
     * cycles for sub-millisecond CPU stages. */
    (void)QueueTraceEvent({
        end_ms,
        0U,
        model_kind_id,
        0xFFFFFFFFU,
        0U,
        0U,
        end_cycles,
        elapsed_cycles,
        0U,
        static_cast<std::uint8_t>(TraceRecordType::kPipelineStage),
        static_cast<std::uint16_t>(stage_id),
    });
}

bool ThreadMonitor::QueueTraceEvent(const PendingTraceEvent &event)
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

void ThreadMonitor::RecordSample(std::uint32_t now,
                                 const T_RTSK &task_status)
{
    WriteRecord(now, TraceRecordType::kSample, &task_status, E_OK,
                TraceFaultCode::kNone);
}

void ThreadMonitor::WriteRecord(std::uint32_t now, TraceRecordType type,
                                const T_RTSK *task_status,
                                ER reference_status,
                                TraceFaultCode fault_code,
                                bool has_npu_timing,
                                std::uint32_t npu_elapsed_ms,
                                std::uint32_t model_kind_id,
                                std::uint16_t phase_id,
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
    record.phase_id = phase_id;
    record.flags = (operation_active_ ? kTraceFlagOperationActive : 0U) |
                   (faulted_ ? kTraceFlagFaulted : 0U);
    if (has_npu_timing) {
        record.flags |= kTraceFlagNpuTimingValid;
        record.npu_elapsed_ms = npu_elapsed_ms;
        record.model_kind_id = model_kind_id;
    } else if (npu_timing_valid_) {
        record.flags |= kTraceFlagNpuTimingValid;
        record.npu_elapsed_ms = last_npu_elapsed_ms_;
        record.model_kind_id = last_model_kind_id_;
    }
    if (task_status != nullptr) {
        record.task_state = task_status->tskstat;
        record.wait_factor = task_status->tskwait;
        record.wait_object_id = static_cast<std::uint32_t>(task_status->wid);
        record.current_priority =
            static_cast<std::int32_t>(task_status->tskpri);
        record.base_priority =
            static_cast<std::int32_t>(task_status->tskbpri);
    }
    if (event != nullptr && type == TraceRecordType::kNpuEpoch) {
        /* Keep the raw format at 64 bytes. For epoch records these existing
         * sample fields carry the dense NPU payload; the decoder interprets
         * them according to the record type. */
        record.task_state = event->epoch_flags;
        record.wait_factor = event->epoch_index;
        record.wait_object_id = event->epoch_address;
        record.current_priority =
            static_cast<std::int32_t>(event->epoch_end_cycles);
        record.base_priority =
            static_cast<std::int32_t>(event->epoch_elapsed_cycles);
        record.progress_tick = event->epoch_end_cycles;
        record.reference_status =
            static_cast<std::int32_t>(event->callback_type);
        record.npu_elapsed_ms = event->epoch_elapsed_cycles;
        record.model_kind_id = event->model_kind_id;
        record.flags |= kTraceFlagNpuCycleValid;
    } else if (event != nullptr && type == TraceRecordType::kPipelineStage) {
        record.progress_tick = event->epoch_end_cycles;
        record.npu_elapsed_ms = event->epoch_elapsed_cycles;
        record.model_kind_id = event->model_kind_id;
        record.flags |= kTraceFlagNpuCycleValid;
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

void ThreadMonitor::FlushPendingTraceEvents()
{
    if (trace_header_ == nullptr || trace_records_ == nullptr) {
        return;
    }

    const std::uint32_t dropped =
        __atomic_exchange_n(&pending_dropped_count_, 0U, __ATOMIC_ACQ_REL);
    trace_header_->dropped_count += dropped;

    std::uint32_t read =
        __atomic_load_n(&pending_read_index_, __ATOMIC_RELAXED);
    for (;;) {
        const std::uint32_t write =
            __atomic_load_n(&pending_write_index_, __ATOMIC_ACQUIRE);
        if (read == write) {
            break;
        }

        const PendingTraceEvent event = pending_trace_events_[read];
        WriteRecord(event.end_ms,
                    static_cast<TraceRecordType>(event.record_type), nullptr,
                    E_OK, TraceFaultCode::kNone, true, event.elapsed_ms,
                    event.model_kind_id, event.phase_id, &event);
        read = (read + 1U) % kPendingTraceEventCapacity;
        __atomic_store_n(&pending_read_index_, read, __ATOMIC_RELEASE);
    }

    if (dropped != 0U) {
        FlushTrace(trace_header_, sizeof(*trace_header_));
    }
}

void ThreadMonitor::FlushTrace(const void *address, std::size_t size) const
{
    if (address == nullptr || size == 0U) {
        return;
    }
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(const_cast<void *>(address)),
        static_cast<std::int32_t>(size));
}

void ThreadMonitor::Entry(INT, void *exinf)
{
    auto *monitor = static_cast<ThreadMonitor *>(exinf);
    if (monitor != nullptr) {
        monitor->Run();
    }
    tk_ext_tsk();
}

void ThreadMonitor::Run()
{
    for (;;) {
        if (stop_requested_) {
            return;
        }
        (void)tk_dly_tsk(kMonitorPeriodTicks);
        if (stop_requested_) {
            return;
        }

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

void ThreadMonitor::ReportFault(std::uint32_t now,
                                const T_RTSK *task_status,
                                ER reference_status,
                                TraceFaultCode fault_code)
{
    if (faulted_) {
        return;
    }
    faulted_ = true;
    WriteRecord(now, TraceRecordType::kFault, task_status, reference_status,
                fault_code);
    const unsigned int task_state =
        task_status == nullptr ? 0U : static_cast<unsigned int>(task_status->tskstat);
    const unsigned int wait_factor =
        task_status == nullptr ? 0U : static_cast<unsigned int>(task_status->tskwait);
    UAI_LOG_ERROR(reinterpret_cast<const UB *>(
                      "ai: thread monitor fault task=%d ref=%x state=%x wait=%x now=%u last=%u\n"),
                  static_cast<int>(monitored_task_id_),
                  static_cast<unsigned int>(reference_status), task_state,
                  wait_factor, static_cast<unsigned int>(now),
                  static_cast<unsigned int>(last_progress_tick_));
}

std::uint32_t ThreadMonitor::Now() const
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

} // namespace uai::ai::npu_runtime
