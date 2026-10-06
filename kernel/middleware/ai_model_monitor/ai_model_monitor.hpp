#pragma once

#include <cstddef>
#include <cstdint>

#include "middleware/ai_model_monitor/trace_step_correlator.hpp"
#include "middleware/ai_runtime/pipeline_types.hpp"
#include "middleware/foundation/error.hpp"
#include "middleware/message_channel/fixed_event_queue.hpp"
#include "middleware/trace_format/ai_model_trace.hpp"

#include <tk/tkernel.h>

namespace uai::ai::middleware::ai_model_monitor {

/*
 * Monitors the ai_runtime NPU worker task. The runtime trace callback may be
 * called by any of the three lane tasks, so step records are first queued and
 * the monitor task is the only writer of the shared trace ring.
 */
class AiModelMonitor final {
public:
    common::Error Start();
    common::Error Stop();
    /* Register the trace label before Start initializes the shared buffer. */
    common::Error RegisterModelName(ai_runtime::AiModelId model_id,
                                    const char *name);

    void ObserveAiRuntimeStep(const ai_runtime::StepTrace &trace);
    bool Faulted() const { return faulted_; }
    bool Active() const { return monitor_task_id_ != 0; }

private:
    static constexpr std::size_t kPendingTraceEventCapacity = 128U;

    struct PendingTraceEvent {
        std::uint32_t timestamp_ms = 0U;
        std::uint32_t elapsed_ms = 0U;
        std::uint32_t inference_id = 0U;
        std::uint32_t model_kind_id = kUnknownModelKindId;
        std::uint32_t step_id = 0U;
        std::uint32_t lane = 0U;
        bool begin = false;
        bool timing_valid = false;
    };

    using PendingTraceQueue =
        message_channel::FixedEventQueue<PendingTraceEvent,
                                         kPendingTraceEventCapacity>;

    static void Entry(INT stacd, void *exinf);
    void Run();
    void ReportFault(std::uint32_t now, const T_RTSK *task_status,
                     ER reference_status, TraceFaultCode fault_code);
    bool InitializeTraceBuffer();
    bool TraceHeaderValid() const;
    bool TraceModelNamesMatch() const;
    void UpdateTraceModelNames();
    void QueueTraceEvent(const PendingTraceEvent &event);
    void FlushPendingTraceEvents();
    void RecordSample(std::uint32_t now, const T_RTSK &task_status);
    void WriteRecord(std::uint32_t now, TraceRecordType type,
                     const T_RTSK *task_status, ER reference_status,
                     TraceFaultCode fault_code,
                     const PendingTraceEvent *event = nullptr);
    void FlushTrace(const void *address, std::size_t size) const;
    std::uint32_t Now() const;

    ID monitored_task_id_ = 0;
    ID monitor_task_id_ = 0;
    volatile bool stop_requested_ = false;
    volatile bool operation_active_ = false;
    volatile bool faulted_ = false;
    volatile std::uint32_t last_progress_tick_ = 0U;

    TraceStepCorrelator step_correlator_{};
    static PendingTraceQueue pending_queue_;
    ThreadMonitorTraceModelName
        registered_model_names_[kThreadMonitorModelNameCapacity]{};
    ThreadMonitorTraceHeader *trace_header_ = nullptr;
    ThreadMonitorTraceModelName *trace_model_names_ = nullptr;
    ThreadMonitorTraceRecord *trace_records_ = nullptr;
    std::uint32_t trace_capacity_ = 0U;
};

} // namespace uai::ai::middleware::ai_model_monitor
