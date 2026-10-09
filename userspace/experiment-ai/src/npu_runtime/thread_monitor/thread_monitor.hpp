#pragma once

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#include "common/error.hpp"
#include "npu_runtime/inference_timing.hpp"

namespace uai::ai::npu_runtime {

/* The trace is deliberately a raw, fixed-layout format. It can be copied
 * from the linker-reserved internal RAM with a debugger and decoded without
 * running the firmware. */
inline constexpr std::uint32_t kThreadMonitorTraceMagic = 0x544D4F4EU;
inline constexpr std::uint16_t kThreadMonitorTraceVersion = 4U;
inline constexpr std::uint32_t kThreadMonitorTraceCommitMagic = 0x434D4954U;
inline constexpr std::uint32_t kUnknownModelKindId = 0xFFFFFFFFU;

enum class TraceRecordType : std::uint8_t {
    kSample = 1U,
    kFault = 2U,
    kNpuExecution = 3U,
    kInferencePhase = 4U,
    kNpuEpoch = 5U,
    kPipelineStage = 6U,
};

enum class TraceFaultCode : std::uint32_t {
    kNone = 0U,
    kTaskReference = 1U,
    kOperationTimeout = 2U,
};

inline constexpr std::uint8_t kTraceFlagOperationActive = 1U << 0U;
inline constexpr std::uint8_t kTraceFlagFaulted = 1U << 1U;
inline constexpr std::uint8_t kTraceFlagNpuTimingValid = 1U << 2U;
inline constexpr std::uint8_t kTraceFlagNpuCycleValid = 1U << 3U;

struct alignas(32) ThreadMonitorTraceHeader {
    std::uint32_t magic = kThreadMonitorTraceMagic;
    std::uint16_t version = kThreadMonitorTraceVersion;
    std::uint16_t header_size = 64U;
    std::uint32_t record_size = 0U;
    std::uint32_t capacity = 0U;
    std::uint32_t write_index = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t dropped_count = 0U;
    std::uint32_t boot_count = 0U;
    std::uint32_t monitored_task_id = 0U;
    std::uint32_t monitor_task_id = 0U;
    std::uint32_t last_fault_code = 0U;
    std::uint32_t fault_count = 0U;
    std::uint32_t next_sequence = 0U;
    std::uint32_t reserved[3] = {};
};

struct alignas(32) ThreadMonitorTraceRecord {
    std::uint32_t sequence = 0U;
    std::uint32_t timestamp_ms = 0U;
    std::uint32_t monitored_task_id = 0U;
    std::uint32_t monitor_task_id = 0U;
    std::uint32_t task_state = 0U;
    std::uint32_t wait_factor = 0U;
    std::uint32_t wait_object_id = 0U;
    std::int32_t current_priority = 0;
    std::int32_t base_priority = 0;
    std::uint32_t progress_tick = 0U;
    std::int32_t reference_status = 0;
    std::uint32_t fault_code = 0U;
    std::uint8_t type = static_cast<std::uint8_t>(TraceRecordType::kSample);
    std::uint8_t flags = 0U;
    /* InferencePhase ID for phase records; zero for samples/faults. */
    std::uint16_t phase_id = 0U;
    std::uint32_t commit_marker = 0U;
    /* For a sample this is the latest observed run; for an NPU event it is
     * the duration of that exact run. */
    std::uint32_t npu_elapsed_ms = 0U;
    std::uint32_t model_kind_id = kUnknownModelKindId;
};

static_assert(sizeof(ThreadMonitorTraceHeader) == 64U);
static_assert(sizeof(ThreadMonitorTraceRecord) == 64U);

/*
 * Monitors the task which owns NpuRuntime. The monitor records periodic
 * samples and latches a fault; it never terminates or manipulates the NPU
 * task from the monitoring context.
 */
class ThreadMonitor final {
public:
    common::Error Start();
    common::Error Stop();

