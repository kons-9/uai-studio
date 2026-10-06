#ifndef UAI_AI2_MIDDLEWARE_AI_MODEL_MONITOR_HPP
#define UAI_AI2_MIDDLEWARE_AI_MODEL_MONITOR_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "middleware/foundation/error.hpp"
#include "middleware/ai_runtime/pipeline_types.hpp"
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

    struct ActiveStep {
        std::uint32_t inference_id = 0U;
        std::uint32_t step_id = 0U;
        std::uint32_t begin_ms = 0U;
        bool valid = false;
    };

    static void Entry(INT stacd, void *exinf);
    void Run();
    void ReportFault(std::uint32_t now, const T_RTSK *task_status,
                     ER reference_status, TraceFaultCode fault_code);
    bool InitializeTraceBuffer();
    bool TraceHeaderValid() const;
    bool TraceModelNamesMatch() const;
    void UpdateTraceModelNames();
    bool QueueTraceEvent(const PendingTraceEvent &event);
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

    static constexpr std::uint32_t kPendingTraceEventCapacity = 128U;
    static constexpr std::size_t kActiveStepCapacity = 16U;
    std::array<ActiveStep, kActiveStepCapacity> active_steps_{};
    static PendingTraceEvent pending_trace_events_[kPendingTraceEventCapacity];
    std::uint32_t pending_write_index_ = 0U;
    std::uint32_t pending_read_index_ = 0U;
    std::uint32_t pending_dropped_count_ = 0U;
    ThreadMonitorTraceModelName
        registered_model_names_[kThreadMonitorModelNameCapacity]{};
    ThreadMonitorTraceHeader *trace_header_ = nullptr;
    ThreadMonitorTraceModelName *trace_model_names_ = nullptr;
    ThreadMonitorTraceRecord *trace_records_ = nullptr;
    std::uint32_t trace_capacity_ = 0U;
};

} // namespace uai::ai::middleware::ai_model_monitor

#endif
