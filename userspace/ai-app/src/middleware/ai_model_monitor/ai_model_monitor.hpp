#ifndef UAI_AI2_MIDDLEWARE_AI_MODEL_MONITOR_HPP
#define UAI_AI2_MIDDLEWARE_AI_MODEL_MONITOR_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "middleware/ai_runtime/pipeline_types.hpp"

#if defined(__arm__) || defined(__thumb__)

#include <tk/tkernel.h>

namespace uai::ai::middleware::ai_model_monitor {

/* The trace is intentionally a raw fixed-layout format. It is stored in the
 * linker-reserved PSRAM trace region and can be copied with SWD after a halt.
 * PSRAM avoids taking the model NOR out of memory-mapped mode while the NPU
 * may still be reading weights. */
inline constexpr std::uint32_t kThreadMonitorTraceMagic = 0x544D4F4EU;
inline constexpr std::uint16_t kThreadMonitorTraceVersion = 5U;
inline constexpr std::uint32_t kThreadMonitorTraceCommitMagic = 0x434D4954U;
inline constexpr std::uint32_t kUnknownModelKindId = 0xFFFFFFFFU;
inline constexpr std::size_t kThreadMonitorModelNameCapacity = 16U;
inline constexpr std::size_t kThreadMonitorModelNameBytes = 28U;

struct ThreadMonitorTraceModelName {
    std::uint32_t model_kind_id = kUnknownModelKindId;
    char name[kThreadMonitorModelNameBytes] = {};
};

static_assert(sizeof(ThreadMonitorTraceModelName) == 32U);
inline constexpr std::uint16_t kThreadMonitorTraceDataOffset =
    static_cast<std::uint16_t>(
        64U + kThreadMonitorModelNameCapacity *
                  sizeof(ThreadMonitorTraceModelName));

enum class TraceRecordType : std::uint8_t {
    kSample = 1U,
    kFault = 2U,
    kAiRuntimeStep = 7U,
};

enum class TraceFaultCode : std::uint32_t {
    kNone = 0U,
    kTaskReference = 1U,
    kOperationTimeout = 2U,
};

inline constexpr std::uint8_t kTraceFlagOperationActive = 1U << 0U;
inline constexpr std::uint8_t kTraceFlagFaulted = 1U << 1U;
inline constexpr std::uint8_t kTraceFlagTimingValid = 1U << 2U;
/* For kAiRuntimeStep records, absence of this bit means the end record. */
inline constexpr std::uint8_t kTraceFlagAiRuntimeBegin = 1U << 4U;

struct alignas(32) ThreadMonitorTraceHeader {
    std::uint32_t magic = kThreadMonitorTraceMagic;
    std::uint16_t version = kThreadMonitorTraceVersion;
    std::uint16_t header_size = kThreadMonitorTraceDataOffset;
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
    std::uint32_t model_name_count = 0U;
    std::uint32_t model_name_entry_size =
        sizeof(ThreadMonitorTraceModelName);
    std::uint32_t reserved = 0U;
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
    /* ai_runtime step ID for kAiRuntimeStep records. */
    std::uint16_t phase_id = 0U;
    std::uint32_t commit_marker = 0U;
    /* Step duration for an end record; zero for a begin record. */
    std::uint32_t npu_elapsed_ms = 0U;
    std::uint32_t model_kind_id = kUnknownModelKindId;
};

static_assert(sizeof(ThreadMonitorTraceHeader) == 64U);
static_assert(sizeof(ThreadMonitorTraceRecord) == 64U);

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
    volatile std::uint32_t last_step_begin_ms_ = 0U;
    volatile std::uint32_t last_step_id_ = 0U;
    volatile std::uint32_t last_inference_id_ = 0U;
    volatile bool last_step_begin_valid_ = false;

    static constexpr std::uint32_t kPendingTraceEventCapacity = 128U;
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

#else

/* The ai_runtime host tests intentionally build without the T-Kernel and HAL
 * headers. Keep the dependency boundary testable while the embedded build
 * uses the implementation above. */
namespace uai::ai::middleware::ai_model_monitor {

class AiModelMonitor final {
public:
    common::Error Start() { return {}; }
    common::Error Stop() { return {}; }
    common::Error RegisterModelName(ai_runtime::AiModelId, const char *)
    {
        return {};
    }
    void ObserveAiRuntimeStep(const ai_runtime::StepTrace &) {}
    bool Faulted() const { return false; }
    bool Active() const { return false; }
};

} // namespace uai::ai::middleware::ai_model_monitor

#endif

#endif