    void BeginOperation();
    void Progress();
    bool EndOperation();
    void ObserveNpuExecution(
        std::uint32_t end_ms,
        std::uint32_t elapsed_ms,
        std::uint32_t model_kind_id
    );
    void ObserveNpuEpoch(
        std::uint32_t end_ms,
        std::uint32_t end_cycles,
        std::uint32_t elapsed_cycles,
        std::uint32_t model_kind_id,
        std::uint32_t epoch_index,
        std::uint32_t epoch_flags,
        std::uint32_t epoch_address,
        std::uint32_t callback_type
    );
    void ObserveInferencePhase(
        std::uint32_t end_ms,
        std::uint32_t elapsed_ms,
        InferencePhase phase,
        std::uint32_t model_kind_id
    );
    void ObservePipelineStage(
        std::uint32_t end_ms,
        std::uint32_t end_cycles,
        std::uint32_t elapsed_cycles,
        std::uint32_t stage_id,
        std::uint32_t model_kind_id
    );

    bool Faulted() const { return faulted_; }

private:
    struct PendingTraceEvent {
        std::uint32_t end_ms = 0U;
        std::uint32_t elapsed_ms = 0U;
        std::uint32_t model_kind_id = kUnknownModelKindId;
        std::uint32_t epoch_index = 0xFFFFFFFFU;
        std::uint32_t epoch_flags = 0U;
        std::uint32_t epoch_address = 0U;
        std::uint32_t epoch_end_cycles = 0U;
        std::uint32_t epoch_elapsed_cycles = 0U;
        std::uint32_t callback_type = 0U;
        std::uint8_t record_type = static_cast<std::uint8_t>(TraceRecordType::kInferencePhase);
        std::uint16_t phase_id = 0U;
    };

    static void Entry(
        INT stacd,
        void *exinf
    );
    void Run();
    void ReportFault(
        std::uint32_t now,
        const T_RTSK *task_status,
        ER reference_status,
        TraceFaultCode fault_code
    );
    bool InitializeTraceBuffer();
    bool TraceHeaderValid() const;
    void RecordSample(
        std::uint32_t now,
        const T_RTSK &task_status
    );
    bool QueueTraceEvent(const PendingTraceEvent &event);
    void FlushPendingTraceEvents();
    void WriteRecord(
        std::uint32_t now,
        TraceRecordType type,
        const T_RTSK *task_status,
        ER reference_status,
        TraceFaultCode fault_code,
        bool has_npu_timing = false,
        std::uint32_t npu_elapsed_ms = 0U,
        std::uint32_t model_kind_id = kUnknownModelKindId,
        std::uint16_t phase_id = 0U,
        const PendingTraceEvent *event = nullptr
    );
    void FlushTrace(
        const void *address,
        std::size_t size
    ) const;
    std::uint32_t Now() const;

    ID monitored_task_id_ = 0;
    ID monitor_task_id_ = 0;
    volatile bool stop_requested_ = false;
    volatile bool operation_active_ = false;
    volatile bool faulted_ = false;
    volatile std::uint32_t last_progress_tick_ = 0U;
    volatile std::uint32_t last_npu_elapsed_ms_ = 0U;
    volatile std::uint32_t last_model_kind_id_ = kUnknownModelKindId;
    volatile bool npu_timing_valid_ = false;

    static constexpr std::uint32_t kPendingTraceEventCapacity = 512U;
    static PendingTraceEvent pending_trace_events_[kPendingTraceEventCapacity];
    std::uint32_t pending_write_index_ = 0U;
    std::uint32_t pending_read_index_ = 0U;
    std::uint32_t pending_dropped_count_ = 0U;
    ThreadMonitorTraceHeader *trace_header_ = nullptr;
    ThreadMonitorTraceRecord *trace_records_ = nullptr;
    std::uint32_t trace_capacity_ = 0U;
};

} // namespace uai::ai::npu_runtime
